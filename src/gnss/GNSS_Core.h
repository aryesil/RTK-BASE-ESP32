#pragma once
#include <Globals.h>

// Per-constellation display names, single-letter RINEX-style codes and band
// labels. Indexed by GnssSys; BAND_NAMES[sys][band] may be NULL when a
// constellation has fewer than BANDS_PER_SYS bands.
extern const char* const SYS_NAMES[SYS_COUNT];
extern const char  SYS_CODES[SYS_COUNT];
extern const char* const BAND_NAMES[SYS_COUNT][BANDS_PER_SYS];

// Talker id ("$GP", "$GL", ...) -> GnssSys. Returns SYS_UNK for "$GN"/unknown.
uint8_t sysFromTalker(const char* nmea);

// Resolves the real constellation of a PRN reported under `talkerSys`, fixing
// up the NMEA shorthands (SBAS as 33..64, QZSS/SBAS inside the GPS talker).
// `prn` is rewritten in place when the numbering has to be normalised.
uint8_t classifySat(uint8_t talkerSys, int &prn);

// NMEA 0183 v4.11 GSV signal id -> band index for that constellation.
uint8_t bandFromSignalId(uint8_t sys, int sigId);

void addSignal(uint8_t sys, uint8_t band, int prn, int elev, int azim, int snr);
void markSatUsed(uint8_t sys, int prn);
bool isSatUsed(uint8_t sys, int prn);
void cleanOldSatellites();

bool isChecksumValid(const char* sentence);
void parseGSV(const char* nmea);
void parseGSA(const char* nmea);

// Field helpers shared with the base-station response parser.
// nmeaIndexFields returns the comma count and reports the '*' offset (-1 if
// absent); nmeaField copies field `n` (0 = talker) out, empty when missing.
int  nmeaIndexFields(const char* nmea, int* commas, int maxCommas, int &starIdx);
void nmeaField(const char* nmea, const int* commas, int cCount, int starIdx,
               int n, char* out, size_t outSize);

bool sendGnssCommand(const char* cmd, unsigned long timeoutMs = 1000);
void applyGnssConfiguration();
