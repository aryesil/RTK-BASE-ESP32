#include <system/SystemManager.h>
#include <Globals.h>
#include <esp_log.h>

// ESP-IDF components log through their own vprintf, which writes UART0 a
// character at a time and spins until each one is out: at 115200 baud a
// typical line holds the calling task for about 8 ms. It also ignores
// logMuted, so with the USB output on those lines were spliced into the RTCM
// stream. Routed through Log instead, IDF output is buffered like everything
// else and falls silent with it.
static int idfLogVprintf(const char *fmt, va_list ap) {
    if (logMuted) return 0;
    char line[160];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n > 0) Log.write((const uint8_t *)line, n < (int)sizeof(line) ? n : sizeof(line) - 1);
    return n;
}

void initHardware() {
    // Must precede begin(). Without it UART0 buffers barely more than the
    // hardware FIFO, and the USB RTCM output would drop every frame larger than
    // that instead of the occasional one.
    Serial.setTxBufferSize(USB_TX_BUF);
    Serial.begin(USB_BAUD_DEFAULT);
    esp_log_set_vprintf(idfLogVprintf);
    pinMode(PPS_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);
}

void initSystemResources() {
    dataMutex = xSemaphoreCreateMutex();
    tcpMutex = xSemaphoreCreateMutex();
    baseMutex = xSemaphoreCreateMutex();
    termQueue = xQueueCreate(15, TERM_MSG_LEN);
}

void IRAM_ATTR ppsKesmesi() {
  portENTER_CRITICAL_ISR(&ppsMux);
  sonPpsZamaniMicros = micros(); 
  portEXIT_CRITICAL_ISR(&ppsMux);
}