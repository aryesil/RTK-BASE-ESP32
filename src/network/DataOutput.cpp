#include <network/DataOutput.h>
#include <Globals.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>
#include <network/NtripPush.h>

static WiFiServer* tcpServer = nullptr;
static Preferences outPrefs;
// One socket for every UDP direction, owned outright rather than through
// WiFiUDP. That library cannot enable SO_BROADCAST and hides its descriptor,
// which forced broadcasts onto a second, unbound socket - and those went out
// from an ephemeral source port, so any receiver connected to <device>:<port>
// silently discarded them. Binding one socket keeps the source port equal to
// the listen port on all three paths.
static int udpSock = -1;

// Listener teardown must happen on the task that polls them. A web handler
// deleting tcpServer while the GNSS task is inside hasClient() would be a
// use-after-free, so config changes only raise this flag.
static volatile bool restartPending = false;

struct TcpSlot {
  WiFiClient sock;
  uint8_t    mode;
  uint32_t   since;
  uint32_t   sent;
  uint16_t   reqLen;
  char       req[NTRIP_REQ_MAX];
};
static TcpSlot slots[MAX_TCP_CLIENTS];

struct UdpSlot {
  IPAddress ip;
  uint16_t  port;
  uint32_t  since;
  uint32_t  lastSeen;
  uint32_t  sent;
  bool      used;
};
static UdpSlot udpSlots[MAX_UDP_CLIENTS];

// Pre-computed "Basic <base64(user:pass)>" for constant-work comparison.
static char expectedAuth[80];

// --------------------------------------------------------------------------
// config
// --------------------------------------------------------------------------
static void rebuildExpectedAuth() {
  expectedAuth[0] = '\0';
  if (outCfg.ntripUser[0] == '\0' && outCfg.ntripPass[0] == '\0') return;

  char raw[52];
  snprintf(raw, sizeof(raw), "%s:%s", outCfg.ntripUser, outCfg.ntripPass);

  unsigned char enc[80];
  size_t encLen = 0;
  if (mbedtls_base64_encode(enc, sizeof(enc) - 1, &encLen,
                            (const unsigned char*)raw, strlen(raw)) == 0) {
    enc[encLen] = '\0';
    snprintf(expectedAuth, sizeof(expectedAuth), "Basic %s", (char*)enc);
  }
}

void loadOutputCfg() {
  outPrefs.begin("outcfg", false);
  outCfg.tcpEnabled = outPrefs.getBool("tcpEn", true);
  outCfg.tcpPort    = (uint16_t)outPrefs.getUShort("tcpPort", RTCM_PORT);
  outCfg.acceptMode = (uint8_t)outPrefs.getUChar("accept", 0);
  outCfg.udpEnabled = outPrefs.getBool("udpEn", true);
  outCfg.udpPort    = (uint16_t)outPrefs.getUShort("udpPort", UDP_PORT);
  strlcpy(outCfg.udpDest, outPrefs.getString("udpDst", "").c_str(), sizeof(outCfg.udpDest));
  outCfg.udpDestPort = (uint16_t)outPrefs.getUShort("udpDstP", 0);
  outCfg.udpBroadcast = outPrefs.getBool("udpBc", false);
  strlcpy(outCfg.mount, outPrefs.getString("mount", "RTK").c_str(), sizeof(outCfg.mount));
  strlcpy(outCfg.ntripUser, outPrefs.getString("user", "").c_str(), sizeof(outCfg.ntripUser));
  strlcpy(outCfg.ntripPass, outPrefs.getString("pass", "").c_str(), sizeof(outCfg.ntripPass));
  rebuildExpectedAuth();
}

void saveOutputCfg() {
  outPrefs.putBool("tcpEn", outCfg.tcpEnabled);
  outPrefs.putUShort("tcpPort", outCfg.tcpPort);
  outPrefs.putUChar("accept", outCfg.acceptMode);
  outPrefs.putBool("udpEn", outCfg.udpEnabled);
  outPrefs.putUShort("udpPort", outCfg.udpPort);
  outPrefs.putString("udpDst", outCfg.udpDest);
  outPrefs.putUShort("udpDstP", outCfg.udpDestPort);
  outPrefs.putBool("udpBc", outCfg.udpBroadcast);
  outPrefs.putString("mount", outCfg.mount);
  outPrefs.putString("user", outCfg.ntripUser);
  outPrefs.putString("pass", outCfg.ntripPass);
  rebuildExpectedAuth();
}

