#include <network/NtripPush.h>
#include <Globals.h>
#include <lwip/sockets.h>

static Preferences pushPrefs;

// Owned by the core-0 state machine until the handshake succeeds, then handed
// to `live` under tcpMutex so the GNSS core can write to it.
static WiFiClient pending;
static WiFiClient live;

static char   rxBuf[128];
static size_t rxLen = 0;
static uint32_t backoffMs = 5000;

static void setState(uint8_t s, const char* msg) {
  pushState.state = s;
  pushState.sinceMs = millis();
  if (msg) strlcpy(pushState.msg, msg, sizeof(pushState.msg));
}

void loadNtripPushCfg() {
  pushPrefs.begin("ntpush", false);
  pushCfg.enabled = pushPrefs.getBool("en", false);
  strlcpy(pushCfg.host, pushPrefs.getString("host", "").c_str(), sizeof(pushCfg.host));
  pushCfg.port = (uint16_t)pushPrefs.getUShort("port", 2101);
  strlcpy(pushCfg.mount, pushPrefs.getString("mount", "").c_str(), sizeof(pushCfg.mount));
  strlcpy(pushCfg.pass, pushPrefs.getString("pass", "").c_str(), sizeof(pushCfg.pass));
}

void saveNtripPushCfg() {
  pushPrefs.putBool("en", pushCfg.enabled);
  pushPrefs.putString("host", pushCfg.host);
  pushPrefs.putUShort("port", pushCfg.port);
  pushPrefs.putString("mount", pushCfg.mount);
  pushPrefs.putString("pass", pushCfg.pass);

  // Force the state machine to re-evaluate immediately.
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    if (live.connected()) live.stop();
    xSemaphoreGive(tcpMutex);
  }
  pending.stop();
  rxLen = 0;
  backoffMs = 5000;
  setState(pushCfg.enabled ? PUSH_WAIT : PUSH_OFF,
           pushCfg.enabled ? "Settings saved, reconnecting" : "Disabled");
}

void initNtripPush() {
  loadNtripPushCfg();
  setState(pushCfg.enabled ? PUSH_WAIT : PUSH_OFF,
           pushCfg.enabled ? "Waiting for uplink" : "Disabled");
}

static void dropAndBackoff(const char* why) {
  pending.stop();
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    if (live.connected()) live.stop();
    xSemaphoreGive(tcpMutex);
  }
  rxLen = 0;
  pushState.retries++;
  setState(PUSH_WAIT, why);
  // 5s, 10s, 20s, 40s, capped at 60s: fast enough for a blip, polite enough
  // that a misconfigured mountpoint does not hammer someone else's caster.
  backoffMs = backoffMs < 60000 ? backoffMs * 2 : 60000;
}

void handleNtripPush() {
  uint32_t now = millis();

  if (!pushCfg.enabled || pushCfg.host[0] == '\0' || pushCfg.mount[0] == '\0') {
    if (pushState.state != PUSH_OFF) {
      pending.stop();
      if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
        if (live.connected()) live.stop();
        xSemaphoreGive(tcpMutex);
      }
      setState(PUSH_OFF, pushCfg.enabled ? "Host and mountpoint required" : "Disabled");
    }
    return;
  }

  switch (pushState.state) {
    case PUSH_OFF:
      setState(PUSH_WAIT, "Waiting for uplink");
      backoffMs = 5000;
      break;

    case PUSH_WAIT:
      // A caster is only reachable through the station interface; the soft-AP
      // has no route off the device.
      if (WiFi.status() != WL_CONNECTED) {
        strlcpy(pushState.msg, "Needs a WiFi uplink (Network tab)", sizeof(pushState.msg));
        pushState.sinceMs = now;
        return;
      }
      if (now - pushState.sinceMs < backoffMs && pushState.retries) return;
      setState(PUSH_CONNECTING, "Connecting");
      break;

    case PUSH_CONNECTING: {
      // Copy the settings before the blocking call: a web request may rewrite
      // them from another task at any moment, and connect() holds no lock.
      char host[sizeof(pushCfg.host)], mount[sizeof(pushCfg.mount)], pw[sizeof(pushCfg.pass)];
      uint16_t port;
      if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
        strlcpy(host, pushCfg.host, sizeof(host));
        strlcpy(mount, pushCfg.mount, sizeof(mount));
        strlcpy(pw, pushCfg.pass, sizeof(pw));
        port = pushCfg.port;
        xSemaphoreGive(baseMutex);
      } else {
        return;
      }

      // Blocking, which is why this runs on the network task and not on the
      // core that forwards RTCM.
      if (!pending.connect(host, port, 5000)) {
        dropAndBackoff("Connection refused or host unreachable");
        return;
      }
      pending.setNoDelay(true);

      char req[192];
      int n = snprintf(req, sizeof(req),
                       "SOURCE %s /%s\r\n"
                       "Source-Agent: NTRIP ESP32-RTK-Base/" FW_VERSION "\r\n"
                       "STR: \r\n\r\n",
                       pw, mount);
      pending.write((const uint8_t*)req, n);
      rxLen = 0;
      setState(PUSH_HANDSHAKE, "Waiting for caster response");
      break;
    }

    case PUSH_HANDSHAKE: {
      while (pending.available() && rxLen < sizeof(rxBuf) - 1) {
        rxBuf[rxLen++] = (char)pending.read();
      }
      rxBuf[rxLen] = '\0';

      if (strstr(rxBuf, "ICY 200") || strstr(rxBuf, "200 OK")) {
        if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
          live = pending;
          xSemaphoreGive(tcpMutex);
        }
        pushState.sent = 0;
        backoffMs = 5000;
        setState(PUSH_STREAMING, "Streaming");
        if (xSemaphoreTake(baseMutex, portMAX_DELAY)) {
          Log.printf("[NTRIP] Pushing to %s:%u/%s\n",
                        pushCfg.host, pushCfg.port, pushCfg.mount);
          xSemaphoreGive(baseMutex);
        }
        return;
      }
      if (rxLen && (strstr(rxBuf, "ERROR") || strstr(rxBuf, "401") || strstr(rxBuf, "400"))) {
        char why[56];
        snprintf(why, sizeof(why), "Caster rejected: %.38s", rxBuf);
        // Strip the line ending so the message stays on one UI line.
        for (char* p = why; *p; p++) if (*p == '\r' || *p == '\n') { *p = '\0'; break; }
        dropAndBackoff(why);
        return;
      }
      if (!pending.connected() || now - pushState.sinceMs > 8000) {
        dropAndBackoff("No usable response from caster");
      }
      break;
    }

    case PUSH_STREAMING: {
      bool up;
      if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
        up = live.connected();
        while (up && live.available()) live.read();  // casters may send keepalives
        xSemaphoreGive(tcpMutex);
      } else {
        return;
      }
      if (!up) dropAndBackoff("Caster closed the connection");
      break;
    }

    default:
      setState(PUSH_WAIT, "Retrying");
      break;
  }
}

// Same non-blocking writability probe used for the local consumers: a stalled
// caster must never hold up the GNSS core.
static bool writable(WiFiClient &c) {
  int fd = c.fd();
  if (fd < 0) return false;
  fd_set set;
  struct timeval tv = {0, 0};
  FD_ZERO(&set);
  FD_SET(fd, &set);
  return select(fd + 1, NULL, &set, NULL, &tv) > 0 && FD_ISSET(fd, &set);
}

void ntripPushWrite(const uint8_t* frame, size_t len) {
  if (pushState.state != PUSH_STREAMING) return;
  if (!live.connected() || !writable(live)) return;
  pushState.sent += live.write(frame, len);
}
