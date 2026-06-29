#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "Globals.h"
#include "WebUI.h"
#include "GNSS_Core.h"

// ==========================================
// GLOBAL DEĞİŞKENLERİN TANIMLANMASI
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

uint32_t core1BusyTime = 0; 

char nmeaBuff[MAX_NMEA];
int nmeaIdx = 0;

// ==========================================
// KESMELER VE OLAYLAR
// ==========================================
void IRAM_ATTR ppsKesmesi() {
  portENTER_CRITICAL_ISR(&ppsMux);
  sonPpsZamaniMicros = micros(); 
  portEXIT_CRITICAL_ISR(&ppsMux);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      char cmd[128];
      size_t copyLen = len < 127 ? len : 127;
      memcpy(cmd, data, copyLen);
      cmd[copyLen] = '\0'; 
      
      Serial2.print(cmd);
      Serial2.print("\r\n");
      Serial.print("[WS] Command Sent to Module: ");
      Serial.println(cmd);
    }
  }
}

// ==========================================
// CORE 0: NETWORK, TELEMETRY AND WATCHDOG TASK
// ==========================================
void networkTaskCode(void * parameter) {
  static unsigned long sonJsonZamani = 0;
  static SatData localSats[MAX_SATS];
  
  static char jsonBuffer[8192]; 
  
  static uint32_t core0BusyTimeAcc = 0; 
  static uint32_t lastCpuCheckTime = millis(); 

  for(;;) {
    uint32_t c0TaskStart = micros();
    uint32_t now = millis();

    if (currentNetState == NET_AP) {
        ArduinoOTA.handle();
        if (newCredentialsReceived) {
            newCredentialsReceived = false;
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(targetSSID.c_str(), targetPass.c_str());
            currentNetState = NET_CONNECTING;
            netStateTimer = millis();
            Serial.println("[WIFI] Trying to connect to new network: " + targetSSID);
        }
    }
    else if (currentNetState == NET_CONNECTING) {
        ArduinoOTA.handle();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WIFI] Connected! STA IP: " + WiFi.localIP().toString());
            prefs.putString("ssid", targetSSID);
            prefs.putString("pass", targetPass);
            currentNetState = NET_SHOW_IP;
            netStateTimer = millis();
        } else if (now - netStateTimer > 30000) { 
            Serial.println("[WIFI] Connection failed. Returning to AP Mode.");
            WiFi.disconnect();
            WiFi.mode(WIFI_AP);
            WiFi.softAP("ESP32_RTK_BASE");
            currentNetState = NET_AP;
        }
    }
    else if (currentNetState == NET_SHOW_IP) {
        ArduinoOTA.handle(); 
        if (now - netStateTimer > 60000) { 
            Serial.println("[WIFI] 1 minute display over. AP closing, continuing in STA mode only.");
            WiFi.mode(WIFI_STA); 
            currentNetState = NET_STA;
        }
    }
    else if (currentNetState == NET_STA) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] Connection lost! Attempting to reconnect (Watchdog Active)...");
            WiFi.disconnect();
            WiFi.reconnect();
            currentNetState = NET_RECONNECTING;
            netStateTimer = millis();
        }
    }
    else if (currentNetState == NET_RECONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WIFI] Reconnected!");
            currentNetState = NET_STA;
        } else if (now - netStateTimer > 60000) { 
            Serial.println("[WIFI] Could not connect to network in 1 minute. Opening AP for recovery.");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("ESP32_RTK_BASE");
            currentNetState = NET_AP;
        }
    }

    if (ws.count() > 0) {
      ws.cleanupClients();
    }

    char queuedMsg[160];
    while (xQueueReceive(termQueue, queuedMsg, 0) == pdTRUE) {
      if (ws.count() > 0) {
        size_t len = strlen(queuedMsg);
        AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len);
        if (buffer) {
            memcpy(buffer->get(), queuedMsg, len);
            ws.textAll(buffer);
        }
      }
    }

    uint32_t timeDiffMillis = now - lastCpuCheckTime;
    if (timeDiffMillis >= 1000) {
      
      cleanOldSatellites(); 

      if (ws.count() > 0) {
        int localSatCount = 0;
        
        double locLat = 0.0, locLon = 0.0, locAlt = 0.0, locHdop = 0.0;
        bool locValidLoc = false, locValidAlt = false, locValidHdop = false, locTimeValid = false;
        char locTime[12] = "--:--:--";
        uint32_t currentRtcmCount = 0;
        uint32_t lastPps = 0;
        uint8_t locFixQual = 0;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
          localSatCount = activeSatCount;
          memcpy(localSats, (const void*)activeSats, localSatCount * sizeof(SatData));
          
          locLat = safeGps.lat; locLon = safeGps.lon; 
          locAlt = safeGps.alt; locHdop = safeGps.hdop;
          locValidLoc = safeGps.validLoc; 
          locValidAlt = safeGps.validAlt;
          locValidHdop = safeGps.validHdop; 
          locTimeValid = safeGps.validTime;
          locFixQual = safeGps.fixQual;

          if (locTimeValid) sprintf(locTime, "%02d:%02d:%02d", safeGps.hour, safeGps.min, safeGps.sec);
          
          currentRtcmCount = rtcmPaketSayaci;
          rtcmPaketSayaci = 0; 
          
          xSemaphoreGive(dataMutex);
        }

        portENTER_CRITICAL(&ppsMux);
        lastPps = sonPpsZamaniMicros;
        portEXIT_CRITICAL(&ppsMux);

        uint32_t timeDiffMicros = timeDiffMillis * 1000;

        uint32_t c1Busy = core1BusyTime;
        core1BusyTime = 0; 
        int cpu1Usage = (c1Busy * 100) / timeDiffMicros;
        if(cpu1Usage > 100) cpu1Usage = 100;

        uint32_t c0Busy = core0BusyTimeAcc;
        core0BusyTimeAcc = 0; 
        int cpu0Usage = (c0Busy * 100) / timeDiffMicros;
        if(cpu0Usage > 100) cpu0Usage = 100;
        
        lastCpuCheckTime = now; 

        int activeTcp = 0;
        if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
          for (int i = 0; i < 3; i++) {
            if (tcpClients[i].connected()) {
              activeTcp++;
            } else {
              tcpClients[i].stop();
            }
          }
          xSemaphoreGive(tcpMutex);
        }

        const char* modeStr = "NO FIX";
        if (locValidLoc && locFixQual > 0) {
            modeStr = locValidAlt ? "3D" : "2D";
        }
        
        const char* qualStr = "INVALID";
        switch(locFixQual) {
            case 1: qualStr = "GPS FIX"; break;
            case 2: qualStr = "DGNSS"; break;
            case 3: qualStr = "PPS FIX"; break;
            case 4: qualStr = "RTK FIXED"; break;
            case 5: qualStr = "RTK FLOAT"; break;
            case 6: qualStr = "ESTIMATED"; break;
        }

        #if ARDUINOJSON_VERSION_MAJOR >= 7
          static JsonDocument doc; 
          doc.clear(); 
        #else
          StaticJsonDocument<8192> doc; 
        #endif
        
        doc["lat"] = locValidLoc ? locLat : 0.0;
        doc["lon"] = locValidLoc ? locLon : 0.0;
        doc["alt"] = locValidAlt ? locAlt : 0.0;
        doc["hdop"] = locValidHdop ? locHdop : 0.0;
        doc["tcp_clients"] = activeTcp;
        doc["rtcm"] = currentRtcmCount;
        doc["sat_time"] = locTime;
        doc["pps_active"] = (micros() - lastPps < 2000000) ? true : false; 
        
        doc["f_mode"] = modeStr;
        doc["f_qual"] = qualStr;
        doc["cpu0"] = cpu0Usage; 
        doc["cpu1"] = cpu1Usage; 

        int gpsL1=0, gpsL5=0, gloL1=0, galE1=0, galE5a=0, beiB1=0, beiB2a=0, qzsL1=0, qzsL5=0, navL5=0, sbaL1=0;
        JsonArray sky = doc["sky"].to<JsonArray>();
        
        for (int i = 0; i < localSatCount; i++) {
          SatData s = localSats[i];
          if(strcmp(s.sys, "GP") == 0) { if(s.sig == 8) gpsL5++; else gpsL1++; }
          else if(strcmp(s.sys, "GL") == 0) { gloL1++; }
          else if(strcmp(s.sys, "GA") == 0) { if(s.sig == 1 || s.sig == 2 || s.sig == 3) galE5a++; else galE1++; }
          else if(strcmp(s.sys, "GB") == 0) { if(s.sig == 5 || s.sig == 6 || s.sig == 7 || s.sig == 0xB || s.sig == 0xC) beiB2a++; else beiB1++; }
          else if(strcmp(s.sys, "GQ") == 0) { if(s.sig == 8) qzsL5++; else qzsL1++; }
          else if(strcmp(s.sys, "GI") == 0) { navL5++; }
          else if(strcmp(s.sys, "SB") == 0) { sbaL1++; } 
          
          if(s.elev != 0 || s.azim != 0) {
            bool isPrimary = true;
            if((strcmp(s.sys, "GP") == 0 || strcmp(s.sys, "GQ") == 0) && s.sig == 8) isPrimary = false;
            if(strcmp(s.sys, "GA") == 0 && (s.sig == 1 || s.sig == 2 || s.sig == 3)) isPrimary = false;
            if(strcmp(s.sys, "GB") == 0 && (s.sig == 5 || s.sig == 6 || s.sig == 7 || s.sig == 0xB || s.sig == 0xC)) isPrimary = false;

            if (isPrimary) {
              JsonObject obj = sky.add<JsonObject>();
              obj["s"] = s.sys; obj["id"] = s.id; obj["e"] = s.elev; obj["a"] = s.azim; obj["sn"] = s.snr; 
            }
          }
        }

        JsonObject sats = doc["sats"].to<JsonObject>();
        JsonObject s_gps = sats["gps"].to<JsonObject>(); s_gps["L1"] = gpsL1; s_gps["L5"] = gpsL5;
        JsonObject s_glo = sats["glo"].to<JsonObject>(); s_glo["L1"] = gloL1;
        JsonObject s_gal = sats["gal"].to<JsonObject>(); s_gal["E1"] = galE1; s_gal["E5a"] = galE5a;
        JsonObject s_bei = sats["bei"].to<JsonObject>(); s_bei["B1"] = beiB1; s_bei["B2a"] = beiB2a;
        JsonObject s_qzs = sats["qzs"].to<JsonObject>(); s_qzs["L1"] = qzsL1; s_qzs["L5"] = qzsL5;
        JsonObject s_nav = sats["nav"].to<JsonObject>(); s_nav["L5"] = navL5;
        
        JsonObject s_sba = sats["sba"].to<JsonObject>(); s_sba["L1"] = sbaL1;

        size_t jsonLen = serializeJson(doc, jsonBuffer);
        
        AsyncWebSocketMessageBuffer * wsBuf = ws.makeBuffer(jsonLen);
        if (wsBuf) {
            memcpy(wsBuf->get(), jsonBuffer, jsonLen);
            ws.textAll(wsBuf); 
        }
      }
    }
    
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
  
  prefs.begin("wifi_creds", false);
  targetSSID = prefs.getString("ssid", "");
  targetPass = prefs.getString("pass", "");

  if (targetSSID != "") {
      currentNetState = NET_CONNECTING;
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("ESP32_RTK_BASE"); 
      WiFi.begin(targetSSID.c_str(), targetPass.c_str());
      netStateTimer = millis();
      Serial.println("Connecting to saved network: " + targetSSID);
  } else {
      currentNetState = NET_AP;
      WiFi.mode(WIFI_AP);
      WiFi.softAP("ESP32_RTK_BASE");
      Serial.println("No saved network. AP Mode started: ESP32_RTK_BASE");
  }

  ArduinoOTA.setHostname("ESP32-RTK-BASE");
  ArduinoOTA.begin();

  Serial.print("WiFi Settings Page IP Address (AP Mode): ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      if (currentNetState == NET_STA) {
          request->send(200, "text/html", index_html);
      } else if (currentNetState == NET_SHOW_IP) {
          String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body style='background:#121212;color:#00ffcc;font-family:sans-serif;text-align:center;margin-top:50px;'>";
          html += "<h2>✅ Connected Successfully!</h2>";
          html += "<p>ESP32 received the following IP address from the network:</p>";
          html += "<h1 style='color:#fff;'>" + WiFi.localIP().toString() + "</h1>";
          html += "<p style='color:#aaa;'>Please go to this new IP address in your browser. The ESP32 will turn off its own AP broadcast in 1 minute.</p>";
          html += "</body></html>";
          request->send(200, "text/html", html);
      } else if (currentNetState == NET_CONNECTING) {
          String html = "<html><head><meta http-equiv='refresh' content='3'><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body style='background:#121212;color:#ffdd00;text-align:center;font-family:sans-serif;margin-top:50px;'><h2>Connecting to Network...</h2><p>Please wait...</p></body></html>";
          request->send(200, "text/html", html);
      } else {
          request->send(200, "text/html", wifi_html);
      }
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
      int n = WiFi.scanNetworks(false, true); 
      String json = "[";
      for (int i = 0; i < n; ++i) {
          if (i > 0) json += ",";
          json += "\"" + WiFi.SSID(i) + "\"";
      }
      json += "]";
      request->send(200, "application/json", json);
  });

  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
      if (request->hasParam("ssid") && request->hasParam("pass")) {
          targetSSID = request->getParam("ssid")->value();
          targetPass = request->getParam("pass")->value();
          newCredentialsReceived = true;
          request->send(200, "text/plain", "OK");
      } else {
          request->send(400, "text/plain", "Missing parameter sent.");
      }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
      String json = "{\"state\":" + String(currentNetState) + ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      request->send(200, "application/json", json);
  });

  server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("c")){
      String cmdStr = request->getParam("c")->value();
      Serial2.print(cmdStr); 
      Serial2.print("\r\n"); 
      request->send(200, "text/plain", "OK"); 
    } else {
      request->send(400, "text/plain", "EMPTY");
    }
  });

  ws.onEvent(onWsEvent); 
  server.addHandler(&ws);
  rtcmServer.begin();
  server.begin();

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
// 7. CORE 1: HIGH-SPEED GNSS DATA PROCESSING
// ==========================================
void loop() {
  uint32_t loopStart = micros();

  if (rtcmServer.hasClient()) {
    WiFiClient newClient = rtcmServer.available();
    if (newClient) {
      newClient.setNoDelay(true); 
      
      bool added = false;
      if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
        for (int i = 0; i < 3; i++) {
          if (!tcpClients[i].connected()) {
            tcpClients[i].stop();
            tcpClients[i] = newClient;
            added = true;
            break;
          }
        }
        xSemaphoreGive(tcpMutex);
      }
      if (!added) newClient.stop(); 
    }
  }

  size_t bytesAvailable = Serial2.available();
  if (bytesAvailable > 0) {
    uint8_t buf[256]; 
    if (bytesAvailable > sizeof(buf)) bytesAvailable = sizeof(buf);
    
    size_t len = Serial2.read(buf, bytesAvailable);

    if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
      for (int i = 0; i < 3; i++) {
        if (tcpClients[i].connected()) {
          tcpClients[i].write(buf, len);
        }
      }
      xSemaphoreGive(tcpMutex);
    }

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