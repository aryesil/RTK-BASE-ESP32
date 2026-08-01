#include <web/WebServerManager.h>
#include <Globals.h>
#include <web/WebUI.h>
#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <network/DataOutput.h>
#include <network/NetworkManager.h>
#include <network/NtripPush.h>
#include <stdarg.h>
#include <gnss/Iono.h>
#include <system/History.h>

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


namespace {
// Scope guard mirroring the one in BaseConfig.cpp, for the config structs the
// web handlers share with the network task.
struct BaseLockWeb {
  BaseLockWeb()  { xSemaphoreTake(baseMutex, portMAX_DELAY); }
  ~BaseLockWeb() { xSemaphoreGive(baseMutex); }
};
}

// --------------------------------------------------------------------------
// Telemetry serialisation
//
// Written straight into a static buffer rather than through a JsonDocument.
// ArduinoJson 7 releases its whole pool on clear(), so building this document
// once a second meant hundreds of allocate/free cycles and about 9 kB of heap
// churn every second, on a device expected to run for days.
// --------------------------------------------------------------------------
namespace {

struct JBuf {
  char*  b;
  size_t cap;
  size_t n = 0;
  bool   first = true;   // no comma before the first member of the current level

  JBuf(char* buf, size_t c) : b(buf), cap(c) {}

  void put(char c) { if (n + 1 < cap) b[n++] = c; }
  void raw(const char* s) { while (*s && n + 1 < cap) b[n++] = *s++; }

  void fmt(const char* f, ...) {
    if (n + 1 >= cap) return;
    va_list ap;
    va_start(ap, f);
    int w = vsnprintf(b + n, cap - n, f, ap);
    va_end(ap);
    if (w > 0) n += (size_t)w < cap - n ? (size_t)w : cap - n - 1;
  }

  // Only the few operator-supplied strings can contain anything awkward, but
  // escaping every string keeps one rule instead of two.
  void str(const char* s) {
    put('"');
    for (; *s && n + 7 < cap; s++) {
      unsigned char c = (unsigned char)*s;
      if (c == '"' || c == '\\') { put('\\'); put((char)c); }
      else if (c == '\n') raw("\\n");
      else if (c == '\r') raw("\\r");
      else if (c == '\t') raw("\\t");
      else if (c < 0x20) fmt("\\u%04X", c);
      else put((char)c);
    }
    put('"');
  }

  void comma() { if (!first) put(','); first = false; }
  void key(const char* k) { comma(); str(k); put(':'); }

  void obj(const char* k) { key(k); put('{'); first = true; }
  void arr(const char* k) { key(k); put('['); first = true; }
  void end(char c) { put(c); first = false; }

  void kv(const char* k, double v, int dp) { key(k); fmt("%.*f", dp, v); }
  void kv(const char* k, long v)           { key(k); fmt("%ld", v); }
  void kv(const char* k, unsigned long v)  { key(k); fmt("%lu", v); }
  void kv(const char* k, int v)            { key(k); fmt("%d", v); }
  void kv(const char* k, bool v)           { key(k); raw(v ? "true" : "false"); }
  void kv(const char* k, const char* v)    { key(k); str(v); }

  // Elements of an array: same rules without a key.
  void el(long v)          { comma(); fmt("%ld", v); }
  void el(int v)           { comma(); fmt("%d", v); }
  void el(unsigned long v) { comma(); fmt("%lu", v); }
  void el(double v, int dp){ comma(); fmt("%.*f", dp, v); }
  void el(const char* v)   { comma(); str(v); }
};

}  // namespace

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
      Log.print("[WS] Command Sent to Module: ");
      Log.println(cmd);
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
    // Read by the telemetry builder on the other task; keep the three
    // components consistent with each other.
    if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
      if (request->hasParam("n")) arpCfg.north = request->getParam("n")->value().toFloat();
      if (request->hasParam("e")) arpCfg.east  = request->getParam("e")->value().toFloat();
      if (request->hasParam("u")) arpCfg.up    = request->getParam("u")->value().toFloat();
      xSemaphoreGive(baseMutex);
    }
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

    if (request->hasParam("usbEn")) outCfg.usbEnabled = request->getParam("usbEn")->value().toInt() != 0;
    if (request->hasParam("usbBaud")) {
      // saveOutputCfg() rejects anything off the supported list and falls back
      // to the console rate, so a bad value cannot leave the port unusable.
      outCfg.usbBaud = (uint32_t)request->getParam("usbBaud")->value().toInt();
    }

    saveOutputCfg();
    request->send(200, "application/json", "{\"ok\":true}");
    restartDataOutput();
    return;
  }

  request->send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown action\"}");
}

