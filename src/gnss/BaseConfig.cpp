#include <gnss/BaseConfig.h>
#include <gnss/GNSS_Core.h>
#include <Globals.h>
#include <math.h>

static Preferences basePrefs;

// Scope guard for baseCfg: written from the GNSS task (module replies) and
// from the web handlers, read by the telemetry builder.
namespace {
struct BaseLock {
  BaseLock()  { xSemaphoreTake(baseMutex, portMAX_DELAY); }
  ~BaseLock() { xSemaphoreGive(baseMutex); }
};
}

void initBaseConfig() {
  basePrefs.begin("basecfg", false);
  loadArpCfg();
}

String buildNmeaSentence(const String &body) {
  uint8_t cs = 0;
  for (size_t i = 0; i < body.length(); i++) cs ^= (uint8_t)body[i];
  char tail[5];
  snprintf(tail, sizeof(tail), "*%02X", cs);
  return "$" + body + tail;
}

void sendNmeaSentence(const String &body) {
  String cmd = buildNmeaSentence(body);
  Serial2.print(cmd);
  Serial2.print("\r\n");
  Serial.print("[GNSS-TX] ");
  Serial.println(cmd);

  char msg[TERM_MSG_LEN];
  snprintf(msg, sizeof(msg), "TXCMD:%s", cmd.c_str());
  xQueueSend(termQueue, msg, 0);
}

void applySvinConfig(int mode, uint32_t minDur, float accLimit,
                     double x, double y, double z) {
  if (mode < 0) mode = 0;
  if (mode > 2) mode = 2;
  if (minDur > 86400) minDur = 86400;
  if (accLimit < 0.0f) accLimit = 0.0f;

  char body[160];
  snprintf(body, sizeof(body),
           "PQTMCFGSVIN,W,%d,%lu,%.1f,%.4f,%.4f,%.4f",
           mode, (unsigned long)minDur, accLimit, x, y, z);
  sendNmeaSentence(body);

  BaseLock lock;
  baseCfg.svinMode = (int8_t)mode;
  baseCfg.minDur = minDur;
  baseCfg.accLimit = accLimit;
  baseCfg.ecefX = x;
  baseCfg.ecefY = y;
  baseCfg.ecefZ = z;
  baseCfg.svinStartMs = (mode == 1) ? millis() : 0;
}

void applyRtcmMode(int mode) {
  if (mode < -1) mode = -1;
  if (mode > 1) mode = 1;

  char body[24];
  snprintf(body, sizeof(body), "PAIR432,%d", mode);
  sendNmeaSentence(body);
  basePrefs.putInt("rtcm", mode);

  BaseLock lock;
  baseCfg.rtcmMode = (int8_t)mode;
}

void applyArpOutput(int enable) {
  enable = enable ? 1 : 0;
  char body[24];
  snprintf(body, sizeof(body), "PAIR434,%d", enable);
  sendNmeaSentence(body);
  basePrefs.putInt("arp", enable);

  BaseLock lock;
  baseCfg.arpEnabled = (int8_t)enable;
}

void applyEphOutput(int enable) {
  enable = enable ? 1 : 0;
  char body[24];
  snprintf(body, sizeof(body), "PAIR436,%d", enable);
  sendNmeaSentence(body);
  basePrefs.putInt("eph", enable);

  BaseLock lock;
  baseCfg.ephEnabled = (int8_t)enable;
}

void saveModuleParams()    { sendNmeaSentence("PQTMSAVEPAR"); }
void restoreModuleParams() { sendNmeaSentence("PQTMRESTOREPAR"); }

void queryBaseConfig() {
  sendNmeaSentence("PQTMCFGSVIN,R");
  sendNmeaSentence("PAIR433");
  sendNmeaSentence("PAIR435");
  sendNmeaSentence("PAIR437");
  sendNmeaSentence("PQTMVERNO");
}

void applyStoredRtcmSettings() {
  int rtcm = basePrefs.getInt("rtcm", 1); // default MSM7
  int arp  = basePrefs.getInt("arp", 1);
  int eph  = basePrefs.getInt("eph", 1);

  char cmd[32];
  snprintf(cmd, sizeof(cmd), "%s", buildNmeaSentence(String("PAIR432,") + rtcm).c_str());
  sendGnssCommand(cmd, 1000);
  snprintf(cmd, sizeof(cmd), "%s", buildNmeaSentence(String("PAIR434,") + arp).c_str());
  sendGnssCommand(cmd, 1000);
  snprintf(cmd, sizeof(cmd), "%s", buildNmeaSentence(String("PAIR436,") + eph).c_str());
  sendGnssCommand(cmd, 1000);

  BaseLock lock;
  baseCfg.rtcmMode = (int8_t)rtcm;
  baseCfg.arpEnabled = (int8_t)arp;
  baseCfg.ephEnabled = (int8_t)eph;
}

