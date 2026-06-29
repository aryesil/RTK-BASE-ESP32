#include <Arduino.h>
#include <Config.h>
#include <Globals.h>
#include <gnss/GNSS_Core.h>
#include <network/NetworkManager.h>
#include <web/WebServerManager.h>
#include <network/RTCMSocket.h>

// ==========================================
// IDENTIFICATION OF GLOBAL VARIABLES
// ==========================================
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

SatData activeSats[MAX_SATS];
int activeSatCount = 0;
GpsSnapshot safeGps;

uint32_t rtcmPaketSayaci = 0;
volatile uint32_t sonPpsZamaniMicros = 0; 
uint8_t globalFixQuality = 0; 

uint32_t core0BusyTimeAcc = 0;
uint32_t core1BusyTime = 0; 

char nmeaBuff[MAX_NMEA];
int nmeaIdx = 0;

// ==========================================
// INTERRUPTS AND EVENTS
// ==========================================
void IRAM_ATTR ppsKesmesi() {
  portENTER_CRITICAL_ISR(&ppsMux);
  sonPpsZamaniMicros = micros(); 
  portEXIT_CRITICAL_ISR(&ppsMux);
}

// ==========================================
// CORE 0: NETWORK, TELEMETRY AND WATCHDOG TASK
// ==========================================
void networkTaskCode(void * parameter) {
  for(;;) {
    uint32_t c0TaskStart = micros();
    uint32_t now = millis();

    // Check the WiFi connection status (AP, STA, etc.).
    handleNetworkState(now);

    // Manage requests and message queues coming from WebSocket.
    handleWebSocketQueue();

    // Run CPU load measurements and send telemetry (JSON) once per second.
    handleTelemetry(now);
    
    uint32_t c0TaskEnd = micros();
    if (c0TaskEnd >= c0TaskStart) {
        core0BusyTimeAcc += (c0TaskEnd - c0TaskStart);
    }
    
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  Serial2.setRxBufferSize(2048);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  dataMutex = xSemaphoreCreateMutex();
  tcpMutex = xSemaphoreCreateMutex();
  termQueue = xQueueCreate(15, sizeof(char) * 160);

  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);

  Serial.println("\n=== SYSTEM STARTING ===");
  
  // Starting Modules
  setupNetwork();
  setupWebServer();
  setupRTCMSocket();

  const char* initCommands[] = {
    "$PAIR062,0,1*3F",
    "$PAIR062,1,1*3E", 
    "$PAIR062,2,1*3D", 
    "$PAIR062,3,1*3C", 
    "$PAIR062,4,1*3B",  
    "$PAIR062,6,1*39", 
    "$PAIR062,7,1*38", 
    "$PAIR062,8,1*37", 
    "$PQTMCFGSVIN,W,1,60,10,0,0,0*25", 
    "$PAIR411,1*23", 
    "$PAIR432,1*22", 
    "$PAIR434,1*24", 
    "$PAIR436,1*26", 
    "$PQTMSAVEPAR*5A"
  };

  int numCmds = sizeof(initCommands) / sizeof(initCommands[0]);
  
  Serial.println("\n=== GNSS CONFIGURATION STARTING ===");
  for (int i = 0; i < numCmds; i++) {
    sendGnssCommand(initCommands[i], 1000); 
  }
  Serial.println("=== GNSS CONFIGURATION COMPLETED ===\n");

  xTaskCreatePinnedToCore(networkTaskCode, "NetworkTask", 16384, NULL, 1, &NetworkTaskHandle, 0);
}

