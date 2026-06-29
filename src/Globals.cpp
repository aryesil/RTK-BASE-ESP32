#include <Globals.h>

// Değişkenleri burada GERÇEK OLARAK tanımlıyoruz
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
WiFiClient tcpClients[3]; 

TaskHandle_t NetworkTaskHandle;
SemaphoreHandle_t dataMutex; 
SemaphoreHandle_t tcpMutex;  
QueueHandle_t termQueue;     

portMUX_TYPE ppsMux = portMUX_INITIALIZER_UNLOCKED; 
volatile uint32_t sonPpsZamaniMicros = 0; 

SatData activeSats[MAX_SATS];
int activeSatCount = 0;
GpsSnapshot safeGps;

uint32_t rtcmPaketSayaci = 0;
uint8_t globalFixQuality = 0; 

uint32_t core0BusyTimeAcc = 0;
uint32_t core1BusyTime = 0; 

char nmeaBuff[MAX_NMEA];
int nmeaIdx = 0;