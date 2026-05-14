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
// TCP RTCM Yayın Sunucusu
#define RTCM_PORT 2101
WiFiServer rtcmServer(RTCM_PORT);
WiFiClient tcpClients[3]; 

// --- BELLEK DOSTU UYDU VERİ YAPISI (std::set YERİNE) ---
struct SatData {
  int id;
  char sys[3]; 
  int elev;   
  int azim;   
  int snr;    
};

#define MAX_SATS 100
SatData activeSats[MAX_SATS];
int activeSatCount = 0;

// Yeni uyduyu listeye ekleyen veya güncelleyen fonksiyon
void addSat(const char* sys, int id, int elev, int azim, int snr) {
  for (int i = 0; i < activeSatCount; i++) {
    if (strcmp(activeSats[i].sys, sys) == 0 && activeSats[i].id == id) {
      activeSats[i].elev = elev; activeSats[i].azim = azim; activeSats[i].snr = snr;
      return;
    }
  }
  if (activeSatCount < MAX_SATS) {
    activeSats[activeSatCount].id = id;
    strcpy(activeSats[activeSatCount].sys, sys);
    activeSats[activeSatCount].elev = elev;
    activeSats[activeSatCount].azim = azim;
    activeSats[activeSatCount].snr = snr;
    activeSatCount++;
  }
}

// Sayaçlar ve Bellek Dostu Tamponlar (String yerine Char Array)
volatile uint32_t rtcmPaketSayaci = 0;
volatile uint32_t ppsSayaci = 0;
volatile uint32_t sonPpsZamaniMicros = 0; 
uint32_t sonGuncelleme = 0;

char nmeaBuff[128];
int nmeaIdx = 0;

// --- PPS KESME FONKSİYONU ---
void IRAM_ATTR ppsKesmesi() {
  ppsSayaci++;
  sonPpsZamaniMicros = micros(); 
}

// --- WEBSOCKET GELEN MESAJ YAKALAYICI ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      data[len] = 0; 
      String command = (char*)data;
      Serial2.print(command + "\r\n");
      Serial.print("[WS] Modüle Giden Komut: ");
      Serial.println(command);
    }
  }
}

