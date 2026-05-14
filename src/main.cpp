#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <set>

// ==========================================
// 1. KULLANICI AYARLARI 
// ==========================================
const char* ssid = "IHA_MARMARA_TEST";
const char* password = "HezarfenCelebi2023";

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
WiFiClient tcpClients[3]; // Aynı anda 3 istemciye kadar hizmet verebilecek şekilde dizi

// --- Kapsamlı Uydu Veri Yapısı (ID, Tip, Elevasyon, Azimut, SNR) ---
struct SatData {
  int id;
  String sys; 
  int elev;   
  int azim;   
  int snr;    // Sinyal Gücü (Glow efekti için)
  
  // Set içinde mükerrer uyduları engellemek için kimlik kuralı
  bool operator<(const SatData& other) const {
    if (sys != other.sys) return sys < other.sys;
    return id < other.id;
  }
};

// Tüm aktif uyduları açılarıyla beraber tutacağımız tek havuz
std::set<SatData> activeSats;

// Sayaçlar
int rtcmPaketSayaci = 0;
volatile unsigned long ppsSayaci = 0;
volatile unsigned long sonPpsZamani = 0;
unsigned long sonGuncelleme = 0;
String nmeaTampon = "";

// --- PPS KESME FONKSİYONU ---
void IRAM_ATTR ppsKesmesi() {
  ppsSayaci++;
  sonPpsZamani = millis();
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
    <title>ESP32- RTK Telemetri</title>
    <!-- Leaflet Harita Kütüphanesi -->
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
        /* Terminal Stilleri */
        #terminal { height: 110px; overflow-y: auto; background: #000; color: #00ffcc; padding: 8px; border: 1px solid #444; border-radius: 4px; font-size: 12px; margin-top: 5px; font-family: monospace; }
        .term-line { border-bottom: 1px dashed #222; padding-bottom: 3px; margin-bottom: 3px; word-wrap: break-word;}
    </style>
</head>
<body>
    <h2>📡 ESP32 RTK BASE</h2>
    <div class="grid">
        <!-- KONUM VE SİSTEM KARTI -->
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

        <!-- UYDU VE SKYVIEW KARTI -->
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
        // Harita Kurulumu
        var map = L.map('map').setView([41.0, 28.9], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
        var marker = L.marker([41.0, 28.9]).addTo(map);
        var firstLock = false;

        // BAYRAKLARI ÖNCEDEN YÜKLEME
        var flags = {};
        var flagUrls = {
            "GP": "https://flagcdn.com/w40/us.png", 
            "GL": "https://flagcdn.com/w40/ru.png", 
            "GA": "https://flagcdn.com/w40/eu.png", 
            "GB": "https://flagcdn.com/w40/cn.png", 
            "GI": "https://flagcdn.com/w40/in.png", 
            "GQ": "https://flagcdn.com/w40/jp.png"  
        };
        
        for(let key in flagUrls){
            let img = new Image();
            img.src = flagUrls[key];
            flags[key] = img;
        }

        // Skyview Çizimi 
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

                var glowColor = sat.sn > 35 ? "rgba(0, 255, 0, 0.7)" : 
                                sat.sn > 25 ? "rgba(255, 255, 0, 0.7)" : 
                                "rgba(255, 0, 0, 0.7)"; 

                ctx.save();
                ctx.shadowBlur = (sat.sn / 2) + 5; 
                ctx.shadowColor = glowColor;
                ctx.fillStyle = glowColor;
                ctx.beginPath();
                ctx.arc(x, y, balonYaricap + 2, 0, 2*Math.PI);
                ctx.fill();
                ctx.restore(); 

                ctx.save(); 
                ctx.beginPath();
                ctx.arc(x, y, balonYaricap, 0, 2*Math.PI);
                ctx.clip(); 

                if(flags[sat.s] && flags[sat.s].complete) {
                    ctx.drawImage(flags[sat.s], x - balonYaricap, y - balonYaricap, balonYaricap*2, balonYaricap*2);
                } else {
                    ctx.fillStyle = "#888"; 
                    ctx.fill();
                }
                ctx.restore(); 

                ctx.beginPath();
                ctx.arc(x, y, balonYaricap, 0, 2*Math.PI);
                ctx.strokeStyle = "#fff";
                ctx.lineWidth = 1.5;
                ctx.stroke();

                var sysPrefix = sat.s === "GP" ? "G" : sat.s === "GL" ? "R" : 
                                sat.s === "GA" ? "E" : sat.s === "GB" ? "B" :
                                sat.s === "GI" ? "I" : sat.s === "GQ" ? "Q" : "U";
                
                ctx.fillStyle = "#eee";
                ctx.font = "bold 11px Arial";
                ctx.textAlign = "left";
                ctx.fillText(sysPrefix + sat.id, x + balonYaricap + 4, y + 4);
            });
        }
        drawSkyview();

        // WebSocket İşlemleri
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        function initWebSocket() {
            websocket = new WebSocket(gateway);
            
            websocket.onopen = function(event) {
                logTerminal("<span style='color:#33ff33;'>[SİSTEM] ESP32 Bağlantısı Kuruldu.</span>");
            };
            
            websocket.onclose = function(event) {
                logTerminal("<span style='color:#ffaa00;'>[SİSTEM] Bağlantı Koptu! Yeniden bağlanılıyor...</span>");
                setTimeout(initWebSocket, 2000); 
            };

            websocket.onmessage = onMessage;
        }

        // --- HTTP FETCH İLE KOMUT GÖNDERİMİ ---
        function sendCommand() {
            let cmdInput = document.getElementById('cmdInput');
            let cmd = cmdInput.value.trim();
            
            if (cmd) {
                let finalCmd = cmd;
                
                // Otomatik Checksum hesapla 
                if (cmd.startsWith('$') && !cmd.includes('*')) {
                    let checksum = 0;
                    for (let i = 1; i < cmd.length; i++) {
                        checksum ^= cmd.charCodeAt(i);
                    }
                    let hexCS = checksum.toString(16).toUpperCase().padStart(2, '0');
                    finalCmd = cmd + '*' + hexCS;
                }

                // HTTP üzerinden komutu gönder
                fetch('/cmd?c=' + encodeURIComponent(finalCmd))
                .then(response => {
                    if(response.ok) {
                        logTerminal("<span style='color:#fff; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + finalCmd + "</span>");
                    } else {
                        logTerminal("<span style='color:#ffaa00;'>Cihaz komutu aldı ama sorun oluştu.</span>");
                    }
                })
                .catch(error => {
                    logTerminal("<span style='color:#ff3333;'>HATA: Modüle ulaşılamadı!</span>");
                });

                cmdInput.value = ""; 
            }
        }

        // Enter tuşu ile gönderme
        document.getElementById("cmdInput").addEventListener("keyup", function(event) {
            if (event.key === "Enter") sendCommand();
        });

        // Terminal ekranına yazı ekleme
        function logTerminal(msg) {
            var term = document.getElementById('terminal');
            term.innerHTML += "<div class='term-line'>" + msg + "</div>";
            term.scrollTop = term.scrollHeight; 
        }

        function onMessage(event) {
            // ==============================================================
            // YENİ VE %100 GÜVENLİ: JSON'DAN BAĞIMSIZ HAM METİN YAKALAYICI
            // Eğer gelen mesaj "TERM:" ile başlıyorsa, json parse etmeden direkt yazdır!
            // ==============================================================
            if (typeof event.data === "string" && event.data.startsWith("TERM:")) {
                let msg = event.data.substring(5); // "TERM:" kısmını kes
                logTerminal("<span style='color:#ff3333; font-weight:bold;'>RX:</span> <span style='color:#fff;'>" + msg + "</span>");
                return; // JSON'a hiç bulaşmadan fonksiyondan çık
            }

            var data;
            try { 
                data = JSON.parse(event.data); 
            } catch(e) { 
                return; // JSON hatası varsa sessizce çık
            }

            // Sadece konum/sistem verisi barındıran paketleri haritaya ve UI'a yansıt
            if (data.lat !== undefined) {
                document.getElementById('lat').innerText = data.lat.toFixed(6);
                document.getElementById('lon').innerText = data.lon.toFixed(6);
                document.getElementById('alt').innerText = data.alt.toFixed(2);
                document.getElementById('hdop').innerText = data.hdop.toFixed(2);
                document.getElementById('rtcm').innerText = data.rtcm;
                document.getElementById('tcp_clients').innerText = data.tcp_clients;
                document.getElementById('pps_count').innerText = data.pps_count;
                
                var ppsEl = document.getElementById('pps_status');
                if(data.pps_active) {
                    ppsEl.innerText = "AKTİF (KİLİTLİ)";
                    ppsEl.className = "value good";
                } else {
                    ppsEl.innerText = "BEKLENİYOR...";
                    ppsEl.className = "value alert";
                }

                document.getElementById('s-gps').innerText = data.sats.gps;
                document.getElementById('s-glo').innerText = data.sats.glo;
                document.getElementById('s-gal').innerText = data.sats.gal;
                document.getElementById('s-bei').innerText = data.sats.bei;
                document.getElementById('s-nav').innerText = data.sats.nav;
                document.getElementById('s-qzs').innerText = data.sats.qzs;
                
                var totalSats = data.sats.gps + data.sats.glo + data.sats.gal + data.sats.bei + data.sats.nav + data.sats.qzs;
                document.getElementById('total_sats').innerText = totalSats;
              
                if(data.lat !== 0.0 && data.lon !== 0.0) {
                    var newLatLng = new L.LatLng(data.lat, data.lon);
                    marker.setLatLng(newLatLng);
                    if(!firstLock) {
                        map.setView(newLatLng, 18);
                        firstLock = true;
                    }            
                }
                if(data.sky) {
                    drawSkyview(data.sky);
                }
            }
        }
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// 4. NMEA AYRIŞTIRICI
// ==========================================
void uyduTipleriniAyristir(String nmea) {
  if (nmea.indexOf("GSV") != -1) {
    String sys = "UN";
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
          SatData s = {id, sys, elev, azim, snr};
          activeSats.insert(s); 
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
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nBAGLANDI! Tarayicinizdan su adrese gidin:");
  Serial.println(WiFi.localIP());

  // Web Sunucu Yönlendirmeleri
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
   request->send_P(200, "text/html", index_html);
  });

  // --- HTTP Komut API'si ---
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

  // Modül Ayarları 
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
  
  // --- 1. TCP İstemci Bağlantı Kontrolü ---
  if (rtcmServer.hasClient()) {
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

  // --- 2. UART Veri Okuma, Filtreleme ve TCP Yayını ---
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

    // B) Kendi Sistemimiz İçin Veriyi Ayrıştır ve RX Cevaplarını Yakala
    for (size_t i = 0; i < len; i++) {
      uint8_t b = buf[i];
      gps.encode(b); 
      if (b == 0xD3) rtcmPaketSayaci++;
      
      // --- AKILLI NMEA VE RX FİLTRESİ ---
      if (b == '$') {
        nmeaTampon = "$";
      } else if (b == '\n') {
        // Satır tamamlandı. Boşlukları ve \r'yi temizle.
        nmeaTampon.trim(); 
        
        // TİTANYUM FİLTRE
        String temizCevap = "";
        for (int k = 0; k < nmeaTampon.length(); k++) {
          char c = nmeaTampon[k];
          // Sadece harfler, rakamlar ve standart noktalama işaretleri
          if (isalnum(c) || c == '$' || c == ',' || c == '.' || c == '*' || c == '-' || c == '_' || c == ' ' || c == ':') {
            temizCevap += c;
          }
        }
        nmeaTampon = temizCevap; // %100 temizlenmiş metin
        
        if (nmeaTampon.length() > 0) {
          // Eğer $ ile başlıyorsa TinyGPS harici kendi uydu radarımız için de ayrıştır
          if (nmeaTampon.startsWith("$")) {
             uyduTipleriniAyristir(nmeaTampon); 
          }
          
          // KARA LİSTE (BLACKLIST) FİLTRESİ
          // Standart konum verileri DEĞİLSE bu bir cevaptır!
          if (!nmeaTampon.startsWith("$GN") && 
              !nmeaTampon.startsWith("$GP") && 
              !nmeaTampon.startsWith("$GL") && 
              !nmeaTampon.startsWith("$GA") && 
              !nmeaTampon.startsWith("$GB") && 
              !nmeaTampon.startsWith("$GQ") &&
              !nmeaTampon.startsWith("$BD")) {
            
            Serial.println("[MODUL CEVABI]: " + nmeaTampon);
            
            // ==============================================================
            // YENİ: JSON İLE UĞRAŞMADAN ANINDA HAM METİN OLARAK FIRLAT!
            // Tarayıcı bunu görüp anında kırmızı RX olarak ekrana basacak.
            // ==============================================================
            ws.textAll("TERM:" + nmeaTampon);
          }
        }
        nmeaTampon = ""; // Tamponu sıfırla
      } else if (b >= 32 && b <= 126) {
        // Sadece okunabilir karakterleri al (RTCM gürültüsünü engeller)
        if (nmeaTampon.length() < 120) {
          nmeaTampon += (char)b;
        }
      }
    }
  }

  // 3. Her 1 Saniyede Bir Web Arayüzüne Genel Veri Yolla
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
      doc["pps_active"] = (millis() - sonPpsZamani < 2000) ? true : false;

      int sGPS=0, sGLO=0, sGAL=0, sBEI=0, sNAV=0, sQZS=0;
      JsonArray sky = doc["sky"].to<JsonArray>();
      
      for (auto const& s : activeSats) {
        if(s.sys == "GP") sGPS++;
        else if(s.sys == "GL") sGLO++;
        else if(s.sys == "GA") sGAL++;
        else if(s.sys == "GB") sBEI++;
        else if(s.sys == "GI") sNAV++;
        else if(s.sys == "GQ") sQZS++;
        
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
    activeSats.clear();
  }
}