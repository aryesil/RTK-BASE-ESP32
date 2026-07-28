#include <gnss/Iono.h>
#include <gnss/GNSS_Core.h>
#include <Globals.h>
#include <math.h>

static const double C_M_PER_MS = 299792.458;

// Frequency ratio squared between the two bands each constellation is tracked
// on: GPS L1/L5 and Galileo E1/E5a share a ratio, BeiDou B1I/B2a differs.
static double gammaFor(uint8_t sys) {
  if (sys == SYS_BDS) return (1561.098 / 1176.45) * (1561.098 / 1176.45);
  return (1575.42 / 1176.45) * (1575.42 / 1176.45);
}

static uint64_t bitsOf(const uint8_t* p, int pos, int len) {
  uint64_t v = 0;
  for (int i = pos; i < pos + len; i++)
    v = (v << 1) | ((p[i >> 3] >> (7 - (i & 7))) & 1);
  return v;
}
static int64_t sbitsOf(const uint8_t* p, int pos, int len) {
  int64_t v = (int64_t)bitsOf(p, pos, len);
  if (v & ((int64_t)1 << (len - 1))) v -= ((int64_t)1 << len);
  return v;
}

// RTCM signal mask index -> which of the two bands it belongs to.
// 2..6 are the L1/E1/B1 family, 22..24 the L5/E5a/B2a family.
static int bandOfSignal(int sigId) {
  if (sigId >= 2 && sigId <= 6) return 0;
  if (sigId >= 22 && sigId <= 24) return 1;
  return -1;
}

void initIono() {
  ionoSatCount = 0;
  ionoEpochs = 0;
}

static IonoSat* slotFor(uint8_t sys, int prn) {
  for (int i = 0; i < ionoSatCount; i++)
    if (ionoSats[i].sys == sys && ionoSats[i].prn == prn) return &ionoSats[i];
  if (ionoSatCount >= MAX_IONO_SATS) return NULL;

  IonoSat &s = ionoSats[ionoSatCount++];
  memset(&s, 0, sizeof(s));
  s.sys = sys;
  s.prn = (int16_t)prn;
  return &s;
}

void ionoFeedMsm7(const uint8_t* p, uint16_t payloadLen, uint16_t msgType) {
  uint8_t sys;
  switch (msgType) {
    case 1077: sys = SYS_GPS; break;
    case 1097: sys = SYS_GAL; break;
    case 1127: sys = SYS_BDS; break;
    default: return;
  }
  if (payloadLen < 22) return;

  uint64_t satMask = bitsOf(p, 73, 64);
  uint32_t sigMask = (uint32_t)bitsOf(p, 137, 32);

  int sats[64], sigs[32], nSat = 0, nSig = 0;
  for (int i = 0; i < 64; i++) if (satMask >> (63 - i) & 1) sats[nSat++] = i + 1;
  for (int i = 0; i < 32; i++) if (sigMask >> (31 - i) & 1) sigs[nSig++] = i + 1;
  if (nSat == 0 || nSig < 2) return;

  int nCellBits = nSat * nSig;
  int pos = 169;
  // Cell mask can exceed 64 bits, so walk it a bit at a time.
  int cellSat[64], cellSig[64], nCell = 0;
  for (int k = 0; k < nCellBits; k++) {
    if (bitsOf(p, pos + k, 1)) {
      if (nCell < 64) {
        cellSat[nCell] = sats[k / nSig];
        cellSig[nCell] = sigs[k % nSig];
        nCell++;
      }
    }
  }
  pos += nCellBits;

  // Bit budget check before touching any of the data blocks.
  int need = pos + nSat * (8 + 4 + 10 + 14) + nCell * (20 + 24 + 10 + 1 + 10 + 15);
  if (need > payloadLen * 8) return;

  int roughMsPos   = pos;                       pos += 8 * nSat;
  pos += 4 * nSat;                              // extended satellite info
  int roughModPos  = pos;                       pos += 10 * nSat;
  pos += 14 * nSat;                             // rough phase range rate
  int finePrPos    = pos;                       pos += 20 * nCell;
  int finePhPos    = pos;                       pos += 24 * nCell;
  int lockPos      = pos;                       pos += 10 * nCell;
  int halfPos      = pos;

  double gamma = gammaFor(sys);
  uint32_t now = millis();

  if (!xSemaphoreTake(dataMutex, portMAX_DELAY)) return;

  for (int a = 0; a < nCell; a++) {
    if (bandOfSignal(cellSig[a]) != 0) continue;

    // Find the matching band-2 cell for the same satellite.
    int b = -1;
    for (int j = 0; j < nCell; j++)
      if (cellSat[j] == cellSat[a] && bandOfSignal(cellSig[j]) == 1) { b = j; break; }
    if (b < 0) continue;

    int si = 0;
    while (si < nSat && sats[si] != cellSat[a]) si++;
    if (si >= nSat) continue;

    uint32_t rMs  = (uint32_t)bitsOf(p, roughMsPos + 8 * si, 8);
    uint32_t rMod = (uint32_t)bitsOf(p, roughModPos + 10 * si, 10);
    if (rMs == 255) continue;                    // satellite marked invalid

    int64_t fp1 = sbitsOf(p, finePrPos + 20 * a, 20);
    int64_t fp2 = sbitsOf(p, finePrPos + 20 * b, 20);
    int64_t ph1 = sbitsOf(p, finePhPos + 24 * a, 24);
    int64_t ph2 = sbitsOf(p, finePhPos + 24 * b, 24);
    if (fp1 == -(1 << 19) || fp2 == -(1 << 19)) continue;

    double rough = (double)rMs + (double)rMod * (1.0 / 1024.0);
    double P1 = (rough + (double)fp1 * pow(2, -29)) * C_M_PER_MS;
    double P2 = (rough + (double)fp2 * pow(2, -29)) * C_M_PER_MS;

    IonoSat* s = slotFor(sys, cellSat[a]);
    if (!s) continue;

    // Code: absolute but bias-contaminated. P2 - P1 should be positive for a
    // pure ionosphere; on this receiver it often is not, hence "raw".
    s->slantRaw = (float)((P2 - P1) / (gamma - 1.0));

    bool phaseOk = (ph1 != -((int64_t)1 << 23)) && (ph2 != -((int64_t)1 << 23));
    if (phaseOk) {
      // The rough range is per satellite and identical for both signals, so it
      // cancels here and only the fine phase difference matters.
      double L1 = (rough + (double)ph1 * pow(2, -31)) * C_M_PER_MS;
      double L2 = (rough + (double)ph2 * pow(2, -31)) * C_M_PER_MS;
      float phase = (float)((L1 - L2) / (gamma - 1.0));

      uint16_t lk1 = (uint16_t)bitsOf(p, lockPos + 10 * a, 10);
      uint16_t lk2 = (uint16_t)bitsOf(p, lockPos + 10 * b, 10);
      uint8_t  hf1 = (uint8_t)bitsOf(p, halfPos + a, 1);
      uint8_t  hf2 = (uint8_t)bitsOf(p, halfPos + b, 1);

      // Any of these means the carrier ambiguity may have changed, so the arc
      // restarts instead of folding a step into the reported variation.
      bool broken = !s->hasRef ||
                    (now - s->lastMs > IONO_ARC_TIMEOUT_MS) ||
                    lk1 < s->lock1 || lk2 < s->lock2 ||
                    hf1 != s->half1 || hf2 != s->half2 ||
                    // Ionosphere cannot move this fast; anything faster is a slip.
                    (s->hasRef && fabsf(phase - s->lastPhase) > 0.5f);
      if (broken) {
        s->refPhase = phase;
        s->arcStartMs = now;
        s->hasRef = true;
      }
      s->lock1 = lk1; s->lock2 = lk2;
      s->half1 = hf1; s->half2 = hf2;
      s->lastPhase = phase;
      s->slantDelta = phase - s->refPhase;
    }
    s->lastMs = now;
  }

  ionoEpochs++;
  xSemaphoreGive(dataMutex);
}

