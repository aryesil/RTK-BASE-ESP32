#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <Globals.h>

const char* const SYS_NAMES[SYS_COUNT] = {
  "GPS", "GLONASS", "Galileo", "BeiDou", "QZSS", "SBAS", "NavIC", "Unknown"
};

// RINEX-ish single letter used by the sky plot labels (G01, R18, E13, C24 ...).
const char SYS_CODES[SYS_COUNT] = { 'G', 'R', 'E', 'C', 'J', 'S', 'I', 'U' };

const char* const BAND_NAMES[SYS_COUNT][BANDS_PER_SYS] = {
  { "L1",  "L2",  "L5"  },   // GPS
  { "G1",  "G2",  NULL  },   // GLONASS
  { "E1",  "E5a", "E5b" },   // Galileo
  { "B1",  "B2",  "B3"  },   // BeiDou
  { "L1",  "L2",  "L5"  },   // QZSS
  { "L1",  NULL,  NULL  },   // SBAS
  { "L5",  "S",   NULL  },   // NavIC
  { "?",   NULL,  NULL  }    // Unknown
};

uint8_t sysFromTalker(const char* nmea) {
  if (strncmp(nmea, "$GP", 3) == 0) return SYS_GPS;
  if (strncmp(nmea, "$GL", 3) == 0) return SYS_GLO;
  if (strncmp(nmea, "$GA", 3) == 0) return SYS_GAL;
  if (strncmp(nmea, "$GB", 3) == 0 || strncmp(nmea, "$BD", 3) == 0) return SYS_BDS;
  if (strncmp(nmea, "$GQ", 3) == 0) return SYS_QZS;
  if (strncmp(nmea, "$GI", 3) == 0) return SYS_NAV;
  if (strncmp(nmea, "$SB", 3) == 0) return SYS_SBS;
  return SYS_UNK; // "$GN" and anything else
}

uint8_t classifySat(uint8_t talkerSys, int &prn) {
  // Only the GPS/SBAS/combined talkers mix constellations into one PRN space.
  // Galileo (1..36) and BeiDou (1..63) must never be reinterpreted here, which
  // is what used to leak SBAS satellites into the GPS counters.
  if (talkerSys == SYS_GPS || talkerSys == SYS_SBS || talkerSys == SYS_UNK) {
    if (prn >= 33 && prn <= 64)   { prn += 87; return SYS_SBS; } // NMEA shorthand
    if (prn >= 120 && prn <= 158) { return SYS_SBS; }
    if (prn >= 193 && prn <= 202) { return SYS_QZS; }
    if (prn >= 65 && prn <= 96)   { return SYS_GLO; }
    if (prn >= 1 && prn <= 32)    { return SYS_GPS; }
    return talkerSys == SYS_UNK ? SYS_UNK : talkerSys;
  }
  if (talkerSys == SYS_QZS && prn >= 193 && prn <= 202) return SYS_QZS;
  return talkerSys;
}

uint8_t bandFromSignalId(uint8_t sys, int sigId) {
  switch (sys) {
    case SYS_GPS:
    case SYS_QZS:
      // 1..4 L1 (C/A, P(Y), M, LIS) | 5,6 L2C | 7,8 L5 (I and Q)
      if (sigId == 5 || sigId == 6) return 1;
      if (sigId == 7 || sigId == 8) return 2;
      return 0;
    case SYS_GLO:
      // 1,2 G1 (C/A, P) | 3,4 G2
      return (sigId == 3 || sigId == 4) ? 1 : 0;
    case SYS_GAL:
      // 1 E5a | 2,3 E5b and E5 a+b | 6,7 E1 (and 0 = unspecified)
      if (sigId == 1) return 1;
      if (sigId == 2 || sigId == 3) return 2;
      return 0;
    case SYS_BDS:
      // 1..4 B1 | 5,6,7 B2a/B2b/B2(a+b), 0xB,0xC B2I/B2Q | 8,9,0xA B3
      if ((sigId >= 5 && sigId <= 7) || sigId == 0x0B || sigId == 0x0C) return 1;
      if (sigId >= 8 && sigId <= 0x0A) return 2;
      return 0;
    case SYS_NAV:
      // 1,3 L5 | 2,4 S band
      return (sigId == 2 || sigId == 4) ? 1 : 0;
    default:
      return 0;
  }
}

