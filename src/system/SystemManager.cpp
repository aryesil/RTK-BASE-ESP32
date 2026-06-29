#include <system/SystemManager.h>
#include <Globals.h>

void initHardware() {
    Serial.begin(115200);
    pinMode(PPS_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsKesmesi, RISING);
}

void initSystemResources() {
    dataMutex = xSemaphoreCreateMutex();
    tcpMutex = xSemaphoreCreateMutex();
    termQueue = xQueueCreate(15, sizeof(char) * 160);
}

void IRAM_ATTR ppsKesmesi() {
  portENTER_CRITICAL_ISR(&ppsMux);
  sonPpsZamaniMicros = micros(); 
  portEXIT_CRITICAL_ISR(&ppsMux);
}