// --------------------------------------------------------------------------
// listeners
// --------------------------------------------------------------------------
static void closeAllClients() {
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (slots[i].mode != CM_EMPTY) slots[i].sock.stop();
      slots[i].mode = CM_EMPTY;
      slots[i].reqLen = 0;
    }
    for (int i = 0; i < MAX_UDP_CLIENTS; i++) udpSlots[i].used = false;
    xSemaphoreGive(tcpMutex);
  }
}

void restartDataOutput() {
  restartPending = true;
}

static void applyRestart() {
  restartPending = false;
  closeAllClients();

  if (tcpServer) {
    tcpServer->end();
    delete tcpServer;
    tcpServer = nullptr;
  }
  if (udpSock >= 0) { ::close(udpSock); udpSock = -1; }

  if (outCfg.tcpEnabled) {
    tcpServer = new WiFiServer(outCfg.tcpPort);
    tcpServer->begin();
    tcpServer->setNoDelay(true);
  }
  if (outCfg.udpEnabled) {
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock >= 0) {
      int yes = 1;
      setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      setsockopt(udpSock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

      struct sockaddr_in me;
      memset(&me, 0, sizeof(me));
      me.sin_family = AF_INET;
      me.sin_addr.s_addr = htonl(INADDR_ANY);
      me.sin_port = htons(outCfg.udpPort);
      if (bind(udpSock, (struct sockaddr*)&me, sizeof(me)) < 0) {
        ::close(udpSock);
        udpSock = -1;
      }
    }
  }

  Serial.printf("[OUT] TCP %s:%u  UDP %s:%u  mount=/%s  auth=%s\n",
                outCfg.tcpEnabled ? "on" : "off", outCfg.tcpPort,
                udpSock >= 0 ? "on" : "off", outCfg.udpPort,
                outCfg.mount, expectedAuth[0] ? "yes" : "no");
}

void initDataOutput() {
  loadOutputCfg();
  applyRestart();   // setup() runs before the polling task exists
}

// --------------------------------------------------------------------------
// NTRIP handshake
// --------------------------------------------------------------------------
static void sendSourcetable(WiFiClient &c) {
  String str = "STR;";
  str += outCfg.mount;
  str += ";ESP32 RTK Base;RTCM 3.3;";
  // Advertised message list follows what the module is actually emitting.
  // Read without dataMutex on purpose: this runs while tcpMutex is held, and
  // taking dataMutex here would be the only place the two nest. noteMessageType
  // fills types[n] before bumping typeCount, so a count read first is safe.
  uint8_t n = rtcmStats.typeCount;
  for (uint8_t i = 0; i < n && i < RTCM_MAX_TYPES; i++) {
    if (i) str += ",";
    str += String(rtcmStats.types[i]) + "(1)";
  }
  str += ";2;GPS+GLO+GAL+BDS+QZSS;ESP32;XXX;0.00;0.00;0;0;LC29H(BS);none;";
  str += (expectedAuth[0] ? "B" : "N");
  str += ";N;0;\r\nENDSOURCETABLE\r\n";

  String head = "SOURCETABLE 200 OK\r\nServer: ESP32-RTK-Base\r\n"
                "Content-Type: text/plain\r\nContent-Length: " +
                String(str.length()) + "\r\nConnection: close\r\n\r\n";
  c.print(head);
  c.print(str);
}

// Extracts a header value into out. Returns false when the header is absent.
static bool headerValue(const char* req, const char* name, char* out, size_t outSize) {
  const char* p = strcasestr(req, name);
  if (!p) return false;
  p += strlen(name);
  while (*p == ' ' || *p == ':') p++;
  size_t n = 0;
  while (*p && *p != '\r' && *p != '\n' && n < outSize - 1) out[n++] = *p++;
  out[n] = '\0';
  return true;
}

