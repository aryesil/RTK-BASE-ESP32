#pragma once

#define RXD2 17
#define TXD2 16
#define GNSS_BAUD 115200
#define PPS_PIN 27
#define RTCM_PORT 2101

// One entry per tracked (satellite, band) pair, not per satellite.
#define MAX_SIGNALS 120
// Satellites reported as used in the position solution by GSA.
#define MAX_USED_SATS 48

#define MAX_NMEA 256

#define SAT_TIMEOUT_MS  5000
#define USED_TIMEOUT_MS 3000

#define UDP_PORT  2102

// Max simultaneous RTCM stream consumers
#define MAX_TCP_CLIENTS 6
#define MAX_UDP_CLIENTS 6

// Largest RTCM 3 frame: 3 byte header + 1023 byte payload + 3 byte CRC24Q
#define RTCM_MAX_FRAME 1029
// Distinct RTCM message types tracked for the statistics table
#define RTCM_MAX_TYPES 12

// A raw TCP client sends nothing after connecting while an NTRIP client sends
// its GET immediately. Wait this long, with the socket silent, before assuming
// raw mode.
#define NTRIP_SNIFF_MS 250
// Once the request is recognised as HTTP it may still arrive in pieces, so it
// gets a far longer window to finish than the silence-detection above.
#define NTRIP_REQ_MS   5000
#define NTRIP_REQ_MAX  384

// Longest NMEA sentence accepted back from a consumer. Rovers send GGA; a
// longer line is a client that is not speaking NMEA and gets discarded.
#define NMEA_BACK_MAX 96
// A rover report older than this is stale: the client is connected but no
// longer telling us anything.
#define ROVER_GGA_STALE_MS 60000

// UDP subscribers must re-send a keepalive within this window.
#define UDP_CLIENT_TIMEOUT_MS 30000

// UART0 reaches the PC through the on-board USB bridge. Its transmit buffer has
// to hold a complete RTCM frame with room to spare, or the rate limiter below
// would reject every large MSM7 message. Set before Serial.begin().
#define USB_TX_BUF 2048
// The console runs at this rate and the RTCM stream falls back to it.
#define USB_BAUD_DEFAULT 115200
// Most the RTCM path may leave queued on UART0. Half the buffer, so a write
// always finds contiguous room and returns without waiting on the line - see
// the token bucket in DataOutput.cpp for why free space cannot be measured.
#define USB_CREDIT_MAX (USB_TX_BUF / 2)

// Terminal queue item size: "TERM:" prefix + longest NMEA sentence + NUL
#define TERM_MSG_LEN (MAX_NMEA + 8)

// Optional module messages are probed at boot. If nothing arrives within this
// window the feature is marked unsupported and the UI falls back.
#define FEATURE_PROBE_MS 15000
// A periodic message older than this counts as stale.
#define FEATURE_STALE_MS 6000

// Dual-frequency satellites tracked by the ionospheric monitor.
#define MAX_IONO_SATS 24
// An arc older than this has lost continuity; its phase reference is dropped.
#define IONO_ARC_TIMEOUT_MS 20000
// Ionospheric shell height used for the pierce point, kilometres.
#define IONO_SHELL_KM 350.0

// Twelve hours at half-minute resolution. A base station's problems are slow -
// a drifting solution, a connector letting go, interference that shows up at
// the same time every evening - so the window matters more than the rate.
#define HISTORY_SAMPLES     1440
#define HISTORY_INTERVAL_MS 30000

#define AP_SSID   "ESP32_RTK_BASE"
#define RX_MODEL  "LC29H (BS)"
#define FW_VERSION "1.2.0"
