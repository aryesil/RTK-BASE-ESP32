#include <gnss/GNSS_Processor.h>
#include <Globals.h>
#include <gnss/GNSS_Core.h>
#include <network/RTCMSocket.h>

void setupGNSS() {
    Serial2.setRxBufferSize(2048);
    Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);
}

void runGNSSProcessing() {
uint32_t loopStart = micros();

    // Check for new TCP client connections.
    handleNewRTCMClients();

    size_t bytesAvailable = Serial2.available();
    if (bytesAvailable > 0) {
        uint8_t buf[256]; 
        if (bytesAvailable > sizeof(buf)) bytesAvailable = sizeof(buf);
        
        size_t len = Serial2.read(buf, bytesAvailable);

        // Broadcast incoming Raw Data / RTCM data to TCP clients
        broadcastRTCM(buf, len);

        static enum { WAIT_SYNC, WAIT_LEN1, WAIT_LEN2, SKIP_PAYLOAD } rtcmState = WAIT_SYNC;
        static uint16_t rtcmLen = 0;
        static uint16_t rtcmBytesRead = 0;
        static uint32_t lastRtcmTime = 0;
        static uint16_t totalRtcmBytes = 0; 

        if (rtcmState != WAIT_SYNC && (millis() - lastRtcmTime > 50)) {
            rtcmState = WAIT_SYNC;
        }
        lastRtcmTime = millis();

        for (size_t i = 0; i < len; i++) {
            uint8_t b = buf[i];
            
            if (rtcmState == WAIT_SYNC && b == 0xD3) {
                rtcmState = WAIT_LEN1;
                rtcmBytesRead = 1;
            } else if (rtcmState == WAIT_LEN1) {
                rtcmLen = (b & 0x03) << 8;
                rtcmState = WAIT_LEN2;
                rtcmBytesRead++;
            } else if (rtcmState == WAIT_LEN2) {
                rtcmLen |= b;
                totalRtcmBytes = rtcmLen + 6; 
                rtcmState = SKIP_PAYLOAD;
                rtcmBytesRead++;
            } else if (rtcmState == SKIP_PAYLOAD) {
                rtcmBytesRead++;
                if (rtcmBytesRead >= totalRtcmBytes) { 
                    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
                        rtcmPaketSayaci++;
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
                        if (nmeaBuff[nmeaIdx - 1] == '\r') nmeaBuff[nmeaIdx - 1] = '\0';
                        else nmeaBuff[nmeaIdx] = '\0'; 
                        
                        if (isChecksumValid(nmeaBuff)) {
                            if (strncmp(nmeaBuff + 3, "GGA,", 4) == 0) {
                                int commas[10];
                                int cCount = 0;
                                for (int k = 0; nmeaBuff[k] != '\0' && cCount < 10; k++) {
                                    if (nmeaBuff[k] == ',') commas[cCount++] = k;
                                }
                                if (cCount >= 6) {
                                    char qChar = nmeaBuff[commas[5] + 1];
                                    if (qChar >= '0' && qChar <= '9') {
                                        globalFixQuality = qChar - '0';
                                    }
                                }
                            }

                            uyduTipleriniAyristir(nmeaBuff); 
                            
                            if (strncmp(nmeaBuff, "$GN", 3) != 0 && strncmp(nmeaBuff, "$GP", 3) != 0 && 
                                strncmp(nmeaBuff, "$GL", 3) != 0 && strncmp(nmeaBuff, "$GA", 3) != 0 && 
                                strncmp(nmeaBuff, "$GB", 3) != 0 && strncmp(nmeaBuff, "$GQ", 3) != 0 && 
                                strncmp(nmeaBuff, "$GI", 3) != 0 && strncmp(nmeaBuff, "$BD", 3) != 0 &&
                                strncmp(nmeaBuff, "$SB", 3) != 0) {
                                
                                char termMsg[160];
                                snprintf(termMsg, sizeof(termMsg), "TERM:%s", nmeaBuff);
                                if(xQueueSend(termQueue, termMsg, 0) != pdTRUE) {}
                            }
                        }
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
            xSemaphoreGive(dataMutex);
        }
    }

    uint32_t loopEnd = micros();
    if (loopEnd >= loopStart) {
        core1BusyTime += (loopEnd - loopStart);
    }
    
    vTaskDelay(pdMS_TO_TICKS(1)); 
}