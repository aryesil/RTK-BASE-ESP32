#include <Globals.h>
#include <system/SystemManager.h>
#include <gnss/GNSS_Processor.h>
#include <network/NetworkManager.h>
#include <web/WebServerManager.h>
#include <network/DataOutput.h>
#include <gnss/GNSS_Core.h>
#include <gnss/BaseConfig.h>
#include <network/NtripPush.h>
#include <network/Tailscale.h>
#include <gnss/Iono.h>
#include <system/History.h>

void setup() {
    initHardware();
    initSystemResources();
    // Read first: everything sized below depends on whether the Tailscale
    // client will run, and the client needs about 80 kB at runtime on a board
    // with 320 kB and no PSRAM. Off, the base station gets its full tables.
    loadTailscaleCfg();
    rt.lean = tsCfg.enabled;
    if (rt.lean) {
        rt.historySamples = LEAN_HISTORY_SAMPLES;
        rt.tcpClients     = LEAN_TCP_CLIENTS;
        rt.udpClients     = LEAN_UDP_CLIENTS;
        rt.telemetryBuf   = LEAN_TELEMETRY_BUF;
        rt.iono           = false;
    }

    initBaseConfig();
    initIono();
    historyInit();
    setupGNSS();

    initTailscale();   // claims its buffer before the web server fragments the heap
    setupNetwork();
    setupWebServer();
    initDataOutput();
    initNtripPush();

    applyGnssConfiguration();
    // Priority 8, not 1. The Tailscale client puts four tasks on this core at
    // priorities 5 to 7; at priority 1 this one - which drives telemetry and
    // the WebSocket queue - was preempted by all of them and the browser
    // connection dropped every few seconds. It sits below AsyncTCP's 10 and
    // does bounded work before yielding, so nothing it outranks is starved.
    xTaskCreatePinnedToCore(networkTaskCode, "NetworkTask", 16384, NULL, 8, &NetworkTaskHandle, 0);
}

void loop() {
    runGNSSProcessing();
}
