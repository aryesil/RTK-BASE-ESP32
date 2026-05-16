#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>

// ==========================================
// 1. KULLANICI AYARLARI 
// ==========================================
const char* ssid = "Arda";
const char* password = "ozurlubaskan";

// ==========================================
// 2. DONANIM VE NESNE TANIMLAMALARI
// ==========================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

TinyGPSPlus gps;

#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200 
#define PPS_PIN 27
#define RTCM_PORT 2101

WiFiServer rtcmServer(RTCM_PORT);
WiFiClient tcpClients[3]; 

// FreeRTOS Görev, Mutex ve Kuyruk Tanımlamaları
TaskHandle_t NetworkTaskHandle;
SemaphoreHandle_t dataMutex; 
SemaphoreHandle_t tcpMutex;  
QueueHandle_t termQueue;     

// --- ÇİFT BANT DESTEKLİ UYDU VERİ YAPISI ---
struct SatData {
  int id;
  char sys[4]; 
  int elev;   
  int azim;   
  int snr;    
  int sig; 
  uint32_t lastSeen; 
};

// JSON Truncation'ı engellemek için sınır 150'ye çıkarıldı
#define MAX_SATS 150
SatData activeSats[MAX_SATS];
volatile int activeSatCount = 0;

// Thread-Safe GPS Snapshot Yapısı
struct GpsSnapshot {
  double lat, lon, alt, hdop;
  bool validLoc, validAlt, validHdop, validTime;
  uint8_t hour, min, sec;
} safeGps;

volatile uint32_t rtcmPaketSayaci = 0;
volatile uint32_t sonPpsZamaniMicros = 0; 

#define MAX_NMEA 256
char nmeaBuff[MAX_NMEA];
int nmeaIdx = 0;

void IRAM_ATTR ppsKesmesi() {
  sonPpsZamaniMicros = micros(); 
}

// Mutex Korumalı Uydu Ekleme
void addSat(const char* sys, int id, int elev, int azim, int snr, int sig) {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    for (int i = 0; i < activeSatCount; i++) {
      if (strcmp(activeSats[i].sys, sys) == 0 && activeSats[i].id == id && activeSats[i].sig == sig) {
        activeSats[i].elev = elev; 
        activeSats[i].azim = azim; 
        activeSats[i].snr = snr;
        activeSats[i].lastSeen = now;
        xSemaphoreGive(dataMutex);
        return;
      }
    }
    if (activeSatCount < MAX_SATS) {
      activeSats[activeSatCount].id = id;
      strlcpy(activeSats[activeSatCount].sys, sys, sizeof(activeSats[0].sys));
      activeSats[activeSatCount].elev = elev;
      activeSats[activeSatCount].azim = azim;
      activeSats[activeSatCount].snr = snr;
      activeSats[activeSatCount].sig = sig; 
      activeSats[activeSatCount].lastSeen = now;
      activeSatCount++;
    }
    xSemaphoreGive(dataMutex);
  }
}

void cleanOldSatellites() {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    int newCount = 0;
    for (int i = 0; i < activeSatCount; i++) {
      if (now - activeSats[i].lastSeen <= 5000) {
        activeSats[newCount++] = activeSats[i];
      }
    }
    activeSatCount = newCount;
    xSemaphoreGive(dataMutex);
  }
}

// ==========================================
// 3. AĞ VE TERMİNAL OLAYLARI
// ==========================================
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
      Serial.print("[WS] Modüle Giden Komut: ");
      Serial.println(cmd);
    }
  }
}

