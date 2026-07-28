#!/usr/bin/env python3
"""Run the web interface on a PC, without flashing an ESP32.

Extracts index_html from src/web/WebUI.h, serves it, and fakes the device
behind it: a WebSocket pushing synthetic telemetry once a second plus the
/api/* endpoints, so every page and form can be exercised end to end.

    python3 tools/mock_ui.py            # http://localhost:8080
    python3 tools/mock_ui.py --port 9000 --scenario fixed

Scenarios: survey (default), fixed, nofix, cold

?page=<tab>&scroll=<px> opens the page at a given tab and offset, which is how
the README screenshots are captured.

Stdlib only, no dependencies.
"""

import argparse
import base64
import hashlib
import json
import math
import os
import random
import re
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBUI = os.path.join(ROOT, "src", "web", "WebUI.h")
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

SYS_BANDS = [3, 2, 3, 3, 3, 1, 2]  # GPS GLO GAL BDS QZSS SBAS NavIC


def load_page():
    with open(WEBUI, encoding="utf-8") as fh:
        src = fh.read()
    m = re.search(r'const char index_html\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral";',
                  src, re.S)
    if not m:
        raise SystemExit("index_html not found in %s" % WEBUI)
    return m.group(1)


class Device:
    """Mutable state the fake endpoints write and the telemetry loop reads."""

    def __init__(self, scenario):
        self.lock = threading.Lock()
        self.t0 = time.time()
        self.scenario = scenario
        self.base = {"m": 1, "dur": 43200, "acc": 15.0, "x": 0.0, "y": 0.0, "z": 0.0,
                     "rtcm": 1, "arp": 1, "eph": 1, "el": 0,
                     "ver": "LC29HBSNR01A01S", "bd": "2022/08/31", "msg": ""}
        self.out = {"tcpEn": True, "tcpPort": 2101, "accept": 0, "udpEn": True,
                    "udpPort": 2102, "mount": "RTK", "user": "", "auth": False}
        self.ap = {"ssid": "ESP32_RTK_BASE", "ch": 6, "sec": False, "hide": False, "n": 2}
        self.net = 0
        self.sta_ssid = ""
        self.frames = 0
        self.svin_start = time.time()
        self.arp = {"n": 0.0, "e": 0.0, "u": 0.0}
        self.avg = {"run": False, "started": 0.0, "tgt": 300, "n": 0,
                    "have": False, "lat": 0.0, "lon": 0.0, "alt": 0.0, "rms": 0.0}
        self.push = {"en": False, "host": "", "port": 2101, "mount": "",
                     "st": 0, "msg": "Disabled", "sent": 0, "retry": 0, "since": 0.0}
        # "live" mimics firmware that emits $PQTMSVINSTATUS; "off" mimics one
        # that does not, so the UI fallback path can be exercised too.
        self.svin_live = scenario != "cold"
        self.jam_feat = 1 if scenario != "cold" else 2

        if scenario == "fixed":
            self.base.update(m=2, x=4121053.4321, y=2278251.9876, z=4162456.1234)
        elif scenario == "cold":
            self.base.update(m=0)

    def uptime(self):
        return int(time.time() - self.t0)

    def signals(self):
        if self.scenario in ("nofix", "cold"):
            plan = [(0, 1, 12, 123, [0], 0), (0, 17, 8, 15, [0], 0), (2, 13, 6, 205, [0], 0)]
        else:
            plan = [(0, 1, 45, 123, [0, 2], 1), (0, 3, 61, 210, [0, 2], 1),
                    (0, 6, 22, 45, [0], 1), (0, 14, 33, 300, [0, 2], 1),
                    (0, 17, 71, 15, [0], 0), (0, 22, 18, 260, [0], 1),
                    (0, 30, 12, 190, [0], 0),
                    (1, 65, 40, 88, [0], 1), (1, 72, 55, 315, [0], 1), (1, 80, 15, 120, [0], 0),
                    (2, 13, 66, 205, [0, 1], 1), (2, 21, 34, 160, [0, 1], 1), (2, 26, 25, 240, [0], 0),
                    (3, 24, 48, 275, [0, 1], 1), (3, 26, 20, 190, [0], 1), (3, 35, 70, 100, [0, 1], 0),
                    (4, 194, 52, 140, [0, 2], 0),
                    (5, 136, 28, 175, [0], 0), (5, 123, 31, 200, [0], 0)]
        out = []
        for si, prn, el, az, bands, used in plan:
            for b in bands:
                out.append([si, prn, el, az, random.randint(30, 50), b, used])
        return out

    def snapshot(self):
        with self.lock:
            base = dict(self.base)
            out = dict(self.out)
            ap = dict(self.ap)
            net = self.net
            ssid = self.sta_ssid
            self.frames += 14
            frames = self.frames

        sig = self.signals()
        cons, bands = [], []
        for s in range(7):
            prns = {x[1] for x in sig if x[0] == s}
            used = {x[1] for x in sig if x[0] == s and x[6]}
            cons.append([len(prns), len(used)])
            bands.append([sum(1 for x in sig if x[0] == s and x[5] == b)
                          for b in range(SYS_BANDS[s])])

        base["el"] = int(time.time() - self.svin_start) if base["m"] == 1 else 0
        t = time.gmtime()
        wobble = 3e-7 * math.sin(time.time() / 4.0) + random.uniform(-2e-7, 2e-7)
        fix = 0 if self.scenario in ("nofix", "cold") else 4
        valid = fix > 0

        return {
            "up": self.uptime(), "ip": "192.168.1.42" if net == 2 else "0.0.0.0",
            "ap": "192.168.4.1", "ssid": ssid, "rssi": -58, "net": net,
            "heap": 168432, "model": "LC29H (BS)", "esp": "0.5.0",
            "lat": 52.00000000 + wobble, "lon": 4.00000000 + wobble * 0.7,
            "alt": 102.418 + wobble * 8e5, "sep": 36.512,
            "vloc": valid, "valt": valid,
            "hdop": 0.71, "pdop": 1.34, "vdop": 1.12,
            "fq": fix, "ft": 3 if valid else 1,
            "siu": sum(c[1] for c in cons),
            "time": time.strftime("%H:%M:%S", t),
            "pps": valid, "rtcm": 14 if base["m"] > 0 else 0,
            "tcp": 2, "udp": 2, "cpu0": random.randint(8, 15), "cpu1": random.randint(28, 40),
            "apinfo": ap, "out": out,
            "rst": {"f": frames, "crc": 3, "bps": 612, "age": 0,
                    "ty": [[1005, frames // 60, 10000, 42], [1077, frames, 1000, 12],
                           [1087, frames, 1000, 15], [1097, frames, 1000, 11],
                           [1127, frames, 1000, 18], [1019, frames // 150, 30000, 90]]},
            "cl": [["192.168.4.3", "ntrip", 940, 1250000],
                   ["192.168.1.77", "raw", 120, 86000]],
            "ul": [["192.168.4.5", 9000, 300, 410000],
                   ["192.168.4.9", 9000, 15, 9800]],
            "cons": cons, "bands": bands, "sig": sig, "base": base,
            "svin": self._svin(base),
            "jam": {"feat": self.jam_feat,
                    "l1": 1 if self.jam_feat == 1 else 0,
                    "l5": 2 if self.jam_feat == 1 else -1},
            "arp": self.arp,
            "bc": self._bcast(base),
            "io": self._iono(sig),
            "ion": sum(1 for x in self._iono(sig) if x[2] > 0 and x[6] > 0),
            "iond": 0.041,
            "avg": self._avg(),
            "push": self._push(),
        }

    def _svin(self, base):
        if not self.svin_live or base["m"] != 1:
            return {"feat": 2 if not self.svin_live else 0, "v": 0, "obs": 0,
                    "dur": 0, "acc": 0.0, "x": 0.0, "y": 0.0, "z": 0.0}
        obs = int(time.time() - self.svin_start)
        return {"feat": 1, "v": 2 if obs >= base["dur"] else 1, "obs": obs,
                "dur": base["dur"], "acc": max(0.8, 40.0 / max(1, obs) ** 0.5),
                "x": -2472436.0802, "y": 4828383.0026, "z": 3343698.4839}

    def _bcast(self, base):
        # Example coordinates only, not a real station.
        x, y, z = 3925405.8088, 274491.1138, 5002842.7460
        lat, lon, hgt = 52.00000000, 4.00000000, 50.000
        live = base["m"] in (1, 2)
        return {"v": live, "id": 3335, "x": x, "y": y, "z": z,
                "age": 0 if live else -1, "st": int(time.time() - self.t0),
                "lat": lat, "lon": lon, "hgt": hgt}

    def _iono(self, sig):
        """Pierce points from the same geometry the firmware uses."""
        base_lat, base_lon = 52.0, 4.0
        Re, H = 6371.0, 350.0
        out, seen = [], set()
        for si, prn, el, az, _cn, band, used in sig:
            if si not in (0, 2, 3) or band == 0 or (si, prn) in seen:
                continue
            seen.add((si, prn))
            z = math.radians(90 - el)
            zp = math.asin(min(1.0, Re / (Re + H) * math.sin(z)))
            psi = z - zp
            la = math.radians(base_lat)
            azr = math.radians(az)
            ipp_lat = math.asin(math.sin(la) * math.cos(psi) +
                                math.cos(la) * math.sin(psi) * math.cos(azr))
            ipp_lon = math.radians(base_lon) + math.asin(
                math.sin(psi) * math.sin(azr) / max(1e-9, math.cos(ipp_lat)))
            phase = math.sin(time.time() / 40.0 + prn)
            dv = round(phase * 0.06 * math.cos(zp), 4)
            out.append([si, prn, el, az,
                        int(random.uniform(-2500, 1800)),
                        int(dv * 100),
                        random.randint(40, 300),
                        round(math.degrees(ipp_lat), 4),
                        round(math.degrees(ipp_lon), 4)])
        return out

    def _avg(self):
        a = dict(self.avg)
        if a["run"]:
            el = time.time() - a["started"]
            a["n"] = int(el)
            if el >= a["tgt"]:
                self._finish_avg()
                a = dict(self.avg)
        a["el"] = int(time.time() - self.avg["started"]) if self.avg["run"] else 0
        a["tgt"] = self.avg["tgt"]
        return a

    def _finish_avg(self):
        self.avg.update(run=False, have=True, lat=52.00000000, lon=4.00000000,
                        alt=102.418, rms=0.021,
                        n=int(time.time() - self.avg["started"]))

    def _push(self):
        p = dict(self.push)
        if p["en"] and p["st"] in (1, 2, 3) and time.time() - p["since"] > 3:
            self.push["st"] = 4
            self.push["msg"] = "Streaming"
            self.push["since"] = time.time()
            p = dict(self.push)
        if p["st"] == 4:
            self.push["sent"] += 620
            p["sent"] = self.push["sent"]
            p["up"] = int(time.time() - p["since"])
        else:
            p["up"] = 0
        return p


class WsPeer:
    def __init__(self, conn):
        self.conn = conn
        self.lock = threading.Lock()

    def send(self, text):
        data = text.encode()
        n = len(data)
        if n < 126:
            head = struct.pack("!BB", 0x81, n)
        elif n < 65536:
            head = struct.pack("!BBH", 0x81, 126, n)
        else:
            head = struct.pack("!BBQ", 0x81, 127, n)
        with self.lock:
            self.conn.sendall(head + data)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "MockRTK/1.0"

    def log_message(self, fmt, *args):
        if self.server.verbose:
            super().log_message(fmt, *args)

    # ---------------------------------------------------------------- helpers
    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _text(self, body, ctype="text/plain", code=200):
        raw = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        # The page is re-read from WebUI.h on every restart, so a cached copy
        # would silently hide the edit you just made.
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.end_headers()
        self.wfile.write(raw)

    # ------------------------------------------------------------------ routes
    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        dev = self.server.device

        if url.path == "/ws":
            return self._websocket()

        if url.path == "/":
            page = self.server.page
            # Documentation helper: ?page=<tab>&scroll=<px> opens straight at a
            # given tab and offset so screenshots can be scripted.
            if "page" in q or "scroll" in q:
                shot = ("<script>window.addEventListener('load',function(){"
                        "var p=new URLSearchParams(location.search);"
                        "if(p.get('page')){location.hash='#'+p.get('page');"
                        "window.dispatchEvent(new HashChangeEvent('hashchange'));}"
                        "setTimeout(function(){window.scrollTo(0,"
                        "parseInt(p.get('scroll')||'0',10));},%d);});</script>"
                        % 3000)
                page = page.replace("</body>", shot + "</body>")
            return self._text(page, "text/html")

        if url.path == "/scan":
            self.server.scan_polls += 1
            if self.server.scan_polls % 3:
                return self._json({"scanning": True}, 202)
            return self._json(["HomeNet", "Neighbour 2.4G", "iPhone", 'Weird"Name'])

        if url.path == "/status":
            return self._json({"state": dev.net, "ip": "192.168.1.42"})

        if url.path == "/cmd":
            cmd = q.get("c", "")
            self.server.push("TXCMD:" + cmd)
            self.server.push("TERM:" + self._fake_reply(cmd))
            return self._text("OK")

        if url.path == "/reboot":
            return self._text("Rebooting")

        if url.path == "/resetwifi":
            with dev.lock:
                dev.net = 0
                dev.sta_ssid = ""
            return self._text("OK")

        if url.path == "/api/base":
            return self._api_base(q)
        if url.path == "/api/output":
            return self._api_output(q)
        if url.path == "/api/net":
            return self._api_net(q)
        if url.path == "/api/push":
            return self._api_push(q)

        self._text("Not found", code=404)

    # ------------------------------------------------------------------- APIs
    def _api_base(self, q):
        dev = self.server.device
        action = q.get("action")
        with dev.lock:
            if action == "svin":
                mode = int(q.get("mode", 0))
                dev.base["m"] = mode
                if mode == 1:
                    dev.base["dur"] = int(q.get("dur", 43200))
                    dev.base["acc"] = float(q.get("acc", 15))
                    dev.svin_start = time.time()
                elif mode == 2:
                    if "lat" in q:
                        x, y, z = lla_to_ecef(float(q["lat"]), float(q["lon"]), float(q["hgt"]))
                    else:
                        x, y, z = (float(q.get("x", 0)), float(q.get("y", 0)), float(q.get("z", 0)))
                    if x == y == z == 0:
                        return self._json({"ok": False, "msg": "Fixed mode needs a station position"}, 400)
                    dev.base.update(x=x, y=y, z=z)
                dev.base["msg"] = "SVIN config accepted"
            elif action in ("rtcm", "arp", "eph"):
                dev.base[action] = int(q.get("v", 0))
                dev.base["msg"] = "PAIR command acknowledged"
            elif action == "save":
                dev.base["msg"] = "Saved to module NVM"
            elif action == "restore":
                dev.base.update(m=1, dur=43200, acc=15.0, x=0, y=0, z=0,
                                msg="Module defaults restored")
            elif action == "arpoffset":
                dev.arp = {"n": float(q.get("n", 0)), "e": float(q.get("e", 0)),
                           "u": float(q.get("u", 0))}
            elif action == "avgstart":
                dev.avg.update(run=True, started=time.time(),
                               tgt=int(q.get("s", 300)), n=0, have=False)
            elif action == "avgstop":
                if dev.avg["run"]:
                    dev._finish_avg()
            elif action == "adopt":
                if dev.svin_live and dev.base["m"] == 1:
                    dev.base.update(m=2, x=-2472436.0802, y=4828383.0026, z=3343698.4839,
                                    msg="Survey-in result adopted")
                else:
                    return self._json({"ok": False,
                                       "msg": "No completed survey-in result available"}, 400)
            elif action == "query":
                dev.base["msg"] = "Configuration re-read"
            else:
                return self._json({"ok": False, "msg": "Unknown action"}, 400)
        self.server.push("TERM:$PQTMCFGSVIN,OK*70")
        return self._json({"ok": True})

    def _api_output(self, q):
        dev = self.server.device
        if q.get("action") != "save":
            return self._json({"ok": False, "msg": "Unknown action"}, 400)
        tcp_en = q.get("tcpEn", "1") == "1"
        udp_en = q.get("udpEn", "1") == "1"
        tcp_p, udp_p = int(q.get("tcpPort", 2101)), int(q.get("udpPort", 2102))
        if tcp_en and udp_en and tcp_p == udp_p:
            return self._json({"ok": False, "msg": "TCP and UDP need different ports"}, 400)
        with dev.lock:
            dev.out.update(tcpEn=tcp_en, udpEn=udp_en, tcpPort=tcp_p, udpPort=udp_p,
                           accept=int(q.get("accept", 0)),
                           mount=(q.get("mount") or "RTK"),
                           user=q.get("user", ""),
                           auth=bool(q.get("user") or q.get("pass")))
        return self._json({"ok": True})

    def _api_net(self, q):
        dev = self.server.device
        action = q.get("action")
        if action == "ap":
            ssid = (q.get("ssid") or "").strip()
            pwd = q.get("pass", "")
            if not 1 <= len(ssid) <= 32:
                return self._json({"ok": False, "msg": "SSID must be 1-32 characters"}, 400)
            if 0 < len(pwd) < 8:
                return self._json({"ok": False, "msg": "WPA2 needs 8+ characters"}, 400)
            with dev.lock:
                dev.ap.update(ssid=ssid, ch=int(q.get("ch", 6)),
                              sec=len(pwd) >= 8, hide=q.get("hide") == "1")
            return self._json({"ok": True})
        if action == "join":
            if not q.get("ssid"):
                return self._json({"ok": False, "msg": "Missing SSID"}, 400)
            with dev.lock:
                dev.sta_ssid = q["ssid"]
                dev.net = 1
            threading.Timer(3.0, self._finish_join).start()
            return self._json({"ok": True})
        if action == "forget":
            with dev.lock:
                dev.net = 0
                dev.sta_ssid = ""
            return self._json({"ok": True})
        return self._json({"ok": False, "msg": "Unknown action"}, 400)

    def _api_push(self, q):
        dev = self.server.device
        en = q.get("en") == "1"
        host, mount = q.get("host", ""), q.get("mount", "")
        if en and (not host or not mount):
            return self._json({"ok": False, "msg": "Host and mountpoint are required"}, 400)
        with dev.lock:
            dev.push.update(en=en, host=host, port=int(q.get("port", 2101)),
                            mount=mount, sent=0, since=time.time(),
                            st=2 if en else 0,
                            msg="Connecting" if en else "Disabled")
        return self._json({"ok": True})

    def _finish_join(self):
        dev = self.server.device
        with dev.lock:
            dev.net = 2

    @staticmethod
    def _fake_reply(cmd):
        if cmd.startswith("$PQTMVERNO"):
            return "$PQTMVERNO,LC29HBSNR01A01S,2022/08/31,15:22:59*27"
        if cmd.startswith("$PQTMCFGSVIN,R"):
            return "$PQTMCFGSVIN,OK,1,43200,15.0,0.0000,0.0000,0.0000*67"
        if cmd.startswith("$PAIR"):
            return "$PAIR001,%s,0*3D" % cmd[5:8]
        return "$PQTM,OK*00"

    # -------------------------------------------------------------- websocket
    def _websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            return self._text("Expected WebSocket upgrade", code=400)
        accept = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()

        peer = WsPeer(self.connection)
        self.server.add_peer(peer)
        print("[ws] browser connected")
        try:
            peer.send("TERM:$PQTMVERNO,LC29HBSNR01A01S,2022/08/31,15:22:59*27")
            while True:                       # drain client frames until close
                hdr = self.rfile.read(2)
                if len(hdr) < 2:
                    break
                op = hdr[0] & 0x0F
                ln = hdr[1] & 0x7F
                masked = hdr[1] & 0x80
                if ln == 126:
                    ln = struct.unpack("!H", self.rfile.read(2))[0]
                elif ln == 127:
                    ln = struct.unpack("!Q", self.rfile.read(8))[0]
                mask = self.rfile.read(4) if masked else b""
                payload = self.rfile.read(ln)
                if op == 0x8:
                    break
                if op == 0x1 and masked:
                    text = bytes(b ^ mask[i % 4] for i, b in enumerate(payload)).decode(errors="replace")
                    self.server.push("TXCMD:" + text)
                    self.server.push("TERM:" + self._fake_reply(text))
        except OSError:
            pass
        finally:
            self.server.drop_peer(peer)
            print("[ws] browser disconnected")
        self.close_connection = True


def lla_to_ecef(lat_deg, lon_deg, h):
    a, f = 6378137.0, 1 / 298.257223563
    e2 = f * (2 - f)
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    return ((n + h) * math.cos(lat) * math.cos(lon),
            (n + h) * math.cos(lat) * math.sin(lon),
            (n * (1 - e2) + h) * math.sin(lat))


class MockServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, page, device, verbose):
        super().__init__(addr, Handler)
        self.page = page
        self.device = device
        self.verbose = verbose
        self.scan_polls = 0
        self.peers = []
        self.peers_lock = threading.Lock()

    def add_peer(self, p):
        with self.peers_lock:
            self.peers.append(p)

    def drop_peer(self, p):
        with self.peers_lock:
            if p in self.peers:
                self.peers.remove(p)

    def push(self, text):
        with self.peers_lock:
            peers = list(self.peers)
        for p in peers:
            try:
                p.send(text)
            except OSError:
                self.drop_peer(p)

    def telemetry_loop(self):
        while True:
            time.sleep(1.0)
            with self.peers_lock:
                any_peer = bool(self.peers)
            if any_peer:
                self.push(json.dumps(self.device.snapshot()))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--scenario", default="survey",
                    choices=["survey", "fixed", "nofix", "cold"])
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    page = load_page()
    srv = MockServer((args.host, args.port), page, Device(args.scenario), args.verbose)
    threading.Thread(target=srv.telemetry_loop, daemon=True).start()

    print("Serving the RTK web interface at http://%s:%d/  (scenario: %s)"
          % (args.host, args.port, args.scenario))
    print("Page is read from %s at startup - restart after editing it." % WEBUI)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
