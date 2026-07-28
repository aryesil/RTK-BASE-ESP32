#pragma once
#include <Globals.h>

// RTCM fan-out. One complete, CRC-checked RTCM 3 frame goes to every consumer:
//
//   TCP raw      classic byte stream, what most rover software expects
//   NTRIP caster HTTP/ICY handshake on the same port, sniffed on connect
//   UDP unicast  lowest latency: no retransmit head-of-line blocking, while
//                802.11 unicast still gives link-level ACK and retry
//
// Subscribing to UDP: send any datagram to UDP_PORT from the rover and repeat
// it at least every UDP_CLIENT_TIMEOUT_MS to stay registered.

void initDataOutput();
void restartDataOutput();      // re-open listeners after a config change

void handleOutputClients();    // poll: accept, handshake, expire (core 1)
void sendRtcmFrame(const uint8_t* frame, size_t len, uint16_t msgType);

int  tcpClientCount();
int  udpClientCount();
int  snapshotTcpClients(RtcmClientInfo* out, int max);
int  snapshotUdpClients(UdpClientInfo* out, int max);

void saveOutputCfg();
void loadOutputCfg();