// ==========================================
// 4. GÖMÜLÜ WEB SAYFASI
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 - RTK Telemetry</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <style>
        body { background-color: #121212; color: #00ffcc; font-family: 'Courier New', Courier, monospace; margin: 0; padding: 10px; }
        h2 { text-align: center; color: #fff; margin-bottom: 5px; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        @media (max-width: 768px) { .grid { grid-template-columns: 1fr; } }
        .card { background: #1e1e1e; padding: 15px; border-radius: 8px; border: 1px solid #333; }
        .card h3 { margin-top: 0; color: #aaa; font-size: 14px; border-bottom: 1px solid #444; padding-bottom: 5px;}
        .value { font-size: 18px; color: #fff; font-weight: bold; }
        .alert { color: #ff3333; }
        .good { color: #33ff33; }
        .sat-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px;}
        .sat-box { background: #2a2a2a; padding: 6px; border-radius: 6px; display: flex; flex-direction: column; border: 1px solid #333;}
        .sat-box-title { font-size: 12px; color: #aaa; border-bottom: 1px solid #444; padding-bottom: 3px; margin-bottom: 4px; font-weight: bold; text-align: center;}
        .sat-sig-row { display: flex; justify-content: space-between; font-size: 11px; color: #888; padding: 2px 0;}
        .sat-sig-row span.val { color: #00ffcc; font-weight: bold; }
        #map { height: 250px; border-radius: 8px; margin-top: 10px; z-index: 1;}
        canvas { background: #1a1a1a; border-radius: 50%; display: block; margin: 0 auto; border: 2px solid #333;}
        #terminal { height: 110px; overflow-y: auto; background: #000; color: #00ffcc; padding: 8px; border: 1px solid #444; border-radius: 4px; font-size: 12px; margin-top: 5px; font-family: monospace; }
        .term-line { border-bottom: 1px dashed #222; padding-bottom: 3px; margin-bottom: 3px; word-wrap: break-word;}
    </style>
</head>
<body>
    <h2>📡 ESP32 - RTK BASE</h2>
    <div class="grid">
        <div class="card">
            <h3>🌐 SİSTEM & KONUM DURUMU</h3>
            <div>Enlem: <span id="lat" class="value">Bekleniyor...</span></div>
            <div>Boylam: <span id="lon" class="value">Bekleniyor...</span></div>
            <div>İrtifa: <span id="alt" class="value">0.0</span> m</div>
            <div>HDOP: <span id="hdop" class="value">0.0</span></div>
            <hr style="border: 0; border-top: 1px solid #444; margin: 10px 0;">
            <div>PPS Durumu: <span id="pps_status" class="value alert">KİLİT YOK</span></div>
            <div>Uydu Saati (UTC): <span id="sat_time" class="value" style="color:#aaffaa">--:--:--</span></div>
            <div>RTCM3 Akışı: <span id="rtcm" class="value">0</span> pkt/sn</div>
            <div>TCP Yayın (Port 2101): <span id="tcp_clients" class="value" style="color:#00ffcc">0</span> İstemci</div>
            <div id="map"></div>
            <h3 style="margin-top: 15px;">⌨️ SERİ PORT TERMİNALİ</h3>
            <div style="display: flex; gap: 5px;">
                <input type="text" id="cmdInput" placeholder="$PQTMCFGSVIN..." style="flex: 1; padding: 5px; border-radius: 4px; border: 1px solid #444; background: #000; color: #00ffcc; font-family: monospace;">
                <button onclick="sendCommand()" style="padding: 5px 15px; background: #00ffcc; color: #000; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;">GÖNDER</button>
            </div>
            <div id="terminal"></div>
            <p style="font-size: 10px; color: #888; margin-top: 5px; margin-bottom: 0;">* '$' ile başlayan komutlara otomatik Checksum eklenir.</p>
        </div>

        <div class="card">
            <h3>🛰️ AKTİF SİNYAL DAĞILIMI (Toplam: <span id="total_sats">0</span> Sinyal)</h3>
            <div class="sat-grid">
                <div class="sat-box">
                    <div class="sat-box-title">GPS</div>
                    <div class="sat-sig-row"><span>L1 C/A:</span><span class="val" id="gps-l1">0</span></div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="gps-l5">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">GLONASS</div>
                    <div class="sat-sig-row"><span>L1/G1:</span><span class="val" id="glo-l1">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">GALILEO</div>
                    <div class="sat-sig-row"><span>E1:</span><span class="val" id="gal-e1">0</span></div>
                    <div class="sat-sig-row"><span>E5a:</span><span class="val" id="gal-e5a">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">BDS</div>
                    <div class="sat-sig-row"><span>B1I:</span><span class="val" id="bei-b1">0</span></div>
                    <div class="sat-sig-row"><span>B2a:</span><span class="val" id="bei-b2a">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">QZSS</div>
                    <div class="sat-sig-row"><span>L1 C/A:</span><span class="val" id="qzs-l1">0</span></div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="qzs-l5">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">NAVIC</div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="nav-l5">0</span></div>
                </div>
                <!-- SBAS KUTUSU -->
                <div class="sat-box" style="border-color: #4169E1;">
                    <div class="sat-box-title" style="color: #66aaff;">SBAS</div>
                    <div class="sat-sig-row"><span>L1:</span><span class="val" id="sba-l1" style="color:#ffffff;">0</span></div>
                </div>
            </div>
            <h3 style="margin-top: 15px;">🌌 SKYVIEW (Canlı Radar)</h3>
            <canvas id="skyview" width="500" height="500"></canvas>
        </div>
    </div>

    <script>
        var map = L.map('map').setView([41.0, 28.9], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
        var marker = L.marker([41.0, 28.9]).addTo(map);
        var firstLock = false;

        var flags = {};
        var flagUrls = {
            "GP": "https://flagcdn.com/w40/us.png", "GL": "https://flagcdn.com/w40/ru.png", 
            "GA": "https://flagcdn.com/w40/eu.png", "GB": "https://flagcdn.com/w40/cn.png", 
            "GI": "https://flagcdn.com/w40/in.png", "GQ": "https://flagcdn.com/w40/jp.png",
            "SB": "https://flagcdn.com/w40/eu.png" // SBAS için Avrupa Birliği (EGNOS) Bayrağı
        };
        for(let key in flagUrls){ let img = new Image(); img.src = flagUrls[key]; flags[key] = img; }

        function drawSkyview(skyData) {
            var canvas = document.getElementById("skyview");
            var ctx = canvas.getContext("2d");
            var cx = canvas.width / 2; var cy = canvas.height / 2; var r = cx - 25; 
            
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = "#444"; ctx.lineWidth = 1;
            [r, r*0.66, r*0.33].forEach(rad => { ctx.beginPath(); ctx.arc(cx, cy, rad, 0, 2*Math.PI); ctx.stroke(); });
            ctx.beginPath(); ctx.moveTo(cx, cy-r); ctx.lineTo(cx, cy+r); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(cx-r, cy); ctx.lineTo(cx+r, cy); ctx.stroke();

            if(!skyData) return;

            skyData.forEach(sat => {
                var satR = r * (1 - (sat.e / 90.0));
                var rad = (sat.a - 90) * Math.PI / 180.0;
                var x = cx + satR * Math.cos(rad);
                var y = cy + satR * Math.sin(rad);

                var balonYaricap = 10; 
                
                var glowColor = sat.s === "SB" ? "rgba(65, 105, 225, 0.9)" : 
                                sat.sn > 35 ? "rgba(0, 255, 0, 0.7)" : 
                                sat.sn > 25 ? "rgba(255, 255, 0, 0.7)" : "rgba(255, 0, 0, 0.7)"; 

                ctx.save(); ctx.shadowBlur = (sat.sn / 2) + 5; ctx.shadowColor = glowColor; ctx.fillStyle = glowColor;
                ctx.beginPath(); ctx.arc(x, y, balonYaricap + 2, 0, 2*Math.PI); ctx.fill(); ctx.restore(); 

                ctx.save(); ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.clip(); 
                if(flags[sat.s] && flags[sat.s].complete) {
                    ctx.drawImage(flags[sat.s], x - balonYaricap, y - balonYaricap, balonYaricap*2, balonYaricap*2);
                } else { ctx.fillStyle = "#888"; ctx.fill(); }
                ctx.restore(); 

                ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5; ctx.stroke();

                var sysPrefix = sat.s === "GP" ? "G" : sat.s === "GL" ? "R" : sat.s === "GA" ? "E" : sat.s === "GB" ? "B" : sat.s === "GI" ? "I" : sat.s === "GQ" ? "Q" : sat.s === "SB" ? "S" : "U";
                ctx.fillStyle = "#eee"; ctx.font = "bold 11px Arial"; ctx.textAlign = "left";
                ctx.fillText(sysPrefix + sat.id, x + balonYaricap + 4, y + 4);
            });
        }
        drawSkyview();

        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onopen = function(event) { 
                logTerminal("<span style='color:#33ff33;'>[SİSTEM] ESP32 Bağlantısı Kuruldu.</span>"); 
                
                setTimeout(function() {
                    let versiyonKomutu = "$PQTMVERNO*58";
                    websocket.send(versiyonKomutu); 
                    logTerminal("<span style='color:#fff; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + versiyonKomutu + "</span>");
                }, 1000);
            };
            websocket.onclose = function(event) { logTerminal("<span style='color:#ffaa00;'>[SİSTEM] Bağlantı Koptu! Yeniden bağlanılıyor...</span>"); setTimeout(initWebSocket, 2000); };
            websocket.onmessage = onMessage;
        }

        function sendCommand() {
            let cmdInput = document.getElementById('cmdInput');
            let cmd = cmdInput.value.trim();
            if (cmd) {
                let finalCmd = cmd;
                if (cmd.startsWith('$') && !cmd.includes('*')) {
                    let checksum = 0;
                    for (let i = 1; i < cmd.length; i++) checksum ^= cmd.charCodeAt(i);
                    let hexCS = checksum.toString(16).toUpperCase().padStart(2, '0');
                    finalCmd = cmd + '*' + hexCS;
                }
                fetch('/cmd?c=' + encodeURIComponent(finalCmd))
                .then(response => { if(response.ok) logTerminal("<span style='color:#33ff33; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + finalCmd + "</span>"); })
                .catch(error => { logTerminal("<span style='color:#ff3333;'>HATA: Modüle ulaşılamadı!</span>"); });
                cmdInput.value = ""; 
            }
        }
        document.getElementById("cmdInput").addEventListener("keyup", function(event) { if (event.key === "Enter") sendCommand(); });

        function logTerminal(msg) {
            var term = document.getElementById('terminal');
            var timeStr = new Date().toLocaleTimeString('tr-TR', { hour12: false });
            term.innerHTML += "<div class='term-line'><span style='color:#888; font-size:10px;'>[" + timeStr + "]</span> " + msg + "</div>";
            term.scrollTop = term.scrollHeight; 
        }

        function onMessage(event) {
            if (typeof event.data === "string" && event.data.startsWith("TERM:")) {
                let msg = event.data.substring(5); 
                logTerminal("<span style='color:#ff3333; font-weight:bold;'>RX:</span> <span style='color:#fff;'>" + msg + "</span>");
                return; 
            }
            var data;
            try { data = JSON.parse(event.data); } catch(e) { return; }

            if (data.lat !== undefined) {
                document.getElementById('lat').innerText = data.lat.toFixed(6); 
                document.getElementById('lon').innerText = data.lon.toFixed(6);
                document.getElementById('alt').innerText = data.alt.toFixed(2); 
                document.getElementById('hdop').innerText = data.hdop.toFixed(2);
                document.getElementById('rtcm').innerText = data.rtcm; 
                document.getElementById('tcp_clients').innerText = data.tcp_clients;
                if(data.sat_time) document.getElementById('sat_time').innerText = data.sat_time;
                
                var ppsEl = document.getElementById('pps_status');
                if(data.pps_active) {
                    ppsEl.innerText = "AKTİF (KİLİTLİ)"; ppsEl.className = "value good";
                } else {
                    ppsEl.innerText = "BEKLENİYOR..."; ppsEl.className = "value alert";
                }

                let t = data.sats;
                document.getElementById('gps-l1').innerText = t.gps.L1;
                document.getElementById('gps-l5').innerText = t.gps.L5;
                document.getElementById('glo-l1').innerText = t.glo.L1;
                document.getElementById('gal-e1').innerText = t.gal.E1;
                document.getElementById('gal-e5a').innerText = t.gal.E5a;
                document.getElementById('bei-b1').innerText = t.bei.B1;
                document.getElementById('bei-b2a').innerText = t.bei.B2a;
                document.getElementById('qzs-l1').innerText = t.qzs.L1;
                document.getElementById('qzs-l5').innerText = t.qzs.L5;
                document.getElementById('nav-l5').innerText = t.nav.L5;
                
                document.getElementById('sba-l1').innerText = t.sba ? t.sba.L1 : 0;
                
                document.getElementById('total_sats').innerText = t.gps.L1 + t.gps.L5 + t.glo.L1 + t.gal.E1 + t.gal.E5a + t.bei.B1 + t.bei.B2a + t.qzs.L1 + t.qzs.L5 + t.nav.L5 + (t.sba ? t.sba.L1 : 0);
              
                if(data.lat !== 0.0 && data.lon !== 0.0) {
                    var newLatLng = new L.LatLng(data.lat, data.lon); marker.setLatLng(newLatLng);
                    if(!firstLock) { map.setView(newLatLng, 18); firstLock = true; }            
                }
                if(data.sky) drawSkyview(data.sky);
            }
        }
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. GÜVENLİ NMEA AYRIŞTIRICI
// ==========================================
bool isChecksumValid(const char* sentence) {
  int len = strlen(sentence);
  if (len < 8) return false; 
  
  int starIndex = -1;
  for (int i = 0; i < len; i++) {
    if (sentence[i] == '*') {
      starIndex = i;
      break;
    }
  }
  
  if (starIndex == -1 || starIndex > len - 3) return false;

  char c1 = sentence[starIndex + 1];
  char c2 = sentence[starIndex + 2];
  if (!isxdigit(c1) || !isxdigit(c2)) return false; 
  
  uint8_t calculatedCS = 0;
  for (int i = 1; i < starIndex; i++) {
    calculatedCS ^= sentence[i];
  }
  
  char hexStr[3] = {c1, c2, '\0'};
  uint8_t providedCS = (uint8_t)strtol(hexStr, NULL, 16);
  
  return (calculatedCS == providedCS); 
}

void uyduTipleriniAyristir(const char* nmea) {
  if (strstr(nmea, "GSV") != NULL) {
    const char* sys = "UN";
    if (strncmp(nmea, "$GP", 3) == 0) sys = "GP";
    else if (strncmp(nmea, "$GL", 3) == 0) sys = "GL";
    else if (strncmp(nmea, "$GA", 3) == 0) sys = "GA";
    else if (strncmp(nmea, "$GB", 3) == 0 || strncmp(nmea, "$BD", 3) == 0) sys = "GB";
    else if (strncmp(nmea, "$GI", 3) == 0) sys = "GI";
    else if (strncmp(nmea, "$GQ", 3) == 0) sys = "GQ";
    else if (strncmp(nmea, "$SB", 3) == 0) sys = "SB"; 

    int commas[25]; 
    int cCount = 0;
    int len = strlen(nmea);
    int starIdx = -1;
    
    for (int i = 0; i < len; i++) {
      if (nmea[i] == ',') {
        if (cCount < 25) commas[cCount++] = i;
      } else if (nmea[i] == '*') {
        starIdx = i;
      }
    }

    if (starIdx < 0) return;

    bool hasSignalId = false;
    int sig_id = 1; 
    
    if (cCount >= 4) {
      int lastFieldLen = starIdx - commas[cCount - 1] - 1;
      if ((cCount - 3) % 4 == 1 && lastFieldLen > 0 && lastFieldLen <= 2) { 
        char sigStr[4] = {0};
        strncpy(sigStr, nmea + commas[cCount - 1] + 1, lastFieldLen);
        
        bool isHex = true;
        for(int k = 0; k < lastFieldLen; k++) {
            if(!isxdigit(sigStr[k])) isHex = false;
        }
        
        if (isHex) {
          hasSignalId = true;
          sig_id = strtol(sigStr, NULL, 16);
        }
      }
    }

    for (int i = 4; i < cCount; i += 4) {
      if (i + 2 < cCount) {
        int id = atoi(nmea + commas[i-1] + 1);
        int elev = atoi(nmea + commas[i] + 1);
        int azim = atoi(nmea + commas[i+1] + 1);
        int snr = 0;
        
        if (i + 3 < cCount) {
          snr = atoi(nmea + commas[i+2] + 1);
        } else if (!hasSignalId) {
          char snrStr[8] = {0};
          int snrLen = starIdx - commas[i+2] - 1;
          if(snrLen > 0 && snrLen < 8) {
              strncpy(snrStr, nmea + commas[i+2] + 1, snrLen);
              snr = atoi(snrStr);
          }
        }
        
        if (id > 0) {
          // --- EGNOS / SBAS / QZSS İÇİN NOKTA ATIŞI FİLTRELEME VE 87 OFFSET GERİ YÜKLEME ---
          const char* finalSys = sys;

          if (strcmp(sys, "GP") == 0 || strcmp(sys, "UN") == 0 || strcmp(sys, "SB") == 0) {
              
              if (id == 121 || id == 123 || id == 126 || id == 136 || // EGNOS
                  id == 131 || id == 133 || id == 135 ||              // WAAS
                  id == 127 || id == 128 || id == 157 ||              // GAGAN
                  id == 129 || id == 137 ||                           // MSAS
                  id == 134 || id == 149 ||                           // KASS
                  id == 130 || id == 143) {                           // BDSBAS
                  finalSys = "SB"; 
              } 
              // --- EĞER MODÜL 87 ÇIKARARAK (33-64) GÖNDERDİYSE: ---
              else if (id >= 33 && id <= 64) {                           
                  finalSys = "SB"; 
                  id += 87; // GERÇEK PRN NUMARASINI (120+) RESTORE ET!
              } 
              else if (id == 183 || id == 193 || (id >= 193 && id <= 200)) {
                  finalSys = "GQ"; 
              }
          }
          
          addSat(finalSys, id, elev, azim, snr, sig_id); 
        }
      }
    }
  }
}

// ==========================================
// CORE 0: AĞ, TELEMETRİ VE WATCHDOG GÖREVİ
// ==========================================
void networkTaskCode(void * parameter) {
  static unsigned long sonJsonZamani = 0;
  static unsigned long sonWifiKontrol = 0;

  static SatData localSats[MAX_SATS];
  static char jsonBuffer[6144]; 

  for(;;) {
    uint32_t now = millis();

    if (now - sonWifiKontrol >= 10000) {
      sonWifiKontrol = now;
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Baglanti Koptu! Yeniden baglaniliyor...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
      }
    }

    if (ws.count() > 0) {
      ws.cleanupClients();
    }

    char queuedMsg[160];
    while (xQueueReceive(termQueue, queuedMsg, 0) == pdTRUE) {
      if (ws.count() > 0) {
        ws.textAll(queuedMsg);
      }
    }

    if (now - sonJsonZamani >= 1000) {
      sonJsonZamani = now;
      cleanOldSatellites(); 

      if (ws.count() > 0) {
        int localSatCount = 0;
        
        double locLat = 0.0, locLon = 0.0, locAlt = 0.0, locHdop = 0.0;
        bool locValidLoc = false, locValidAlt = false, locValidHdop = false, locTimeValid = false;
        char locTime[12] = "--:--:--";
        uint32_t currentRtcmCount = 0;
        uint32_t lastPps = 0;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
          localSatCount = activeSatCount;
          memcpy(localSats, (const void*)activeSats, localSatCount * sizeof(SatData));
          
          locLat = safeGps.lat; locLon = safeGps.lon; 
          locAlt = safeGps.alt; locHdop = safeGps.hdop;
          locValidLoc = safeGps.validLoc; 
          locValidAlt = safeGps.validAlt;
          locValidHdop = safeGps.validHdop; 
          locTimeValid = safeGps.validTime;

          if (locTimeValid) sprintf(locTime, "%02d:%02d:%02d", safeGps.hour, safeGps.min, safeGps.sec);
          
          currentRtcmCount = rtcmPaketSayaci;
          rtcmPaketSayaci = 0; 

          lastPps = sonPpsZamaniMicros;
          
          xSemaphoreGive(dataMutex);
        }

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

        #if ARDUINOJSON_VERSION_MAJOR >= 7
          JsonDocument doc; 
        #else
          DynamicJsonDocument doc(6144); 
        #endif
        
        doc["lat"] = locValidLoc ? locLat : 0.0;
        doc["lon"] = locValidLoc ? locLon : 0.0;
        doc["alt"] = locValidAlt ? locAlt : 0.0;
        doc["hdop"] = locValidHdop ? locHdop : 0.0;
        doc["tcp_clients"] = activeTcp;
        doc["rtcm"] = currentRtcmCount;
        doc["sat_time"] = locTime;
        doc["pps_active"] = (micros() - lastPps < 2000000) ? true : false; 

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
          
          if(s.elev > 0 || s.azim > 0 || s.snr > 0) {
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

        serializeJson(doc, jsonBuffer);
        ws.textAll(jsonBuffer);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ==========================================
// 6. KURULUM (SETUP)
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  dataMutex = xSemaphoreCreateMutex();
  tcpMutex = xSemaphoreCreateMutex();
  termQueue = xQueueCreate(15, sizeof(char) * 160);

  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);

  Serial.println("\n=== WI-FI BAGLANTISI BEKLENIYOR ===");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nBAGLANDI! Tarayicinizdan su adrese gidin:");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
   request->send_P(200, "text/html", index_html);
  });

  server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("c")){
      const char* komut = request->getParam("c")->value().c_str();
      Serial2.print(komut); 
      Serial2.print("\r\n"); 
      request->send(200, "text/plain", "OK"); 
    } else {
      request->send(400, "text/plain", "BOS");
    }
  });

  ws.onEvent(onWsEvent); 
  server.addHandler(&ws);
  rtcmServer.begin();
  server.begin();

  Serial2.println("$PAIR062,0,1*3F"); delay(100);
  Serial2.println("$PAIR062,1,0*3E"); delay(100); 
  Serial2.println("$PAIR062,2,0*3D"); delay(100); 
  Serial2.println("$PAIR062,3,1*3C"); delay(100); 
  Serial2.println("$PAIR062,4,0*3B"); delay(100); 
  Serial2.println("$PAIR062,5,0*3A"); delay(100); 
  Serial2.println("$PAIR062,6,1*39"); delay(100); 
  Serial2.println("$PAIR062,7,1*38"); delay(100); 
  Serial2.println("$PAIR062,8,1*37"); delay(100); 
  Serial2.println("$PQTMCFGSVIN,W,1,300,2,0,0,0*20"); delay(100); 
  Serial2.println("$PAIR411,1*23"); delay(100); 
  Serial2.println("$PAIR432,1*22"); delay(100); 
  Serial2.println("$PAIR434,1*24"); delay(100); 
  Serial2.println("$PAIR436,1*26"); delay(100); 
  
  Serial2.println("$PQTMSAVEPAR*5A"); delay(200);

  xTaskCreatePinnedToCore(networkTaskCode, "NetworkTask", 16384, NULL, 1, &NetworkTaskHandle, 0);
}

// ==========================================
// 7. CORE 1: YÜKSEK HIZLI GNSS VERİ İŞLEME
// ==========================================
void loop() {
  if (rtcmServer.hasClient()) {
    WiFiClient newClient = rtcmServer.available();
    if (newClient) {
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
          size_t written = tcpClients[i].write(buf, len);
          if (written < len) {
              tcpClients[i].stop();
          }
        }
      }
      xSemaphoreGive(tcpMutex);
    }

    static enum { WAIT_SYNC, WAIT_LEN1, WAIT_LEN2, SKIP_PAYLOAD } rtcmState = WAIT_SYNC;
    static uint16_t rtcmLen = 0;
    static uint16_t rtcmBytesRead = 0;
    static uint32_t lastRtcmTime = 0;

    if (rtcmState != WAIT_SYNC && (millis() - lastRtcmTime > 50)) {
        rtcmState = WAIT_SYNC;
    }
    lastRtcmTime = millis();

    for (size_t i = 0; i < len; i++) {
      uint8_t b = buf[i];
      gps.encode(b); 
      
      if (rtcmState == WAIT_SYNC && b == 0xD3) {
        rtcmState = WAIT_LEN1;
      } else if (rtcmState == WAIT_LEN1) {
        rtcmLen = (b & 0x03) << 8;
        rtcmState = WAIT_LEN2;
      } else if (rtcmState == WAIT_LEN2) {
        rtcmLen |= b;
        rtcmBytesRead = 0;
        rtcmState = SKIP_PAYLOAD;
      } else if (rtcmState == SKIP_PAYLOAD) {
        rtcmBytesRead++;
        if (rtcmBytesRead >= rtcmLen + 3) { 
          if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            rtcmPaketSayaci++;
            xSemaphoreGive(dataMutex);
          }
          rtcmState = WAIT_SYNC;
        }
      }
      
      char c = (char)b;
      
      if (c == '$') {
        nmeaIdx = 0;
        nmeaBuff[nmeaIdx++] = c;
      } else if (c == '\n') {
        if (nmeaIdx > 0 && nmeaBuff[0] == '$') {
          if (nmeaBuff[nmeaIdx - 1] == '\r') nmeaBuff[nmeaIdx - 1] = '\0';
          else nmeaBuff[nmeaIdx] = '\0'; 
          
          if (isChecksumValid(nmeaBuff)) {
            uyduTipleriniAyristir(nmeaBuff); 
            
            if (strncmp(nmeaBuff, "$GN", 3) != 0 && strncmp(nmeaBuff, "$GP", 3) != 0 && 
                strncmp(nmeaBuff, "$GL", 3) != 0 && strncmp(nmeaBuff, "$GA", 3) != 0 && 
                strncmp(nmeaBuff, "$GB", 3) != 0 && strncmp(nmeaBuff, "$GQ", 3) != 0 && 
                strncmp(nmeaBuff, "$GI", 3) != 0 && strncmp(nmeaBuff, "$BD", 3) != 0 &&
                strncmp(nmeaBuff, "$SB", 3) != 0) {
              
              char termMsg[160];
              snprintf(termMsg, sizeof(termMsg), "TERM:%s", nmeaBuff);
              xQueueSend(termQueue, termMsg, 0); 
            }
          }
        }
        nmeaIdx = 0; 
      } else if (c >= 32 && c <= 126 && nmeaIdx > 0) {
        if (nmeaIdx < MAX_NMEA - 1) {
            nmeaBuff[nmeaIdx++] = c;
        } else {
            nmeaIdx = 0; 
        }
      }
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      safeGps.lat = gps.location.lat(); safeGps.lon = gps.location.lng();
      safeGps.alt = gps.altitude.meters(); safeGps.hdop = gps.hdop.hdop();
      safeGps.validLoc = gps.location.isValid(); safeGps.validAlt = gps.altitude.isValid();
      safeGps.validHdop = gps.hdop.isValid(); safeGps.validTime = gps.time.isValid();
      safeGps.hour = gps.time.hour(); safeGps.min = gps.time.minute(); safeGps.sec = gps.time.second();
      xSemaphoreGive(dataMutex);
    }
  }
}