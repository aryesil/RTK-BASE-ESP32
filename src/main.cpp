#include <Arduino.h>
#include <TinyGPS++.h>
#include <set> // Benzersiz ID'leri filtrelemek için gerekli kütüphane

// TinyGPS++ nesnemiz
TinyGPSPlus gps;

// UART2 Pinleri 
#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200 

// PPS Pini
#define PPS_PIN 27

// Zamanlama ve Sayaçlar
unsigned long sonEkranaYazma = 0;
int rtcmPaketSayaci = 0;

// Benzersiz Uydu ID'lerini Tutacak Kümeler (Sets)
std::set<int> gpsIds, glonassIds, galileoIds, beidouIds, navicIds, qzssIds;

volatile unsigned long ppsSayaci = 0;
volatile unsigned long sonPpsZamani = 0;

// --- KESME FONKSİYONU ---
void IRAM_ATTR ppsKesmesi() {
  ppsSayaci++;
  sonPpsZamani = millis();
}

// NMEA cümlelerinden fiziksel uydu ID'lerini cımbızla çeken fonksiyon
void uyduTipleriniAyristir(String nmea) {
  // Satırın içinde GSV (Satellites in View) geçiyor mu?
  if (nmea.indexOf("GSV") != -1) {
    int commaCount = 0;
    
    // NMEA cümlesindeki virgülleri sayarak Uydu ID sütunlarını buluyoruz
    // NMEA Formatı: ... ,UyduSayisi, [ID, Elev, Azim, SNR], [ID, Elev, Azim, SNR] ...
    for (int i = 0; i < nmea.length(); i++) {
      if (nmea[i] == ',') {
        commaCount++;
        
        // 4., 8., 12., ve 16. virgüllerden hemen sonrası Uydu ID'sidir
        if (commaCount >= 4 && (commaCount % 4 == 0)) {
          int nextComma = nmea.indexOf(',', i + 1);
          if (nextComma == -1) nextComma = nmea.indexOf('*', i + 1); // Satır sonu koruması
          
          if (nextComma != -1) {
            int satId = nmea.substring(i + 1, nextComma).toInt();
            
            // Eğer geçerli bir ID okuduysak ait olduğu kümeye (set) ekle
            if (satId > 0) {
              if (nmea.startsWith("$GP"))      gpsIds.insert(satId);
              else if (nmea.startsWith("$GL")) glonassIds.insert(satId);
              else if (nmea.startsWith("$GA")) galileoIds.insert(satId);
              else if (nmea.startsWith("$GB") || nmea.startsWith("$BD")) beidouIds.insert(satId);
              else if (nmea.startsWith("$GI")) navicIds.insert(satId);
              else if (nmea.startsWith("$GQ")) qzssIds.insert(satId);
            }
          }
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);

  Serial.println("=== GELİŞMİŞ ÇOKLU GNSS İZLEME (FİZİKSEL SAYIM) BAŞLATILDI ===");
  
  // NMEA GGA (Konum) ve GSV (Detaylı Uydu Görünümü) mesajlarını açıyoruz
  Serial2.println("$PAIR062,0,1*3F"); // GGA Aç
  delay(100);
  Serial2.println("$PAIR062,3,1*3C"); // GSV Aç 
  
  delay(2000); 

  // Survey-In Komutu (300 saniye / 5 Dakika)
  Serial2.println("$PQTMCFGSVIN,W,1,300,1,0,0,0*20");
}

String nmeaTampon = "";

void loop() {
  while (Serial2.available() > 0) {
    uint8_t b = Serial2.read();
    gps.encode(b);

    // RTCM Sayacı
    if (b == 0xD3) rtcmPaketSayaci++;

    // NMEA Ayrıştırma İşlemi (Satır satır okuma)
    if (b == '$') {
      uyduTipleriniAyristir(nmeaTampon);
      nmeaTampon = "$";
    } else if (b != '\r' && b != '\n') {
      nmeaTampon += (char)b;
    }
  }

  // Her 2 saniyede bir ekranı güncelle
  if (millis() - sonEkranaYazma >= 2000) {
    sonEkranaYazma = millis();

    Serial.println("\n--- GNSS AYRINTILI DURUM RAPORU ---");
    
    // Konum ve HDOP
    if (gps.location.isValid()) {
      Serial.printf("KONUM: %.6f, %.6f | FIX: %s (HDOP: %.2f)\n", 
                    gps.location.lat(), gps.location.lng(), 
                    gps.hdop.hdop() < 2.0 ? "MUKEMMEL" : "ORTA", gps.hdop.hdop());
    } else {
      Serial.println("KONUM: Bekleniyor (Fix Yok)...");
    }

    // Benzersiz Uydu Dağılımını Hesapla
    int sGPS = gpsIds.size();
    int sGLO = glonassIds.size();
    int sGAL = galileoIds.size();
    int sBEI = beidouIds.size();
    int sNAV = navicIds.size();
    int sQZS = qzssIds.size();
    int sTotal = sGPS + sGLO + sGAL + sBEI + sNAV + sQZS;

    Serial.print("FİZİKSEL UYDULAR: ");
    Serial.printf("GPS[%d] GLN[%d] GAL[%d] BEI[%d] NAV[%d] QZSS[%d]", 
                  sGPS, sGLO, sGAL, sBEI, sNAV, sQZS);
    Serial.printf(" | TOPLAM: %d\n", sTotal);

    // Sistem Verileri ve PPS
    Serial.printf("RTCM3: %d paket/2sn | PPS: %s (%lu sn)\n", 
                  rtcmPaketSayaci, 
                  (millis() - sonPpsZamani < 2000) ? "KILITLI" : "YOK", 
                  ppsSayaci);
    
    // Değişkenleri sonraki 2 saniyelik okuma döngüsü için SIFIRLA
    rtcmPaketSayaci = 0;
    gpsIds.clear(); glonassIds.clear(); galileoIds.clear();
    beidouIds.clear(); navicIds.clear(); qzssIds.clear();
  }
}