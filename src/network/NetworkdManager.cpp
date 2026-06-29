#include <network/NetworkManager.h>
#include <Globals.h>
#include <ArduinoOTA.h>
#include <web/WebServerManager.h>

extern void networkTaskCode(void * parameter);

void setupNetwork() {
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
}

void handleNetworkState(uint32_t now) {
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
}
void networkTaskCode(void * parameter) {
  for(;;) {
    uint32_t c0TaskStart = micros();
    uint32_t now = millis();

    handleNetworkState(now);
    handleWebSocketQueue();
    handleTelemetry(now);
    
    uint32_t c0TaskEnd = micros();
    if (c0TaskEnd >= c0TaskStart) {
        core0BusyTimeAcc += (c0TaskEnd - c0TaskStart);
    }
    
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }
}