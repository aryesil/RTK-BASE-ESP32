#include <Globals.h>
#include <system/SystemManager.h>
#include <gnss/GNSS_Processor.h>
#include <network/NetworkManager.h>
#include <web/WebServerManager.h>
#include <network/DataOutput.h>
#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <network/NtripPush.h>
#include <gnss/Iono.h>
#include <system/History.h>

void setup() {
    initHardware();
    initSystemResources();
    initBaseConfig();
    initIono();
    historyInit();
    setupGNSS();

    setupNetwork();
    setupWebServer();
    initDataOutput();
    initNtripPush();

    applyGnssConfiguration();
    xTaskCreatePinnedToCore(networkTaskCode, "NetworkTask", 16384, NULL, 1, &NetworkTaskHandle, 0);
}

void loop() {
    runGNSSProcessing();
}
