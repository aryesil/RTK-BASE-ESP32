#include <network/NetworkManager.h>
#include <Globals.h>
#include <ArduinoOTA.h>
#include <web/WebServerManager.h>
#include <network/DataOutput.h>
#include <network/NtripPush.h>

// The soft-AP is the rover's data path and stays up in every state. Going
// through a router instead costs a second wireless hop for every RTCM frame,
// which is exactly the latency this base is trying to avoid. Station mode is
// therefore additive: it only provides a management/uplink route.

static void startAccessPoint() {
  const char* pass = strlen(apCfg.pass) >= 8 ? apCfg.pass : NULL;
  WiFi.softAP(apCfg.ssid, pass, apCfg.channel, apCfg.hidden ? 1 : 0, 4);
  Serial.printf("[WIFI] AP \"%s\" ch%u %s -> %s\n", apCfg.ssid, apCfg.channel,
                pass ? "WPA2" : "open", WiFi.softAPIP().toString().c_str());
}

void loadApConfig() {
  strlcpy(apCfg.ssid, prefs.getString("apssid", AP_SSID).c_str(), sizeof(apCfg.ssid));
  strlcpy(apCfg.pass, prefs.getString("appass", "").c_str(), sizeof(apCfg.pass));
  apCfg.channel = (uint8_t)prefs.getUChar("apch", 6);
  if (apCfg.channel < 1 || apCfg.channel > 13) apCfg.channel = 6;
  apCfg.hidden = prefs.getBool("aphide", false);
  if (apCfg.ssid[0] == '\0') strlcpy(apCfg.ssid, AP_SSID, sizeof(apCfg.ssid));
}

void saveApConfig() {
  prefs.putString("apssid", apCfg.ssid);
  prefs.putString("appass", apCfg.pass);
  prefs.putUChar("apch", apCfg.channel);
  prefs.putBool("aphide", apCfg.hidden);
}

void restartAccessPoint() {
  WiFi.softAPdisconnect(false);
  delay(20);
  startAccessPoint();
  restartDataOutput();  // listeners must be re-bound after the interface moves
}

void setupNetwork() {
  prefs.begin("wifi_creds", false);
  loadApConfig();

  targetSSID = prefs.getString("ssid", "");
  targetPass = prefs.getString("pass", "");

  // AP_STA up front so the access point never drops, with or without an uplink.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);   // power save adds tens of ms of jitter to every frame
  startAccessPoint();

  if (targetSSID != "") {
    currentNetState = NET_CONNECTING;
    WiFi.begin(targetSSID.c_str(), targetPass.c_str());
    netStateTimer = millis();
    Serial.println("[WIFI] Joining saved network: " + targetSSID);
  } else {
    currentNetState = NET_AP;
    Serial.println("[WIFI] No saved network, access point only.");
  }

  ArduinoOTA.setHostname("ESP32-RTK-BASE");
  ArduinoOTA.begin();
}

void handleNetworkState(uint32_t now) {
  // Serviced in every state: OTA must stay reachable during normal STA
  // operation, not just while the setup AP is up.
  ArduinoOTA.handle();

  if (apRestartRequested) {
    apRestartRequested = false;
    Serial.println("[WIFI] Restarting access point with new settings.");
    restartAccessPoint();
    return;
  }

  if (wifiResetRequested) {
    wifiResetRequested = false;
    prefs.remove("ssid");
    prefs.remove("pass");
    targetSSID = "";
    targetPass = "";
    newCredentialsReceived = false;
    WiFi.disconnect(false, true);
    currentNetState = NET_AP;
    Serial.println("[WIFI] Station credentials cleared, AP still up.");
    return;
  }

  if (newCredentialsReceived) {
    newCredentialsReceived = false;
    WiFi.disconnect(false, false);
    WiFi.begin(targetSSID.c_str(), targetPass.c_str());
    currentNetState = NET_CONNECTING;
    netStateTimer = now;
    Serial.println("[WIFI] Connecting to: " + targetSSID);
    return;
  }

  switch (currentNetState) {
    case NET_AP:
      break;

    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Connected, STA IP " + WiFi.localIP().toString());
        prefs.putString("ssid", targetSSID);
        prefs.putString("pass", targetPass);
        currentNetState = NET_STA;
      } else if (now - netStateTimer > 30000) {
        Serial.println("[WIFI] Join failed; staying on the access point.");
        WiFi.disconnect(false, false);
        currentNetState = NET_AP;
      }
      break;

    case NET_STA:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Uplink lost, reconnecting in the background...");
        WiFi.reconnect();
        currentNetState = NET_RECONNECTING;
        netStateTimer = now;
      }
      break;

    case NET_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Uplink restored.");
        currentNetState = NET_STA;
      } else if (now - netStateTimer > 60000) {
        // Retry from scratch, but the AP has been serving clients throughout.
        WiFi.begin(targetSSID.c_str(), targetPass.c_str());
        netStateTimer = now;
      }
      break;
  }
}

void networkTaskCode(void * parameter) {
  for(;;) {
    uint32_t c0TaskStart = micros();
    uint32_t now = millis();

    handleNetworkState(now);
    handleNtripPush();
    handleWebSocketQueue();
    handleTelemetry(now);

    uint32_t c0TaskEnd = micros();
    if (c0TaskEnd >= c0TaskStart) {
        // Accumulated and cleared by this same task, so no sync needed here.
        core0BusyTimeAcc += (c0TaskEnd - c0TaskStart);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
