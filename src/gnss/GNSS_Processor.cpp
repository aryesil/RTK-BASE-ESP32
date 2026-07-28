#include <gnss/GNSS_Processor.h>
#include <Globals.h>
#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <gnss/Iono.h>
#include <network/DataOutput.h>
#include <math.h>

void setupGNSS() {
    Serial2.setRxBufferSize(2048);
    Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);
}

// Big-endian bit field reader over the RTCM payload.
static uint64_t rtcmBits(const uint8_t* p, int pos, int len) {
    uint64_t v = 0;
    for (int i = pos; i < pos + len; i++)
        v = (v << 1) | ((p[i >> 3] >> (7 - (i & 7))) & 1);
    return v;
}
static int64_t rtcmBitsSigned(const uint8_t* p, int pos, int len) {
    int64_t v = (int64_t)rtcmBits(p, pos, len);
    if (v & ((int64_t)1 << (len - 1))) v -= ((int64_t)1 << len);
    return v;
}

// RTCM 1005, stationary RTK reference station ARP. Fixed 19 byte payload:
// 12 msg + 12 station id + 6 ITRF year + 4 indicator bits, then three 38 bit
// signed ECEF components at 0.1 mm resolution, separated by flag bits.
static void parseRtcm1005(const uint8_t* payload, uint16_t payloadLen) {
    if (payloadLen < 19) return;

    double x = rtcmBitsSigned(payload,  34, 38) * 0.0001;
    double y = rtcmBitsSigned(payload,  74, 38) * 0.0001;
    double z = rtcmBitsSigned(payload, 114, 38) * 0.0001;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
        uint32_t now = millis();
        // 0.1 mm is the encoding resolution, so anything at or below it is the
        // same coordinate repeated rather than a survey still converging.
        bool moved = fabs(x - bcast.x) > 0.0002 ||
                     fabs(y - bcast.y) > 0.0002 ||
                     fabs(z - bcast.z) > 0.0002;
        if (moved || !bcast.stableSince) bcast.stableSince = now;

        bcast.stationId = (uint16_t)rtcmBits(payload, 12, 12);
        bcast.x = x; bcast.y = y; bcast.z = z;
        bcast.valid = (x != 0.0 || y != 0.0 || z != 0.0);
        bcast.lastMs = now;
        xSemaphoreGive(dataMutex);
    }
}

// RTCM 10403.3 CRC-24Q, polynomial 0x1864CFB, zero seed.
static uint32_t crc24q(const uint8_t* d, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)d[i] << 16;
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if (crc & 0x1000000) crc ^= 0x1864CFB;
        }
    }
    return crc & 0xFFFFFF;
}

// $--GGA,<utc>,<lat>,<N/S>,<lon>,<E/W>,<quality>,<numSV>,<hdop>,<alt>,M,<sep>,M,...
static void parseGGA(const char* nmea) {
    int commas[16];
    int starIdx;
    int cCount = nmeaIndexFields(nmea, commas, 16, starIdx);
    if (cCount < 7) return;

    char f[16];
    nmeaField(nmea, commas, cCount, starIdx, 6, f, sizeof(f));
    if (f[0] >= '0' && f[0] <= '9') globalFixQuality = (uint8_t)(f[0] - '0');

    nmeaField(nmea, commas, cCount, starIdx, 7, f, sizeof(f));
    stageSatsInUse = (uint8_t)atoi(f);

    if (cCount >= 11) {
        nmeaField(nmea, commas, cCount, starIdx, 11, f, sizeof(f));
        if (f[0]) stageGeoidSep = atof(f);
    }
}

// Sentences that are pure GNSS observation data never reach the terminal view;
// everything else (module replies, $PQTM*, $PAIR*) does.
static bool isObservationSentence(const char* nmea) {
    static const char* const talkers[] = {
        "$GN", "$GP", "$GL", "$GA", "$GB", "$GQ", "$GI", "$BD", "$SB"
    };
    for (size_t i = 0; i < sizeof(talkers) / sizeof(talkers[0]); i++) {
        if (strncmp(nmea, talkers[i], 3) == 0) return true;
    }
    return false;
}

static void handleNmeaSentence(char* nmea) {
    if (!isChecksumValid(nmea)) return;

    if (isObservationSentence(nmea)) {
        const char* type = nmea + 3;
        if (strncmp(type, "GGA,", 4) == 0)      parseGGA(nmea);
        else if (strncmp(type, "GSV,", 4) == 0) parseGSV(nmea);
        else if (strncmp(type, "GSA,", 4) == 0) parseGSA(nmea);
        return;
    }

    // Module replies: update the base-config mirror, then echo to the terminal.
    parseBaseResponse(nmea);

    // $PAIRSPF, $PAIRSPF5 and $PQTMSVINSTATUS arrive once a second each and are
    // already rendered as UI state. Tagging them separately keeps the terminal
    // readable while still letting the page opt in to the raw stream.
    bool periodic = strncmp(nmea, "$PAIRSPF", 8) == 0 ||
                    strncmp(nmea, "$PQTMSVINSTATUS", 15) == 0;

    char termMsg[TERM_MSG_LEN];
    snprintf(termMsg, sizeof(termMsg), periodic ? "TERMP:%s" : "TERM:%s", nmea);
    xQueueSend(termQueue, termMsg, 0);
}

