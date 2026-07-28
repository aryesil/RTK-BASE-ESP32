# ESP32 RTK Base

This is an ongoing project that integrates the budget Waveshare Quectel LC29H (BS) hat with an ESP32 for wireless RTK BASE setups. The ESP32 configures the GNSS module, validates and re-frames its RTCM 3 output, and hands it to rovers over WiFi — as a raw TCP stream, as an NTRIP caster, over UDP, or by pushing to a remote caster. Everything is driven from a self-hosted web interface that needs no internet access at all.

<img width="616" height="810" alt="ESP32-RTK-DIAGRAM" src="https://github.com/user-attachments/assets/40c695e6-1b1a-47fd-8738-563983d75720" />

![Overview](docs/img/overview.png)

---

## What it does

* **Configures the module for you.** NMEA output, RTCM message set, station ARP and ephemerides are applied at boot; survey-in and fixed mode are set from the web page.
* **Forwards only valid RTCM.** Frames are reassembled and CRC-24Q checked before they leave the device. NMEA never reaches the rover, which keeps the radio link free for corrections.
* **Four ways out at once.** Raw TCP, NTRIP caster (v1 and v2 on the same port), UDP unicast/broadcast, and outbound NTRIP push to a public caster.
* **Access point first.** The soft-AP is always up, so the rover talks to the base over a single wireless hop. Joining a WiFi network is optional and only adds a management route.
* **Tells you what it is really doing.** Survey-in progress and interference status come from the receiver itself, and the broadcast station position is decoded back out of the RTCM 1005 the rovers receive.

---

## Hardware

| | |
|---|---|
| Board | ESP32 dev board (tested on ESP32-WROOM-32D / DOIT DevKit v1) |
| GNSS | Waveshare Quectel **LC29H (BS)** hat |
| Link | UART, 115200 8N1 |

Wiring, as configured in `include/Config.h`:

| ESP32 | LC29H | Note |
|---|---|---|
| GPIO17 | TX | module transmit → ESP32 receive |
| GPIO16 | RX | ESP32 transmit → module receive |
| GPIO27 | PPS | pulse-per-second input |
| 5V / GND | 5V / GND | |

The module needs a clear sky view and a stable antenna mount. A base whose antenna moves is worse than no base at all.

---

## Building and flashing

