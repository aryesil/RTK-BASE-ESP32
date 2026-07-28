#include <web/WebServerManager.h>
#include <Globals.h>
#include <web/WebUI.h>
#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <network/DataOutput.h>
#include <network/NetworkManager.h>
#include <network/NtripPush.h>
#include <gnss/Iono.h>

// Escapes a string for embedding in a JSON string literal. SSIDs are arbitrary
// bytes and would otherwise break the /scan response (or leak into the page).
static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04X", (uint8_t)c);
          out += esc;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Streams the page straight out of flash. request->send(..., const char*) would
// first copy the whole document into a heap String, which is a ~40 KB spike for
// the telemetry UI.
static void sendProgmemPage(AsyncWebServerRequest *request, const char* page) {
  request->send(request->beginResponse(200, "text/html",
                                       (const uint8_t*)page, strlen_P(page)));
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    // Only act on a complete, unfragmented text frame; forwarding a partial
    // frame would push a truncated command to the GNSS module.
    if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len) {
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

// --------------------------------------------------------------------------
// Base station configuration endpoint
// --------------------------------------------------------------------------
static void handleBaseApi(AsyncWebServerRequest *request) {
  String action = request->hasParam("action") ? request->getParam("action")->value() : "";

  if (action == "svin") {
    int mode = request->hasParam("mode") ? request->getParam("mode")->value().toInt() : 0;

    uint32_t dur = request->hasParam("dur")
        ? (uint32_t)request->getParam("dur")->value().toInt() : 43200;
    float acc = request->hasParam("acc")
        ? request->getParam("acc")->value().toFloat() : 15.0f;

    double x = 0, y = 0, z = 0;
    if (mode == 2) {
      // Accept either ECEF directly or geodetic coordinates to convert.
      if (request->hasParam("lat") && request->hasParam("lon") && request->hasParam("hgt")) {
        // Geodetic entry is treated as the ground marker, so the configured
        // marker -> ARP offset is added; RTCM 1005 must carry the ARP.
        bool raw = request->hasParam("noarp") &&
                   request->getParam("noarp")->value().toInt() != 0;
        double la = request->getParam("lat")->value().toDouble();
        double lo = request->getParam("lon")->value().toDouble();
        double hg = request->getParam("hgt")->value().toDouble();
        if (raw) llaToEcef(la, lo, hg, x, y, z);
        else     markerToArpEcef(la, lo, hg, x, y, z);
      } else {
        x = request->hasParam("x") ? request->getParam("x")->value().toDouble() : 0.0;
        y = request->hasParam("y") ? request->getParam("y")->value().toDouble() : 0.0;
        z = request->hasParam("z") ? request->getParam("z")->value().toDouble() : 0.0;
      }
      if (x == 0.0 && y == 0.0 && z == 0.0) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"msg\":\"Fixed mode needs a station position\"}");
        return;
      }
    }

    applySvinConfig(mode, dur, acc, x, y, z);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (action == "rtcm" && request->hasParam("v")) {
    applyRtcmMode(request->getParam("v")->value().toInt());
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (action == "arp" && request->hasParam("v")) {
    applyArpOutput(request->getParam("v")->value().toInt());
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (action == "eph" && request->hasParam("v")) {
    applyEphOutput(request->getParam("v")->value().toInt());
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  // Distinct from action=arp above, which toggles RTCM 1005 output: this is the
  // geometric offset from the surveyed ground marker to the antenna.
  if (action == "arpoffset") {
    if (request->hasParam("n")) arpCfg.north = request->getParam("n")->value().toFloat();
    if (request->hasParam("e")) arpCfg.east  = request->getParam("e")->value().toFloat();
    if (request->hasParam("u")) arpCfg.up    = request->getParam("u")->value().toFloat();
    saveArpCfg();
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (action == "avgstart") {
    uint32_t secs = request->hasParam("s") ? (uint32_t)request->getParam("s")->value().toInt() : 300;
    if (secs < 10) secs = 10;
    if (secs > 86400) secs = 86400;
    startPosAvg(secs);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (action == "avgstop") {
    stopPosAvg(true);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  // Promotes the receiver's own survey-in mean straight into fixed mode. No
  // ARP offset here: that mean already refers to the antenna, not a marker.
  if (action == "adopt") {
    double x, y, z;
    bool ok = false;
    if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
      ok = (svin.feat == FEAT_OK && svin.valid == 2);
      x = svin.x; y = svin.y; z = svin.z;
      xSemaphoreGive(baseMutex);
    }
    if (!ok) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"msg\":\"No completed survey-in result available\"}");
      return;
    }
    applySvinConfig(2, 0, 0.0f, x, y, z);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (action == "save")    { saveModuleParams();    request->send(200, "application/json", "{\"ok\":true}"); return; }
  if (action == "restore") { restoreModuleParams(); request->send(200, "application/json", "{\"ok\":true}"); return; }
  if (action == "query")   { queryBaseConfig();     request->send(200, "application/json", "{\"ok\":true}"); return; }

  request->send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown action\"}");
}

// --------------------------------------------------------------------------
// Data output (TCP raw / NTRIP caster / UDP) endpoint
// --------------------------------------------------------------------------
static uint16_t portParam(AsyncWebServerRequest *r, const char* name, uint16_t fallback) {
  if (!r->hasParam(name)) return fallback;
  long v = r->getParam(name)->value().toInt();
  return (v >= 1 && v <= 65535) ? (uint16_t)v : fallback;
}

static void handleOutputApi(AsyncWebServerRequest *request) {
  String action = request->hasParam("action") ? request->getParam("action")->value() : "";

  if (action == "save") {
    if (request->hasParam("tcpEn")) outCfg.tcpEnabled = request->getParam("tcpEn")->value().toInt() != 0;
    if (request->hasParam("udpEn")) outCfg.udpEnabled = request->getParam("udpEn")->value().toInt() != 0;
    outCfg.tcpPort = portParam(request, "tcpPort", outCfg.tcpPort);
    outCfg.udpPort = portParam(request, "udpPort", outCfg.udpPort);

    if (outCfg.tcpEnabled && outCfg.udpEnabled && outCfg.tcpPort == outCfg.udpPort) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"msg\":\"TCP and UDP need different ports\"}");
      return;
    }
    if (request->hasParam("udpDst")) {
      String d = request->getParam("udpDst")->value();
      d.trim();
      strlcpy(outCfg.udpDest, d.c_str(), sizeof(outCfg.udpDest));
    }
    if (request->hasParam("udpBc"))
      outCfg.udpBroadcast = request->getParam("udpBc")->value().toInt() != 0;
    if (request->hasParam("udpDstP"))
      outCfg.udpDestPort = (uint16_t)request->getParam("udpDstP")->value().toInt();

    if (request->hasParam("accept")) {
      int m = request->getParam("accept")->value().toInt();
      outCfg.acceptMode = (m >= 0 && m <= 2) ? (uint8_t)m : 0;
    }
    if (request->hasParam("mount")) {
      String m = request->getParam("mount")->value();
      m.trim();
      if (m.length() == 0) m = "RTK";
      strlcpy(outCfg.mount, m.c_str(), sizeof(outCfg.mount));
    }
    if (request->hasParam("user"))
      strlcpy(outCfg.ntripUser, request->getParam("user")->value().c_str(), sizeof(outCfg.ntripUser));
    if (request->hasParam("pass"))
      strlcpy(outCfg.ntripPass, request->getParam("pass")->value().c_str(), sizeof(outCfg.ntripPass));

    saveOutputCfg();
    request->send(200, "application/json", "{\"ok\":true}");
    restartDataOutput();
    return;
  }

  request->send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown action\"}");
}

