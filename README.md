<h1>ESP32-RTK-BASE</h1>
ESP32-based RTK (Real-Time Kinematic) base station for GNSS correction data streaming over TCP.

___
<h2>Overview</h2>
This project turns an ESP32 into a networked RTK base station that receives RTCM3 and NMEA messages from a multi-constellation GNSS receiver, parses satellite tracking data, and broadcasts raw RTCM streams to up to 3 connected rovers over TCP port 2101. A web interface provides real-time telemetry (position, fix quality, sky view, CPU load) via WebSocket, and the device manages its own WiFi connection with automatic AP fallback for recovery.

___
<h2>Features</h2>
<h3>RTCM3 pass-through streaming</h3>  Raw RTCM data forwarded to up to 3 TCP clients on port 2101.
<h3>Multi-constellation NMEA parsing</h3>  Tracks GPS, GLONASS, Galileo, BeiDou, QZSS, NavIC, and SBAS satellites with signal band identification (L1/L5, E1/E5a, B1/B2a).
<h3>Real-time web telemetry dashboard</h3>  WebSocket-based live feed of position, HDOP, fix quality, satellite counts by constellation/band, sky view diagram data, CPU utilization per core, active TCP client count, and PPS status.
<h3>GNSS module configuration</h3>  Automated multi-band/multi-system initialization via $PAIR/$PQTM commands to Quectel-style modules.
<h3>WiFi state machine with AP recovery</h3>  Connects to a saved SSID; falls back to "ESP32_RTK_BASE" access point if connection fails or is lost, with OTA and web-based credential entry.
<h3>Dual-core task architecture</h3>  Core 1 handles GNSS I/O; Core 0 handles networking, web server, and telemetry at ~20 ms intervals.

___
<h2>Hardware Requirements</h2>
<h3>ESP32 Dev Board</h3>
Any ESP32 with dual-core
<h3>GNSS Module</h3>
Waveshare LC29H or RTK capable GNSS Module with NMEA and RTCM output

___
<h2>Wiring / Circuit Setup</h2>
<img width="616" height="810" alt="ESP32-RTK-DIAGRAM" src="https://github.com/user-attachments/assets/40c695e6-1b1a-47fd-8738-563983d75720" />

Handled by the user. Key conenctions:
<h3>GNSS TX => ESP32 RXD2 (GPIO16)</h3>
<h3>GNSS RX <= ESP32 TXD2 (GPIO 17)</h3>
<h3>GNSS PPS => ESP32 (GPIO 27)</h3>

___
<h2>Software Requirements</h2>

|         Item         | Version / Source              |
|----------------------|-------------------------------|
| Framework            | Arduino (via PlatformIO)      |
| PlatformIO Core      | Latest stable                 |
| Espressif32 Platform | espressif32@6.5.0             |
| ArduinoJson          | v7.0.4                        |
| ESPAsyncWebServe     | v3.3.23 (by mathieucarbou)    |
| AsyncTCP             | Included by ESPAsyncWebServer |
| TinyGPSPlus          | v1.0.3                        |

___
<h2>Installation Steps:</h2>
<h3>1) Clone the repository</h3>
git clone https://github.com/aryesil/RTK-BASE-ESP32

```text
cd esp32-rtk-base
```
<h3>2) Install PlatformIO</h3> (if not already installed):
Via VS Code: Install the "PlatformIO IDE" extension.
Or via CLI: 

```text
pip install platformio
```
<h3>3) Open in your editor</h3>

```text
code .
```
<h3>4) Select the environment</h3>
The default is [env:esp32-dev], which targets a standard ESP32-DevKitC. Adjust platformio.ini if using a different board.

<h3>5) Build and upload</h3>

```text
pio run --target upload
```
Or use the PlatformIO toolbar buttons: Upload.

<h3>6) Open the serial monitor</h3>
at 115200 baud to observe boot messages, GNSS configuration output, and WiFi status.

___
<h2>Configuration</h2>
<h3>WiFi Credentials</h3>
The device stores its last-known SSID and password in ESP32 NVS under the namespace wifi_creds. On first boot (or if no credentials exist), it starts an access point named ESP32_RTK_BASE.

To set Wifi credentials:
1) Connect to the ESP32_RTK_BASE AP with any device.
2) Open a browser and navigate to the ESP32's AP IP address (printed on serial).
3) Use the web interface to scan for nearby networks, select one, enter its password, and submit.
4) The device will attempt to connect in STA+AP mode. If successful, it prints the assigned IP on serial and stores credentials persistently.

To reset Wifi:
Press "Select Another WiFi" on the connected-success page, or call GET /resetwifi. This erases stored credentials and returns the device to AP-only mode.

___
<h2>GNSS Module Configuration</h2>
The following commands are sent during boot via Serial2 (UART):

### Check firmware version

```text
$PQTMVERNO*
```

### Enable RTCM output

```text
$PAIR432,1*    # Enable MSM7 observations (1077/1087/1097/1117/1127)
$PAIR434,1*    # Enable RTCM 1005 ARP message
$PAIR436,1*    # Enable RTCM ephemeris messages
```

### Enable NMEA output messages
```text
$PAIR062,0,1*    # GGA - Time, position and fix information
$PAIR062,1,1*    # GLL - Geographic position
$PAIR062,2,1*    # GSA - DOP and active satellites
$PAIR062,3,1*    # GSV - Satellites in view
$PAIR062,4,1*    # RMC - Recommended minimum navigation data
$PAIR062,5,1*    # VTG - Course and speed over ground (Not Enabled due RTK Base operation)
$PAIR062,6,1*    # ZDA - UTC date and time
$PAIR062,7,1*    # GRS - Range residuals
$PAIR062,8,1*    # GST - Position error statistics
```

