#include <Arduino.h>
#include <TinyGPS++.h>

// TinyGPS++ nesnemiz
TinyGPSPlus gps;

// UART2 Pinleri (Çapraz bağlantı yaptığınız çalışan hali)
#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200 

// Zamanlama ve Sayaçlar
unsigned long sonEkranaYazma = 0;
int rtcmPaketSayaci = 0;

void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  Serial.println("=== ÇİFT ÇIKTI (NMEA + RTCM3) MİMARİSİ BAŞLATILDI ===");
  
  // ---------------------------------------------------------
  // YENİ EKLENEN KISIM: NMEA Çıktısını Açma
  // ---------------------------------------------------------
  Serial.println(">>> NMEA GGA Çıktısı Aktifleştiriliyor (1 Hz)...");
  // 0: GGA Mesajı, 1: Saniyede 1 kez
  Serial2.println("$PAIR062,0,1*3F");
  
  // Modülün komutu sindirmesi için ufak bir pay
  delay(500); 

  // ---------------------------------------------------------
  // SURVEY-IN KISMI
  // ---------------------------------------------------------
  Serial.println(">>> Modüle Survey-In (SVIN) komutu gönderiliyor...");
  // DİKKAT: Komutu döngüde değil, sadece açılışta BİR KEZ gönderiyoruz!
  // W,1(Aktif), 3600(Saniye), 1(Metre Hassasiyet)
  Serial2.println("$PQTMCFGSVIN,W,1,3600,1,0,0,0*16");
  
  delay(500); 

  Serial.println("Komutlar iletildi! 3600 saniyelik haritalama süreci başladı.");
  Serial.println("GNSS verileri süzülüyor...");
  Serial.println("-----------------------------------------------------");
}

void loop() {
  
  // ---------------------------------------------------------
  // 1. GÖREV: KARIŞIK VERİYİ OKUMA VE AYRIŞTIRMA
  // ---------------------------------------------------------
  while (Serial2.available() > 0) {
    // Veriyi char olarak değil, 8-bit işaretsiz sayı (byte) olarak okuyoruz
    // Çünkü RTCM3 binary (ikili) bir veridir.
    uint8_t gelenByte = Serial2.read();
    
    // YOL A: EĞER GELEN VERİ NMEA İSE (TinyGPS++ halleder)
    // TinyGPS++ binary verileri otomatik çöpe atar, sadece $ ile başlayan metinleri işler.
    gps.encode(gelenByte);

    // YOL B: EĞER GELEN VERİ RTCM3 İSE (0xD3 ile başlar)
    if (gelenByte == 0xD3) {
      rtcmPaketSayaci++;
      
      // NOT: İleride bu bloğun içine bir kod yazarak, bu D3 baytını ve devamını 
      // doğrudan Wi-Fi veya RF/LoRa üzerinden İHA'ya yollayacaksınız.
    }
  }

  // ---------------------------------------------------------
  // 2. GÖREV: TEMİZ EKRAN ÇIKTISI (Her 2 saniyede 1 kez özet geçer)
  // ---------------------------------------------------------
  if (millis() - sonEkranaYazma >= 2000) {
    sonEkranaYazma = millis();

    Serial.print("GNSS DURUM: ");

    // Geçerli bir NMEA konumu var mı?
    if (gps.location.isValid()) {
      Serial.print("Enlem: "); Serial.print(gps.location.lat(), 6); 
      Serial.print(" | Boylam: "); Serial.print(gps.location.lng(), 6);
      Serial.print(" | Uydu: "); Serial.print(gps.satellites.value());
    } else {
      Serial.print("Konum Çözümleniyor... (Kilitli Uydu: ");
      Serial.print(gps.satellites.value());
      Serial.print(")");
    }

    // Arka planda RTCM3 akıyor mu onu da görelim
    Serial.print("  ||  Son 2 Saniyede Yakalanan RTCM3 Paketi: ");
    Serial.println(rtcmPaketSayaci);

    // Sayacı sıfırla ki anlık akışı görebilelim
    rtcmPaketSayaci = 0;
  }
}