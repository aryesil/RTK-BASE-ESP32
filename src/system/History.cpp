#include <system/History.h>
#include <math.h>

// Wire format. Offsets are fixed and the browser reads them directly, so the
// padding here is explicit rather than left to the compiler.
struct HistHeader {
  uint32_t magic;        //  0  'RTKH'
  uint16_t count;        //  4  ring size
  uint16_t intervalSec;  //  6
  uint16_t head;         //  8  next slot to write = oldest sample
  uint16_t filled;       // 10  samples written so far, capped at count
  uint32_t uptimeSec;    // 12
  uint32_t pad0;         // 16
  uint32_t pad1;         // 20  keeps the doubles 8-byte aligned
  double   refLat;       // 24
  double   refLon;       // 32
  double   refAlt;       // 40
};                       // 48

struct HistSample {
  uint8_t  sats;      //  0  satellites in the position solution (from GSA)
  uint8_t  cn0;       //  1  mean C/N0 of tracked signals, dB-Hz
  uint16_t hdop;      //  2  x100, 0xFFFF unknown
  uint8_t  flags;     //  4  bits 0-3 fix quality, 4-5 jam L1, 6-7 jam L5
  uint8_t  iono;      //  5  mean |vertical delta|, cm, 255 unknown
  int16_t  dN;        //  6  cm from the reference position
  int16_t  dE;        //  8
  int16_t  dU;        // 10
  uint16_t bps;       // 12  RTCM bytes/s, clipped
  uint8_t  valid;     // 14
  uint8_t  tracked;   // 15  satellites being tracked (from GSV)
};                    // 16

struct HistBlob {
  HistHeader hdr;
  HistSample s[HISTORY_SAMPLES];
};

static HistBlob blob;
static uint32_t lastSampleMs = 0;
static bool     haveRef = false;

static_assert(sizeof(HistHeader) == 48, "header layout changed");
static_assert(sizeof(HistSample) == 16, "sample layout changed");

void historyInit() {
  memset(&blob, 0, sizeof(blob));
  blob.hdr.magic       = 0x484B5452;   // 'RTKH' little-endian
  blob.hdr.count       = HISTORY_SAMPLES;
  blob.hdr.intervalSec = HISTORY_INTERVAL_MS / 1000;
  lastSampleMs = 0;
  haveRef = false;
}

// Clamped conversion: a wild position early in a cold start must not wrap the
// int16 and draw a spike that never happened.
static int16_t toCm(double metres) {
  double cm = metres * 100.0;
  if (cm >  32000.0) return  32000;
  if (cm < -32000.0) return -32000;
  return (int16_t)lround(cm);
}

void historyFeed(uint32_t nowMs, uint8_t satsUsed, uint8_t satsTracked,
                 uint8_t meanCn0, double hdop,
                 uint8_t fixQual, uint8_t jamL1, uint8_t jamL5,
                 bool haveFix, double lat, double lon, double alt,
                 uint32_t bytesSec, float ionoMeanM) {
  if (lastSampleMs && (nowMs - lastSampleMs) < HISTORY_INTERVAL_MS) return;
  lastSampleMs = nowMs;

  // The reference is latched on the first fix and never moves. A reference
  // that tracked the current position would centre itself and hide the very
  // drift this chart exists to show.
  if (haveFix && !haveRef) {
    blob.hdr.refLat = lat;
    blob.hdr.refLon = lon;
    blob.hdr.refAlt = alt;
    haveRef = true;
  }

  HistSample &e = blob.s[blob.hdr.head];
  e.sats    = satsUsed;
  e.tracked = satsTracked;
  e.cn0   = meanCn0;
  e.hdop  = (hdop > 0.0 && hdop < 655.0) ? (uint16_t)lround(hdop * 100.0) : 0xFFFF;
  e.flags = (uint8_t)((fixQual & 0x0F) | ((jamL1 & 0x03) << 4) | ((jamL5 & 0x03) << 6));
  e.iono  = (ionoMeanM >= 0.0f && ionoMeanM < 2.55f)
              ? (uint8_t)lroundf(ionoMeanM * 100.0f) : 255;
  e.bps   = bytesSec > 65535 ? 65535 : (uint16_t)bytesSec;

  if (haveFix && haveRef) {
    double mPerDegLon = 111320.0 * cos(blob.hdr.refLat * M_PI / 180.0);
    e.dN = toCm((lat - blob.hdr.refLat) * 111320.0);
    e.dE = toCm((lon - blob.hdr.refLon) * mPerDegLon);
    e.dU = toCm(alt - blob.hdr.refAlt);
    e.valid = 1;
  } else {
    e.dN = e.dE = e.dU = 0;
    // Still a real sample: satellites, C/N0 and RTCM rate are meaningful
    // without a fix, and a gap in the position trace is itself information.
    e.valid = 2;
  }

  blob.hdr.head = (uint16_t)((blob.hdr.head + 1) % HISTORY_SAMPLES);
  if (blob.hdr.filled < HISTORY_SAMPLES) blob.hdr.filled++;
}

const uint8_t* historySnapshot(size_t &len) {
  blob.hdr.uptimeSec = millis() / 1000;
  len = sizeof(blob);
  return (const uint8_t*)&blob;
}