### Save configuration

```text
$PQTMSAVEPAR*
```

### Start Survey-In

```text
$PQTMCFGSVIN,W,1,60,10,0,0,0*
```

These are designed for Quectel LC29H(BS) module. If your GNSS receiver uses a different command set, edit the initCommands[] array in src/gnss/GNSS_Core.cpp.

___
<h2>Pin Assignments</h2> (hardcoded in include/Config.h)

| Constant    | Value    | Purpose                            |
| ----------- | -------- | ---------------------------------- |
| `RXD2`      | `16`     | GNSS TX → ESP32 UART2 RX           |
| `TXD2`      | `17`     | ESP32 UART2 TX → GNSS RX           |
| `GNSS_BAUD` | `115200` | Serial2 baud rate                  |
| `PPS_PIN`   | `27`     | PPS interrupt input                |
| `RTCM_PORT` | `2101`   | TCP listening port for RTCM stream |

<h2>Other Defaults</h2>
<h3>Max TCP clients:</h3> 3 (defined in src/Globals.cpp
<h3>Max tracked satellites:</h3> 150 (MAX_SATS in Config.h)
<h3>NMEA buffer size:</h3> 256 chars (MAX_NMEA in Config.h)
<h3>Serial2 RX buffer:</h3> 2048 bytes (set at runtime in setupGNSS())

___
<h2>Usage</h2>
<h3>Boot Sequence</h3>

1) Device initializes hardware, UART2, and PPS interrupt.
2) Sends GNSS configuration commands and waits for ACK on each.
3) Starts WiFi (STA or AP depending on saved credentials).
4) Starts web server on port 80 and RTCM TCP listener on port 2101.
5) Main loop reads from Serial2, parses NMEA/RTCM, and broadcasts data.

### Expected Serial Output at Boot

```text
=== GNSS CONFIGURATION STARTING ===
[GNSS-TX] $PAIR062,0,1*3F
[GNSS-RX] ACKNOWLEDGED: ...
...
=== GNSS CONFIGURATION COMPLETED ===

Connecting to saved network: MY_WIFI_SSID
WiFi Settings Page IP Address (AP Mode): 192.168.4.1
[WIFI] Connected! STA IP: 192.168.1.50
```

___
<h2>RTCM TCP Stream</h2>
Connect a rover or RTCM client to the ESP32's IP on port 2101. The device streams raw bytes read from the GNSS module with no filtering — both RTCM3 frames and NMEA lines are forwarded. Up to 3 simultaneous connections are supported; new connections are rejected when all slots are occupied.

___
<h2>Web Interface</h2>
Open a browser at http://<ESP32-IP>/ (or 192.168.4.1 in AP mode). The dashboard displays:

- Latitude, longitude, altitude, HDOP
- Fix quality and mode (NO FIX / 2D / 3D / DGNSS)
- Satellite counts per constellation and band (GPS L1/L5, GLONASS L1, Galileo E1/E5a, BeiDou B1/B2a, QZSS L1/L5, NavIC L5, SBAS L1)
Sky view data (system, PRN, elevation, azimuth, SNR for primary-band satellites)
- Active TCP client count
- RTCM packet rate (packets per second, counted on Core 1)
- CPU utilization per core (%)
- PPS active status
- Terminal

___
<h2>GNSS Command via Web UI</h2>
Send custom NMEA or Quectel bianry commands to the GNSS module through WebUI terminal, checksum will be automatically added to the end of command.

___
<h2>WebUI Telemetry Screen</h2>

<img width="1914" height="910" alt="WebUI" src="https://github.com/user-attachments/assets/6b16723d-665d-4f40-893b-8e4975fcd039" />

___
<h2>Project Structure</h2>

````markdown
├── platformio.ini              # Build configuration (env: esp32-dev)
├── include/
│   ├── Config.h                # Pin definitions, baud rate, RTCM port, constants
│   └── Globals.h               # Global variables, objects and FreeRTOS handles
├── src/
│   ├── CMakeLists.txt
│   ├── Globals.cpp             # Global variable instantiations
│   ├── core/
│   │   └── main.cpp            # setup()/loop(), task creation and CPU pinning
│   ├── gnss/
│   │   ├── GNSS_Core.h         # Satellite tracking, checksum validation, command handling
│   │   ├── GNSS_Core.cpp       # GSV parsing, satellite management and GNSS configuration
│   │   ├── GNSS_Processor.h    # setupGNSS(), runGNSSProcessing()
│   │   └── GNSS_Processor.cpp  # Serial2 polling, RTCM state machine, NMEA parsing and GPS updates
│   ├── network/
│   │   ├── NetworkManager.h    # Network setup and state management
│   │   ├── NetworkManager.cpp  # WiFi state machine, OTA handling and credential persistence
│   │   ├── RTCMSocket.h        # RTCM socket API
│   │   └── RTCMSocket.cpp      # TCP RTCM server and client broadcast handling
│   ├── system/
│   │   ├── SystemManager.h     # Hardware initialization and PPS ISR
│   │   └── SystemManager.cpp   # UART setup, semaphores, queues and interrupt handling
│   └── web/
│       ├── WebServerManager.h  # Web server and telemetry interfaces
│       └── WebServerManager.cpp# HTTP routes, WebSocket handling and telemetry JSON generation

````

## License

This project is licensed under the MIT License.
See `LICENSE` for details.