// ==========================================
// CORE 1: HIGH-SPEED GNSS DATA PROCESSING
// ==========================================
void loop() {
  uint32_t loopStart = micros();

  // Yeni TCP İstemci bağlantılarını kontrol et
  handleNewRTCMClients();

  size_t bytesAvailable = Serial2.available();
  if (bytesAvailable > 0) {
    uint8_t buf[256]; 
    if (bytesAvailable > sizeof(buf)) bytesAvailable = sizeof(buf);
    
    size_t len = Serial2.read(buf, bytesAvailable);

    // Transfer incoming Raw Data / RTCM data to TCP Clients
    broadcastRTCM(buf, len);

    static enum { WAIT_SYNC, WAIT_LEN1, WAIT_LEN2, SKIP_PAYLOAD } rtcmState = WAIT_SYNC;
    static uint16_t rtcmLen = 0;
    static uint16_t rtcmBytesRead = 0;
    static uint32_t lastRtcmTime = 0;
    static uint16_t totalRtcmBytes = 0; 

    if (rtcmState != WAIT_SYNC && (millis() - lastRtcmTime > 50)) {
        rtcmState = WAIT_SYNC;
    }
    lastRtcmTime = millis();

    for (size_t i = 0; i < len; i++) {
      uint8_t b = buf[i];
      
      if (rtcmState == WAIT_SYNC && b == 0xD3) {
        rtcmState = WAIT_LEN1;
        rtcmBytesRead = 1;
      } else if (rtcmState == WAIT_LEN1) {
        rtcmLen = (b & 0x03) << 8;
        rtcmState = WAIT_LEN2;
        rtcmBytesRead++;
      } else if (rtcmState == WAIT_LEN2) {
        rtcmLen |= b;
        totalRtcmBytes = rtcmLen + 6; 
        rtcmState = SKIP_PAYLOAD;
        rtcmBytesRead++;
      } else if (rtcmState == SKIP_PAYLOAD) {
        rtcmBytesRead++;
        if (rtcmBytesRead >= totalRtcmBytes) { 
          if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            rtcmPaketSayaci++;
            xSemaphoreGive(dataMutex);
          }
          rtcmState = WAIT_SYNC;
        }
      } else {
        gps.encode(b); 
        char c = (char)b;
        
        if (c == '$') {
          nmeaIdx = 0;
          nmeaBuff[nmeaIdx++] = c;
        } else if (c == '\n') {
          if (nmeaIdx > 0 && nmeaBuff[0] == '$') {
            if (nmeaBuff[nmeaIdx - 1] == '\r') nmeaBuff[nmeaIdx - 1] = '\0';
            else nmeaBuff[nmeaIdx] = '\0'; 
            
            if (isChecksumValid(nmeaBuff)) {
              
              if (strncmp(nmeaBuff + 3, "GGA,", 4) == 0) {
                  int commas[10];
                  int cCount = 0;
                  for (int k = 0; nmeaBuff[k] != '\0' && cCount < 10; k++) {
                      if (nmeaBuff[k] == ',') commas[cCount++] = k;
                  }
                  if (cCount >= 6) {
                      char qChar = nmeaBuff[commas[5] + 1];
                      if (qChar >= '0' && qChar <= '9') {
                          globalFixQuality = qChar - '0';
                      }
                  }
              }

              uyduTipleriniAyristir(nmeaBuff); 
              
              if (strncmp(nmeaBuff, "$GN", 3) != 0 && strncmp(nmeaBuff, "$GP", 3) != 0 && 
                  strncmp(nmeaBuff, "$GL", 3) != 0 && strncmp(nmeaBuff, "$GA", 3) != 0 && 
                  strncmp(nmeaBuff, "$GB", 3) != 0 && strncmp(nmeaBuff, "$GQ", 3) != 0 && 
                  strncmp(nmeaBuff, "$GI", 3) != 0 && strncmp(nmeaBuff, "$BD", 3) != 0 &&
                  strncmp(nmeaBuff, "$SB", 3) != 0) {
                
                char termMsg[160];
                snprintf(termMsg, sizeof(termMsg), "TERM:%s", nmeaBuff);
                if(xQueueSend(termQueue, termMsg, 0) != pdTRUE) {
                   // Silent drop when queue is full
                }
              }
            }
          }
          nmeaIdx = 0; 
        } else if (c >= 32 && c <= 126) {
          if (nmeaIdx < MAX_NMEA - 1) {
              nmeaBuff[nmeaIdx++] = c;
          }
        }
      }
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      safeGps.lat = gps.location.lat(); safeGps.lon = gps.location.lng();
      safeGps.alt = gps.altitude.meters(); safeGps.hdop = gps.hdop.hdop();
      safeGps.validLoc = gps.location.isValid(); safeGps.validAlt = gps.altitude.isValid();
      safeGps.validHdop = gps.hdop.isValid(); safeGps.validTime = gps.time.isValid();
      safeGps.hour = gps.time.hour(); safeGps.min = gps.time.minute(); safeGps.sec = gps.time.second();
      safeGps.fixQual = globalFixQuality; 
      xSemaphoreGive(dataMutex);
    }
  }

  uint32_t loopEnd = micros();
  if (loopEnd >= loopStart) {
      core1BusyTime += (loopEnd - loopStart);
  }
  
  vTaskDelay(pdMS_TO_TICKS(1)); 
}