bool parseBaseResponse(const char* nmea) {
  int commas[16];
  int starIdx;
  int cCount = nmeaIndexFields(nmea, commas, 16, starIdx);
  char f[48];

  BaseLock lock;

  // $PQTMSVINSTATUS,<MsgVer>,<TOW>,<Valid>,<Res0>,<Res1>,<Obs>,<CfgDur>,
  //                 <MeanX>,<MeanY>,<MeanZ>,<MeanAcc>
  if (strncmp(nmea, "$PQTMSVINSTATUS,", 16) == 0) {
    if (cCount >= 11) {
      nmeaField(nmea, commas, cCount, starIdx, 3, f, sizeof(f));
      svin.valid = (uint8_t)atoi(f);
      nmeaField(nmea, commas, cCount, starIdx, 6, f, sizeof(f));
      svin.obs = (uint32_t)strtoul(f, NULL, 10);
      nmeaField(nmea, commas, cCount, starIdx, 7, f, sizeof(f));
      svin.cfgDur = (uint32_t)strtoul(f, NULL, 10);
      nmeaField(nmea, commas, cCount, starIdx, 8, f, sizeof(f));
      svin.x = atof(f);
      nmeaField(nmea, commas, cCount, starIdx, 9, f, sizeof(f));
      svin.y = atof(f);
      nmeaField(nmea, commas, cCount, starIdx, 10, f, sizeof(f));
      svin.z = atof(f);
      nmeaField(nmea, commas, cCount, starIdx, 11, f, sizeof(f));
      svin.acc = atof(f);
      svin.feat = FEAT_OK;
      svin.lastMs = millis();
    }
    return true;
  }

  // $PAIRSPF,<status> is L1, $PAIRSPF5,<status> is L5. Check the longer name
  // first: "$PAIRSPF," would otherwise never match the L5 variant.
  if (strncmp(nmea, "$PAIRSPF5,", 10) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    jam.l5 = (uint8_t)atoi(f);
    jam.haveL5 = true;
    jam.feat = FEAT_OK;
    jam.lastMs = millis();
    return true;
  }
  if (strncmp(nmea, "$PAIRSPF,", 9) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    jam.l1 = (uint8_t)atoi(f);
    jam.feat = FEAT_OK;
    jam.lastMs = millis();
    return true;
  }

  if (strncmp(nmea, "$PQTMCFGMSGRATE,", 16) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    if (strcmp(f, "ERROR") == 0) svin.feat = FEAT_UNSUPPORTED;
    return true;
  }

  if (strncmp(nmea, "$PQTMCFGSVIN", 12) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    if (strcmp(f, "ERROR") == 0) {
      nmeaField(nmea, commas, cCount, starIdx, 2, f, sizeof(f));
      snprintf(baseCfg.lastResult, sizeof(baseCfg.lastResult),
               "SVIN rejected (error %s)", f);
      return true;
    }
    if (strcmp(f, "OK") != 0) return true;

    // Set acknowledgement carries no payload; the query reply carries all six.
    if (cCount < 7) {
      snprintf(baseCfg.lastResult, sizeof(baseCfg.lastResult), "SVIN config accepted");
      return true;
    }

    nmeaField(nmea, commas, cCount, starIdx, 2, f, sizeof(f));
    int8_t mode = (int8_t)atoi(f);
    nmeaField(nmea, commas, cCount, starIdx, 3, f, sizeof(f));
    baseCfg.minDur = (uint32_t)strtoul(f, NULL, 10);
    nmeaField(nmea, commas, cCount, starIdx, 4, f, sizeof(f));
    baseCfg.accLimit = atof(f);
    nmeaField(nmea, commas, cCount, starIdx, 5, f, sizeof(f));
    baseCfg.ecefX = atof(f);
    nmeaField(nmea, commas, cCount, starIdx, 6, f, sizeof(f));
    baseCfg.ecefY = atof(f);
    nmeaField(nmea, commas, cCount, starIdx, 7, f, sizeof(f));
    baseCfg.ecefZ = atof(f);

    // The module restarts the survey on every power-up, so anchor the elapsed
    // timer the first time a survey-in configuration is observed.
    if (mode == 1 && (baseCfg.svinMode != 1 || baseCfg.svinStartMs == 0)) {
      baseCfg.svinStartMs = millis();
    } else if (mode != 1) {
      baseCfg.svinStartMs = 0;
    }
    baseCfg.svinMode = mode;
    return true;
  }

  if (strncmp(nmea, "$PQTMSAVEPAR", 12) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    snprintf(baseCfg.lastResult, sizeof(baseCfg.lastResult),
             strcmp(f, "OK") == 0 ? "Saved to module NVM" : "Save failed");
    return true;
  }

  if (strncmp(nmea, "$PQTMRESTOREPAR", 15) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    snprintf(baseCfg.lastResult, sizeof(baseCfg.lastResult),
             strcmp(f, "OK") == 0 ? "Module defaults restored" : "Restore failed");
    return true;
  }

  if (strncmp(nmea, "$PQTMVERNO,", 11) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    if (strcmp(f, "ERROR") != 0) {
      strlcpy(baseCfg.verStr, f, sizeof(baseCfg.verStr));
      nmeaField(nmea, commas, cCount, starIdx, 2, f, sizeof(f));
      strlcpy(baseCfg.buildDate, f, sizeof(baseCfg.buildDate));
    }
    return true;
  }

  if (strncmp(nmea, "$PAIR433,", 9) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    baseCfg.rtcmMode = (int8_t)atoi(f);
    return true;
  }
  if (strncmp(nmea, "$PAIR435,", 9) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    baseCfg.arpEnabled = (int8_t)atoi(f);
    return true;
  }
  if (strncmp(nmea, "$PAIR437,", 9) == 0) {
    nmeaField(nmea, commas, cCount, starIdx, 1, f, sizeof(f));
    baseCfg.ephEnabled = (int8_t)atoi(f);
    return true;
  }

  if (strncmp(nmea, "$PAIR001,", 9) == 0) {
    char idStr[8], resStr[8];
    nmeaField(nmea, commas, cCount, starIdx, 1, idStr, sizeof(idStr));
    nmeaField(nmea, commas, cCount, starIdx, 2, resStr, sizeof(resStr));
    int res = atoi(resStr);

    // Result 3 is "command not supported"; that is the definitive answer for
    // anything taken from the series spec rather than the (BS) one.
    if (atoi(idStr) == 391 && res >= 2) jam.feat = FEAT_UNSUPPORTED;

    if (res != 0 && res != 1) {
      static const char* const reasons[] = {
        "sent", "processing", "send failed", "unsupported command",
        "parameter error", "service busy"
      };
      snprintf(baseCfg.lastResult, sizeof(baseCfg.lastResult), "PAIR%s: %s",
               idStr, (res >= 0 && res <= 5) ? reasons[res] : "unknown result");
    }
    return false; // still show it in the terminal
  }

  return false;
}