// Consumes the buffered request and decides what this client is.
// Returns the new ClientMode, or CM_EMPTY when the socket must be dropped.
static uint8_t resolveNtripRequest(TcpSlot &s) {
  char path[40] = {0};
  const char* sp = strchr(s.req, ' ');
  if (sp) {
    sp++;
    size_t n = 0;
    while (sp[n] && sp[n] != ' ' && sp[n] != '\r' && n < sizeof(path) - 1) {
      path[n] = sp[n];
      n++;
    }
    path[n] = '\0';
  }

  char ver[24] = {0};
  bool v2 = headerValue(s.req, "Ntrip-Version", ver, sizeof(ver)) &&
            strstr(ver, "2.0") != NULL;

  // "GET /" (no mountpoint) is a source-table request.
  if (path[0] == '\0' || strcmp(path, "/") == 0) {
    sendSourcetable(s.sock);
    s.sock.stop();
    return CM_EMPTY;
  }

  const char* want = path[0] == '/' ? path + 1 : path;
  if (strcmp(want, outCfg.mount) != 0) {
    s.sock.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
    s.sock.stop();
    return CM_EMPTY;
  }

  if (expectedAuth[0]) {
    char auth[80] = {0};
    if (!headerValue(s.req, "Authorization", auth, sizeof(auth)) ||
        strcmp(auth, expectedAuth) != 0) {
      s.sock.print("HTTP/1.1 401 Unauthorized\r\n"
                   "WWW-Authenticate: Basic realm=\"ESP32 RTK Base\"\r\n"
                   "Connection: close\r\n\r\n");
      s.sock.stop();
      return CM_EMPTY;
    }
  }

  if (v2) {
    s.sock.print("HTTP/1.1 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n"
                 "Server: ESP32-RTK-Base\r\nContent-Type: gnss/data\r\n"
                 "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
  } else {
    s.sock.print("ICY 200 OK\r\n\r\n");
  }
  return CM_NTRIP;
}

// --------------------------------------------------------------------------
// polling
// --------------------------------------------------------------------------
static void acceptNewClients() {
  if (!tcpServer || !tcpServer->hasClient()) return;

  WiFiClient nc = tcpServer->available();
  if (!nc) return;
  nc.setNoDelay(true);

  bool placed = false;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (slots[i].mode != CM_EMPTY && slots[i].sock.connected()) continue;
      if (slots[i].mode != CM_EMPTY) slots[i].sock.stop();

      slots[i].sock = nc;
      slots[i].since = millis();
      slots[i].sent = 0;
      slots[i].reqLen = 0;
      slots[i].req[0] = '\0';
      // acceptMode 2 skips the sniff entirely so raw clients start instantly.
      slots[i].mode = (outCfg.acceptMode == 2) ? CM_RAW : CM_SNIFF;
      placed = true;
      break;
    }
    xSemaphoreGive(tcpMutex);
  }
  if (!placed) {
    nc.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
    nc.stop();
  }
}

static void serviceHandshakes() {
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      TcpSlot &s = slots[i];
      if (s.mode == CM_EMPTY) continue;

      if (!s.sock.connected()) {
        s.sock.stop();
        s.mode = CM_EMPTY;
        continue;
      }
      if (s.mode != CM_SNIFF) {
        // Drain anything a streaming client sends; NTRIP clients push GGA.
        while (s.sock.available()) s.sock.read();
        continue;
      }

      while (s.sock.available() && s.reqLen < NTRIP_REQ_MAX - 1) {
        s.req[s.reqLen++] = (char)s.sock.read();
      }
      s.req[s.reqLen] = '\0';

      // "GET" can itself be split across segments, so anything that is still a
      // viable prefix counts as pending rather than as raw data.
      bool prefixOk = s.reqLen > 0 &&
                      strncmp(s.req, "GET", s.reqLen < 3 ? s.reqLen : 3) == 0;
      bool isHttp = s.reqLen >= 3 && prefixOk;
      bool complete = strstr(s.req, "\r\n\r\n") || strstr(s.req, "\n\n");

      if (isHttp && complete) {
        s.mode = resolveNtripRequest(s);
        if (s.mode == CM_EMPTY) s.reqLen = 0;
      } else if (prefixOk) {
        // Request recognised but still arriving. Real clients routinely split
        // it across segments, so the short sniff window must not apply here:
        // it used to drop them, which broke every fragmented NTRIP connect.
        if (millis() - s.since > NTRIP_REQ_MS || s.reqLen >= NTRIP_REQ_MAX - 1) {
          s.sock.stop();
          s.mode = CM_EMPTY;
        }
      } else if (s.reqLen) {
        // Data that is not an HTTP request: treat as a raw consumer.
        s.mode = (outCfg.acceptMode == 1) ? CM_EMPTY : CM_RAW;
        if (s.mode == CM_EMPTY) s.sock.stop();
      } else if (millis() - s.since > NTRIP_SNIFF_MS) {
        // Still silent: raw software connects and just listens.
        if (outCfg.acceptMode == 1) {
          s.sock.stop();
          s.mode = CM_EMPTY;
        } else {
          s.mode = CM_RAW;
        }
      }
    }
    xSemaphoreGive(tcpMutex);
  }
}

