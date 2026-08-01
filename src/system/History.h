#pragma once
#include <Globals.h>

// Twelve hours of receiver health, sampled every 30 s into a ring buffer.
//
// Every other panel in this firmware shows the current instant, which is the
// wrong resolution for the failures that actually happen to a base station:
// the solution wandering over an afternoon, carrier-to-noise sagging as water
// gets into a connector, interference that appears on a schedule. Those are
// only visible against time.
//
// The buffer is served as a binary blob rather than JSON. It is 23 kB of
// fixed-width records; serialising that into text would cost about 58 kB of
// heap on a device that has ~148 kB free, for no benefit, when the browser can
// read the records directly.

void historyInit();

// Called once per telemetry tick; stores a sample when the interval is up.
// satsUsed and satsTracked are the same two figures the header shows, counted
// the same way, so the chart and the header can never disagree.
void historyFeed(uint32_t nowMs, uint8_t satsUsed, uint8_t satsTracked,
                 uint8_t meanCn0, double hdop,
                 uint8_t fixQual, uint8_t jamL1, uint8_t jamL5,
                 bool haveFix, double lat, double lon, double alt,
                 uint32_t bytesSec, float ionoMeanM);

// Pointer to the blob, valid for the lifetime of the program. The header is
// refreshed on each call.
const uint8_t* historySnapshot(size_t &len);
