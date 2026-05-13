#include <Arduino.h>
#include <TinyGPS++.h>

TinyGPSPlus gps;

#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200 
#define PPS_PIN 27

unsigned long sonEkranaYazma = 0;
int rtcmPaketSayaci = 0;

// Uydu Sayaçları
int satsGPS = 0, satsGlonass = 0, satsGalileo = 0, satsBeidou = 0, satsNavic = 0, satsQZSS = 0;

volatile unsigned long ppsSayaci = 0;
volatile unsigned long sonPpsZamani = 0;

void IRAM_ATTR ppsKesmesi() {
  ppsSayaci++;
  sonPpsZamani = millis();
}

// Ham NMEA cümlelerini parçalayıp uydu tiplerini sayan yardımcı fonksiyon
void uyduTipleriniAyristir(String nmea) {
  // GSV cümleleri uyduların detaylarını verir
  if (nmea.indexOf("GSV") != -1) {
    int totalSats = 0;
    int firstComma = nmea.indexOf(',');
    int secondComma = nmea.indexOf(',', firstComma + 1);
    int thirdComma = nmea.indexOf(',', secondComma + 1);
    
    if (thirdComma != -1) {
      totalSats = nmea.substring(secondComma + 1, thirdComma).toInt();
      
      if (nmea.startsWith("$GPGSV")) satsGPS = totalSats;      // GPS
      else if (nmea.startsWith("$GLGSV")) satsGlonass = totalSats; // GLONASS
      else if (nmea.startsWith("$GAGSV")) satsGalileo = totalSats; // Galileo
      else if (nmea.startsWith("$GBGSV") || nmea.startsWith("$BDGSV")) satsBeidou = totalSats; // Beidou
      else if (nmea.startsWith("$GIGSV")) satsNavic = totalSats;   // NavIC
      else if (nmea.startsWith("$GQGSV")) satsQZSS = totalSats;    // QZSS
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);

  Serial.println("=== GELİŞMİŞ ÇOKLU GNSS İZLEME BAŞLATILDI ===");
  
  // NMEA GGA (Konum) ve GSV (Detaylı Uydu Görünümü) mesajlarını açıyoruz
  Serial2.println("$PAIR062,0,1*3F"); // GGA Aç
  delay(100);
  Serial2.println("$PAIR062,3,1*3C"); // GSV Aç (Uydu detayları için şart)
  
  delay(2000); 

  // Survey-In Komutu
  Serial2.println("$PQTMCFGSVIN,W,1,300,1,0,0,0*20");
}

String nmeaTampon = "";

void loop() {
  while (Serial2.available() > 0) {
    uint8_t b = Serial2.read();
    gps.encode(b);

    // RTCM Sayacı
    if (b == 0xD3) rtcmPaketSayaci++;

    // NMEA Ayrıştırma İşlemi
    if (b == '$') {
      uyduTipleriniAyristir(nmeaTampon);
      nmeaTampon = "$";
    } else if (b != '\r' && b != '\n') {
      nmeaTampon += (char)b;
    }
  }

  if (millis() - sonEkranaYazma >= 2000) {
    sonEkranaYazma = millis();

    Serial.println("\n--- GNSS AYRINTILI DURUM RAPORU ---");
    
    if (gps.location.isValid()) {
      Serial.printf("KONUM: %.6f, %.6f | FIX: %s\n", 
                    gps.location.lat(), gps.location.lng(), 
                    gps.hdop.hdop() < 2.0 ? "MUKEMMEL" : "ORTA");
    } else {
      Serial.println("KONUM: Bekleniyor (Fix Yok)...");
    }

    // Uydu Dağılımı
    Serial.print("UYDULAR: ");
    Serial.printf("GPS[%d] GLN[%d] GAL[%d] BEI[%d] NAV[%d] QZSS[%d]", 
                  satsGPS, satsGlonass, satsGalileo, satsBeidou, satsNavic, satsQZSS);
    Serial.printf(" | TOPLAM: %d\n", (satsGPS + satsGlonass + satsGalileo + satsBeidou + satsNavic + satsQZSS));

    // Sistem Verileri
    Serial.printf("RTCM3: %d paket/2sn | PPS: %s (%lu sn)\n", 
                  rtcmPaketSayaci, 
                  (millis() - sonPpsZamani < 2000) ? "KILITLI" : "YOK", 
                  ppsSayaci);
    
    rtcmPaketSayaci = 0;
  }
}