The project builds with [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/aryesil/RTK-BASE-ESP32
cd RTK-BASE-ESP32
pio run -t upload
```

Dependencies are pulled automatically: `ArduinoJson`, `ESPAsyncWebServer`, `AsyncTCP`, `TinyGPSPlus`.

`platformio.ini` pins the AsyncTCP task to core 0 so that the HTTP and WebSocket work can never land on the core that forwards RTCM:

```ini
build_flags =
    -D CONFIG_ASYNC_TCP_RUNNING_CORE=0
```

Firmware over the air is available too — the device advertises itself as `ESP32-RTK-BASE` and OTA is serviced in every network state.

---

## First run

1. Power the device. It starts an open access point called **`ESP32_RTK_BASE`** on channel 6.
2. Connect to it and open **`http://192.168.4.1`**.
3. Go to **Network** and set an AP password and a quiet channel. Optionally join your own WiFi as an uplink — the access point stays up either way.
4. Go to **Base Mode** and choose Survey-In or Fixed (see below).
5. Point your rover at the base. Endpoints are listed on **Data Output**.

There is no separate setup page any more; the full interface is served from the first boot.

---

## The web interface

Seven tabs, all served from flash, no CDN, no internet.

### Overview

Quality indicators, the constellation fan with a live downlink animation, correction status, position, the ionosphere monitor and a position scatter plot.

![Position, ionosphere and scatter](docs/img/overview-position.png)

The **position scatter** shows horizontal deviation from the running mean in centimetres. During survey-in the cloud should shrink and stay centred; a drifting cloud means the antenna or the multipath environment is not stable yet.

### GNSS

![Signal distribution and sky plot](docs/img/gnss-skyplot.png)

Note the distinction the **Signal Distribution** table makes: a satellite tracked on two frequencies counts **once** under Satellites and **once per band** under Signals. Getting this wrong is the classic way to end up reporting twice as many satellites as you really have.

The sky plot draws one marker per satellite (strongest band), filled when the satellite is used in the position solution. Below it are a carrier-to-noise chart and a C/N0-versus-elevation scatter, which is the standard way to spot a bad antenna, an obstruction or multipath.

![Carrier to noise](docs/img/gnss-signals.png)

### Base Mode

![Base mode](docs/img/base-mode.png)

Three panels matter here:

* **Position Mode** — Disabled, Survey-In (`$PQTMCFGSVIN` mode 1, with minimum duration and 3D accuracy limit) or Fixed (mode 2). Fixed coordinates can be entered as ECEF or as latitude/longitude/height, which the device converts.
* **Current Module Configuration** — shows **operating state** separately from **configured mode**. This matters: the LC29H switches to the surveyed coordinates by itself when the survey completes but leaves its configured mode at "survey-in", so reading the configuration alone will tell you it is still surveying forever. Survey-in progress, observation count and mean accuracy come live from `$PQTMSVINSTATUS`.
* **Broadcast Station Position (RTCM 1005)** — decoded straight out of the stream the rovers receive. This, not the configuration, is the definitive answer to "which coordinate am I actually sending". When the survey completes you can promote its result into fixed mode with one button.

**Antenna Reference Point.** RTCM 1005 broadcasts the antenna reference point. If you enter the coordinates of a surveyed ground mark, set the marker → ARP offset (usually just the antenna height) or every rover position will be off by that amount. Leave it at zero when the coordinates already refer to the antenna, as they do for a survey-in result.

**Position Averaging** averages the receiver's own fix on the ESP32 and reports the spread. It is a sanity check, not a substitute for a surveyed coordinate: it converges on the receiver's systematic error, not on the truth.

### Data Output

![Data output](docs/img/data-output.png)

| Transport | Port | Notes |
|---|---|---|
| Raw TCP | 2101 | connect and listen; nothing to send |
| NTRIP caster | 2101 | same port, auto-detected |
| UDP | 2102 | fixed destination, broadcast, or registration |
| NTRIP push | outbound | this base uploads to a remote caster |

**NTRIP caster.** A connection that starts with an HTTP `GET` is answered as NTRIP — `ICY 200 OK` for v1, a proper HTTP response for clients that send `Ntrip-Version: Ntrip/2.0`. `GET /` returns the source table. Anything else is served the raw stream, so existing rover setups keep working unchanged. Optional Basic authentication.

```
NTRIP URL:  http://192.168.4.1:2101/RTK
Raw TCP:    192.168.4.1 port 2101
```

**UDP** is the lowest-latency option: no retransmission means a lost frame costs one epoch instead of stalling the stream behind a TCP retransmit. One datagram carries exactly one RTCM frame. There are three ways to receive it:

* **Fixed destination** — enter the receiver's address and listen port. Plain unicast, and the only option that also reaches a socket which has *connected* to the device. This is what to use with Mission Planner.
* **Broadcast** — reaches anything on the network bound to the listen port with no configuration, but only if that socket is left unconnected. It also goes out unacknowledged at the lowest WiFi rate, so prefer the fixed destination when you know the address.
* **Registration** — the rover sends any datagram to the port and repeats it every 30 s; its source address then receives the stream.

> **Mission Planner:** use **UDP Host** (not UDP Client) on port 2102 with broadcast enabled, or give the fixed destination its address and listen port. A UDP *client* socket calls `connect()`, and a connected UDP socket does not accept broadcast datagrams — that is standard socket behaviour, not a quirk of this firmware.

**NTRIP push** connects outbound to a caster such as RTK2go and uploads with the NTRIP v1 `SOURCE` handshake. It needs a WiFi uplink; the access point alone has no route off the device. Reconnection backs off from 5 s to 60 s so a wrong mountpoint does not hammer someone else's server.

The **RTCM Messages** table lists every message type actually being forwarded with its interval and worst jitter. A 1077 whose spacing drifts from 1.00 s is a base whose rovers will start reporting stale corrections.

### Network

![Network](docs/img/network.png)

Access point SSID, password, channel and hidden flag, plus the optional WiFi uplink with a network scan.

The access point is the rover's data path and stays up in every state. Going through a router instead costs a second wireless hop for every correction frame, which is exactly the latency this base is trying to avoid. Pick a channel that is quiet where you fly — a congested channel is the most common cause of correction latency spikes.

### Terminal

A serial terminal to the module. Commands starting with `$` get an NMEA checksum appended automatically. The periodic status messages (`$PQTMSVINSTATUS`, `$PAIRSPF`) are hidden by default so your own command replies stay readable; a checkbox brings them back.

### Admin

Module and firmware versions, uptime, free heap, CPU load per core, PPS status, reboot.

---

## Ionosphere monitor

The panel on the Overview page shows where each satellite's ray crosses the ionosphere, on a fixed ±10° geographic grid centred on the station, coloured by how much the vertical delay has **changed** since that satellite's arc began.

It is built from the dual-frequency observations the module already broadcasts: MSM7 messages are decoded on the device, and the geometry-free combination gives an ionospheric delay per satellite. The module tracks GPS L1+L5, Galileo E1+E5a and BeiDou B1I+B2a, which yields roughly 20 usable arcs.

**Read the honest limits before you trust it:**

* The **change** is real. It comes from the carrier phase, is precise to millimetres, and the hardware biases cancel in a difference. Cycle slips are caught using the receiver's own lock-time and half-cycle indicators rather than a magnitude threshold, which is what makes the numbers stable.
* The **absolute** delay is measured but left uncalibrated. On this receiver the differential code bias is large enough to reverse its sign, so the absolute value is displayed for reference only.
* **Nothing is interpolated.** A cell is filled only where a measurement actually falls. With roughly twenty pierce points and a per-satellite phase reference there is no honest way to fill the gaps, so they are left empty.

This is not the same thing as the SBAS ionosphere grid you may have seen on a Septentrio receiver. That grid is decoded from EGNOS messages that were already calibrated by a network of ground stations; the LC29H (BS) does not output raw SBAS data, so it cannot be reproduced here. What this panel shows is a single-station relative monitor, which is a different and more modest instrument.

---

## Module commands used

Everything in the *LC29H (BS) GNSS Protocol Specification v1.0* is exercised:

| Command | Use |
|---|---|
| `$PQTMCFGSVIN` | survey-in / fixed / disabled receiver mode |
| `$PQTMSAVEPAR` | persist settings to module NVM |
| `$PQTMRESTOREPAR` | restore module defaults |
| `$PQTMVERNO` | firmware version |
| `$PAIR432` / `433` | RTCM output mode (MSM7, MSM4, off) |
| `$PAIR434` / `435` | RTCM 1005 station ARP output |
| `$PAIR436` / `437` | ephemeris output (1019/1020/1042/1044/1046) |

Three more come from the wider LC29H series specification rather than the (BS) document, so they are **probed at boot** and the interface silently falls back if the firmware does not answer:

| Command | Use |
|---|---|
| `$PQTMCFGMSGRATE` + `$PQTMSVINSTATUS` | live survey-in progress and mean accuracy |
| `$PAIR391` → `$PAIRSPF` / `$PAIRSPF5` | L1 and L5 interference status |
| `$PAIR062` | NMEA sentence selection |

Both optional features were confirmed working on firmware `LC29HBSNR11A01S`.

Survey-in and fixed settings live in the module's own non-volatile memory and are written only when you press Save, so a reboot never silently restarts a survey. RTCM output settings are stored on the ESP32 and re-applied at boot.

---

## Testing without hardware

`tools/mock_ui.py` runs the entire web interface on a PC. It extracts the page from `src/web/WebUI.h`, serves it, and fakes the device behind it: a WebSocket pushing synthetic telemetry once a second plus all the `/api/*` endpoints, so every page and form can be exercised end to end. Standard library only, no dependencies.

```bash
python3 tools/mock_ui.py                     # http://localhost:8080
python3 tools/mock_ui.py --scenario fixed    # survey | fixed | nofix | cold
```

The screenshots in this README were taken from it. Restart the script after editing the page — it is read once at startup.

---

## How it is put together

```
CORE 1 (loopTask)                          CORE 0 (NetworkTask, 20 ms)
 UART read, 256 B at a time                 WiFi state machine, OTA
 RTCM 3 reassembly + CRC-24Q                NTRIP push state machine
 RTCM 1005 / MSM7 decode                    telemetry JSON at 1 Hz
 fan-out: TCP, NTRIP, UDP                   WebSocket queue drain
 NMEA parse: GGA, GSA, GSV                  AsyncTCP (HTTP / WS)
```

Core 1 is kept for the correction path and nothing else. Client housekeeping — accepting connections, NTRIP handshakes, UDP registration — is polled at 20 ms rather than every loop, because each of those calls is an lwIP socket operation and at 1 ms they burned thousands of syscalls a second on the core that has to stay deterministic. The RTCM send path itself is never throttled.

Shared state is guarded by three mutexes (`dataMutex` for the satellite and position snapshot, `tcpMutex` for the output sockets, `baseMutex` for the module configuration mirror). Sentence parsers write to staging variables owned by core 1 and publish under lock, so no lock is taken in the byte loop.

Measured on an ESP32-WROOM-32D with the module tracking 39 satellites: **2 % load on each core**, 21 % of RAM, 78 % of the 1.25 MB application partition. Correction throughput was 11 RTCM frames per second at about 1.1 kB/s with zero CRC errors.

```
src/
  core/main.cpp            setup order and task creation
  gnss/GNSS_Core.cpp       satellite classification, NMEA helpers, module config
  gnss/GNSS_Processor.cpp  serial loop, RTCM framing, sentence dispatch
  gnss/BaseConfig.cpp      survey-in / fixed, ARP offset, position averaging
  gnss/Iono.cpp            MSM7 decode and ionospheric monitor
  network/NetworkManager.cpp  AP + optional station, OTA
  network/DataOutput.cpp   TCP raw, NTRIP caster, UDP
  network/NtripPush.cpp    outbound NTRIP server
  web/WebServerManager.cpp HTTP endpoints and telemetry
  web/WebUI.h              the entire interface, served from flash
tools/mock_ui.py           run the interface without hardware
```

---

## Known limits

* **No authentication on the web interface.** Anyone on the network can reach the configuration pages and the serial terminal. Set an access point password and treat the AP as the trust boundary.
* **The soft-AP is open by default.** Change it on the Network tab before deploying anywhere real.
* **Survey-in is not a survey.** A few hours of survey-in gives roughly decimetre absolute accuracy. For centimetre work, log raw data and post-process, then enter the result in Fixed mode.
* **GLONASS is single-frequency** on this module, so it contributes no ionospheric measurements.
* **Six simultaneous TCP/NTRIP consumers and six UDP subscribers.** Beyond that the caster answers `503`.

---

## Credits

Built around the Quectel LC29H (BS) and its protocol specification. RTCM 3 framing follows RTCM 10403.3; the NTRIP caster implements NTRIP v1 with v2 responses where the client asks for them.