void addSignal(uint8_t sys, uint8_t band, int prn, int elev, int azim, int snr) {
  if (band >= BANDS_PER_SYS || BAND_NAMES[sys][band] == NULL) band = 0;

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    for (int i = 0; i < activeSignalCount; i++) {
      if (activeSignals[i].sys == sys && activeSignals[i].prn == prn &&
          activeSignals[i].band == band) {
        activeSignals[i].elev = elev;
        activeSignals[i].azim = azim;
        activeSignals[i].snr = (uint8_t)constrain(snr, 0, 99);
        activeSignals[i].lastSeen = now;
        xSemaphoreGive(dataMutex);
        return;
      }
    }
    if (activeSignalCount < MAX_SIGNALS) {
      SatSignal &s = activeSignals[activeSignalCount++];
      s.sys = sys;
      s.band = band;
      s.prn = (int16_t)prn;
      s.elev = (int16_t)elev;
      s.azim = (int16_t)azim;
      s.snr = (uint8_t)constrain(snr, 0, 99);
      s.lastSeen = now;
    }
    xSemaphoreGive(dataMutex);
  }
}

void markSatUsed(uint8_t sys, int prn) {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    for (int i = 0; i < usedSatCount; i++) {
      if (usedSats[i].sys == sys && usedSats[i].prn == prn) {
        usedSats[i].lastSeen = now;
        xSemaphoreGive(dataMutex);
        return;
      }
    }
    if (usedSatCount < MAX_USED_SATS) {
      usedSats[usedSatCount].sys = sys;
      usedSats[usedSatCount].prn = (int16_t)prn;
      usedSats[usedSatCount].lastSeen = now;
      usedSatCount++;
    }
    xSemaphoreGive(dataMutex);
  }
}

// Caller must already hold dataMutex.
bool isSatUsed(uint8_t sys, int prn) {
  for (int i = 0; i < usedSatCount; i++) {
    if (usedSats[i].sys == sys && usedSats[i].prn == prn) return true;
  }
  return false;
}

void cleanOldSatellites() {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();

    int keep = 0;
    for (int i = 0; i < activeSignalCount; i++) {
      if (now - activeSignals[i].lastSeen <= SAT_TIMEOUT_MS) {
        activeSignals[keep++] = activeSignals[i];
      }
    }
    activeSignalCount = keep;

    keep = 0;
    for (int i = 0; i < usedSatCount; i++) {
      if (now - usedSats[i].lastSeen <= USED_TIMEOUT_MS) {
        usedSats[keep++] = usedSats[i];
      }
    }
    usedSatCount = keep;

    xSemaphoreGive(dataMutex);
  }
}

bool isChecksumValid(const char* sentence) {
  int len = strlen(sentence);
  if (len < 8) return false;

  int starIndex = -1;
  for (int i = 0; i < len; i++) {
    if (sentence[i] == '*') {
      starIndex = i;
      break;
    }
  }

  if (starIndex == -1 || starIndex > len - 3) return false;

  char c1 = sentence[starIndex + 1];
  char c2 = sentence[starIndex + 2];
  if (!isxdigit(c1) || !isxdigit(c2)) return false;

  uint8_t calculatedCS = 0;
  for (int i = 1; i < starIndex; i++) {
    calculatedCS ^= sentence[i];
  }

  char hexStr[3] = {c1, c2, '\0'};
  uint8_t providedCS = (uint8_t)strtol(hexStr, NULL, 16);

  return (calculatedCS == providedCS);
}