static const double WGS84_A  = 6378137.0;
static const double WGS84_F  = 1.0 / 298.257223563;
static const double WGS84_E2 = WGS84_F * (2.0 - WGS84_F);

void llaToEcef(double latDeg, double lonDeg, double hgt,
               double &x, double &y, double &z) {
  double lat = latDeg * DEG_TO_RAD;
  double lon = lonDeg * DEG_TO_RAD;
  double sinLat = sin(lat), cosLat = cos(lat);
  double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sinLat * sinLat);

  x = (N + hgt) * cosLat * cos(lon);
  y = (N + hgt) * cosLat * sin(lon);
  z = (N * (1.0 - WGS84_E2) + hgt) * sinLat;
}

// Bowring's method: converges to well under a millimetre in one pass at
// terrestrial heights, which is far below the accuracy of anything upstream.
void ecefToLla(double x, double y, double z,
               double &latDeg, double &lonDeg, double &hgt) {
  const double b = WGS84_A * (1.0 - WGS84_F);
  const double ep2 = (WGS84_A * WGS84_A - b * b) / (b * b);
  double p = sqrt(x * x + y * y);

  if (p < 1e-9) {                       // on the spin axis
    latDeg = z >= 0 ? 90.0 : -90.0;
    lonDeg = 0.0;
    hgt = fabs(z) - b;
    return;
  }

  double th = atan2(WGS84_A * z, b * p);
  double lat = atan2(z + ep2 * b * pow(sin(th), 3),
                     p - WGS84_E2 * WGS84_A * pow(cos(th), 3));
  double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin(lat) * sin(lat));

  latDeg = lat * RAD_TO_DEG;
  lonDeg = atan2(y, x) * RAD_TO_DEG;
  hgt = p / cos(lat) - N;
}

void enuToEcefDelta(double latDeg, double lonDeg,
                    double e, double n, double u,
                    double &dx, double &dy, double &dz) {
  double lat = latDeg * DEG_TO_RAD, lon = lonDeg * DEG_TO_RAD;
  double sLat = sin(lat), cLat = cos(lat), sLon = sin(lon), cLon = cos(lon);

  dx = -sLon * e - sLat * cLon * n + cLat * cLon * u;
  dy =  cLon * e - sLat * sLon * n + cLat * sLon * u;
  dz =                    cLat * n +        sLat * u;
}