void ionoUpdate(double baseLat, double baseLon, bool basePosValid) {
  if (!xSemaphoreTake(dataMutex, portMAX_DELAY)) return;
  uint32_t now = millis();

  // Drop arcs the receiver has stopped reporting.
  int keep = 0;
  for (int i = 0; i < ionoSatCount; i++)
    if (now - ionoSats[i].lastMs <= IONO_ARC_TIMEOUT_MS) ionoSats[keep++] = ionoSats[i];
  ionoSatCount = keep;

  for (int i = 0; i < ionoSatCount; i++) {
    IonoSat &s = ionoSats[i];

    s.elev = 0; s.azim = 0;
    for (int k = 0; k < activeSignalCount; k++) {
      if (activeSignals[k].sys == s.sys && activeSignals[k].prn == s.prn) {
        s.elev = activeSignals[k].elev;
        s.azim = activeSignals[k].azim;
        break;
      }
    }

    s.vertDelta = 0.0f;
    s.ippLat = s.ippLon = 0.0f;
    if (s.elev <= 0) continue;

    // Thin-shell geometry: map the slant measurement to the vertical and place
    // it at the point where the ray pierces the shell.
    const double Re = 6371.0;
    double z = (90.0 - s.elev) * DEG_TO_RAD;
    double sinZp = Re / (Re + IONO_SHELL_KM) * sin(z);
    if (sinZp > 1.0) sinZp = 1.0;
    double zp = asin(sinZp);
    s.vertDelta = (float)(s.slantDelta * cos(zp));

    if (!basePosValid) continue;
    double psi = z - zp;
    double lat = baseLat * DEG_TO_RAD, az = s.azim * DEG_TO_RAD;
    double latIpp = asin(sin(lat) * cos(psi) + cos(lat) * sin(psi) * cos(az));
    double denom = cos(latIpp);
    double lonIpp = baseLon * DEG_TO_RAD;
    if (fabs(denom) > 1e-9) {
      double arg = sin(psi) * sin(az) / denom;
      if (arg > 1.0) arg = 1.0;
      if (arg < -1.0) arg = -1.0;
      lonIpp += asin(arg);
    }
    s.ippLat = (float)(latIpp * RAD_TO_DEG);
    s.ippLon = (float)(lonIpp * RAD_TO_DEG);
  }

  xSemaphoreGive(dataMutex);
}