// --------------------------------------------------------------------------
// Network endpoint
// --------------------------------------------------------------------------
static void handleNetApi(AsyncWebServerRequest *request) {
  String action = request->hasParam("action") ? request->getParam("action")->value() : "";

  if (action == "ap") {
    String ssid = request->hasParam("ssid") ? request->getParam("ssid")->value() : "";
    String pass = request->hasParam("pass") ? request->getParam("pass")->value() : "";
    ssid.trim();
    if (ssid.length() < 1 || ssid.length() > 32) {
      request->send(400, "application/json", "{\"ok\":false,\"msg\":\"SSID must be 1-32 characters\"}");
      return;
    }
    if (pass.length() > 0 && pass.length() < 8) {
      request->send(400, "application/json",
                    "{\"ok\":false,\"msg\":\"WPA2 needs 8+ characters (leave empty for an open AP)\"}");
      return;
    }
    strlcpy(apCfg.ssid, ssid.c_str(), sizeof(apCfg.ssid));
    strlcpy(apCfg.pass, pass.c_str(), sizeof(apCfg.pass));
    if (request->hasParam("ch")) {
      int ch = request->getParam("ch")->value().toInt();
      apCfg.channel = (ch >= 1 && ch <= 13) ? (uint8_t)ch : 6;
    }
    if (request->hasParam("hide")) apCfg.hidden = request->getParam("hide")->value().toInt() != 0;

    saveApConfig();
    request->send(200, "application/json", "{\"ok\":true}");
    apRestartRequested = true;   // done off-thread: it drops the caller's link
    return;
  }

  if (action == "join") {
    if (!request->hasParam("ssid")) {
      request->send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing SSID\"}");
      return;
    }
    targetSSID = request->getParam("ssid")->value();
    targetPass = request->hasParam("pass") ? request->getParam("pass")->value() : "";
    newCredentialsReceived = true;
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (action == "forget") {
    request->send(200, "application/json", "{\"ok\":true}");
    wifiResetRequested = true;
    return;
  }

  request->send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown action\"}");
}

