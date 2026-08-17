#pragma once
#include <Globals.h>

// Tailscale client, via the microlink implementation of the ts2021 protocol.
//
// The point of this is remote access to the web interface without exposing
// anything: microlink registers a real lwIP netif carrying the device's
// 100.x.y.z address, so the async web server already bound to INADDR_ANY
// answers on the tailnet with no changes of its own. Nothing here forwards
// HTTP; it only brings the interface up.
//
// A tailnet needs an uplink, so this stays idle until the station interface is
// connected. The soft-AP path is unaffected either way.

void initTailscale();     // claim the map buffer while the heap is unfragmented
void handleTailscale();   // state machine, driven from the core 0 task
void loadTailscaleCfg();
void saveTailscaleCfg();  // applying new settings restarts the client