// Splits a sentence into comma offsets. Returns the number of commas found and
// writes the '*' offset to starIdx (-1 when absent).
int nmeaIndexFields(const char* nmea, int* commas, int maxCommas, int &starIdx) {
  int count = 0;
  starIdx = -1;
  for (int i = 0; nmea[i] != '\0'; i++) {
    if (nmea[i] == ',') {
      if (count < maxCommas) commas[count++] = i;
    } else if (nmea[i] == '*') {
      starIdx = i;
      break;
    }
  }
  return count;
}

// Copies field `n` (0 = talker) into out. Empty fields yield an empty string.
void nmeaField(const char* nmea, const int* commas, int cCount, int starIdx,
               int n, char* out, size_t outSize) {
  out[0] = '\0';
  if (n < 1 || n > cCount) return;

  int start = commas[n - 1] + 1;
  int end = (n == cCount) ? starIdx : commas[n];
  if (end < 0 || end <= start) return;

  size_t len = (size_t)(end - start);
  if (len >= outSize) len = outSize - 1;
  memcpy(out, nmea + start, len);
  out[len] = '\0';
}

// $--GSV,<numMsg>,<msgNum>,<numSV>{,<prn>,<elev>,<azim>,<cno>}[,<signalId>]*cs
void parseGSV(const char* nmea) {
  int commas[25];
  int starIdx;
  int cCount = nmeaIndexFields(nmea, commas, 25, starIdx);
  if (starIdx < 0 || cCount < 4) return;

  uint8_t talkerSys = sysFromTalker(nmea);

  // NMEA 4.10+ appends a signal id after the last satellite block, so the field
  // count is 4 + 4*N + 1 with it and 4 + 4*N without. Only trust the trailing
  // field when the arithmetic says one must be there.
  int sigId = 0;
  bool hasSignalId = ((cCount - 3) % 4 == 1);
  if (hasSignalId) {
    char sigStr[4];
    nmeaField(nmea, commas, cCount, starIdx, cCount, sigStr, sizeof(sigStr));
    if (sigStr[0] != '\0' && isxdigit((unsigned char)sigStr[0])) {
      sigId = (int)strtol(sigStr, NULL, 16);
    }
  }

  int satBlocks = (cCount - 3) / 4;
  char buf[12];

  for (int b = 0; b < satBlocks; b++) {
    int base = 4 + b * 4; // field index of this block's PRN

    nmeaField(nmea, commas, cCount, starIdx, base, buf, sizeof(buf));
    int prn = atoi(buf);
    if (prn <= 0) continue; // padded/empty block

    nmeaField(nmea, commas, cCount, starIdx, base + 1, buf, sizeof(buf));
    int elev = atoi(buf);
    nmeaField(nmea, commas, cCount, starIdx, base + 2, buf, sizeof(buf));
    int azim = atoi(buf);
    nmeaField(nmea, commas, cCount, starIdx, base + 3, buf, sizeof(buf));
    int snr = atoi(buf);

    uint8_t sys = classifySat(talkerSys, prn);
    if (sys == SYS_UNK) continue; // unattributable, better than a phantom entry

    addSignal(sys, bandFromSignalId(sys, sigId), prn, elev, azim, snr);
  }
}

// $--GSA,<mode1>,<mode2>,{12 x <prn>},<pdop>,<hdop>,<vdop>[,<systemId>]*cs
void parseGSA(const char* nmea) {
  int commas[20];
  int starIdx;
  int cCount = nmeaIndexFields(nmea, commas, 20, starIdx);
  if (starIdx < 0 || cCount < 17) return;

  char buf[12];

  uint8_t sys = sysFromTalker(nmea);
  if (cCount >= 18) { // NMEA 4.10 system id
    nmeaField(nmea, commas, cCount, starIdx, 18, buf, sizeof(buf));
    switch (atoi(buf)) {
      case 1: sys = SYS_GPS; break;
      case 2: sys = SYS_GLO; break;
      case 3: sys = SYS_GAL; break;
      case 4: sys = SYS_BDS; break;
      case 5: sys = SYS_QZS; break;
      case 6: sys = SYS_NAV; break;
      default: break;
    }
  }

  nmeaField(nmea, commas, cCount, starIdx, 2, buf, sizeof(buf));
  int fixType = atoi(buf);
  if (fixType >= 1 && fixType <= 3) stageFixType = (uint8_t)fixType;

  nmeaField(nmea, commas, cCount, starIdx, 15, buf, sizeof(buf));
  if (buf[0]) stagePdop = atof(buf);
  nmeaField(nmea, commas, cCount, starIdx, 17, buf, sizeof(buf));
  if (buf[0]) stageVdop = atof(buf);

  for (int f = 3; f <= 14; f++) {
    nmeaField(nmea, commas, cCount, starIdx, f, buf, sizeof(buf));
    int prn = atoi(buf);
    if (prn <= 0) continue;
    uint8_t satSys = classifySat(sys, prn);
    if (satSys == SYS_UNK) continue;
    markSatUsed(satSys, prn);
  }
}

