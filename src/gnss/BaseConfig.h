#pragma once
#include <Globals.h>

// Base-station control for the Quectel LC29H (BS), per "LC29H (BS) GNSS
// Protocol Specification v1.0":
//   $PQTMCFGSVIN   survey-in / fixed / disabled receiver mode
//   $PQTMSAVEPAR   persist $PQTM settings to the module NVM
//   $PQTMRESTOREPAR restore $PQTM defaults
//   $PQTMVERNO     firmware version query
//   $PAIR432/433   RTCM output mode  (-1 off, 0 MSM4, 1 MSM7)
//   $PAIR434/435   RTCM 1005 station ARP output
//   $PAIR436/437   RTCM ephemeris output (1019/1020/1042/1044/1046)

void initBaseConfig();

// Builds "$<body>*CS" with the NMEA XOR checksum and writes it to the module.
String buildNmeaSentence(const String &body);
void sendNmeaSentence(const String &body);

// Mode: 0 = disabled, 1 = survey-in, 2 = fixed (ECEF metres).
// minDur 0..86400 s, accLimit metres (0 = no limit).
void applySvinConfig(int mode, uint32_t minDur, float accLimit,
                     double x, double y, double z);
void applyRtcmMode(int mode);
void applyArpOutput(int enable);
void applyEphOutput(int enable);

void saveModuleParams();
void restoreModuleParams();
void queryBaseConfig();

// Re-sends the RTCM output settings stored in NVS. Called once at boot.
void applyStoredRtcmSettings();

// Consumes $PQTM*/$PAIR4xx replies and updates baseCfg.
// Returns true when the sentence was a base-configuration response.
bool parseBaseResponse(const char* nmea);

// WGS84 geodetic (deg, deg, metres above ellipsoid) -> ECEF metres.
void llaToEcef(double latDeg, double lonDeg, double hgt,
               double &x, double &y, double &z);
void ecefToLla(double x, double y, double z,
               double &latDeg, double &lonDeg, double &hgt);

// Rotates a local East/North/Up offset at (lat, lon) into an ECEF delta.
void enuToEcefDelta(double latDeg, double lonDeg,
                    double e, double n, double u,
                    double &dx, double &dy, double &dz);

// Applies the configured marker -> ARP offset to a geodetic marker position,
// returning the ECEF coordinate the module should broadcast in RTCM 1005.
void markerToArpEcef(double latDeg, double lonDeg, double hgt,
                     double &x, double &y, double &z);

void loadArpCfg();
void saveArpCfg();

// Optional messages borrowed from the LC29H series spec. Both are requested
// once at boot and marked unsupported if nothing ever arrives.
void requestOptionalMessages();
void updateFeatureProbes();     // ages the probes; call ~1 Hz

// ESP-side position averaging used to seed a fixed base coordinate.
void startPosAvg(uint32_t seconds);
void stopPosAvg(bool keepResult);
void feedPosAvg();              // call once per telemetry tick