// ==========================================
// 3. GÖMÜLÜ WEB SAYFASI (HTML + JS + CSS)
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
        .sat-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 5px; text-align: center; margin-top: 10px;}
        .sat-box { background: #2a2a2a; padding: 5px; border-radius: 5px; }
        .sat-box span { display: block; font-size: 10px; color: #888; }
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
            <div>PPS Sayacı: <span id="pps_count" class="value">0</span></div>
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
            <h3>🛰️ UYDU DAĞILIMI (Fiziksel Toplam: <span id="total_sats">0</span>)</h3>
            <div class="sat-grid">
                <div class="sat-box"><span>GPS</span><div id="s-gps" class="value">0</div></div>
                <div class="sat-box"><span>GLO</span><div id="s-glo" class="value">0</div></div>
                <div class="sat-box"><span>GAL</span><div id="s-gal" class="value">0</div></div>
                <div class="sat-box"><span>BDS</span><div id="s-bei" class="value">0</div></div>
                <div class="sat-box"><span>NAV</span><div id="s-nav" class="value">0</div></div>
                <div class="sat-box"><span>QZS</span><div id="s-qzs" class="value">0</div></div>
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
            "GI": "https://flagcdn.com/w40/in.png", "GQ": "https://flagcdn.com/w40/jp.png"  
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
                var glowColor = sat.sn > 35 ? "rgba(0, 255, 0, 0.7)" : sat.sn > 25 ? "rgba(255, 255, 0, 0.7)" : "rgba(255, 0, 0, 0.7)"; 

                ctx.save(); ctx.shadowBlur = (sat.sn / 2) + 5; ctx.shadowColor = glowColor; ctx.fillStyle = glowColor;
                ctx.beginPath(); ctx.arc(x, y, balonYaricap + 2, 0, 2*Math.PI); ctx.fill(); ctx.restore(); 

                ctx.save(); ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.clip(); 
                if(flags[sat.s] && flags[sat.s].complete) {
                    ctx.drawImage(flags[sat.s], x - balonYaricap, y - balonYaricap, balonYaricap*2, balonYaricap*2);
                } else { ctx.fillStyle = "#888"; ctx.fill(); }
                ctx.restore(); 

                ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5; ctx.stroke();

                var sysPrefix = sat.s === "GP" ? "G" : sat.s === "GL" ? "R" : sat.s === "GA" ? "E" : sat.s === "GB" ? "B" : sat.s === "GI" ? "I" : sat.s === "GQ" ? "Q" : "U";
                ctx.fillStyle = "#eee"; ctx.font = "bold 11px Arial"; ctx.textAlign = "left";
                ctx.fillText(sysPrefix + sat.id, x + balonYaricap + 4, y + 4);
            });
        }
        drawSkyview();

        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onopen = function(event) { logTerminal("<span style='color:#33ff33;'>[SİSTEM] ESP32 Bağlantısı Kuruldu.</span>"); };
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
                .then(response => { if(response.ok) logTerminal("<span style='color:#fff; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + finalCmd + "</span>"); })
                .catch(error => { logTerminal("<span style='color:#ff3333;'>HATA: Modüle ulaşılamadı!</span>"); });
                cmdInput.value = ""; 
            }
        }
        document.getElementById("cmdInput").addEventListener("keyup", function(event) { if (event.key === "Enter") sendCommand(); });

        function logTerminal(msg) {
            var term = document.getElementById('terminal');
            term.innerHTML += "<div class='term-line'>" + msg + "</div>";
            term.scrollTop = term.scrollHeight; 
        }

        function onMessage(event) {
            if (typeof event.data === "string" && event.data.startsWith("TERM:")) {
                let msg = event.data.substring(5); 
                logTerminal("<span style='color:#fff; font-weight:bold;'>RX:</span> <span style='color:#FF0000;'>" + msg + "</span>");
                return; 
            }
            var data;
            try { data = JSON.parse(event.data); } catch(e) { return; }

            if (data.lat !== undefined) {
                document.getElementById('lat').innerText = data.lat.toFixed(6); document.getElementById('lon').innerText = data.lon.toFixed(6);
                document.getElementById('alt').innerText = data.alt.toFixed(2); document.getElementById('hdop').innerText = data.hdop.toFixed(2);
                document.getElementById('rtcm').innerText = data.rtcm; document.getElementById('tcp_clients').innerText = data.tcp_clients;
                document.getElementById('pps_count').innerText = data.pps_count;
                
                var ppsEl = document.getElementById('pps_status');
                if(data.pps_active) {
                    ppsEl.innerText = "AKTİF (KİLİTLİ)"; ppsEl.className = "value good";
                } else {
                    ppsEl.innerText = "BEKLENİYOR..."; ppsEl.className = "value alert";
                }

                document.getElementById('s-gps').innerText = data.sats.gps; document.getElementById('s-glo').innerText = data.sats.glo;
                document.getElementById('s-gal').innerText = data.sats.gal; document.getElementById('s-bei').innerText = data.sats.bei;
                document.getElementById('s-nav').innerText = data.sats.nav; document.getElementById('s-qzs').innerText = data.sats.qzs;
                document.getElementById('total_sats').innerText = data.sats.gps + data.sats.glo + data.sats.gal + data.sats.bei + data.sats.nav + data.sats.qzs;
              
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
// 4. NMEA AYRIŞTIRICI & GÜVENLİK
// ==========================================

// --- GÜVENLİK DUVARI: NMEA CHECKSUM DOĞRULAYICI (KURŞUN GEÇİRMEZ) ---
bool isChecksumValid(const char* sentence) {
  int len = strlen(sentence);
  // Gerçek bir NMEA komutu bu kadar kısa olamaz (Örn: en az $GPGGA*XX)
  if (len < 8) return false; 
  
  // Yıldız (*) işaretinin yerini bul
  int starIndex = -1;
  for (int i = 0; i < len; i++) {
    if (sentence[i] == '*') {
      starIndex = i;
      break;
    }
  }
  
  // Yıldız yoksa veya yıldızdan sonra en az 2 karakter yoksa ÇÖP!
  if (starIndex == -1 || starIndex > len - 3) return false;

  // =======================================================
  // KATI KONTROL: Yıldızdan sonraki 2 karakter HEX (0-9, A-F) mi?
  // =======================================================
  char c1 = sentence[starIndex + 1];
  char c2 = sentence[starIndex + 2];
  if (!isxdigit(c1) || !isxdigit(c2)) return false; // Hex değilse anında reddet!
  
  // Sadece A-Z, a-z, 0-9 ve standart noktalama işaretleri içerebilir.
  // RTCM içindeki garip ASCII sembollerini ( }, { vb.) engeller.
  for (int i = 1; i < starIndex; i++) {
    if (sentence[i] < 32 || sentence[i] > 126 || sentence[i] == '{' || sentence[i] == '}' || sentence[i] == '`') {
        return false;
    }
  }

  // Her şey mükemmelse XOR hesabını yap
  uint8_t calculatedCS = 0;
  for (int i = 1; i < starIndex; i++) {
    calculatedCS ^= sentence[i];
  }
  
  char hexStr[3] = {c1, c2, '\0'};
  uint8_t providedCS = (uint8_t)strtol(hexStr, NULL, 16);
  
  return (calculatedCS == providedCS); 
}

void uyduTipleriniAyristir(String nmea) {
  if (nmea.indexOf("GSV") != -1) {
    const char* sys = "UN";
    if (nmea.startsWith("$GP")) sys = "GP";
    else if (nmea.startsWith("$GL")) sys = "GL";
    else if (nmea.startsWith("$GA")) sys = "GA";
    else if (nmea.startsWith("$GB") || nmea.startsWith("$BD")) sys = "GB";
    else if (nmea.startsWith("$GI")) sys = "GI";
    else if (nmea.startsWith("$GQ")) sys = "GQ";

    int commaIndex[20];
    int cCount = 0;
    for (int i = 0; i < nmea.length(); i++) {
      if (nmea[i] == ',') {
        commaIndex[cCount++] = i;
        if (cCount >= 20) break;
      }
    }

    for (int i = 4; i < cCount; i += 4) {
      if (i + 3 < cCount) { 
        int id = nmea.substring(commaIndex[i-1] + 1, commaIndex[i]).toInt();
        int elev = nmea.substring(commaIndex[i] + 1, commaIndex[i+1]).toInt();
        int azim = nmea.substring(commaIndex[i+1] + 1, commaIndex[i+2]).toInt();
        int snr = nmea.substring(commaIndex[i+2] + 1, commaIndex[i+3]).toInt(); 
        
        if (id > 0) {
          addSat(sys, id, elev, azim, snr); 
        }
      }
    }
  }
}

// ==========================================
// 5. KURULUM (SETUP)
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

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
      String komut = request->getParam("c")->value();
      Serial2.print(komut + "\r\n"); 
      Serial.println("[HTTP] Modüle Giden: " + komut);
      request->send(200, "text/plain", "OK"); 
    } else {
      request->send(400, "text/plain", "BOS");
    }
  });

  ws.onEvent(onWsEvent); 
  server.addHandler(&ws);
  rtcmServer.begin();
  server.begin();

  Serial2.println("$PAIR062,0,1*3F"); 
  delay(100);
  Serial2.println("$PAIR062,3,1*3C"); 
  delay(2000);
  Serial2.println("$PQTMCFGSVIN,W,1,300,1,0,0,0*20");
  delay(200);
  Serial2.println("$PAIR432,1*22"); 
  delay(200);
  Serial2.println("$PAIR436,1*26");
}