void runGNSSProcessing() {
uint32_t loopStart = micros();

    // Accept connections, run NTRIP handshakes, expire UDP subscribers.
    handleOutputClients();

    size_t bytesAvailable = Serial2.available();
    if (bytesAvailable > 0) {
        uint8_t buf[256];
        if (bytesAvailable > sizeof(buf)) bytesAvailable = sizeof(buf);

        size_t len = Serial2.read(buf, bytesAvailable);

        // Frames are reassembled here and forwarded whole: NMEA never reaches
        // the rover (wasted airtime) and a corrupt frame is dropped instead of
        // being relayed. Waiting for the last byte costs nothing, since a
        // partial frame is unusable to the receiver anyway.
        static enum { WAIT_SYNC, WAIT_LEN1, WAIT_LEN2, BODY } rtcmState = WAIT_SYNC;
        static uint8_t  frame[RTCM_MAX_FRAME];
        static uint16_t frameIdx = 0;
        static uint16_t frameTotal = 0;
        static uint32_t lastRtcmTime = 0;

        if (rtcmState != WAIT_SYNC && (millis() - lastRtcmTime > 50)) {
            rtcmState = WAIT_SYNC;
        }
        lastRtcmTime = millis();

        for (size_t i = 0; i < len; i++) {
            uint8_t b = buf[i];

            if (rtcmState == WAIT_SYNC && b == 0xD3) {
                frame[0] = b;
                frameIdx = 1;
                rtcmState = WAIT_LEN1;
            } else if (rtcmState == WAIT_LEN1) {
                frame[1] = b;
                frameTotal = (uint16_t)(b & 0x03) << 8;
                frameIdx = 2;
                rtcmState = WAIT_LEN2;
            } else if (rtcmState == WAIT_LEN2) {
                frame[2] = b;
                frameTotal |= b;
                frameTotal += 6;              // 3 byte header + payload + CRC24
                frameIdx = 3;
                rtcmState = (frameTotal <= RTCM_MAX_FRAME) ? BODY : WAIT_SYNC;
            } else if (rtcmState == BODY) {
                frame[frameIdx++] = b;
                if (frameIdx >= frameTotal) {
                    uint32_t want = ((uint32_t)frame[frameTotal-3] << 16) |
                                    ((uint32_t)frame[frameTotal-2] << 8) |
                                     (uint32_t)frame[frameTotal-1];
                    if (crc24q(frame, frameTotal - 3) == want) {
                        uint16_t type = ((uint16_t)frame[3] << 4) | (frame[4] >> 4);
                        if (type == 1005) parseRtcm1005(frame + 3, frameTotal - 6);
                        else if (type == 1077 || type == 1097 || type == 1127)
                            ionoFeedMsm7(frame + 3, frameTotal - 6, type);
                        sendRtcmFrame(frame, frameTotal, type);
                    } else if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
                        rtcmStats.crcErrors++;
                        xSemaphoreGive(dataMutex);
                    }
                    rtcmState = WAIT_SYNC;
                }
            } else {
                gps.encode(b);
                char c = (char)b;

                if (c == '$') {
                    nmeaIdx = 0;
                    nmeaBuff[nmeaIdx++] = c;
                } else if (c == '\n') {
                    if (nmeaIdx > 0 && nmeaBuff[0] == '$') {
                        nmeaBuff[nmeaIdx] = '\0';
                        handleNmeaSentence(nmeaBuff);
                    }
                    nmeaIdx = 0;
                } else if (c >= 32 && c <= 126) {
                    if (nmeaIdx < MAX_NMEA - 1) {
                        nmeaBuff[nmeaIdx++] = c;
                    }
                }
            }
        }

        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            safeGps.lat = gps.location.lat(); safeGps.lon = gps.location.lng();
            safeGps.alt = gps.altitude.meters(); safeGps.hdop = gps.hdop.hdop();
            safeGps.validLoc = gps.location.isValid(); safeGps.validAlt = gps.altitude.isValid();
            safeGps.validHdop = gps.hdop.isValid(); safeGps.validTime = gps.time.isValid();
            safeGps.hour = gps.time.hour(); safeGps.min = gps.time.minute(); safeGps.sec = gps.time.second();
            safeGps.fixQual = globalFixQuality;
            safeGps.fixType = stageFixType; safeGps.satsInUse = stageSatsInUse;
            safeGps.pdop = stagePdop; safeGps.vdop = stageVdop;
            safeGps.geoidSep = stageGeoidSep;
            xSemaphoreGive(dataMutex);
        }
    }

    uint32_t loopEnd = micros();
    if (loopEnd >= loopStart) {
        // Read and cleared once per second by the network task on core 0.
        __atomic_fetch_add(&core1BusyTime, loopEnd - loopStart, __ATOMIC_RELAXED);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
}
