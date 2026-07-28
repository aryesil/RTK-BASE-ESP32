#pragma once
#include <Globals.h>

// Outbound NTRIP: this base connects to a remote caster and pushes its RTCM
// stream, rather than waiting for clients to pull from us. Implements the
// NTRIP v1 server handshake, which is what public casters (RTK2go, Emlid,
// rtk2go-style software) accept:
//
//   SOURCE <password> /<mountpoint>\r\n
//   Source-Agent: NTRIP <agent>\r\n
//   \r\n
//   -> ICY 200 OK\r\n\r\n
//
// The connect() call blocks for seconds, so the state machine deliberately
// lives on the core-0 network task; only the non-blocking frame write is
// reached from the GNSS core.

void initNtripPush();
void handleNtripPush();   // core 0, ~20 ms

// Writes one RTCM frame. Caller must already hold tcpMutex (sendRtcmFrame
// does), which is what serialises this against the state machine's handover.
void ntripPushWrite(const uint8_t* frame, size_t len);

void saveNtripPushCfg();
void loadNtripPushCfg();