// ==========================================
// 6. ANA DÖNGÜ (LOOP)
// ==========================================
void loop() {
  ws.cleanupClients(); 
  
  // --- TCP İstemci Bağlantı Kontrolü (While ile Çoklu Bağlantı Desteği) ---
  while (rtcmServer.hasClient()) {
    bool added = false;
    for (int i = 0; i < 3; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        if (tcpClients[i]) tcpClients[i].stop();
        tcpClients[i] = rtcmServer.available();
        added = true;
        break;
      }
    }
    if (!added) rtcmServer.available().stop(); 
  }

  // --- UART Veri Okuma (BELLEK DOSTU CHAR BUFFER MİMARİSİ) ---
  size_t bytesAvailable = Serial2.available();
  if (bytesAvailable > 0) {
    uint8_t buf[128]; 
    if (bytesAvailable > sizeof(buf)) bytesAvailable = sizeof(buf);
    size_t len = Serial2.read(buf, bytesAvailable);

    // A) Veriyi TCP İstemcilerine Fırlat
    for (int i = 0; i < 3; i++) {
      if (tcpClients[i] && tcpClients[i].connected()) {
        tcpClients[i].write(buf, len);
      }
    }

    // B) Kendi Sistemimiz İçin Veriyi Ayrıştır
    for (size_t i = 0; i < len; i++) {
      uint8_t b = buf[i];
      gps.encode(b); 
      if (b == 0xD3) rtcmPaketSayaci++;
      
      char c = (char)b;
      
      if (c == '$') {
        nmeaIdx = 0;
        nmeaBuff[nmeaIdx++] = c;
      } else if (c == '\n') {
        if (nmeaIdx > 0 && nmeaBuff[0] == '$') {
          // Satır sonundaki gizli \r karakterini temizle
          if (nmeaBuff[nmeaIdx - 1] == '\r') {
            nmeaBuff[nmeaIdx - 1] = '\0';
          } else {
            nmeaBuff[nmeaIdx] = '\0'; 
          }
          
          // ==========================================================
          // GÜVENLİK KAPISI: EĞER MATEMATİK TUTMUYORSA RTCM GÜRÜLTÜSÜDÜR!
          // ==========================================================
          if (isChecksumValid(nmeaBuff)) {
            String gecerliCevap = String(nmeaBuff); 
            
            uyduTipleriniAyristir(gecerliCevap); 
            
            // KARA LİSTE FİLTRESİ
            if (!gecerliCevap.startsWith("$GN") && 
                !gecerliCevap.startsWith("$GP") && 
                !gecerliCevap.startsWith("$GL") && 
                !gecerliCevap.startsWith("$GA") && 
                !gecerliCevap.startsWith("$GB") && 
                !gecerliCevap.startsWith("$GQ") &&
                !gecerliCevap.startsWith("$BD")) {
              
              Serial.println("[MODUL CEVABI]: " + gecerliCevap);
              ws.textAll("TERM:" + gecerliCevap);
            }
          }
        }
        nmeaIdx = 0; 
      } else if (c >= 32 && c <= 126 && nmeaIdx > 0) {
        if (nmeaIdx < 127) {
          nmeaBuff[nmeaIdx++] = c;
        }
      }
    }
  }

  // --- Her 1 Saniyede Bir Web Arayüzüne Genel Veri Yolla ---
  if (millis() - sonGuncelleme >= 1000) {
    sonGuncelleme = millis();

    if (ws.count() > 0) {
      JsonDocument doc; 
      
      doc["lat"] = gps.location.isValid() ? gps.location.lat() : 0.0;
      doc["lon"] = gps.location.isValid() ? gps.location.lng() : 0.0;
      doc["alt"] = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
      doc["hdop"] = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0;
      
      int activeTcp = 0;
      for (int i = 0; i < 3; i++) {
        if (tcpClients[i] && tcpClients[i].connected()) activeTcp++;
      }
      doc["tcp_clients"] = activeTcp;

      doc["rtcm"] = rtcmPaketSayaci;
      doc["pps_count"] = ppsSayaci;
      // millis() yerine micros() üzerinden 2 saniyelik (2.000.000 mikrosaniye) fark kontrolü
      doc["pps_active"] = (micros() - sonPpsZamaniMicros < 2000000) ? true : false; 

      int sGPS=0, sGLO=0, sGAL=0, sBEI=0, sNAV=0, sQZS=0;
      JsonArray sky = doc["sky"].to<JsonArray>();
      
      for (int i = 0; i < activeSatCount; i++) {
        SatData s = activeSats[i];
        if(strcmp(s.sys, "GP") == 0) sGPS++;
        else if(strcmp(s.sys, "GL") == 0) sGLO++;
        else if(strcmp(s.sys, "GA") == 0) sGAL++;
        else if(strcmp(s.sys, "GB") == 0) sBEI++;
        else if(strcmp(s.sys, "GI") == 0) sNAV++;
        else if(strcmp(s.sys, "GQ") == 0) sQZS++;
        
        if(s.elev > 0 || s.azim > 0) {
          JsonObject obj = sky.add<JsonObject>();
          obj["s"] = s.sys;
          obj["id"] = s.id;
          obj["e"] = s.elev;
          obj["a"] = s.azim;
          obj["sn"] = s.snr; 
        }
      }

      JsonObject sats = doc["sats"].to<JsonObject>();
      sats["gps"] = sGPS; sats["glo"] = sGLO; sats["gal"] = sGAL;
      sats["bei"] = sBEI; sats["nav"] = sNAV; sats["qzs"] = sQZS;

      String jsonString;
      serializeJson(doc, jsonString);
      ws.textAll(jsonString);
    }

    rtcmPaketSayaci = 0;
    activeSatCount = 0; // Diziyi her saniye baştan yazmak için sayacı sıfırla
  }
}