static void serviceUdpRegistrations() {
  if (udpSock < 0) return;

  uint8_t sink[64];
  struct sockaddr_in from;
  socklen_t fromLen = sizeof(from);
  int n;

  while ((n = recvfrom(udpSock, sink, sizeof(sink), MSG_DONTWAIT,
                       (struct sockaddr*)&from, &fromLen)) > 0) {
    IPAddress ip(from.sin_addr.s_addr);
    uint16_t port = ntohs(from.sin_port);
    uint32_t now = millis();

    if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
      int free = -1;
      bool known = false;
      for (int i = 0; i < MAX_UDP_CLIENTS; i++) {
        if (udpSlots[i].used && udpSlots[i].ip == ip && udpSlots[i].port == port) {
          udpSlots[i].lastSeen = now;
          known = true;
          break;
        }
        if (!udpSlots[i].used && free < 0) free = i;
      }

      udpRxCount++;
      udpLastRxMs = now;
      snprintf(udpLastFrom, sizeof(udpLastFrom), "%s:%u", ip.toString().c_str(), port);

      if (!known && free >= 0) {
        udpSlots[free].ip = ip;
        udpSlots[free].port = port;
        udpSlots[free].since = now;
        udpSlots[free].lastSeen = now;
        udpSlots[free].sent = 0;
        udpSlots[free].used = true;
        Serial.printf("[OUT] UDP subscriber %s:%u\n", ip.toString().c_str(), port);
      }
      xSemaphoreGive(tcpMutex);
    }
    fromLen = sizeof(from);
  }

  uint32_t now = millis();
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_UDP_CLIENTS; i++) {
      if (udpSlots[i].used && now - udpSlots[i].lastSeen > UDP_CLIENT_TIMEOUT_MS) {
        udpSlots[i].used = false;
      }
    }
    xSemaphoreGive(tcpMutex);
  }
}

void handleOutputClients() {
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll < 20) return;
  lastPoll = now;

  if (restartPending) applyRestart();
  acceptNewClients();
  serviceHandshakes();
  serviceUdpRegistrations();
}

// --------------------------------------------------------------------------
// fan-out
// --------------------------------------------------------------------------

// WiFiClient::write() waits on select() with a hard-coded 1 s timeout and up to
// 10 retries, so a stalled client could block the GNSS loop for ~10 s while
// holding tcpMutex. Probe writability first and skip the client if its send
// buffer is full; rovers resync on the next RTCM preamble.
static bool isWritable(WiFiClient &client) {
  int sock = client.fd();
  if (sock < 0) return false;

  fd_set set;
  struct timeval tv = {0, 0};
  FD_ZERO(&set);
  FD_SET(sock, &set);

  return select(sock + 1, NULL, &set, NULL, &tv) > 0 && FD_ISSET(sock, &set);
}

// Tracks how often each message type actually arrives. A base whose 1077
// spacing drifts from 1000 ms, or whose jitter climbs, is a base whose rovers
// will start reporting stale corrections.
static size_t udpSendTo(IPAddress ip, uint16_t port, const uint8_t* d, size_t n) {
  if (udpSock < 0) return 0;
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = (uint32_t)ip;
  int sent = sendto(udpSock, d, n, MSG_DONTWAIT, (struct sockaddr*)&to, sizeof(to));
  return sent > 0 ? (size_t)sent : 0;
}

static void noteMessageType(uint16_t type) {
  uint32_t now = millis();

  for (int i = 0; i < rtcmStats.typeCount; i++) {
    if (rtcmStats.types[i] != type) continue;
    rtcmStats.typeHits[i]++;
    if (rtcmStats.typeLastMs[i]) {
      uint32_t gap = now - rtcmStats.typeLastMs[i];
      if (gap > 60000) gap = 60000;
      uint16_t prev = rtcmStats.typeInterval[i];
      // Exponential mean, then the worst deviation from it seen so far.
      rtcmStats.typeInterval[i] = prev ? (uint16_t)((prev * 3 + gap) / 4) : (uint16_t)gap;
      if (prev) {
        uint16_t dev = (uint16_t)(gap > prev ? gap - prev : prev - gap);
        if (dev > rtcmStats.typeJitter[i]) rtcmStats.typeJitter[i] = dev;
      }
    }
    rtcmStats.typeLastMs[i] = now;
    return;
  }

  if (rtcmStats.typeCount < RTCM_MAX_TYPES) {
    int i = rtcmStats.typeCount++;
    rtcmStats.types[i] = type;
    rtcmStats.typeHits[i] = 1;
    rtcmStats.typeLastMs[i] = now;
    rtcmStats.typeInterval[i] = 0;
    rtcmStats.typeJitter[i] = 0;
  }
}