static void handlePushApi(AsyncWebServerRequest *request) {
  if (request->hasParam("en")) pushCfg.enabled = request->getParam("en")->value().toInt() != 0;
  if (request->hasParam("host")) {
    String h = request->getParam("host")->value();
    h.trim();
    strlcpy(pushCfg.host, h.c_str(), sizeof(pushCfg.host));
  }
  if (request->hasParam("port")) pushCfg.port = portParam(request, "port", pushCfg.port);
  if (request->hasParam("mount")) {
    String m = request->getParam("mount")->value();
    m.trim();
    strlcpy(pushCfg.mount, m.c_str(), sizeof(pushCfg.mount));
  }
  if (request->hasParam("pass"))
    strlcpy(pushCfg.pass, request->getParam("pass")->value().c_str(), sizeof(pushCfg.pass));

  if (pushCfg.enabled && (pushCfg.host[0] == 0 || pushCfg.mount[0] == 0)) {
    request->send(400, "application/json",
                  "{\"ok\":false,\"msg\":\"Host and mountpoint are required\"}");
    return;
  }

  saveNtripPushCfg();
  request->send(200, "application/json", "{\"ok\":true}");
}

void setupWebServer() {
  // The soft-AP is always up and the WiFi setup now lives in the Network tab,
  // so there is no separate captive setup page any more.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    sendProgmemPage(request, index_html);
  });

  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "OK");
    wifiResetRequested = true;   // applied by the network task, off this thread
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Rebooting");
    Serial.println("[SYS] Reboot requested from web UI");
    // Let the response flush before pulling the rug out.
    xTaskCreate([](void*){ vTaskDelay(pdMS_TO_TICKS(400)); ESP.restart(); },
                "reboot", 2048, NULL, 1, NULL);
  });

  server.on("/api/base", HTTP_GET, handleBaseApi);
  server.on("/api/output", HTTP_GET, handleOutputApi);
  server.on("/api/net", HTTP_GET, handleNetApi);
  server.on("/api/push", HTTP_GET, handlePushApi);

  // Asynchronous: a blocking scan here would stall the AsyncTCP task (and every
  // other HTTP/WebSocket client) for several seconds. Clients poll until the
  // array arrives; 202 means "still scanning".
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
      int n = WiFi.scanComplete();

      if (n == WIFI_SCAN_RUNNING) {
          request->send(202, "application/json", "{\"scanning\":true}");
          return;
      }
      if (n < 0) { // WIFI_SCAN_FAILED: nothing started yet, or the last one failed
          WiFi.scanNetworks(true, true);
          request->send(202, "application/json", "{\"scanning\":true}");
          return;
      }

      String json = "[";
      for (int i = 0; i < n; i++) {
          if (i) json += ",";
          json += "\"" + jsonEscape(WiFi.SSID(i)) + "\"";
      }
      json += "]";
      WiFi.scanDelete(); // otherwise every scan leaks its result list
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

  char queuedMsg[TERM_MSG_LEN];
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
  static SatSignal localSigs[MAX_SIGNALS];
  static bool localUsed[MAX_SIGNALS];
  static char jsonBuffer[10240];

  uint32_t timeDiffMillis = now - lastCpuCheckTime;
  if (timeDiffMillis < 1000) return;

  cleanOldSatellites();
  updateFeatureProbes();
  feedPosAvg();

  // Counters must be drained every tick, client or not: leaving them to
  // accumulate while nobody is connected overflows the busy-time arithmetic
  // and makes the first reported RTCM rate a total instead of a rate.
  uint32_t timeDiffMicros = timeDiffMillis * 1000;

  uint32_t c1Busy = __atomic_exchange_n(&core1BusyTime, 0, __ATOMIC_RELAXED);
  int cpu1Usage = (int)((uint64_t)c1Busy * 100 / timeDiffMicros);
  if (cpu1Usage > 100) cpu1Usage = 100;

  uint32_t c0Busy = core0BusyTimeAcc;
  core0BusyTimeAcc = 0;
  int cpu0Usage = (int)((uint64_t)c0Busy * 100 / timeDiffMicros);
  if (cpu0Usage > 100) cpu0Usage = 100;

  uint32_t currentRtcmCount = 0;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    currentRtcmCount = rtcmPaketSayaci;
    rtcmPaketSayaci = 0;
    xSemaphoreGive(dataMutex);
  }

  int activeTcp = tcpClientCount();
  int activeUdp = udpClientCount();

  RtcmStats st;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    rtcmStats.bytesSec = (uint32_t)((uint64_t)rtcmStats.bytesAcc * 1000 / timeDiffMillis);
    rtcmStats.bytesAcc = 0;
    st = rtcmStats;
    xSemaphoreGive(dataMutex);
  }

  lastCpuCheckTime = now;
  if (ws.count() == 0) return;

  // ---- snapshot ---------------------------------------------------------
  int sigCount = 0;
  GpsSnapshot g;
  memset(&g, 0, sizeof(g));

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    sigCount = activeSignalCount;
    memcpy(localSigs, activeSignals, sigCount * sizeof(SatSignal));
    for (int i = 0; i < sigCount; i++) {
      localUsed[i] = isSatUsed(localSigs[i].sys, localSigs[i].prn);
    }
    g = safeGps;
    xSemaphoreGive(dataMutex);
  }

  BaseCfg base;
  if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
    base = baseCfg;
    xSemaphoreGive(baseMutex);
  }

  uint32_t lastPps;
  portENTER_CRITICAL(&ppsMux);
  lastPps = sonPpsZamaniMicros;
  portEXIT_CRITICAL(&ppsMux);

  // ---- per-constellation aggregation ------------------------------------
  // Satellites are counted once regardless of how many bands they are tracked
  // on; the band matrix separately reports how many signals each band carries.
  uint8_t satTracked[SYS_COUNT] = {0};
  uint8_t satUsed[SYS_COUNT] = {0};
  uint8_t bandCount[SYS_COUNT][BANDS_PER_SYS] = {{0}};

  for (int i = 0; i < sigCount; i++) {
    uint8_t sys = localSigs[i].sys;
    if (sys >= SYS_COUNT) continue;

    if (localSigs[i].band < BANDS_PER_SYS) bandCount[sys][localSigs[i].band]++;

    bool firstForSat = true;
    for (int j = 0; j < i; j++) {
      if (localSigs[j].sys == sys && localSigs[j].prn == localSigs[i].prn) {
        firstForSat = false;
        break;
      }
    }
    if (firstForSat) {
      satTracked[sys]++;
      if (localUsed[i]) satUsed[sys]++;
    }
  }

  // ---- serialise --------------------------------------------------------
  static JsonDocument doc;
  doc.clear();

  doc["up"]   = millis() / 1000;
  doc["ip"]   = WiFi.localIP().toString();
  doc["ap"]   = WiFi.softAPIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["net"]  = (int)currentNetState;
  doc["heap"] = ESP.getFreeHeap();
  doc["model"] = RX_MODEL;
  doc["esp"]  = FW_VERSION;

  doc["lat"]  = g.validLoc ? g.lat : 0.0;
  doc["lon"]  = g.validLoc ? g.lon : 0.0;
  doc["alt"]  = g.validAlt ? g.alt : 0.0;
  doc["sep"]  = g.geoidSep;
  doc["vloc"] = g.validLoc;
  doc["valt"] = g.validAlt;
  doc["hdop"] = g.validHdop ? g.hdop : 0.0;
  doc["pdop"] = g.pdop;
  doc["vdop"] = g.vdop;
  doc["fq"]   = g.fixQual;
  doc["ft"]   = g.fixType;
  doc["siu"]  = g.satsInUse;

  char timeStr[12] = "--:--:--";
  if (g.validTime) snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", g.hour, g.min, g.sec);
  doc["time"] = timeStr;

  doc["pps"]  = (micros() - lastPps < 2000000);
  doc["rtcm"] = currentRtcmCount;
  doc["tcp"]  = activeTcp;
  doc["udp"]  = activeUdp;
  doc["cpu0"] = cpu0Usage;
  doc["cpu1"] = cpu1Usage;

  JsonObject ap = doc["apinfo"].to<JsonObject>();
  ap["ssid"] = apCfg.ssid;
  ap["ch"]   = apCfg.channel;
  ap["sec"]  = strlen(apCfg.pass) >= 8;
  ap["hide"] = apCfg.hidden;
  ap["n"]    = WiFi.softAPgetStationNum();

  JsonObject o = doc["out"].to<JsonObject>();
  o["tcpEn"]   = outCfg.tcpEnabled;
  o["tcpPort"] = outCfg.tcpPort;
  o["accept"]  = outCfg.acceptMode;
  o["udpEn"]   = outCfg.udpEnabled;
  o["udpPort"] = outCfg.udpPort;
  o["udpDst"]  = outCfg.udpDest;
  o["udpDstP"] = outCfg.udpDestPort;
  o["udpBc"]   = outCfg.udpBroadcast;
  o["udpRx"]   = udpRxCount;
  o["udpFrom"] = udpLastFrom;
  o["udpAge"]  = udpLastRxMs ? (int32_t)((millis() - udpLastRxMs) / 1000) : -1;
  o["mount"]   = outCfg.mount;
  o["user"]    = outCfg.ntripUser;
  o["auth"]    = (outCfg.ntripUser[0] || outCfg.ntripPass[0]);

  // Optional module features and the ESP-side helpers that back them up.
  SvinStatus sv;
  JamStatus jm;
  PosAvg pa;
  if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
    sv = svin; jm = jam; pa = posAvg;
    xSemaphoreGive(baseMutex);
  }

  BaseBroadcast bc;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    bc = bcast;
    xSemaphoreGive(dataMutex);
  }
  ionoUpdate(g.validLoc ? g.lat : 0.0, g.validLoc ? g.lon : 0.0, g.validLoc);

  // [sysIdx, prn, elev, azim, rawSlant*100, deltaVert*100, arcSeconds,
  //  ippLat, ippLon]
  JsonArray io = doc["io"].to<JsonArray>();
  int ionoUsable = 0;
  float dSum = 0;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    for (int i = 0; i < ionoSatCount; i++) {
      const IonoSat &s = ionoSats[i];
      JsonArray e = io.add<JsonArray>();
      e.add(s.sys);
      e.add(s.prn);
      e.add(s.elev);
      e.add(s.azim);
      e.add((int)lroundf(s.slantRaw * 100));
      e.add((int)lroundf(s.vertDelta * 100));
      e.add(s.hasRef ? (millis() - s.arcStartMs) / 1000 : 0);
      e.add(s.ippLat);
      e.add(s.ippLon);
      if (s.elev > 0 && s.hasRef) { ionoUsable++; dSum += fabsf(s.vertDelta); }
    }
    xSemaphoreGive(dataMutex);
  }
  doc["ion"] = ionoUsable;
  doc["iond"] = ionoUsable ? dSum / ionoUsable : 0.0f;

  JsonObject bj = doc["bc"].to<JsonObject>();
  bj["v"]   = bc.valid;
  bj["id"]  = bc.stationId;
  bj["x"]   = bc.x;
  bj["y"]   = bc.y;
  bj["z"]   = bc.z;
  bj["age"] = bc.lastMs ? (int32_t)((millis() - bc.lastMs) / 1000) : -1;
  bj["st"]  = bc.stableSince ? (millis() - bc.stableSince) / 1000 : 0;
  if (bc.valid) {
    double la, lo, hg;
    ecefToLla(bc.x, bc.y, bc.z, la, lo, hg);
    bj["lat"] = la; bj["lon"] = lo; bj["hgt"] = hg;
  }

  JsonObject sj = doc["svin"].to<JsonObject>();
  sj["feat"] = sv.feat;
  sj["v"]    = sv.valid;
  sj["obs"]  = sv.obs;
  sj["dur"]  = sv.cfgDur;
  sj["acc"]  = sv.acc;
  sj["x"]    = sv.x;
  sj["y"]    = sv.y;
  sj["z"]    = sv.z;

  JsonObject jj = doc["jam"].to<JsonObject>();
  jj["feat"] = jm.feat;
  jj["l1"]   = jm.l1;
  jj["l5"]   = jm.haveL5 ? jm.l5 : -1;

  JsonObject aj = doc["arp"].to<JsonObject>();
  aj["n"] = arpCfg.north;
  aj["e"] = arpCfg.east;
  aj["u"] = arpCfg.up;

  JsonObject av = doc["avg"].to<JsonObject>();
  av["run"]  = pa.running;
  av["el"]   = pa.running ? (millis() - pa.startedMs) / 1000 : 0;
  av["tgt"]  = pa.targetSec;
  av["n"]    = pa.count;
  av["have"] = pa.haveResult;
  av["lat"]  = pa.lat;
  av["lon"]  = pa.lon;
  av["alt"]  = pa.alt;
  av["rms"]  = pa.rms;

  JsonObject pu = doc["push"].to<JsonObject>();
  pu["en"]    = pushCfg.enabled;
  pu["host"]  = pushCfg.host;
  pu["port"]  = pushCfg.port;
  pu["mount"] = pushCfg.mount;
  pu["st"]    = pushState.state;
  pu["msg"]   = pushState.msg;
  pu["sent"]  = pushState.sent;
  pu["retry"] = pushState.retries;
  pu["up"]    = pushState.state == PUSH_STREAMING ? (millis() - pushState.sinceMs) / 1000 : 0;

  JsonObject rs = doc["rst"].to<JsonObject>();
  rs["f"]   = st.frames;
  rs["crc"] = st.crcErrors;
  rs["bps"] = st.bytesSec;
  rs["age"] = st.lastFrameMs ? (millis() - st.lastFrameMs) / 1000 : -1;
  JsonArray ty = rs["ty"].to<JsonArray>();
  for (int i = 0; i < st.typeCount; i++) {
    JsonArray e = ty.add<JsonArray>();
    e.add(st.types[i]);
    e.add(st.typeHits[i]);
    e.add(st.typeInterval[i]);
    e.add(st.typeJitter[i]);
  }

  // [ip, mode, seconds connected, bytes sent]
  RtcmClientInfo tinfo[MAX_TCP_CLIENTS];
  int tn = snapshotTcpClients(tinfo, MAX_TCP_CLIENTS);
  JsonArray cl = doc["cl"].to<JsonArray>();
  for (int i = 0; i < tn; i++) {
    JsonArray e = cl.add<JsonArray>();
    e.add(tinfo[i].ip);
    e.add(tinfo[i].mode == CM_NTRIP ? "ntrip" : "raw");
    e.add((millis() - tinfo[i].since) / 1000);
    e.add(tinfo[i].sent);
  }

  UdpClientInfo uinfo[MAX_UDP_CLIENTS];
  int un = snapshotUdpClients(uinfo, MAX_UDP_CLIENTS);
  JsonArray ul = doc["ul"].to<JsonArray>();
  for (int i = 0; i < un; i++) {
    JsonArray e = ul.add<JsonArray>();
    e.add(uinfo[i].ip);
    e.add(uinfo[i].port);
    e.add((millis() - uinfo[i].since) / 1000);
    e.add(uinfo[i].sent);
  }

  // [tracked, used] per constellation, in GnssSys order (SYS_UNK excluded).
  JsonArray cons = doc["cons"].to<JsonArray>();
  JsonArray bands = doc["bands"].to<JsonArray>();
  for (int s = 0; s < SYS_UNK; s++) {
    JsonArray c = cons.add<JsonArray>();
    c.add(satTracked[s]);
    c.add(satUsed[s]);

    JsonArray b = bands.add<JsonArray>();
    for (int k = 0; k < BANDS_PER_SYS; k++) {
      if (BAND_NAMES[s][k] == NULL) break;
      b.add(bandCount[s][k]);
    }
  }

  // [sys, prn, elev, azim, snr, band, used] per tracked signal.
  JsonArray sig = doc["sig"].to<JsonArray>();
  for (int i = 0; i < sigCount; i++) {
    if (localSigs[i].sys >= SYS_UNK) continue;
    JsonArray e = sig.add<JsonArray>();
    e.add(localSigs[i].sys);
    e.add(localSigs[i].prn);
    e.add(localSigs[i].elev);
    e.add(localSigs[i].azim);
    e.add(localSigs[i].snr);
    e.add(localSigs[i].band);
    e.add(localUsed[i] ? 1 : 0);
  }

  JsonObject b = doc["base"].to<JsonObject>();
  b["m"]    = base.svinMode;
  b["dur"]  = base.minDur;
  b["acc"]  = base.accLimit;
  b["x"]    = base.ecefX;
  b["y"]    = base.ecefY;
  b["z"]    = base.ecefZ;
  b["rtcm"] = base.rtcmMode;
  b["arp"]  = base.arpEnabled;
  b["eph"]  = base.ephEnabled;
  b["el"]   = base.svinStartMs ? (millis() - base.svinStartMs) / 1000 : 0;
  b["ver"]  = base.verStr;
  b["bd"]   = base.buildDate;
  b["msg"]  = base.lastResult;

  size_t jsonLen = serializeJson(doc, jsonBuffer);

  AsyncWebSocketMessageBuffer * wsBuf = ws.makeBuffer(jsonLen);
  if (wsBuf) {
      memcpy(wsBuf->get(), jsonBuffer, jsonLen);
      ws.textAll(wsBuf);
  }
}