// --------------------------------------------------------------------------
// Settings backup
// --------------------------------------------------------------------------
// Everything the ESP32 owns, plus the station coordinate. That last part lives
// in the module's own memory rather than here, which is exactly why it belongs
// in the backup: a replaced module, or a $PQTMRESTOREPAR, loses it otherwise.
static void handleBackupApi(AsyncWebServerRequest *request) {
  static char buf[1536];
  JBuf j(buf, sizeof(buf));
  j.put('{'); j.first = true;
    j.kv("fw", FW_VERSION);

    j.obj("ap");
      j.kv("ssid", apCfg.ssid);
      j.kv("pass", apCfg.pass);
      j.kv("ch",   (int)apCfg.channel);
      j.kv("hide", apCfg.hidden);
    j.end('}');

    j.obj("sta");
      j.kv("ssid", targetSSID.c_str());
      j.kv("pass", targetPass.c_str());
    j.end('}');

    j.obj("out");
      j.kv("tcpEn",   outCfg.tcpEnabled);
      j.kv("tcpPort", (int)outCfg.tcpPort);
      j.kv("accept",  (int)outCfg.acceptMode);
      j.kv("udpEn",   outCfg.udpEnabled);
      j.kv("udpPort", (int)outCfg.udpPort);
      j.kv("udpDst",  outCfg.udpDest);
      j.kv("udpDstP", (int)outCfg.udpDestPort);
      j.kv("udpBc",   outCfg.udpBroadcast);
      j.kv("mount",   outCfg.mount);
      j.kv("user",    outCfg.ntripUser);
      j.kv("pass",    outCfg.ntripPass);
      j.kv("usbEn",   outCfg.usbEnabled);
      j.kv("usbBaud", (unsigned long)outCfg.usbBaud);
    j.end('}');

    j.obj("push");
      j.kv("en",    pushCfg.enabled);
      j.kv("host",  pushCfg.host);
      j.kv("port",  (int)pushCfg.port);
      j.kv("mount", pushCfg.mount);
      j.kv("pass",  pushCfg.pass);
    j.end('}');

    j.obj("arp");
      j.kv("n", (double)arpCfg.north, 3);
      j.kv("e", (double)arpCfg.east, 3);
      j.kv("u", (double)arpCfg.up, 3);
    j.end('}');

    // Mirrored from the module. Restoring these re-issues $PQTMCFGSVIN.
    j.obj("base");
      j.kv("mode", (int)baseCfg.svinMode);
      j.kv("dur",  (unsigned long)baseCfg.minDur);
      j.kv("acc",  (double)baseCfg.accLimit, 2);
      j.kv("x",    baseCfg.ecefX, 4);
      j.kv("y",    baseCfg.ecefY, 4);
      j.kv("z",    baseCfg.ecefZ, 4);
    j.end('}');
  j.end('}');

  AsyncWebServerResponse *r = request->beginResponse(200, "application/json", buf);
  r->addHeader("Content-Disposition",
               "attachment; filename=\"rtk-base-settings.json\"");
  request->send(r);
}

// Restore arrives as a POST body: a settings document does not fit comfortably
// in a query string, and every other endpoint here is a small GET.
//
// The body callback only parses and applies; the response is sent from the
// request callback, which always runs afterwards. Answering from both means
// the later one silently overwrites the earlier - which looked exactly like a
// rejected restore that had in fact already been applied.
static bool restoreDone = false;
static char restoreMsg[96];

