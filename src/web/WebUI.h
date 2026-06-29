#pragma once
#include <Arduino.h>

const char wifi_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 RTK - WiFi Setup</title>
    <style>
        body { background-color: #121212; color: #00ffcc; font-family: 'Courier New', Courier, monospace; text-align: center; margin: 0; padding: 20px; }
        .card { background: #1e1e1e; padding: 20px; border-radius: 8px; border: 1px solid #333; display: inline-block; text-align: left; max-width: 400px; width: 100%; box-sizing: border-box; }
        h2 { color: #fff; text-align: center; border-bottom: 1px solid #444; padding-bottom: 10px; margin-top: 0; }
        label { color: #aaa; font-size: 12px; display: block; margin-top: 10px; }
        select, input { width: 100%; padding: 10px; margin: 5px 0 15px 0; background: #000; color: #00ffcc; border: 1px solid #444; border-radius: 4px; box-sizing: border-box; font-family: monospace; }
        button { width: 100%; padding: 12px; background: #00ffcc; color: #000; border: none; border-radius: 4px; font-weight: bold; cursor: pointer; margin-bottom: 10px; }
        button:hover { background: #00ccaa; }
        .scan-btn { background: #444; color: #fff; }
        .scan-btn:hover { background: #555; }
        #status { text-align: center; color: #ffdd00; font-weight: bold; margin-top: 15px; font-size: 14px;}
    </style>
</head>
<body>
    <div class="card" id="mainCard">
        <h2>📡 WiFi Setup</h2>
        <button class="scan-btn" onclick="scanNetworks()">Scan Networks</button>
        
        <label for="networks">Found Networks:</label>
        <select id="networks" onchange="document.getElementById('ssid').value = this.value;">
            <option value="">Scan first...</option>
        </select>
        
        <label for="ssid">SSID (Network Name):</label>
        <input type="text" id="ssid" placeholder="Enter network name or select from list">
        
        <label for="pass">Password:</label>
        <input type="password" id="pass" placeholder="Network password">
        
        <button onclick="connectWiFi()">Connect to Network</button>
        <div id="status"></div>
    </div>

    <script>
        function scanNetworks() {
            document.getElementById('networks').innerHTML = '<option value="">Scanning... Please wait.</option>';
            document.getElementById('status').innerText = "Scanning for nearby networks...";
            fetch('/scan').then(response => response.json()).then(data => {
                let options = '<option value="">Select a network...</option>';
                data.forEach(network => {
                    options += `<option value="${network}">${network}</option>`;
                });
                document.getElementById('networks').innerHTML = options;
                document.getElementById('status').innerText = "Scan complete.";
            }).catch(() => {
                document.getElementById('status').innerText = "Scan error!";
                document.getElementById('status').style.color = "#ff3333";
            });
        }

        function checkStatus() {
            fetch('/status').then(r => r.json()).then(data => {
                if (data.state === 2) { 
                    // NET_SHOW_IP State: Connection successful, show IP from its own broadcast
                    document.body.innerHTML = "<div class='card' style='text-align:center;'><h2> Connected Successfully!</h2><p>ESP32 received the following IP address from the network:</p><h1 style='color:#fff;'>" + data.ip + "</h1><p style='color:#aaa; font-size:13px;'>Please go to this new IP address in your browser. The ESP32 will turn off its own access point in 1 minute and switch to normal mode.</p></div>";
                } else if (data.state === 1) { 
                    // NET_CONNECTING State: Still trying to connect
                    document.getElementById('status').innerText = "Connecting to network, please wait...";
                    setTimeout(checkStatus, 2000); // Ask again after 2 seconds
                } else if (data.state === 0) { 
                    // NET_AP State: Connection lost or wrong password
                    document.getElementById('status').innerText = "Connection failed! Please check the password.";
                    document.getElementById('status').style.color = "#ff3333";
                }
            }).catch(() => {
                 setTimeout(checkStatus, 2000);
            });
        }

        function connectWiFi() {
            let ssid = document.getElementById('ssid').value.trim();
            let pass = document.getElementById('pass').value.trim();
            if (!ssid) {
                alert("Please enter a network name (SSID).");
                return;
            }
            document.getElementById('status').innerText = "Connection request sent. Waiting...";
            document.getElementById('status').style.color = "#33ff33";
            
            fetch(`/connect?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`)
            .then(() => {
                // Request went successfully, now poll the device in the background until it gets an IP
                setTimeout(checkStatus, 2000);
            }).catch(() => {
                document.getElementById('status').innerText = "Error occurred, device unreachable!";
                document.getElementById('status').style.color = "#ff3333";
            });
        }
    </script>
</body>
</html>
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 - RTK Telemetry</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <style>
        body { background-color: #121212; color: #00ffcc; font-family: 'Courier New', Courier, monospace; margin: 0; padding: 10px; }
        h2 { text-align: center; color: #fff; margin-bottom: 5px; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        @media (max-width: 768px) { .grid { grid-template-columns: 1fr; } }
        .card { background: #1e1e1e; padding: 15px; border-radius: 8px; border: 1px solid #333; position: relative; }
        .card h3 { margin-top: 0; color: #aaa; font-size: 14px; border-bottom: 1px solid #444; padding-bottom: 5px;}
        .value { font-size: 18px; color: #fff; font-weight: bold; }
        .alert { color: #ff3333; }
        .good { color: #33ff33; }
        .sat-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px;}
        .sat-box { background: #2a2a2a; padding: 6px; border-radius: 6px; display: flex; flex-direction: column; border: 1px solid #333;}
        .sat-box-title { font-size: 12px; color: #aaa; border-bottom: 1px solid #444; padding-bottom: 3px; margin-bottom: 4px; font-weight: bold; text-align: center;}
        .sat-sig-row { display: flex; justify-content: space-between; font-size: 11px; color: #888; padding: 2px 0;}
        .sat-sig-row span.val { color: #00ffcc; font-weight: bold; }
        #map { height: 250px; border-radius: 8px; margin-top: 10px; z-index: 1;}
        canvas { background: #1a1a1a; border-radius: 50%; display: block; margin: 0 auto; border: 2px solid #333;}
        #terminal { height: 110px; overflow-y: auto; background: #000; color: #00ffcc; padding: 8px; border: 1px solid #444; border-radius: 4px; font-size: 12px; margin-top: 5px; font-family: monospace; }
        .term-line { border-bottom: 1px dashed #222; padding-bottom: 3px; margin-bottom: 3px; word-wrap: break-word;}
    </style>
</head>
<body>
    <h2>📡 ESP32 - RTK BASE</h2>
    <div class="grid">
        <div class="card">
            <h3>🌐 SYSTEM & LOCATION STATUS</h3>
            <div>Latitude: <span id="lat" class="value">Waiting...</span></div>
            <div>Longitude: <span id="lon" class="value">Waiting...</span></div>
            <div>Altitude: <span id="alt" class="value">0.0</span> m</div>
            <div>HDOP: <span id="hdop" class="value">0.0</span></div>
            <hr style="border: 0; border-top: 1px solid #444; margin: 10px 0;">
            <div>Fix Mode: <span id="f_mode" class="value alert">NO FIX</span></div>
            <div>Fix Quality: <span id="f_qual" class="value alert" style="font-size: 16px;">INVALID</span></div>
            <hr style="border: 0; border-top: 1px solid #444; margin: 10px 0;">
            <div>PPS Status: <span id="pps_status" class="value alert">NO LOCK</span></div>
            <div>Satellite Time (UTC): <span id="sat_time" class="value" style="color:#aaffaa">--:--:--</span></div>
            <div>RTCM3 Stream: <span id="rtcm" class="value">0</span> pkt/sec</div>
            <div>TCP Broadcast (Port 2101): <span id="tcp_clients" class="value" style="color:#00ffcc">0</span> Clients</div>
            <div id="map"></div>
            <h3 style="margin-top: 15px;">⌨️ SERIAL PORT TERMINAL</h3>
            <div style="display: flex; gap: 5px;">
                <input type="text" id="cmdInput" placeholder="$PQTMCFGSVIN..." style="flex: 1; padding: 5px; border-radius: 4px; border: 1px solid #444; background: #000; color: #00ffcc; font-family: monospace;">
                <button onclick="sendCommand()" style="padding: 5px 15px; background: #00ffcc; color: #000; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;">SEND</button>
            </div>
            <div id="terminal"></div>
            <p style="font-size: 10px; color: #888; margin-top: 5px; margin-bottom: 0;">* Commands starting with ‘$’ are automatically appended with a checksum.</p>
        </div>

        <div class="card">
            <h3>🛰️ ACTIVE SIGNAL DISTRIBUTION (Total: <span id="total_sats">0</span> Signals)</h3>
            <div class="sat-grid">
                <div class="sat-box">
                    <div class="sat-box-title">GPS</div>
                    <div class="sat-sig-row"><span>L1 C/A:</span><span class="val" id="gps-l1">0</span></div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="gps-l5">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">GLONASS</div>
                    <div class="sat-sig-row"><span>L1/G1:</span><span class="val" id="glo-l1">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">GALILEO</div>
                    <div class="sat-sig-row"><span>E1:</span><span class="val" id="gal-e1">0</span></div>
                    <div class="sat-sig-row"><span>E5a:</span><span class="val" id="gal-e5a">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">BDS</div>
                    <div class="sat-sig-row"><span>B1I:</span><span class="val" id="bei-b1">0</span></div>
                    <div class="sat-sig-row"><span>B2a:</span><span class="val" id="bei-b2a">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">QZSS</div>
                    <div class="sat-sig-row"><span>L1 C/A:</span><span class="val" id="qzs-l1">0</span></div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="qzs-l5">0</span></div>
                </div>
                <div class="sat-box">
                    <div class="sat-box-title">NAVIC</div>
                    <div class="sat-sig-row"><span>L5:</span><span class="val" id="nav-l5">0</span></div>
                </div>
                <div class="sat-box" style="border-color: #4169E1;">
                    <div class="sat-box-title" style="color: #66aaff;">SBAS (EGNOS)</div>
                    <div class="sat-sig-row"><span>L1:</span><span class="val" id="sba-l1" style="color:#ffffff;">0</span></div>
                </div>
            </div>
            
            <div style="text-align: right; font-size: 11px; color: #888; margin-top: 5px; padding-right: 5px;">
                CPU Load -> C0(Network): <span id="cpu0_usage" style="color: #00ffcc; font-weight: bold;">0%</span> | C1(GNSS): <span id="cpu1_usage" style="color: #00ffcc; font-weight: bold;">0%</span>
            </div>

            <h3 style="margin-top: 15px;">🌌 SKYVIEW (Live Radar)</h3>
            <canvas id="skyview" width="500" height="500"></canvas>
        </div>
    </div>

    <script>
        var map = L.map('map').setView([41.0, 28.9], 2);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
        var marker = L.marker([41.0, 28.9]).addTo(map);
        var firstLock = false;

        var flags = {};
        var flagUrls = {
            "GP": "https://flagcdn.com/w40/us.png", "GL": "https://flagcdn.com/w40/ru.png", 
            "GA": "https://flagcdn.com/w40/eu.png", "GB": "https://flagcdn.com/w40/cn.png", 
            "GI": "https://flagcdn.com/w40/in.png", "GQ": "https://flagcdn.com/w40/jp.png",
            "SB": "https://flagcdn.com/w40/eu.png"
        };
        for(let key in flagUrls){ let img = new Image(); img.src = flagUrls[key]; flags[key] = img; }

        function drawSkyview(skyData) {
            var canvas = document.getElementById("skyview");
            var ctx = canvas.getContext("2d");
            var cx = canvas.width / 2; var cy = canvas.height / 2; var r = cx - 25; 
            
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = "#444"; ctx.lineWidth = 1;
            [r, r*0.66, r*0.33].forEach(rad => { ctx.beginPath(); ctx.arc(cx, cy, rad, 0, 2*Math.PI); ctx.stroke(); });
            ctx.beginPath(); ctx.moveTo(cx, cy-r); ctx.lineTo(cx, cy+r); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(cx-r, cy); ctx.lineTo(cx+r, cy); ctx.stroke();

            if(!skyData) return;

            skyData.forEach(sat => {
                var satR = r * (1 - (sat.e / 90.0));
                var rad = (sat.a - 90) * Math.PI / 180.0;
                var x = cx + satR * Math.cos(rad);
                var y = cy + satR * Math.sin(rad);

                var balonYaricap = 10; 
                
                var glowColor = sat.s === "SB" ? "rgba(65, 105, 225, 0.9)" : 
                                sat.sn > 35 ? "rgba(0, 255, 0, 0.7)" : 
                                sat.sn > 25 ? "rgba(255, 255, 0, 0.7)" : "rgba(255, 0, 0, 0.7)"; 

                ctx.save(); ctx.shadowBlur = (sat.sn / 2) + 5; ctx.shadowColor = glowColor; ctx.fillStyle = glowColor;
                ctx.beginPath(); ctx.arc(x, y, balonYaricap + 2, 0, 2*Math.PI); ctx.fill(); ctx.restore(); 

                ctx.save(); ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.clip(); 
                if(flags[sat.s] && flags[sat.s].complete) {
                    ctx.drawImage(flags[sat.s], x - balonYaricap, y - balonYaricap, balonYaricap*2, balonYaricap*2);
                } else { ctx.fillStyle = "#888"; ctx.fill(); }
                ctx.restore(); 

                ctx.beginPath(); ctx.arc(x, y, balonYaricap, 0, 2*Math.PI); ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5; ctx.stroke();

                var sysPrefix = sat.s === "GP" ? "G" : sat.s === "GL" ? "R" : sat.s === "GA" ? "E" : sat.s === "GB" ? "B" : sat.s === "GI" ? "I" : sat.s === "GQ" ? "Q" : sat.s === "SB" ? "S" : "U";
                ctx.fillStyle = "#eee"; ctx.font = "bold 11px Arial"; ctx.textAlign = "left";
                ctx.fillText(sysPrefix + sat.id, x + balonYaricap + 4, y + 4);
            });
        }
        drawSkyview();

        var gateway = `ws://${window.location.hostname}/ws`;
        var websocket;
        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onopen = function(event) { 
                logTerminal("<span style='color:#33ff33;'>[SYSTEM] ESP32 Connection Established.</span>"); 
                
                setTimeout(function() {
                    let versiyonKomutu = "$PQTMVERNO*58";
                    websocket.send(versiyonKomutu); 
                    logTerminal("<span style='color:#fff; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + versiyonKomutu + "</span>");
                }, 1000);
            };
            websocket.onclose = function(event) { logTerminal("<span style='color:#ffaa00;'>[SYSTEM] Connection Lost! Reconnecting...</span>"); setTimeout(initWebSocket, 2000); };
            websocket.onmessage = onMessage;
        }

        function sendCommand() {
            let cmdInput = document.getElementById('cmdInput');
            let cmd = cmdInput.value.trim();
            if (cmd) {
                let finalCmd = cmd;
                if (cmd.startsWith('$') && !cmd.includes('*')) {
                    let checksum = 0;
                    for (let i = 1; i < cmd.length; i++) checksum ^= cmd.charCodeAt(i);
                    let hexCS = checksum.toString(16).toUpperCase().padStart(2, '0');
                    finalCmd = cmd + '*' + hexCS;
                }
                fetch('/cmd?c=' + encodeURIComponent(finalCmd))
                .then(response => { if(response.ok) logTerminal("<span style='color:#33ff33; font-weight:bold;'>TX:</span> <span style='color:#00ffcc;'>" + finalCmd + "</span>"); })
                .catch(error => { logTerminal("<span style='color:#ff3333;'>ERROR: Module unreachable!</span>"); });
                cmdInput.value = ""; 
            }
        }
        document.getElementById("cmdInput").addEventListener("keyup", function(event) { if (event.key === "Enter") sendCommand(); });

        function logTerminal(msg) {
            var term = document.getElementById('terminal');
            var timeStr = new Date().toLocaleTimeString('en-US', { hour12: false });
            term.innerHTML += "<div class='term-line'><span style='color:#888; font-size:10px;'>[" + timeStr + "]</span> " + msg + "</div>";
            
            while(term.children.length > 100) {
                term.removeChild(term.firstChild);
            }
            term.scrollTop = term.scrollHeight; 
        }

        function onMessage(event) {
            if (typeof event.data === "string" && event.data.startsWith("TERM:")) {
                let msg = event.data.substring(5); 
                logTerminal("<span style='color:#ff3333; font-weight:bold;'>RX:</span> <span style='color:#fff;'>" + msg + "</span>");
                return; 
            }
            var data;
            try { data = JSON.parse(event.data); } catch(e) { return; }

            if (data.lat !== undefined) {
                document.getElementById('lat').innerText = data.lat.toFixed(6); 
                document.getElementById('lon').innerText = data.lon.toFixed(6);
                document.getElementById('alt').innerText = data.alt.toFixed(2); 
                document.getElementById('hdop').innerText = data.hdop.toFixed(2);
                document.getElementById('rtcm').innerText = data.rtcm; 
                document.getElementById('tcp_clients').innerText = data.tcp_clients;
                if(data.sat_time) document.getElementById('sat_time').innerText = data.sat_time;
                
                if(data.f_mode) {
                    var fModeEl = document.getElementById('f_mode');
                    fModeEl.innerText = data.f_mode;
                    fModeEl.className = (data.f_mode === "3D" || data.f_mode === "2D") ? "value good" : "value alert";

                    var fQualEl = document.getElementById('f_qual');
                    fQualEl.innerText = data.f_qual;
                    
                    if (data.f_qual.includes("RTK FIXED")) fQualEl.style.color = "#c688ff"; 
                    else if (data.f_qual.includes("RTK FLOAT") || data.f_qual.includes("DGNSS")) fQualEl.style.color = "#00ffcc";
                    else if (data.f_qual.includes("GPS")) fQualEl.style.color = "#ffdd00";
                    else fQualEl.style.color = "#ff3333";
                }

                if(data.cpu0 !== undefined && data.cpu1 !== undefined) {
                    var cpu0El = document.getElementById('cpu0_usage');
                    cpu0El.innerText = data.cpu0 + '%';
                    if (data.cpu0 > 80) cpu0El.style.color = "#ff3333"; 
                    else if (data.cpu0 > 50) cpu0El.style.color = "#ffdd00"; 
                    else cpu0El.style.color = "#00ffcc"; 

                    var cpu1El = document.getElementById('cpu1_usage');
                    cpu1El.innerText = data.cpu1 + '%';
                    if (data.cpu1 > 80) cpu1El.style.color = "#ff3333"; 
                    else if (data.cpu1 > 50) cpu1El.style.color = "#ffdd00"; 
                    else cpu1El.style.color = "#00ffcc"; 
                }

                var ppsEl = document.getElementById('pps_status');
                if(data.pps_active) {
                    ppsEl.innerText = "ACTIVE (LOCKED)"; ppsEl.className = "value good";
                } else {
                    ppsEl.innerText = "WAITING..."; ppsEl.className = "value alert";
                }

                let t = data.sats;
                document.getElementById('gps-l1').innerText = t.gps.L1;
                document.getElementById('gps-l5').innerText = t.gps.L5;
                document.getElementById('glo-l1').innerText = t.glo.L1;
                document.getElementById('gal-e1').innerText = t.gal.E1;
                document.getElementById('gal-e5a').innerText = t.gal.E5a;
                document.getElementById('bei-b1').innerText = t.bei.B1;
                document.getElementById('bei-b2a').innerText = t.bei.B2a;
                document.getElementById('qzs-l1').innerText = t.qzs.L1;
                document.getElementById('qzs-l5').innerText = t.qzs.L5;
                document.getElementById('nav-l5').innerText = t.nav.L5;
                
                document.getElementById('sba-l1').innerText = t.sba ? t.sba.L1 : 0;
                
                document.getElementById('total_sats').innerText = t.gps.L1 + t.gps.L5 + t.glo.L1 + t.gal.E1 + t.gal.E5a + t.bei.B1 + t.bei.B2a + t.qzs.L1 + t.qzs.L5 + t.nav.L5 + (t.sba ? t.sba.L1 : 0);
              
                if(data.lat !== 0.0 && data.lon !== 0.0) {
                    var newLatLng = new L.LatLng(data.lat, data.lon); marker.setLatLng(newLatLng);
                    if(!firstLock) { map.setView(newLatLng, 18); firstLock = true; }            
                }
                if(data.sky) drawSkyview(data.sky);
            }
        }
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";