#include <system/History.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

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

static_assert(sizeof(HistHeader) == 48, "header layout changed");
static_assert(sizeof(HistSample) == 16, "sample layout changed");

/* The ring lives in instruction RAM. The ESP32 keeps about 29 kB of IRAM as a
 * heap that ordinary malloc() never hands out, because it can only be read and
 * written 32 bits at a time: a byte or halfword access raises LoadStoreError.
 * Nothing else in this firmware uses it, and the ring is 23 kB, so the full
 * twelve hours fit there with the Tailscale client running and cost the
 * byte-addressable heap nothing. The rule that makes it safe: the ring is
 * touched only through ringPut() and historyRead() below, whole aligned words
 * each, never through a HistSample pointer. The header is small and stays in
 * ordinary RAM. If IRAM is unavailable the ring falls back to ordinary RAM,
 * but only when the Tailscale client is off - with it on, that RAM is the
 * client's. */
union SampleWords {
  HistSample s;
  uint32_t   w[sizeof(HistSample) / 4];
};

static HistHeader hdrMem;
static HistHeader *hdr     = nullptr;
static uint32_t  *ringMem  = nullptr;   // IRAM (or DRAM fallback), words only
static size_t     ringBytes = 0;
static size_t     blobLen  = 0;
static uint32_t lastSampleMs = 0;
static bool     haveRef = false;

static void ringPut(uint16_t slot, const SampleWords &e) {
  volatile uint32_t *dst = ringMem + (size_t)slot * (sizeof(HistSample) / 4);
  for (size_t i = 0; i < sizeof(HistSample) / 4; i++) dst[i] = e.w[i];
}

void historyInit() {
  uint16_t n = rt.historySamples;
  if (n == 0) return;
  ringBytes = (size_t)n * sizeof(HistSample);
  // An executable allocation can also be served from the IRAM alias of a
  // region shared with data RAM, which would cost the byte-addressable heap
  // after all. Only a block that leaves that heap untouched counts as IRAM.
  size_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ringMem = (uint32_t *)heap_caps_malloc(ringBytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
  bool inIram = ringMem && !esp_ptr_byte_accessible(ringMem) &&
                heap_caps_get_free_size(MALLOC_CAP_8BIT) + 1024 >= freeBefore;
  if (ringMem && !inIram && rt.lean) {
    heap_caps_free(ringMem);
    ringMem = nullptr;
  }
  if (!ringMem && !rt.lean) ringMem = (uint32_t *)malloc(ringBytes);
  if (ringMem) {
    volatile uint32_t *w = ringMem;
    for (size_t i = 0; i < ringBytes / 4; i++) w[i] = 0;   // calloc would memset bytes
  }
  if (!ringMem) {                       // no history rather than no boot
    Log.printf("[HIST] No room for %u samples, history off\n", (unsigned)n);
    rt.historySamples = 0;
    ringBytes = 0;
    return;
  }
  Log.printf("[HIST] %u samples, %u bytes in %s\n", (unsigned)n, (unsigned)ringBytes,
             inIram ? "IRAM" : "RAM");
  hdr = &hdrMem;
  memset(hdr, 0, sizeof(*hdr));
  hdr->magic       = 0x484B5452;   // 'RTKH' little-endian
  hdr->count       = n;
  hdr->intervalSec = HISTORY_INTERVAL_MS / 1000;
  blobLen = sizeof(HistHeader) + ringBytes;
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
  if (!ringMem) return;
  if (lastSampleMs && (nowMs - lastSampleMs) < HISTORY_INTERVAL_MS) return;
  lastSampleMs = nowMs;

  // The reference is latched on the first fix and never moves. A reference
  // that tracked the current position would centre itself and hide the very
  // drift this chart exists to show.
  if (haveFix && !haveRef) {
    hdr->refLat = lat;
    hdr->refLon = lon;
    hdr->refAlt = alt;
    haveRef = true;
  }

  SampleWords w = {};
  HistSample &e = w.s;
  e.sats    = satsUsed;
  e.tracked = satsTracked;
  e.cn0   = meanCn0;
  e.hdop  = (hdop > 0.0 && hdop < 655.0) ? (uint16_t)lround(hdop * 100.0) : 0xFFFF;
  e.flags = (uint8_t)((fixQual & 0x0F) | ((jamL1 & 0x03) << 4) | ((jamL5 & 0x03) << 6));
  e.iono  = (ionoMeanM >= 0.0f && ionoMeanM < 2.55f)
              ? (uint8_t)lroundf(ionoMeanM * 100.0f) : 255;
  e.bps   = bytesSec > 65535 ? 65535 : (uint16_t)bytesSec;

  if (haveFix && haveRef) {
    double mPerDegLon = 111320.0 * cos(hdr->refLat * M_PI / 180.0);
    e.dN = toCm((lat - hdr->refLat) * 111320.0);
    e.dE = toCm((lon - hdr->refLon) * mPerDegLon);
    e.dU = toCm(alt - hdr->refAlt);
    e.valid = 1;
  } else {
    e.dN = e.dE = e.dU = 0;
    // Still a real sample: satellites, C/N0 and RTCM rate are meaningful
    // without a fix, and a gap in the position trace is itself information.
    e.valid = 2;
  }

  ringPut(hdr->head, w);
  hdr->head = (uint16_t)((hdr->head + 1) % hdr->count);
  if (hdr->filled < hdr->count) hdr->filled++;
}

size_t historySize() {
  return ringMem ? blobLen : 0;
}

size_t historyRead(size_t offset, uint8_t *dst, size_t maxLen) {
  if (!ringMem || offset >= blobLen) return 0;
  if (offset == 0) hdr->uptimeSec = millis() / 1000;
  size_t n = blobLen - offset;
  if (n > maxLen) n = maxLen;
  size_t done = 0;
  // Header from ordinary RAM.
  while (done < n && offset + done < sizeof(HistHeader)) {
    dst[done] = ((const uint8_t *)hdr)[offset + done];
    done++;
  }
  // Ring a word at a time; the response buffer is ordinary RAM, so bytes are
  // picked out of each word there.
  while (done < n) {
    size_t r = offset + done - sizeof(HistHeader);
    uint32_t word = ((volatile const uint32_t *)ringMem)[r / 4];
    for (size_t b = r % 4; b < 4 && done < n; b++, done++)
      dst[done] = (uint8_t)(word >> (8 * b));
  }
  return done;
}
