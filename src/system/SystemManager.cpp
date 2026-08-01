#include <system/SystemManager.h>
#include <Globals.h>

void initHardware() {
    // Must precede begin(). Without it UART0 buffers barely more than the
    // hardware FIFO, and the USB RTCM output would drop every frame larger than
    // that instead of the occasional one.
    Serial.setTxBufferSize(USB_TX_BUF);
    Serial.begin(USB_BAUD_DEFAULT);
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