bool sendGnssCommand(const char* cmd, unsigned long timeoutMs) {
  while (Serial2.available()) Serial2.read();

  Serial2.print(cmd);
  Serial2.print("\r\n");
  Serial.print("[GNSS-TX] "); Serial.println(cmd);

  char expectedAck[32] = {0};
  size_t cmdLen = strlen(cmd);

  if (cmdLen >= 8 && strncmp(cmd, "$PAIR", 5) == 0) {
    char cmdId[4] = {0};
    memcpy(cmdId, cmd + 5, 3);
    snprintf(expectedAck, sizeof(expectedAck), "$PAIR001,%s,0", cmdId);
  }
  else if (strncmp(cmd, "$PQTM", 5) == 0) {
    // Copy the message ID (up to the first ',' or '*') and append ",OK",
    // keeping room for the suffix and the terminator.
    const size_t maxId = sizeof(expectedAck) - 4; // ",OK" + '\0'
    size_t endIdx = 0;
    while (endIdx < maxId && cmd[endIdx] != ',' && cmd[endIdx] != '*' && cmd[endIdx] != '\0') {
      expectedAck[endIdx] = cmd[endIdx];
      endIdx++;
    }
    memcpy(expectedAck + endIdx, ",OK", 4);
  }
  else {
    strcpy(expectedAck, "OK");
  }

  unsigned long start = millis();
  String line = "";

  while (millis() - start < timeoutMs) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') {
        if (line.indexOf(expectedAck) != -1) {
          Serial.print("[GNSS-RX] ACKNOWLEDGED: "); Serial.println(line);
          return true;
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
  }

  Serial.print("[GNSS-ERR] TIMEOUT! Module did not acknowledge: ");
  Serial.println(expectedAck);
  return false;
}

void applyGnssConfiguration() {
    // PAIR062,<sentence>,<rate> selects which NMEA sentences the module emits.
    // Not part of the LC29H(BS) protocol spec but supported by the firmware;
    // GGA/GSA/GSV are what the telemetry UI is built on.
    static const char* const nmeaEnables[] = {
        "$PAIR062,0,1*3F", // GGA
        "$PAIR062,1,1*3E", // GLL
        "$PAIR062,2,1*3D", // GSA
        "$PAIR062,3,1*3C", // GSV
        "$PAIR062,4,1*3B", // RMC
        "$PAIR062,6,1*39",
        "$PAIR062,7,1*38",
        "$PAIR062,8,1*37"
    };

    Serial.println("\n=== GNSS CONFIGURATION STARTING ===");
    for (size_t i = 0; i < sizeof(nmeaEnables) / sizeof(nmeaEnables[0]); i++) {
        sendGnssCommand(nmeaEnables[i], 1000);
    }

    // RTCM output settings are re-applied from NVS; the survey-in/fixed setup
    // lives in the module's own NVM and is only touched from the Base Mode
    // page, so a reboot never silently restarts a survey.
    applyStoredRtcmSettings();
    queryBaseConfig();
    requestOptionalMessages();

    Serial.println("=== GNSS CONFIGURATION COMPLETED ===\n");
}
