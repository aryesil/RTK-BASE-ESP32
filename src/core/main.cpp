#include <Globals.h>
#include <system/SystemManager.h>
#include <gnss/GNSS_Processor.h>
#include <network/NetworkManager.h>
#include <web/WebServerManager.h>
#include <network/RTCMSocket.h>
#include <gnss/GNSS_Core.h>

extern void networkTaskCode(void * parameter);
void setup() {
    initHardware();
    initSystemResources();
    setupGNSS();

    setupNetwork();
    setupWebServer();
    setupRTCMSocket();

    applyGnssConfiguration();
    xTaskCreatePinnedToCore(networkTaskCode, "NetworkTask", 16384, NULL, 1, &NetworkTaskHandle, 0);
}

void loop() {
    runGNSSProcessing();
    vTaskDelay(pdMS_TO_TICKS(1)); 
}