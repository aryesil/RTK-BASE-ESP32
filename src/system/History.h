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
// heap for no benefit, when the browser can read the records directly. The
// records live in instruction RAM, which only allows 32-bit access - see
// History.cpp - so they are read out through historyRead(), never a pointer.

void historyInit();

// Called once per telemetry tick; stores a sample when the interval is up.
// satsUsed and satsTracked are the same two figures the header shows, counted
// the same way, so the chart and the header can never disagree.
void historyFeed(uint32_t nowMs, uint8_t satsUsed, uint8_t satsTracked,
                 uint8_t meanCn0, double hdop,
                 uint8_t fixQual, uint8_t jamL1, uint8_t jamL5,
                 bool haveFix, double lat, double lon, double alt,
                 uint32_t bytesSec, float ionoMeanM);

// Size of the blob (header + ring), 0 when history is off.
size_t historySize();

// Copies up to maxLen bytes of the blob starting at offset into dst and
// returns the count. The header's uptime is refreshed when offset is 0.
size_t historyRead(size_t offset, uint8_t *dst, size_t maxLen);