static void handleRestoreBody(AsyncWebServerRequest *request, uint8_t *data,
                              size_t len, size_t index, size_t total) {
  static String body;
  if (index == 0) body = "";
  body.concat((const char*)data, len);
  if (index + len < total) return;

  restoreDone = true;

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    strlcpy(restoreMsg, "{\"ok\":false,\"msg\":\"Not a valid settings file\"}",
            sizeof(restoreMsg));
    body = "";
    return;
  }

  int applied = 0;
  {
    BaseLockWeb lock;
    if (JsonObject o = doc["ap"].as<JsonObject>()) {
      if (o["ssid"].is<const char*>()) strlcpy(apCfg.ssid, o["ssid"], sizeof(apCfg.ssid));
      if (o["pass"].is<const char*>()) strlcpy(apCfg.pass, o["pass"], sizeof(apCfg.pass));
      if (o["ch"].is<int>())   apCfg.channel = (uint8_t)o["ch"].as<int>();
      if (o["hide"].is<bool>()) apCfg.hidden = o["hide"];
      saveApConfig();
      applied++;
    }
    if (JsonObject o = doc["out"].as<JsonObject>()) {
      if (o["tcpEn"].is<bool>())  outCfg.tcpEnabled = o["tcpEn"];
      if (o["tcpPort"].is<int>()) outCfg.tcpPort = (uint16_t)o["tcpPort"].as<int>();
      if (o["accept"].is<int>())  outCfg.acceptMode = (uint8_t)o["accept"].as<int>();
      if (o["udpEn"].is<bool>())  outCfg.udpEnabled = o["udpEn"];
      if (o["udpPort"].is<int>()) outCfg.udpPort = (uint16_t)o["udpPort"].as<int>();
      if (o["udpDst"].is<const char*>()) strlcpy(outCfg.udpDest, o["udpDst"], sizeof(outCfg.udpDest));
      if (o["udpDstP"].is<int>()) outCfg.udpDestPort = (uint16_t)o["udpDstP"].as<int>();
      if (o["udpBc"].is<bool>())  outCfg.udpBroadcast = o["udpBc"];
      if (o["mount"].is<const char*>()) strlcpy(outCfg.mount, o["mount"], sizeof(outCfg.mount));
      if (o["user"].is<const char*>())  strlcpy(outCfg.ntripUser, o["user"], sizeof(outCfg.ntripUser));
      if (o["pass"].is<const char*>())  strlcpy(outCfg.ntripPass, o["pass"], sizeof(outCfg.ntripPass));
      if (o["usbEn"].is<bool>())  outCfg.usbEnabled = o["usbEn"];
      if (o["usbBaud"].is<unsigned long>()) outCfg.usbBaud = o["usbBaud"];
      saveOutputCfg();
      applied++;
    }
    if (JsonObject o = doc["push"].as<JsonObject>()) {
      if (o["en"].is<bool>())    pushCfg.enabled = o["en"];
      if (o["host"].is<const char*>())  strlcpy(pushCfg.host, o["host"], sizeof(pushCfg.host));
      if (o["port"].is<int>())   pushCfg.port = (uint16_t)o["port"].as<int>();
      if (o["mount"].is<const char*>()) strlcpy(pushCfg.mount, o["mount"], sizeof(pushCfg.mount));
      if (o["pass"].is<const char*>())  strlcpy(pushCfg.pass, o["pass"], sizeof(pushCfg.pass));
      saveNtripPushCfg();
      applied++;
    }
    if (JsonObject o = doc["arp"].as<JsonObject>()) {
      if (o["n"].is<float>()) arpCfg.north = o["n"];
      if (o["e"].is<float>()) arpCfg.east  = o["e"];
      if (o["u"].is<float>()) arpCfg.up    = o["u"];
      saveArpCfg();
      applied++;
    }
  }

  if (JsonObject o = doc["sta"].as<JsonObject>()) {
    if (o["ssid"].is<const char*>() && strlen(o["ssid"])) {
      targetSSID = (const char*)o["ssid"];
      targetPass = o["pass"].is<const char*>() ? (const char*)o["pass"] : "";
      prefs.putString("ssid", targetSSID);
      prefs.putString("pass", targetPass);
      applied++;
    }
  }

  // Last, and outside the lock: this one talks to the module over the serial
  // link rather than touching a struct.
  if (JsonObject o = doc["base"].as<JsonObject>()) {
    int mode = o["mode"].is<int>() ? o["mode"].as<int>() : -1;
    if (mode >= 0 && mode <= 2) {
      applySvinConfig(mode,
                      o["dur"].is<unsigned long>() ? o["dur"].as<uint32_t>() : 43200,
                      o["acc"].is<float>() ? o["acc"].as<float>() : 15.0f,
                      o["x"] | 0.0, o["y"] | 0.0, o["z"] | 0.0);
      applied++;
    }
  }

  snprintf(restoreMsg, sizeof(restoreMsg),
           "{\"ok\":true,\"applied\":%d,\"msg\":\"Restored, rebooting\"}", applied);
  body = "";
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
  BaseLockWeb lock;   // the network task reads these while connecting
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
    Log.println("[SYS] Reboot requested from web UI");
    // Let the response flush before pulling the rug out.
    xTaskCreate([](void*){ vTaskDelay(pdMS_TO_TICKS(400)); ESP.restart(); },
                "reboot", 2048, NULL, 1, NULL);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request){
    size_t len = 0;
    const uint8_t *b = historySnapshot(len);
    // Sent straight from the static buffer. A sample may be rewritten while
    // the response streams; one record briefly mixing an old and a new field
    // is invisible on a twelve hour chart, and copying 23 kB to avoid it would
    // not be.
    AsyncWebServerResponse *r =
        request->beginResponse(200, "application/octet-stream", b, len);
    r->addHeader("Cache-Control", "no-store");
    request->send(r);
  });
  server.on("/api/backup", HTTP_GET, handleBackupApi);
  server.on("/api/restore", HTTP_POST,
            [](AsyncWebServerRequest *r){
              if (!restoreDone) {
                // The body never reached the handler: the server parses an
                // urlencoded body away as request parameters.
                r->send(415, "application/json",
                        "{\"ok\":false,\"msg\":\"Send the file as "
                        "Content-Type: application/json\"}");
                return;
              }
              restoreDone = false;
              bool ok = strstr(restoreMsg, "\"ok\":true") != NULL;
              r->send(ok ? 200 : 400, "application/json", restoreMsg);
              // Deferred so the response flushes and the module has time to
              // acknowledge the survey command.
              if (ok) xTaskCreate([](void*){ vTaskDelay(pdMS_TO_TICKS(1200));
                                             ESP.restart(); },
                                  "restore", 2048, NULL, 1, NULL);
            }, NULL, handleRestoreBody);
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

  // AsyncWebSocket discards a message outright when a client's queue is full,
  // which the page sees as a freeze with no explanation. Check first, skip
  // deliberately and count it, so a backed-up link is visible instead of
  // silently eating updates. Building the document is the expensive part, so
  // this also saves the work.
  static uint32_t telemSeq = 0, telemSkipped = 0;
  telemSeq++;
  if (!ws.availableForWriteAll()) {
    telemSkipped++;
    return;
  }

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
  // Snapshots first, so no lock is held while formatting.
  SvinStatus sv; JamStatus jm; PosAvg pa;
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

  static IonoSat ionoLocal[MAX_IONO_SATS];
  int ionoN = 0, ionoUsable = 0;
  float dSum = 0;
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    ionoN = ionoSatCount;
    memcpy(ionoLocal, ionoSats, ionoN * sizeof(IonoSat));
    xSemaphoreGive(dataMutex);
  }
  for (int i = 0; i < ionoN; i++)
    if (ionoLocal[i].elev > 0 && ionoLocal[i].hasRef) {
      ionoUsable++; dSum += fabsf(ionoLocal[i].vertDelta);
    }

  // Mean carrier-to-noise across everything currently tracked. One number is
  // enough to show an antenna or cable going bad over hours, which is what the
  // history is for; the per-satellite detail already lives on the GNSS page.
  uint32_t cnSum = 0; int cnN = 0;
  for (int i = 0; i < sigCount; i++)
    if (localSigs[i].snr > 0) { cnSum += localSigs[i].snr; cnN++; }

  // Not g.satsInUse: GGA field 7 on this module reports more than the number of
  // satellites GSV says are tracked, so it is counting something else - signals,
  // most likely, on a dual-frequency receiver. These two are the figures the
  // header shows, counted from GSV and GSA.
  uint8_t histTracked = 0, histUsed = 0;
  for (int i = 0; i < SYS_COUNT; i++) { histTracked += satTracked[i]; histUsed += satUsed[i]; }

  historyFeed(millis(), histUsed, histTracked,
              cnN ? (uint8_t)(cnSum / cnN) : 0,
              g.validHdop ? g.hdop : 0.0,
              g.fixQual, jam.l1, jam.l5,
              g.validLoc && g.validAlt,
              g.lat, g.lon, g.alt,
              rtcmStats.bytesSec,
              ionoUsable ? dSum / ionoUsable : -1.0f);

  RtcmClientInfo tinfo[MAX_TCP_CLIENTS];
  int tn = snapshotTcpClients(tinfo, MAX_TCP_CLIENTS);
  UdpClientInfo uinfo[MAX_UDP_CLIENTS];
  int un = snapshotUdpClients(uinfo, MAX_UDP_CLIENTS);

  uint32_t nowMs = millis();
  char timeStr[12] = "--:--:--";
  if (g.validTime) snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", g.hour, g.min, g.sec);

  // These allocate a String each; done once here rather than inline.
  String staIp = WiFi.localIP().toString();
  String apIp  = WiFi.softAPIP().toString();
  String ssid  = WiFi.SSID();

  JBuf j(jsonBuffer, sizeof(jsonBuffer));
  j.put('{'); j.first = true;

  j.kv("seq",  (unsigned long)telemSeq);
  j.kv("skip", (unsigned long)telemSkipped);
  j.kv("up",   (unsigned long)(nowMs / 1000));
  j.kv("ip",   staIp.c_str());
  j.kv("ap",   apIp.c_str());
  j.kv("ssid", ssid.c_str());
  j.kv("rssi", (int)WiFi.RSSI());
  j.kv("net",  (int)currentNetState);
  j.kv("heap", (unsigned long)ESP.getFreeHeap());
  j.kv("hmax", (unsigned long)ESP.getMaxAllocHeap());
  j.kv("hmin", (unsigned long)ESP.getMinFreeHeap());
  j.kv("model", RX_MODEL);
  j.kv("esp",  FW_VERSION);

  j.kv("lat",  g.validLoc ? g.lat : 0.0, 8);
  j.kv("lon",  g.validLoc ? g.lon : 0.0, 8);
  j.kv("alt",  g.validAlt ? g.alt : 0.0, 3);
  j.kv("sep",  g.geoidSep, 3);
  j.kv("vloc", g.validLoc);
  j.kv("valt", g.validAlt);
  j.kv("hdop", g.validHdop ? g.hdop : 0.0, 2);
  j.kv("pdop", g.pdop, 2);
  j.kv("vdop", g.vdop, 2);
  j.kv("fq",   (int)g.fixQual);
  j.kv("ft",   (int)g.fixType);
  j.kv("siu",  (int)g.satsInUse);
  j.kv("time", timeStr);
  j.kv("pps",  (micros() - lastPps < 2000000));
  j.kv("rtcm", (unsigned long)currentRtcmCount);
  j.kv("tcp",  activeTcp);
  j.kv("udp",  activeUdp);
  j.kv("cpu0", cpu0Usage);
  j.kv("cpu1", cpu1Usage);

  j.obj("apinfo");
    j.kv("ssid", apCfg.ssid);
    j.kv("ch",   (int)apCfg.channel);
    j.kv("sec",  strlen(apCfg.pass) >= 8);
    j.kv("hide", apCfg.hidden);
    j.kv("n",    (int)WiFi.softAPgetStationNum());
  j.end('}');

  j.obj("out");
    j.kv("tcpEn",   outCfg.tcpEnabled);
    j.kv("tcpPort", (int)outCfg.tcpPort);
    j.kv("accept",  (int)outCfg.acceptMode);
    j.kv("udpEn",   outCfg.udpEnabled);
    j.kv("udpPort", (int)outCfg.udpPort);
    j.kv("udpDst",  outCfg.udpDest);
    j.kv("udpDstP", (int)outCfg.udpDestPort);
    j.kv("udpBc",   outCfg.udpBroadcast);
    j.kv("udpRx",   (unsigned long)udpRxCount);
    j.kv("udpFrom", udpLastFrom);
    j.kv("udpAge",  udpLastRxMs ? (int)((nowMs - udpLastRxMs) / 1000) : -1);
    j.kv("mount",   outCfg.mount);
    j.kv("user",    outCfg.ntripUser);
    j.kv("auth",    (bool)(outCfg.ntripUser[0] || outCfg.ntripPass[0]));
    j.kv("usbEn",   outCfg.usbEnabled);
    j.kv("usbBaud", (unsigned long)outCfg.usbBaud);
    j.kv("usbTx",   (unsigned long)usbStats.bytes);
    j.kv("usbFr",   (unsigned long)usbStats.frames);
    j.kv("usbDrop", (unsigned long)usbStats.dropped);
  j.end('}');

  // [sysIdx, prn, elev, azim, rawSlant*100, deltaVert*100, arcSeconds, ippLat, ippLon]
  j.arr("io");
  for (int i = 0; i < ionoN; i++) {
    const IonoSat &t = ionoLocal[i];
    j.comma(); j.put('['); j.first = true;
      j.el((int)t.sys); j.el((int)t.prn); j.el((int)t.elev); j.el((int)t.azim);
      j.el((long)lroundf(t.slantRaw * 100));
      j.el((long)lroundf(t.vertDelta * 100));
      j.el((unsigned long)(t.hasRef ? (nowMs - t.arcStartMs) / 1000 : 0));
      j.el((double)t.ippLat, 4); j.el((double)t.ippLon, 4);
    j.end(']');
  }
  j.end(']');
  j.kv("ion",  ionoUsable);
  j.kv("iond", ionoUsable ? (double)dSum / ionoUsable : 0.0, 3);

  j.obj("bc");
    j.kv("v",   bc.valid);
    j.kv("id",  (int)bc.stationId);
    j.kv("x",   bc.x, 4);
    j.kv("y",   bc.y, 4);
    j.kv("z",   bc.z, 4);
    j.kv("age", bc.lastMs ? (int)((nowMs - bc.lastMs) / 1000) : -1);
    j.kv("st",  (unsigned long)(bc.stableSince ? (nowMs - bc.stableSince) / 1000 : 0));
    if (bc.valid) {
      double la, lo, hg;
      ecefToLla(bc.x, bc.y, bc.z, la, lo, hg);
      j.kv("lat", la, 8); j.kv("lon", lo, 8); j.kv("hgt", hg, 3);
    }
  j.end('}');

  j.obj("svin");
    j.kv("feat", (int)sv.feat); j.kv("v", (int)sv.valid);
    j.kv("obs", (unsigned long)sv.obs); j.kv("dur", (unsigned long)sv.cfgDur);
    j.kv("acc", (double)sv.acc, 2);
    j.kv("x", sv.x, 4); j.kv("y", sv.y, 4); j.kv("z", sv.z, 4);
  j.end('}');

  j.obj("jam");
    j.kv("feat", (int)jm.feat); j.kv("l1", (int)jm.l1);
    j.kv("l5", jm.haveL5 ? (int)jm.l5 : -1);
  j.end('}');

  j.obj("arp");
    j.kv("n", (double)arpCfg.north, 3);
    j.kv("e", (double)arpCfg.east, 3);
    j.kv("u", (double)arpCfg.up, 3);
  j.end('}');

  j.obj("avg");
    j.kv("run",  pa.running);
    j.kv("el",   (unsigned long)(pa.running ? (nowMs - pa.startedMs) / 1000 : 0));
    j.kv("tgt",  (unsigned long)pa.targetSec);
    j.kv("n",    (unsigned long)pa.count);
    j.kv("have", pa.haveResult);
    j.kv("lat",  pa.lat, 8); j.kv("lon", pa.lon, 8); j.kv("alt", pa.alt, 3);
    j.kv("rms",  (double)pa.rms, 4);
  j.end('}');

  j.obj("push");
    j.kv("en",    pushCfg.enabled);
    j.kv("host",  pushCfg.host);
    j.kv("port",  (int)pushCfg.port);
    j.kv("mount", pushCfg.mount);
    j.kv("st",    (int)pushState.state);
    j.kv("msg",   pushState.msg);
    j.kv("sent",  (unsigned long)pushState.sent);
    j.kv("retry", (unsigned long)pushState.retries);
    j.kv("up",    (unsigned long)(pushState.state == PUSH_STREAMING
                                  ? (nowMs - pushState.sinceMs) / 1000 : 0));
  j.end('}');

  j.obj("rst");
    j.kv("f",   (unsigned long)st.frames);
    j.kv("crc", (unsigned long)st.crcErrors);
    j.kv("bps", (unsigned long)st.bytesSec);
    j.kv("age", st.lastFrameMs ? (int)((nowMs - st.lastFrameMs) / 1000) : -1);
    j.arr("ty");
    for (int i = 0; i < st.typeCount; i++) {
      j.comma(); j.put('['); j.first = true;
        j.el((int)st.types[i]); j.el((int)st.typeHits[i]);
        j.el((int)st.typeInterval[i]); j.el((int)st.typeJitter[i]);
      j.end(']');
    }
    j.end(']');
  j.end('}');

  // [ip, mode, seconds connected, bytes sent, fixQual, sats, hdop, baseline m,
  //  gga age s, lat, lon]
  // Everything from index 4 on comes from the rover's own GGA; a consumer that
  // never reports leaves them at 0 / -1.
  j.arr("cl");
  for (int i = 0; i < tn; i++) {
    j.comma(); j.put('['); j.first = true;
      j.el(tinfo[i].ip);
      j.el(tinfo[i].mode == CM_NTRIP ? "ntrip" : "raw");
      j.el((unsigned long)((nowMs - tinfo[i].since) / 1000));
      j.el((unsigned long)tinfo[i].sent);
      j.el((int)tinfo[i].fixQual);
      j.el((int)tinfo[i].sats);
      j.el((double)tinfo[i].hdop, 2);
      j.el((double)tinfo[i].baseline, 1);
      j.el(tinfo[i].ggaAgeMs == 0xFFFFFFFF ? -1L
                                           : (long)(tinfo[i].ggaAgeMs / 1000));
      j.el(tinfo[i].hasFix ? tinfo[i].lat : 0.0, 7);
      j.el(tinfo[i].hasFix ? tinfo[i].lon : 0.0, 7);
    j.end(']');
  }
  j.end(']');

  j.arr("ul");
  for (int i = 0; i < un; i++) {
    j.comma(); j.put('['); j.first = true;
      j.el(uinfo[i].ip); j.el((int)uinfo[i].port);
      j.el((unsigned long)((nowMs - uinfo[i].since) / 1000));
      j.el((unsigned long)uinfo[i].sent);
    j.end(']');
  }
  j.end(']');

  // [tracked, used] per constellation, in GnssSys order (SYS_UNK excluded).
  j.arr("cons");
  for (int sy = 0; sy < SYS_UNK; sy++) {
    j.comma(); j.put('['); j.first = true;
      j.el((int)satTracked[sy]); j.el((int)satUsed[sy]);
    j.end(']');
  }
  j.end(']');

  j.arr("bands");
  for (int sy = 0; sy < SYS_UNK; sy++) {
    j.comma(); j.put('['); j.first = true;
    for (int k = 0; k < BANDS_PER_SYS; k++) {
      if (BAND_NAMES[sy][k] == NULL) break;
      j.el((int)bandCount[sy][k]);
    }
    j.end(']');
  }
  j.end(']');

  // [sys, prn, elev, azim, snr, band, used] per tracked signal.
  j.arr("sig");
  for (int i = 0; i < sigCount; i++) {
    if (localSigs[i].sys >= SYS_UNK) continue;
    j.comma(); j.put('['); j.first = true;
      j.el((int)localSigs[i].sys);  j.el((int)localSigs[i].prn);
      j.el((int)localSigs[i].elev); j.el((int)localSigs[i].azim);
      j.el((int)localSigs[i].snr);  j.el((int)localSigs[i].band);
      j.el(localUsed[i] ? 1 : 0);
    j.end(']');
  }
  j.end(']');

  j.obj("base");
    j.kv("m",    (int)base.svinMode);
    j.kv("dur",  (unsigned long)base.minDur);
    j.kv("acc",  (double)base.accLimit, 1);
    j.kv("x",    base.ecefX, 4); j.kv("y", base.ecefY, 4); j.kv("z", base.ecefZ, 4);
    j.kv("rtcm", (int)base.rtcmMode);
    j.kv("arp",  (int)base.arpEnabled);
    j.kv("eph",  (int)base.ephEnabled);
    j.kv("el",   (unsigned long)(base.svinStartMs ? (nowMs - base.svinStartMs) / 1000 : 0));
    j.kv("ver",  base.verStr);
    j.kv("bd",   base.buildDate);
    j.kv("msg",  base.lastResult);
  j.end('}');

  j.put('}');
  size_t jsonLen = j.n;

  AsyncWebSocketMessageBuffer * wsBuf = ws.makeBuffer(jsonLen);
  if (wsBuf) {
      memcpy(wsBuf->get(), jsonBuffer, jsonLen);
      ws.textAll(wsBuf);
  }
}
