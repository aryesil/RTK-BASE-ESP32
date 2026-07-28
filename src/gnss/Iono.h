#pragma once
#include <Globals.h>

// Single-station ionospheric monitor built from the module's own dual-frequency
// observations, decoded out of the MSM7 messages it already broadcasts.
//
// What this can and cannot measure, measured on the actual hardware:
//
//   The geometry-free code combination gives a slant delay, but it carries the
//   satellite and receiver differential code biases uncorrected. On this
//   receiver those biases are large enough to flip the sign - L5 pseudoranges
//   come out *shorter* than L1, which is physically impossible for ionospheric
//   delay alone. So the absolute value is reported but must be read as
//   uncalibrated.
//
//   The carrier-phase combination is ambiguous in absolute terms but precise to
//   millimetres in its variation, and the biases cancel in a difference. The
//   change since the start of a continuous tracking arc is therefore a real
//   ionospheric measurement, and that is what the UI leads with.

void initIono();

// Called from the RTCM frame path on core 1 for message types 1077/1097/1127.
void ionoFeedMsm7(const uint8_t* payload, uint16_t payloadLen, uint16_t msgType);

// Called once per telemetry tick on core 0: ages arcs, resolves elevation and
// azimuth from the satellite table and recomputes pierce points.
void ionoUpdate(double baseLat, double baseLon, bool basePosValid);
