#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <Config.h>

// ==========================================
// NETWORK STATUSES
// ==========================================
// The soft-AP is always up: it is the low-latency path for the rover (one
// wireless hop, no router in between). Station mode is optional and only adds
// a management/uplink route.
enum NetState { NET_AP, NET_CONNECTING, NET_STA, NET_RECONNECTING };
extern NetState currentNetState;
extern uint32_t netStateTimer;
extern String targetSSID;
extern String targetPass;
extern Preferences prefs;
extern bool newCredentialsReceived;
extern bool wifiResetRequested;
extern bool apRestartRequested;

struct ApCfg {
  char    ssid[33];
  char    pass[65];   // empty = open network
  uint8_t channel;    // 1..13
  bool    hidden;
};
extern ApCfg apCfg;

// ==========================================
// OBJECTS AND HARDWARE
// ==========================================
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern TinyGPSPlus gps;

extern WiFiServer rtcmServer;

// FreeRTOS Task, Mutex and Queue Definitions
extern TaskHandle_t NetworkTaskHandle;
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t tcpMutex;
extern SemaphoreHandle_t baseMutex;  // guards baseCfg
extern QueueHandle_t termQueue;

extern portMUX_TYPE ppsMux;

// ==========================================
// GNSS DATA MODEL
// ==========================================

// Order matters: it indexes SYS_NAMES / SYS_CODES / BAND_NAMES in GNSS_Core.
enum GnssSys : uint8_t {
  SYS_GPS = 0, SYS_GLO, SYS_GAL, SYS_BDS, SYS_QZS, SYS_SBS, SYS_NAV, SYS_UNK,
  SYS_COUNT
};

#define BANDS_PER_SYS 3

// One tracked signal: a satellite observed on one frequency band. A dual-band
// satellite occupies two entries that share (sys, prn).
struct SatSignal {
  uint8_t  sys;       // GnssSys
  uint8_t  band;      // 0..BANDS_PER_SYS-1, index into BAND_NAMES[sys]
  int16_t  prn;
  int16_t  elev;
  int16_t  azim;
  uint8_t  snr;       // C/N0 in dB-Hz
  uint32_t lastSeen;
};

extern SatSignal activeSignals[MAX_SIGNALS];
extern int activeSignalCount;

// Satellites the receiver reports as contributing to the position solution
// (parsed from GSA). Kept separate because a satellite can appear here before
// its GSV block arrives.
struct UsedSat {
  uint8_t  sys;
  int16_t  prn;
  uint32_t lastSeen;
};

extern UsedSat usedSats[MAX_USED_SATS];
extern int usedSatCount;

struct GpsSnapshot {
  double lat, lon, alt, hdop, pdop, vdop, geoidSep;
  bool validLoc, validAlt, validHdop, validTime;
  uint8_t hour, min, sec;
  uint8_t fixQual;
  uint8_t fixType;    // GSA mode2: 1 = no fix, 2 = 2D, 3 = 3D
  uint8_t satsInUse;  // GGA field 7
};
extern GpsSnapshot safeGps;

// Base station configuration mirrored from the module (LC29H(BS) protocol).
struct BaseCfg {
  int8_t   svinMode;      // -1 unknown, 0 disabled, 1 survey-in, 2 fixed
  uint32_t minDur;        // seconds, 0..86400
  float    accLimit;      // metres, 0 = no limit
  double   ecefX, ecefY, ecefZ;
  int8_t   rtcmMode;      // -2 unknown, -1 off, 0 MSM4, 1 MSM7
  int8_t   arpEnabled;    // -1 unknown, 0/1  (RTCM 1005)
  int8_t   ephEnabled;    // -1 unknown, 0/1  (RTCM 1019/1020/1042/1044/1046)
  uint32_t svinStartMs;   // millis() when survey-in was last (re)armed, 0 = n/a
  char     verStr[40];
  char     buildDate[12];
  char     lastResult[56];
};
extern BaseCfg baseCfg;

// The (BS) protocol spec documents only a subset of the LC29H series command
// set, so anything borrowed from the series spec is probed rather than assumed.
enum FeatState : uint8_t { FEAT_UNKNOWN = 0, FEAT_OK, FEAT_UNSUPPORTED };

// $PQTMSVINSTATUS - real survey-in progress straight from the receiver.
struct SvinStatus {
  uint8_t  feat;
  uint8_t  valid;     // 0 invalid, 1 in progress, 2 valid
  uint32_t obs;       // position observations used so far
  uint32_t cfgDur;    // duration the module was configured with
  double   x, y, z;   // running mean ECEF, metres
  float    acc;       // running mean accuracy, metres
  uint32_t lastMs;
};
extern SvinStatus svin;

// $PAIR391 -> $PAIRSPF / $PAIRSPF5 interference status.
struct JamStatus {
  uint8_t  feat;
  uint8_t  l1, l5;    // 0 unknown, 1 clean, 2 warning, 3 critical
  bool     haveL5;
  uint32_t lastMs;
};
extern JamStatus jam;

// Offset from the surveyed ground marker to the antenna reference point.
struct ArpCfg { float north, east, up; };
extern ArpCfg arpCfg;

// ESP-side averaging of the fix, used to seed a fixed base coordinate.
struct PosAvg {
  bool     running;
  uint32_t startedMs;
  uint32_t targetSec;
  uint32_t count;
  double   sumLat, sumLon, sumAlt;
  double   sumLat2, sumLon2;
  double   lat, lon, alt;   // result of the last completed run
  float    rms;             // horizontal spread of that run, metres
  bool     haveResult;
};
extern PosAvg posAvg;