void sendRtcmFrame(const uint8_t* frame, size_t len, uint16_t msgType) {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    rtcmStats.frames++;
    rtcmStats.bytesAcc += len;
    rtcmStats.lastFrameMs = millis();
    noteMessageType(msgType);
    rtcmPaketSayaci++;
    xSemaphoreGive(dataMutex);
  }

  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      TcpSlot &s = slots[i];
      if (s.mode != CM_RAW && s.mode != CM_NTRIP) continue;
      if (!s.sock.connected()) { s.sock.stop(); s.mode = CM_EMPTY; continue; }
      if (!isWritable(s.sock)) continue;   // full send buffer, drop this epoch
      s.sent += s.sock.write(frame, len);
    }

    ntripPushWrite(frame, len);

    if (udpSock >= 0) {
      // Every datagram leaves from the bound port, so a receiver that connected
      // to <device>:<udpPort> accepts it.
      if (outCfg.udpDest[0] && outCfg.udpDestPort) {
        IPAddress dst;
        if (dst.fromString(outCfg.udpDest))
          udpSendTo(dst, outCfg.udpDestPort, frame, len);
      }

      if (outCfg.udpBroadcast) {
        // One datagram per interface: the rover may be on the soft-AP while a
        // second consumer sits on the uplink LAN.
        if (WiFi.status() == WL_CONNECTED) {
          uint32_t ip = (uint32_t)WiFi.localIP(), mask = (uint32_t)WiFi.subnetMask();
          udpSendTo(IPAddress(ip | ~mask), outCfg.udpPort, frame, len);
        }
        uint32_t ap = (uint32_t)WiFi.softAPIP();
        if (ap) udpSendTo(IPAddress(ap | 0xFF000000), outCfg.udpPort, frame, len);
      }

      for (int i = 0; i < MAX_UDP_CLIENTS; i++) {
        if (!udpSlots[i].used) continue;
        // One datagram per RTCM frame: the receiver never has to re-frame, and
        // a lost packet costs one epoch instead of stalling the stream.
        udpSlots[i].sent += udpSendTo(udpSlots[i].ip, udpSlots[i].port, frame, len);
      }
    }
    xSemaphoreGive(tcpMutex);
  }
}

// --------------------------------------------------------------------------
// introspection
// --------------------------------------------------------------------------
int tcpClientCount() {
  int n = 0;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (slots[i].mode == CM_RAW || slots[i].mode == CM_NTRIP) n++;
    }
    xSemaphoreGive(tcpMutex);
  }
  return n;
}

int udpClientCount() {
  int n = 0;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_UDP_CLIENTS; i++) if (udpSlots[i].used) n++;
    xSemaphoreGive(tcpMutex);
  }
  return n;
}

int snapshotTcpClients(RtcmClientInfo* out, int max) {
  int n = 0;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_TCP_CLIENTS && n < max; i++) {
      if (slots[i].mode != CM_RAW && slots[i].mode != CM_NTRIP) continue;
      out[n].mode = slots[i].mode;
      strlcpy(out[n].ip, slots[i].sock.remoteIP().toString().c_str(), sizeof(out[n].ip));
      out[n].since = slots[i].since;
      out[n].sent = slots[i].sent;
      n++;
    }
    xSemaphoreGive(tcpMutex);
  }
  return n;
}

int snapshotUdpClients(UdpClientInfo* out, int max) {
  int n = 0;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < MAX_UDP_CLIENTS && n < max; i++) {
      if (!udpSlots[i].used) continue;
      strlcpy(out[n].ip, udpSlots[i].ip.toString().c_str(), sizeof(out[n].ip));
      out[n].port = udpSlots[i].port;
      out[n].since = udpSlots[i].since;
      out[n].sent = udpSlots[i].sent;
      n++;
    }
    xSemaphoreGive(tcpMutex);
  }
  return n;
}
