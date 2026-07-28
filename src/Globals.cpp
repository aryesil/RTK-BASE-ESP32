#include <Globals.h>

NetState currentNetState = NET_AP;
uint32_t netStateTimer = 0;
String targetSSID = "";
String targetPass = "";
Preferences prefs;
bool newCredentialsReceived = false;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
TinyGPSPlus gps;

WiFiServer rtcmServer(RTCM_PORT);

ApCfg apCfg = { AP_SSID, "", 6, false };
bool wifiResetRequested = false;
bool apRestartRequested = false;

OutputCfg outCfg = { true, RTCM_PORT, 0, true, UDP_PORT, "", 0, 0, false, "RTK", "", "" };
RtcmStats rtcmStats = {};
BaseBroadcast bcast = {};

IonoSat ionoSats[MAX_IONO_SATS];
int ionoSatCount = 0;
uint32_t ionoEpochs = 0;

uint32_t udpRxCount = 0;
char     udpLastFrom[24] = "";
uint32_t udpLastRxMs = 0;

SvinStatus svin = {};
JamStatus  jam  = {};
ArpCfg     arpCfg = { 0.0f, 0.0f, 0.0f };
PosAvg     posAvg = {};

NtripPushCfg   pushCfg   = { false, "", 2101, "", "" };
NtripPushState pushState = { PUSH_OFF, "", 0, 0, 0 };

TaskHandle_t NetworkTaskHandle;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t tcpMutex;
SemaphoreHandle_t baseMutex;
QueueHandle_t termQueue;

portMUX_TYPE ppsMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t sonPpsZamaniMicros = 0;

SatSignal activeSignals[MAX_SIGNALS];
int activeSignalCount = 0;

UsedSat usedSats[MAX_USED_SATS];
int usedSatCount = 0;

GpsSnapshot safeGps;

BaseCfg baseCfg = {
  -1,          // svinMode
  0,           // minDur
  0.0f,        // accLimit
  0.0, 0.0, 0.0,
  -2,          // rtcmMode
  -1, -1,      // arpEnabled, ephEnabled
  0,           // svinStartMs
  "", "", ""
};

uint8_t stageFixType = 0;
uint8_t stageSatsInUse = 0;
double  stagePdop = 0.0, stageVdop = 0.0, stageGeoidSep = 0.0;

uint32_t rtcmPaketSayaci = 0;
uint8_t globalFixQuality = 0;

uint32_t core0BusyTimeAcc = 0;
uint32_t core1BusyTime = 0;

char nmeaBuff[MAX_NMEA];
int nmeaIdx = 0;