// Staging values written only by the GNSS task on core 1 while a serial buffer
// is being parsed, then published into safeGps under dataMutex. Keeps the
// sentence parsers free of locking without exposing torn reads to core 0.
extern uint8_t stageFixType;
extern uint8_t stageSatsInUse;
extern double  stagePdop, stageVdop, stageGeoidSep;

// ==========================================
// RTCM OUTPUT
// ==========================================
enum ClientMode : uint8_t { CM_EMPTY = 0, CM_SNIFF, CM_RAW, CM_NTRIP };

struct OutputCfg {
  bool     tcpEnabled;
  uint16_t tcpPort;
  uint8_t  acceptMode;   // 0 = auto-detect, 1 = NTRIP only, 2 = raw only
  bool     udpEnabled;
  uint16_t udpPort;
  // Optional fixed destination. Software that cannot send a registration
  // datagram (Mission Planner and most autopilot GCS) is streamed to here.
  char     udpDest[16];
  uint16_t udpDestPort;
  // Resolved once when the setting changes. The RTCM path runs on the other
  // core and used to parse the string on every frame, while a web request
  // could be rewriting it - a torn read waiting to happen, and needless work
  // in the hot path besides.
  uint32_t udpDestAddr;
  // Sends every frame to the subnet broadcast address as well, so a listener
  // bound to udpPort receives the stream without any handshake at all.
  bool     udpBroadcast;
  char     mount[24];    // NTRIP mountpoint, without the leading '/'
  char     ntripUser[24];
  char     ntripPass[24]; // empty user AND pass = unauthenticated
};
extern OutputCfg outCfg;

struct RtcmStats {
  uint32_t frames;        // valid frames forwarded since boot
  uint32_t crcErrors;
  uint32_t bytesSec;      // bytes forwarded in the last telemetry second
  uint32_t bytesAcc;
  uint16_t types[RTCM_MAX_TYPES];
  uint16_t typeHits[RTCM_MAX_TYPES];
  uint32_t typeLastMs[RTCM_MAX_TYPES];
  uint16_t typeInterval[RTCM_MAX_TYPES]; // smoothed spacing, ms
  uint16_t typeJitter[RTCM_MAX_TYPES];   // worst deviation seen, ms
  uint8_t  typeCount;
  uint32_t lastFrameMs;
};
extern RtcmStats rtcmStats;

// Decoded from the outgoing RTCM 1005 stream: the antenna reference point the
// module is actually broadcasting. This is the ground truth for what rovers
// receive, and it is how the UI can tell that a survey-in has finished even
// though $PQTMCFGSVIN still reports the configured mode rather than the
// operating one.
struct BaseBroadcast {
  bool     valid;
  uint16_t stationId;
  double   x, y, z;      // metres
  uint32_t lastMs;
  // The module keeps emitting 1005 while a survey is still running, so mere
  // presence proves nothing. A coordinate that stops moving does.
  uint32_t stableSince;
};
extern BaseBroadcast bcast;

// One dual-frequency satellite arc for the ionospheric monitor.
struct IonoSat {
  uint8_t  sys;         // GnssSys
  int16_t  prn;
  float    slantRaw;    // code-derived slant delay, metres - UNCALIBRATED
  float    slantDelta;  // carrier-derived change since the arc started, metres
  float    vertDelta;   // same, mapped to the vertical
  float    refPhase;    // (L1-L2)/(g-1) at the arc start
  float    lastPhase;
  int16_t  elev, azim;
  float    ippLat, ippLon;
  uint32_t arcStartMs;
  uint32_t lastMs;
  // DF407 lock time and DF420 half-cycle flag per band: the receiver's own
  // continuity indicators. Guessing at slips from a magnitude threshold lets
  // small ones through, and they accumulate into the arc for good.
  uint16_t lock1, lock2;
  uint8_t  half1, half2;
  bool     hasRef;
};
extern IonoSat ionoSats[MAX_IONO_SATS];
extern int     ionoSatCount;
extern uint32_t ionoEpochs;

// UDP receive diagnostics: lets the UI answer "is the rover talking to us at
// all?" instead of leaving the user guessing why nothing arrives.
extern uint32_t udpRxCount;
extern char     udpLastFrom[24];
extern uint32_t udpLastRxMs;

// Outbound NTRIP: this base pushes its stream to a remote caster (NTRIP v1
// server protocol, which is what RTK2go and friends accept).
struct NtripPushCfg {
  bool     enabled;
  char     host[64];
  uint16_t port;
  char     mount[24];
  char     pass[32];
};
extern NtripPushCfg pushCfg;

enum PushState : uint8_t {
  PUSH_OFF = 0, PUSH_WAIT, PUSH_CONNECTING, PUSH_HANDSHAKE, PUSH_STREAMING, PUSH_ERROR
};

struct NtripPushState {
  uint8_t  state;
  char     msg[56];
  uint32_t sinceMs;    // when the current state was entered
  uint32_t sent;       // bytes pushed since the stream came up
  uint32_t retries;
};
extern NtripPushState pushState;

// A connected consumer, either raw TCP or NTRIP.
struct RtcmClientInfo {
  uint8_t  mode;
  char     ip[16];
  uint32_t since;
  uint32_t sent;      // bytes pushed to this client
};

struct UdpClientInfo {
  char     ip[16];
  uint16_t port;
  uint32_t since;
  uint32_t sent;
};

extern uint32_t rtcmPaketSayaci;
extern volatile uint32_t sonPpsZamaniMicros;
extern uint8_t globalFixQuality;

// Counters for Core CPU Load Tracking
extern uint32_t core0BusyTimeAcc;
extern uint32_t core1BusyTime;

extern char nmeaBuff[MAX_NMEA];
extern int nmeaIdx;

void IRAM_ATTR ppsKesmesi();