void markerToArpEcef(double latDeg, double lonDeg, double hgt,
                     double &x, double &y, double &z) {
  llaToEcef(latDeg, lonDeg, hgt, x, y, z);

  if (arpCfg.north == 0.0f && arpCfg.east == 0.0f && arpCfg.up == 0.0f) return;

  double dx, dy, dz;
  enuToEcefDelta(latDeg, lonDeg, arpCfg.east, arpCfg.north, arpCfg.up, dx, dy, dz);
  x += dx; y += dy; z += dz;
}

void loadArpCfg() {
  arpCfg.north = basePrefs.getFloat("arpN", 0.0f);
  arpCfg.east  = basePrefs.getFloat("arpE", 0.0f);
  arpCfg.up    = basePrefs.getFloat("arpU", 0.0f);
}

void saveArpCfg() {
  basePrefs.putFloat("arpN", arpCfg.north);
  basePrefs.putFloat("arpE", arpCfg.east);
  basePrefs.putFloat("arpU", arpCfg.up);
}

// --------------------------------------------------------------------------
// Optional-message probing
// --------------------------------------------------------------------------
static uint32_t probeStartMs = 0;

void requestOptionalMessages() {
  probeStartMs = millis();

  // Survey-in progress. PQTM messages must carry their message version, so the
  // trailing 1 is SVINSTATUS's MsgVer, not part of the rate.
  sendNmeaSentence("PQTMCFGMSGRATE,W,PQTMSVINSTATUS,1,1");
  // Interference detection; the module then emits $PAIRSPF / $PAIRSPF5 at 1 Hz.
  sendNmeaSentence("PAIR391,1");
}

void updateFeatureProbes() {
  uint32_t now = millis();
  BaseLock lock;

  if (svin.feat == FEAT_OK && now - svin.lastMs > FEATURE_STALE_MS) {
    svin.feat = FEAT_UNKNOWN;   // may simply be out of survey-in mode
  }
  if (jam.feat == FEAT_OK && now - jam.lastMs > FEATURE_STALE_MS * 3) {
    jam.feat = FEAT_UNKNOWN;
  }
  if (probeStartMs && now - probeStartMs > FEATURE_PROBE_MS) {
    if (svin.feat == FEAT_UNKNOWN && svin.lastMs == 0) svin.feat = FEAT_UNSUPPORTED;
    if (jam.feat  == FEAT_UNKNOWN && jam.lastMs  == 0) jam.feat  = FEAT_UNSUPPORTED;
  }
}

// --------------------------------------------------------------------------
// Position averaging
// --------------------------------------------------------------------------
void startPosAvg(uint32_t seconds) {
  BaseLock lock;
  posAvg.running = true;
  posAvg.startedMs = millis();
  posAvg.targetSec = seconds;
  posAvg.count = 0;
  posAvg.sumLat = posAvg.sumLon = posAvg.sumAlt = 0.0;
  posAvg.sumLat2 = posAvg.sumLon2 = 0.0;
}

void stopPosAvg(bool keepResult) {
  BaseLock lock;
  posAvg.running = false;
  if (!keepResult || posAvg.count == 0) return;

  double n = (double)posAvg.count;
  posAvg.lat = posAvg.sumLat / n;
  posAvg.lon = posAvg.sumLon / n;
  posAvg.alt = posAvg.sumAlt / n;

  // Spread in metres, from the variance of the raw degree samples.
  double varLat = posAvg.sumLat2 / n - posAvg.lat * posAvg.lat;
  double varLon = posAvg.sumLon2 / n - posAvg.lon * posAvg.lon;
  if (varLat < 0) varLat = 0;
  if (varLon < 0) varLon = 0;
  double mLat = 111320.0;
  double mLon = 111320.0 * cos(posAvg.lat * DEG_TO_RAD);
  posAvg.rms = (float)sqrt(varLat * mLat * mLat + varLon * mLon * mLon);
  posAvg.haveResult = true;
}

void feedPosAvg() {
  bool valid;
  double lat, lon, alt;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    valid = safeGps.validLoc && safeGps.validAlt;
    lat = safeGps.lat; lon = safeGps.lon; alt = safeGps.alt;
    xSemaphoreGive(dataMutex);
  } else {
    return;
  }

  bool done = false;
  {
    BaseLock lock;
    if (!posAvg.running) return;
    if (valid) {
      posAvg.sumLat += lat;   posAvg.sumLat2 += lat * lat;
      posAvg.sumLon += lon;   posAvg.sumLon2 += lon * lon;
      posAvg.sumAlt += alt;
      posAvg.count++;
    }
    done = (millis() - posAvg.startedMs) / 1000 >= posAvg.targetSec;
  }
  if (done) stopPosAvg(true);
}
