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

// --- YENİ: Kapsamlı Uydu Veri Yapısı (ID, Tip, Elevasyon, Azimut) ---
struct SatData {
  int id;
  String sys; 
  int elev;   
  int azim;   
  
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

// ==========================================
// 3. GÖMÜLÜ WEB SAYFASI (HTML + JS + CSS)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>UAV MARMARA - RTK Telemetri</title>
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
    </style>
</head>
<body>
    <h2>📡 UAV MARMARA RTK BASE</h2>
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
            <div id="map"></div>
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
            <canvas id="skyview" width="200" height="200"></canvas>
        </div>
    </div>

    <script>
        // Harita Kurulumu
        var map = L.map('map').setView([41.0, 28.9], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
        var marker = L.marker([41.0, 28.9]).addTo(map);
        var firstLock = false;

        // Skyview Çizimi (Matematiksel Koordinat Çevirici)
        function drawSkyview(skyData) {
            var canvas = document.getElementById("skyview");
            var ctx = canvas.getContext("2d");
            var cx = canvas.width / 2; var cy = canvas.height / 2; var r = cx - 5;
            
            // Ekranı Temizle ve Pusula İzini Çiz
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = "#444"; ctx.lineWidth = 1;
            [r, r*0.66, r*0.33].forEach(rad => { ctx.beginPath(); ctx.arc(cx, cy, rad, 0, 2*Math.PI); ctx.stroke(); });
            ctx.beginPath(); ctx.moveTo(cx, cy-r); ctx.lineTo(cx, cy+r); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(cx-r, cy); ctx.lineTo(cx+r, cy); ctx.stroke();

            if(!skyData) return;

            // Uyduları Açılara Göre Haritaya Ekle
            skyData.forEach(sat => {
                var satR = r * (1 - (sat.e / 90.0));
                var rad = (sat.a - 90) * Math.PI / 180.0;
                var x = cx + satR * Math.cos(rad);
                var y = cy + satR * Math.sin(rad);

                // Renklendirme Sistemi
                ctx.fillStyle = sat.s === "GP" ? "#ff4444" : // Kırmızı
                                sat.s === "GL" ? "#44ff44" : // Yeşil
                                sat.s === "GA" ? "#4444ff" : // Mavi
                                sat.s === "GB" ? "#ffff44" : // Sarı
                                "#ffffff"; // Diğer

                ctx.beginPath();
                ctx.arc(x, y, 4, 0, 2*Math.PI);
                ctx.fill();
                
                ctx.fillStyle = "#aaa";
                ctx.font = "8px Arial";
                ctx.fillText(sat.s + sat.id, x + 5, y + 3);
            });
        }
        drawSkyview(); // Başlangıçta boş çiz

        // WebSocket İşlemleri
        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onmessage = onMessage;
        }

        function onMessage(event) {
            var data = JSON.parse(event.data);
            
            // Konum Verileri
            document.getElementById('lat').innerText = data.lat.toFixed(6);
            document.getElementById('lon').innerText = data.lon.toFixed(6);
            document.getElementById('alt').innerText = data.alt.toFixed(2);
            document.getElementById('hdop').innerText = data.hdop.toFixed(2);
            
            // Sistem Verileri (PPS ve RTCM)
            document.getElementById('rtcm').innerText = data.rtcm;
            document.getElementById('pps_count').innerText = data.pps_count;
            
            var ppsEl = document.getElementById('pps_status');
            if(data.pps_active) {
                ppsEl.innerText = "AKTİF (KİLİTLİ)";
                ppsEl.className = "value good";
            } else {
                ppsEl.innerText = "BEKLENİYOR...";
                ppsEl.className = "value alert";
            }

            // Uydu Verileri
            document.getElementById('s-gps').innerText = data.sats.gps;
            document.getElementById('s-glo').innerText = data.sats.glo;
            document.getElementById('s-gal').innerText = data.sats.gal;
            document.getElementById('s-bei').innerText = data.sats.bei;
            document.getElementById('s-nav').innerText = data.sats.nav;
            document.getElementById('s-qzs').innerText = data.sats.qzs;
            
            var totalSats = data.sats.gps + data.sats.glo + data.sats.gal + data.sats.bei + data.sats.nav + data.sats.qzs;
            document.getElementById('total_sats').innerText = totalSats;

            // Harita Güncelleme
            if(data.lat !== 0.0 && data.lon !== 0.0) {
                var newLatLng = new L.LatLng(data.lat, data.lon);
                marker.setLatLng(newLatLng);
                if(!firstLock) {
                    map.setView(newLatLng, 18);
                    firstLock = true;
                }
            }

            // Skyview Radarını Güncelle
            if(data.sky) {
                drawSkyview(data.sky);
            }
        }
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// 4. YENİ NMEA AYRIŞTIRICI (ID + ELEVASYON + AZİMUT)
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

