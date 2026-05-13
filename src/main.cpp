#include <Arduino.h>
#include <TinyGPS++.h>

// TinyGPS++ nesnemiz
TinyGPSPlus gps;

// UART2 Pinleri (Çapraz bağlantı yaptığınız çalışan hali)
#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200 

// PPS Pini Tanımlaması
#define PPS_PIN 27

// Zamanlama ve Sayaçlar
unsigned long sonEkranaYazma = 0;
int rtcmPaketSayaci = 0;

// Kesme (Interrupt) içinde değişen değişkenler 'volatile' olmalıdır
volatile unsigned long ppsSayaci = 0;
volatile unsigned long sonPpsZamani = 0;

// --- KESME FONKSİYONU (ISR) ---
// ESP32'de kesme fonksiyonlarının RAM'de çalışması için IRAM_ATTR etiketi eklenir.
// PPS pini her HIGH (3.3V) olduğunda bu fonksiyon otomatik tetiklenir.
void IRAM_ATTR ppsKesmesi() {
  ppsSayaci++;
  sonPpsZamani = millis();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(GNSS_BAUD, SERIAL_8N1, RXD2, TXD2);

  // PPS Pinini giriş olarak ayarlayıp Kesme (Interrupt) atıyoruz.
  // RISING: Sinyal LOW'dan HIGH'a çıktığı anı (saniyenin tam başlangıcını) yakalar.
  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);

  Serial.println("=== ÇİFT ÇIKTI (NMEA + RTCM3) MİMARİSİ BAŞLATILDI ===");
  
  // ---------------------------------------------------------
  // NMEA Çıktısını Açma
  // ---------------------------------------------------------
  Serial.println(">>> NMEA GGA Çıktısı Aktifleştiriliyor (1 Hz)...");
  // 0: GGA Mesajı, 1: Saniyede 1 kez
  Serial2.println("$PAIR062,0,1*3F");
  
  // Modülün komutu sindirmesi için ufak bir pay
  delay(20000); 

  // ---------------------------------------------------------
  // SURVEY-IN KISMI
  // ---------------------------------------------------------
  Serial.println(">>> Modüle Survey-In (SVIN) komutu gönderiliyor...");
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
    uint8_t gelenByte = Serial2.read();
    
    // YOL A: EĞER GELEN VERİ NMEA İSE
    gps.encode(gelenByte);

    // YOL B: EĞER GELEN VERİ RTCM3 İSE (0xD3 ile başlar)
    if (gelenByte == 0xD3) {
      rtcmPaketSayaci++;
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

    // RTCM3 Paketi ve PPS Durumu
    Serial.print("  ||  RTCM3: ");
    Serial.print(rtcmPaketSayaci);
    
    // Son PPS üzerinden 2 saniyeden az zaman geçtiyse PPS aktiftir
    if (millis() - sonPpsZamani < 2000 && ppsSayaci > 0) {
      Serial.print("  ||  PPS: AKTİF (Toplam: ");
      Serial.print(ppsSayaci);
      Serial.println(")");
    } else {
      Serial.println("  ||  PPS: BEKLENİYOR...");
    }

    // RTCM Sayacını sıfırla ki anlık akışı görebilelim (PPS sayacını sıfırlamıyoruz)
    rtcmPaketSayaci = 0;
  }
}