#include <web/WebServerManager.h>
#include <Globals.h>
#include <web/WebUI.h>
#include <gnss/GNSS_Core.h>
#include <network/RTCMSocket.h>

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

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){

    IPAddress clientIP = request->client()->remoteIP();
    IPAddress apIP = WiFi.softAPIP();

    bool clientOnAP =
        clientIP[0] == apIP[0] &&
        clientIP[1] == apIP[1] &&
        clientIP[2] == apIP[2];

    if (currentNetState == NET_STA) {
        request->send(200, "text/html", index_html);
        return;
    }

    if (currentNetState == NET_SHOW_IP) {

        // STA users can use direct telemetry.
        if (!clientOnAP) {
            request->send(200, "text/html", index_html);
            return;
        }

        // AP users IP screen + WiFi reset button
        String html;

        html += "<html><head>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "</head><body style='background:#121212;color:#00ffcc;font-family:sans-serif;text-align:center;margin-top:50px;'>";

        html += "<h2>Connected Successfully!</h2>";
        html += "<p>Device connected to WiFi network.</p>";

        html += "<h1 style='color:white;'>";
        html += WiFi.localIP().toString();
        html += "</h1>";

        html += "<p style='color:#aaa;'>";
        html += "Open this IP to access telemetry UI.<br><br>";
        html += "AP will close automatically in 1 minute.";
        html += "</p>";

        html += R"rawliteral(
<button onclick="resetWifi()"
style="padding:12px 20px;font-size:16px;background:#ff4444;color:white;border:none;border-radius:6px;margin-top:20px;">
Select Another WiFi
</button>

<script>
function resetWifi(){
  if(confirm("Disconnect WiFi and return to setup?")){
    fetch('/resetwifi').then(() => {
      document.body.innerHTML =
      "<h2 style='color:white'>Restarting AP mode...</h2>" +
      "<p>Please reconnect to ESP32_RTK_BASE</p>";
    });
  }
}
</script>
)rawliteral";

        html += "</body></html>";

        request->send(200, "text/html", html);
        return;
    }

    if (currentNetState == NET_CONNECTING) {
        request->send(200, "text/html",
            "<html><body style='background:#121212;color:#ffdd00;text-align:center;margin-top:50px;'>"
            "<h2>Connecting...</h2><p>Please wait</p></body></html>"
        );
        return;
    }

    request->send(200, "text/html", wifi_html);
  });


  // WIFI RESET ENDPOINT
  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){

    request->send(200, "text/plain", "OK");

    Serial.println("[WIFI] Reset requested");

    prefs.remove("ssid");
    prefs.remove("pass");

    targetSSID = "";
    targetPass = "";

    WiFi.disconnect(true, true);

    currentNetState = NET_AP;

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_RTK_BASE");

    Serial.println("[WIFI] Back to AP mode");
  });


  // --- EXISTING ROUTES ---
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
      int n = WiFi.scanNetworks(false, true);
      String json = "[";
      for (int i = 0; i < n; i++) {
          if (i) json += ",";
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
          request->send(400, "text/plain", "Missing parameter");
      }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
      String json = "{\"state\":" + String(currentNetState) +
                    ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
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
  server.begin();
}

void handleWebSocketQueue() {
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
}

void handleTelemetry(uint32_t now) {
  static uint32_t lastCpuCheckTime = millis(); 
  static SatData localSats[MAX_SATS];
  static char jsonBuffer[8192]; 
  
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

      int activeTcp = getActiveTCPClientsAndCleanup();

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
    
    lastCpuCheckTime = now; 
  }
}