    // GSV mesajlarında veriler 4. virgülden itibaren 4'lü paketler halinde gelir: [ID, Elev, Azim, SNR]
    for (int i = 4; i < cCount; i += 4) {
      if (i + 2 < cCount) {
        int id = nmea.substring(commaIndex[i-1] + 1, commaIndex[i]).toInt();
        int elev = nmea.substring(commaIndex[i] + 1, commaIndex[i+1]).toInt();
        int azim = nmea.substring(commaIndex[i+1] + 1, commaIndex[i+2]).toInt();
        
        if (id > 0) {
          SatData s = {id, sys, elev, azim};
          activeSats.insert(s); // Kümeye Ekle
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

  // --- PPS Kesme Ayarı ---
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
  
  server.addHandler(&ws);
  server.begin();

  // Modül Ayarları (GGA, GSV, Survey-In)
  Serial2.println("$PAIR062,0,1*3F"); 
  delay(100);
  Serial2.println("$PAIR062,3,1*3C"); 
  delay(2000);
  Serial2.println("$PQTMCFGSVIN,W,1,300,1,0,0,0*20"); 
}

// ==========================================
// 6. ANA DÖNGÜ (LOOP)
// ==========================================
void loop() {
  // 1. Modülden Gelen Veriyi Oku
  while (Serial2.available() > 0) {
    uint8_t b = Serial2.read();
    gps.encode(b);

    // RTCM Paketi Yakalama
    if (b == 0xD3) rtcmPaketSayaci++;

    // NMEA Özel Cümle Yakalama
    if (b == '$') {
      uyduTipleriniAyristir(nmeaTampon);
      nmeaTampon = "$";
    } else if (b != '\r' && b != '\n') {
      nmeaTampon += (char)b;
    }
  }

  // 2. Her 1 Saniyede Bir Web Arayüzüne Veri Yolla
  if (millis() - sonGuncelleme >= 1000) {
    sonGuncelleme = millis();

    if (ws.count() > 0) {
      // ArduinoJson 7 ile uyumlu dinamik boyut. Büyük uydu dizileri için şart.
      JsonDocument doc; 
      
      // Konum Verileri
      doc["lat"] = gps.location.isValid() ? gps.location.lat() : 0.0;
      doc["lon"] = gps.location.isValid() ? gps.location.lng() : 0.0;
      doc["alt"] = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
      doc["hdop"] = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0;

      // Sistem ve Sayaç Verileri
      doc["rtcm"] = rtcmPaketSayaci;
      doc["pps_count"] = ppsSayaci;
      doc["pps_active"] = (millis() - sonPpsZamani < 2000) ? true : false;

      // --- SKYVIEW DİZİSİ VE UYDU SAYILARI ---
      int sGPS=0, sGLO=0, sGAL=0, sBEI=0, sNAV=0, sQZS=0;
      JsonArray sky = doc["sky"].to<JsonArray>();
      
      for (auto const& s : activeSats) {
        // İstatistik
        if(s.sys == "GP") sGPS++;
        else if(s.sys == "GL") sGLO++;
        else if(s.sys == "GA") sGAL++;
        else if(s.sys == "GB") sBEI++;
        else if(s.sys == "GI") sNAV++;
        else if(s.sys == "GQ") sQZS++;
        
        // Radara Çizilecekleri Ekle
        if(s.elev > 0 || s.azim > 0) {
          JsonObject obj = sky.add<JsonObject>();
          obj["s"] = s.sys;
          obj["id"] = s.id;
          obj["e"] = s.elev;
          obj["a"] = s.azim;
        }
      }

      JsonObject sats = doc["sats"].to<JsonObject>();
      sats["gps"] = sGPS; sats["glo"] = sGLO; sats["gal"] = sGAL;
      sats["bei"] = sBEI; sats["nav"] = sNAV; sats["qzs"] = sQZS;

      String jsonString;
      serializeJson(doc, jsonString);
      ws.textAll(jsonString);
    }

    // Sayaçları ve Havuzu Sıfırla
    rtcmPaketSayaci = 0;
    activeSats.clear();
  }
}