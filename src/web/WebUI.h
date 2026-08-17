#pragma once
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 RTK Base</title>
<style>
:root{--or:#ef9421;--or2:#d97f10;--bd:#b7c4d0;--lg:#2f6fad;--tx:#20344a;--mu:#6c7f93;--bg:#fff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:13px/1.45 Arial,Helvetica,sans-serif}
a{color:inherit;text-decoration:none}

/* ---- header ---- */
.top{display:flex;align-items:center;gap:14px;padding:8px 14px 4px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:8px;min-width:150px}
.brand .mark{width:34px;height:34px;border-radius:50%;background:var(--or);display:flex;align-items:center;justify-content:center;color:#fff;font-weight:bold;font-size:11px}
.brand .nm{font-size:15px;font-weight:bold;letter-spacing:.3px}
.brand .sub{font-size:10px;color:var(--mu)}
.hgroups{display:flex;gap:16px;flex-wrap:wrap;flex:1}
.hgroup{min-width:190px}
.hgroup h4{margin:0 0 3px;font-size:11px;text-align:center;color:var(--tx);font-weight:bold}
.hbox{border:1px solid var(--or);background:#fffdf8;border-radius:3px;padding:2px 7px;margin-bottom:3px;display:flex;justify-content:space-between;gap:8px;font-size:12px}
.hbox b{font-weight:normal;color:var(--mu)}
.hbox span{font-weight:bold;white-space:nowrap}
.hstat{display:grid;grid-template-columns:auto auto;gap:2px 18px;font-size:12px;align-self:flex-start;padding-top:14px}
.hstat div{display:flex;align-items:center;gap:6px;white-space:nowrap}
.dot{width:9px;height:9px;border-radius:50%;background:#c2ccd6;flex:none}
.dot.ok{background:#3a9d43}.dot.warn{background:#e8a33d}.dot.bad{background:#c0392b}.dot.rtk{background:#8e5bd6}
.login{font-size:12px;color:var(--or2);white-space:nowrap;align-self:flex-start;padding-top:4px}

/* ---- nav ---- */
nav{display:flex;background:#fff;padding:0 14px;gap:2px;flex-wrap:wrap}
nav a{flex:1;min-width:92px;text-align:center;background:var(--or);color:#fff;font-weight:bold;padding:7px 2px;font-size:12.5px;border-radius:3px 3px 0 0;white-space:nowrap}
nav a:hover{background:var(--or2)}
nav a.on{background:#b9660a}
.crumb{padding:5px 16px;color:var(--mu);font-size:12px}

/* ---- boxes ---- */
main{padding:0 14px 30px}
.box{border:1px solid var(--bd);border-radius:4px;padding:14px 14px 12px;margin:0 0 14px;position:relative}
.box>h3{position:absolute;top:-9px;left:12px;background:#fff;padding:0 6px;margin:0;font-size:12px;color:var(--lg);font-weight:normal}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:14px;align-items:start}
.kv{display:grid;grid-template-columns:auto 1fr;gap:3px 12px;font-size:12.5px}
.kv b{font-weight:normal;color:var(--mu)}
.kv span{font-weight:bold;text-align:right}
table{width:100%;border-collapse:collapse;font-size:12px}
th,td{padding:3px 6px;border-bottom:1px solid #e6ecf1;text-align:right}
th{color:var(--mu);font-weight:normal;text-align:right;border-bottom:1px solid var(--bd)}
th:first-child,td:first-child{text-align:left}
.scroll{overflow-x:auto}
.tall{max-height:330px;overflow-y:auto}

/* ---- quality indicators ---- */
.qi{display:flex;justify-content:space-around;flex-wrap:wrap;gap:10px;text-align:center}
.qi>div{width:120px}
.qi .t{font-size:11.5px;color:var(--tx);margin:4px 0 3px}
.qi .v{font-size:11px;color:var(--mu)}
.bars{display:flex;gap:2px;justify-content:center;margin-top:2px}
.bars i{width:5px;background:#dde4ea;border-radius:1px}
.bars i:nth-child(1){height:5px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:9px}
.bars i:nth-child(4){height:11px}.bars i:nth-child(5){height:13px}
.bars i.f{background:#3a9d43}.bars i.w{background:#e8a33d}.bars i.b{background:#c0392b}

/* ---- forms ---- */
label.f{display:block;font-size:11px;color:var(--mu);margin:8px 0 2px;text-transform:uppercase;letter-spacing:.3px}
input[type=text],input[type=number],select{width:100%;padding:5px 7px;border:1px solid var(--bd);border-radius:3px;font:inherit;font-size:12.5px;background:#fff;color:var(--tx)}
input:disabled{background:#f2f5f8;color:#9aa8b5}
.btn{display:inline-block;padding:6px 16px;border:none;border-radius:3px;background:var(--or);color:#fff;font-weight:bold;cursor:pointer;font-size:12.5px;margin:10px 6px 0 0}
.btn:hover{background:var(--or2)}
.btn.g{background:#5c7089}.btn.g:hover{background:#47586e}
.btn.r{background:#c0392b}.btn.r:hover{background:#a03024}
.radios{display:flex;gap:18px;flex-wrap:wrap;margin:2px 0 6px}
.radios label{font-size:12.5px;display:flex;align-items:center;gap:5px;cursor:pointer}
.note{font-size:11.5px;color:var(--mu);margin-top:8px;line-height:1.5}
.msg{margin-top:10px;padding:6px 9px;border-radius:3px;font-size:12px;display:none}
.msg.ok{display:block;background:#eaf5ea;border:1px solid #b6d9b6;color:#2e6b31}
.msg.err{display:block;background:#fdeceb;border:1px solid #f0b7b2;color:#a03024}
.prog{height:9px;background:#e8eef3;border-radius:5px;overflow:hidden;margin-top:5px}
.prog i{display:block;height:100%;background:var(--or);width:0}

/* ---- terminal ---- */
#term{height:440px;overflow-y:auto;background:#101820;color:#c8e6d0;padding:8px;border:1px solid var(--bd);border-radius:3px;font:12px/1.5 Menlo,Consolas,monospace}
#term div{white-space:pre-wrap;word-break:break-all}
.tx{color:#7fd1ff}.rx{color:#ffd479}.sy{color:#8bc98b}
.termbar{display:flex;gap:6px;margin-bottom:8px}
.termbar input{flex:1}
#map{height:280px;border-radius:3px;border:1px solid var(--bd)}
canvas{max-width:100%;display:block;margin:0 auto}
.page{display:none}.page.on{display:block}
.legend{display:flex;gap:12px;flex-wrap:wrap;justify-content:center;font-size:11.5px;margin-top:6px}
.legend span{display:flex;align-items:center;gap:4px}
.sw{width:10px;height:10px;border-radius:2px}
.hsum{display:flex;flex-wrap:wrap;gap:10px 26px;font-size:12.5px}
.hsum div{min-width:130px}
.hsum b{display:block;font-weight:normal;color:var(--mu);font-size:11px;
        text-transform:uppercase;letter-spacing:.3px}
.hstrip{display:flex;align-items:center;gap:8px;margin-bottom:4px}
.hstrip .hlab{width:62px;flex:none;font-size:11px;color:var(--mu);text-align:right}
.hstrip canvas{flex:1;margin:0}
#hAxis{margin-left:70px;width:calc(100% - 70px)}
/* Downlink indicator. The skeleton is built once and only its attributes are
   refreshed, so the sweep never restarts on a telemetry update. Every tracked
   constellation gets the same green link, matching the receiver convention:
   green = signals coming down, grey = nothing tracked. All lines carry
   pathLength="100" so one dash period spans the line regardless of geometry. */
.fanbase{stroke:#dbe3ea;stroke-width:2.5;stroke-linecap:round}
.fanlive{stroke:#2f8f47;stroke-width:2.5;stroke-linecap:round;opacity:0;
         transition:opacity .5s ease}
.fanlive.on{opacity:.5}
.fanglow,.fansweep{stroke-linecap:round;opacity:0;stroke-dasharray:14 86;
                   stroke-dashoffset:100;transition:opacity .5s ease}
.fanglow{stroke:#63d47e;stroke-width:7}
.fansweep{stroke:#e6fff0;stroke-width:2.2;stroke-dasharray:7 93}
.fanglow.on{opacity:.30;animation:fansweep 1.9s linear infinite}
.fansweep.on{opacity:.95;animation:fansweep 1.9s linear infinite}
@keyframes fansweep{from{stroke-dashoffset:100}to{stroke-dashoffset:0}}
.fansat{fill:#c2ccd6;transition:fill .5s ease}
.fansat.on{fill:#2f8f47}
@media(prefers-reduced-motion:reduce){
  .fanglow,.fansweep{animation:none}
  .fanglow.on,.fansweep.on{opacity:0}
  .fanlive.on{opacity:1}
}
/* Overview's last row: the left column stacks Position over Ionosphere and both
   columns stretch, so the two bottom edges line up whichever side is taller. */
.ovrow{align-items:stretch}
.ovleft{display:flex;flex-direction:column;gap:14px;min-width:0}
.ovleft>.box{margin:0}
.ovleft>.box.grow{flex:1;display:flex;flex-direction:column}
.ovrow>.box{margin:0}
.ionowrap{flex:1;display:flex;flex-direction:column;justify-content:center}
.pill{display:inline-block;padding:1px 7px;border-radius:9px;font-size:11px;font-weight:bold}
.pill.n{background:#e7f0fa;color:#2f6fad}.pill.r{background:#eef1f4;color:#5c7089}
.pill.g{background:#e3f4e8;color:#1f7a45}
code{background:#f2f5f8;padding:1px 5px;border-radius:3px;font-size:12px}
@media(max-width:860px){nav a{min-width:78px;font-size:11.5px}}
@media(max-width:700px){.hstat{padding-top:0}nav a{min-width:70px;font-size:11px}}
</style>
</head>
<body>

<div class="top">
  <div class="brand">
    <div class="mark">RTK</div>
    <div><div class="nm">ESP32 RTK Base</div><div class="sub" id="hModel">LC29H (BS)</div></div>
  </div>

  <div class="hgroups">
    <div class="hgroup">
      <h4>Receiver</h4>
      <div class="hbox"><b>Module</b><span id="hRx">--</span></div>
      <div class="hbox"><b>AP Address</b><span id="hIp">--</span></div>
      <div class="hbox"><b>Uptime</b><span id="hUp">--</span></div>
    </div>
    <div class="hgroup">
      <h4>Position</h4>
      <div class="hbox"><b>Lat</b><span id="hLat">--</span></div>
      <div class="hbox"><b>Lon</b><span id="hLon">--</span></div>
      <div class="hbox"><b>Hgt (MSL)</b><span id="hHgt">--</span></div>
    </div>
    <div class="hgroup">
      <h4>Status</h4>
      <div class="hbox"><b>Tracked Sats</b><span id="hTrk">--</span></div>
      <div class="hbox"><b>Time (UTC)</b><span id="hTime">--:--:--</span></div>
      <div class="hbox"><b>Base Mode</b><span id="hBase">--</span></div>
    </div>
  </div>

  <div class="hstat">
    <div><i class="dot" id="dFix"></i><span id="lFix">No fix</span></div>
    <div><i class="dot" id="dNet"></i><span id="lNet">Network</span></div>
    <div><i class="dot" id="dQual"></i><span>Overall Quality</span></div>
    <div><i class="dot" id="dTcp"></i><span id="lTcp">0 clients</span></div>
    <div><i class="dot" id="dCorr"></i><span id="lCorr">Corrections</span></div>
    <div><i class="dot" id="dPps"></i><span>PPS</span></div>
    <div><i class="dot" id="dJam"></i><span id="lJam">Spectrum</span></div>
    <div><i class="dot" id="dPush"></i><span id="lPush">No push</span></div>
  </div>

  <div class="login" id="link">&#9679; connecting</div>
</div>

<nav>
  <a href="#overview" data-p="overview">Overview</a>
  <a href="#gnss" data-p="gnss">GNSS</a>
  <a href="#base" data-p="base">Base Mode</a>
  <a href="#output" data-p="output">Data Output</a>
  <a href="#network" data-p="network">Network</a>
  <a href="#history" data-p="history">History</a>
  <a href="#terminal" data-p="terminal">Terminal</a>
  <a href="#admin" data-p="admin">Admin</a>
</nav>
<div class="crumb" id="crumb">Overview</div>

<main>

<!-- ================= OVERVIEW ================= -->
<section class="page" id="p-overview">
  <div class="box">
    <h3>Quality Indicators</h3>
    <div class="qi" id="qi"></div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>GNSS</h3>
      <div id="fan"></div>
    </div>

    <div class="box">
      <h3>Corrections / RTCM</h3>
      <div class="kv">
        <b>Base mode</b><span id="oBase">--</span>
        <b>RTCM stream</b><span id="oRtcm">0 pkt/s</span>
        <b>RTCM message set</b><span id="oMsm">--</span>
        <b>Station ARP (1005)</b><span id="oArp">--</span>
        <b>Ephemerides</b><span id="oEph">--</span>
        <b>TCP clients (port 2101)</b><span id="oTcp">0</span>
      </div>
      <div id="oSvin" style="display:none">
        <div class="note" id="oSvinTxt"></div>
        <div class="prog"><i id="oSvinBar"></i></div>
      </div>
    </div>
  </div>

  <div class="cols ovrow">
   <div class="ovleft">
    <div class="box">
      <h3>Position</h3>
      <div class="kv">
        <b>Latitude</b><span id="oLat">--</span>
        <b>Longitude</b><span id="oLon">--</span>
        <b>Height above MSL</b><span id="oHgt">--</span>
        <b>Geoid separation</b><span id="oSep">--</span>
        <b>Ellipsoidal height</b><span id="oHgtE">--</span>
        <b>Position mode</b><span id="oMode">--</span>
        <b>Satellites in use</b><span id="oSiu">--</span>
        <b>PDOP / HDOP / VDOP</b><span id="oDop">-- / -- / --</span>
      </div>
    </div>

    <div class="box grow">
      <h3>Ionosphere</h3>
      <div class="ionowrap">
        <canvas id="iono" width="330" height="270"></canvas>
        <div class="legend" id="ionoLegend"></div>
      </div>
      <div class="kv" style="margin-top:6px">
        <b>Pierce points</b><span id="ioN">--</span>
        <b>Mean vertical change</b><span id="ioD">--</span>
      </div>
      <div class="note">
        Where each satellite's ray crosses the ionosphere, on a geographic grid
        centred on this station. The window is fixed at &plusmn;10&deg; so it
        does not resize as satellites rise and set; that reaches every pierce
        point down to about 13&deg; elevation. A cell is filled only where a measurement
        actually falls; nothing is interpolated between them, because with
        roughly twenty pierce points and a per-satellite phase reference there
        is no honest way to fill the gaps.<br><br>
        Colour is how much the vertical delay has
        <b>changed</b> since each satellite's arc began. That change comes from
        the carrier phase and is real to millimetres. The absolute delay is also
        measured but is left uncalibrated: on this receiver the differential
        code bias is large enough to reverse its sign, so only the variation is
        trustworthy.
      </div>
    </div>
   </div>

    <div class="box">
      <h3>Position Scatter</h3>
      <canvas id="scatter" width="360" height="360"></canvas>
      <div class="kv" style="margin-top:8px">
        <b>Samples</b><span id="scN">0</span>
        <b>Spread (2D RMS)</b><span id="scRms">--</span>
        <b>Peak-to-peak E / N</b><span id="scPp">--</span>
        <b>Height spread</b><span id="scH">--</span>
        <b>Offset from broadcast</b><span id="scBc">--</span>
      </div>
      <div class="note">
        Horizontal deviation of the receiver's own solution from its running
        mean, in centimetres. During survey-in the cloud should shrink and stay
        centred; a drifting cloud means the antenna or the multipath environment
        is not stable yet.<br><br>
        The red cross marks the coordinate being broadcast in RTCM 1005, or an
        arrow points to it when it falls outside the plot. A few metres is
        normal, since the receiver's own solution is only DGNSS-accurate; tens
        of metres means the station coordinate itself is wrong.
      </div>
    </div>
  </div>
</section>

<!-- ================= GNSS ================= -->
<section class="page" id="p-gnss">
  <div class="box">
    <h3>GNSS</h3>
    <div id="fan2"></div>
  </div>

  <div class="box">
    <h3>Signal Distribution</h3>
    <div class="scroll">
      <table id="bandTable">
        <thead><tr><th>Constellation</th><th>Satellites</th><th>In fix</th><th>Signals per band</th></tr></thead>
        <tbody></tbody>
        <tfoot></tfoot>
      </table>
    </div>
    <div class="note">
      A satellite tracked on two frequencies counts once under Satellites and
      once per band under Signals.
    </div>
  </div>

  <div class="box">
    <h3>Sky Plot</h3>
    <canvas id="sky" width="480" height="480"></canvas>
    <div class="legend" id="skyLegend"></div>
    <div class="note" style="text-align:center">
      Filled markers are satellites used in the position solution.
      <span id="skyCount"></span>
    </div>
  </div>

  <div class="box">
    <h3>Carrier-to-Noise</h3>
    <div style="display:flex;gap:14px;align-items:center;margin-bottom:8px;flex-wrap:wrap">
      <span>Antenna: <select id="cnAnt" style="width:auto"><option>Main</option></select></span>
      <span>System: <select id="cnSys" style="width:auto"></select></span>
    </div>
    <canvas id="cn0" width="900" height="260"></canvas>
    <div class="legend" id="cnLegend"></div>
  </div>

  <div class="box">
    <h3>Carrier-to-Noise vs Elevation</h3>
    <canvas id="cnel" width="620" height="300"></canvas>
    <div class="legend" id="cnelLegend"></div>
    <div class="note">
      Signal strength should climb steadily with elevation. Points that sit well
      below the trend, or a band that is depressed across all elevations, point
      at multipath, an obstruction, or a cable/antenna problem.
    </div>
  </div>

  <div class="box">
    <h3>Satellites and Signals</h3>
    <div class="scroll tall">
      <table id="satTable">
        <thead><tr><th>SV</th><th>System</th><th>Band</th><th>Elev</th><th>Azim</th><th>C/N0</th><th>In fix</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
  </div>
</section>

<!-- ================= BASE MODE ================= -->
<section class="page" id="p-base">
  <div class="cols">
    <div class="box">
      <h3>Position Mode</h3>

      <div class="radios">
        <label><input type="radio" name="bm" value="0" onchange="baseModeChanged()"> Disabled</label>
        <label><input type="radio" name="bm" value="1" onchange="baseModeChanged()"> Survey-In</label>
        <label><input type="radio" name="bm" value="2" onchange="baseModeChanged()"> Fixed</label>
      </div>

      <div id="svinBlock">
        <label class="f">Minimum observation time (seconds, 0&ndash;86400)</label>
        <input type="number" id="bDur" min="0" max="86400" step="1" value="43200">
        <label class="f">3D accuracy limit (metres, 0 = no limit)</label>
        <input type="number" id="bAcc" min="0" step="0.1" value="15.0">
        <div class="note">
          The receiver averages valid 3D fixes until <i>both</i> the time and the
          accuracy target are met, then switches to static base operation.
          60&nbsp;s gives a rough base; several hours is normal for centimetre work.
        </div>
      </div>

      <div id="fixedBlock" style="display:none">
        <label class="f">Coordinate entry</label>
        <select id="bCoordMode" onchange="coordModeChanged()">
          <option value="lla">Geodetic (lat / lon / ellipsoidal height)</option>
          <option value="ecef">ECEF X / Y / Z</option>
        </select>

        <div id="llaFields">
          <label class="f">Latitude (deg, + north)</label>
          <input type="number" id="bLat" step="0.0000001" value="0">
          <label class="f">Longitude (deg, + east)</label>
          <input type="number" id="bLon" step="0.0000001" value="0">
          <label class="f">Ellipsoidal height (m)</label>
          <input type="number" id="bHgt" step="0.001" value="0">
          <button class="btn g" type="button" onclick="useCurrentPos()">Use current position</button>
        </div>

        <div id="ecefFields" style="display:none">
          <label class="f">ECEF X (m)</label><input type="number" id="bX" step="0.0001" value="0">
          <label class="f">ECEF Y (m)</label><input type="number" id="bY" step="0.0001" value="0">
          <label class="f">ECEF Z (m)</label><input type="number" id="bZ" step="0.0001" value="0">
        </div>

        <div class="note">
          Any error in the fixed station coordinates transfers directly into every
          rover position. Only use surveyed coordinates here.
        </div>
      </div>

      <button class="btn" onclick="applyBaseMode()">Apply ($PQTMCFGSVIN)</button>
      <button class="btn g" onclick="baseApi('query')">Read from module</button>
      <div class="msg" id="bMsg"></div>
    </div>

    <div class="box">
      <h3>Current Module Configuration</h3>
      <div class="kv">
        <b>Operating state</b><span id="cOper" style="color:#2e6b31">--</span>
        <b>Configured mode</b><span id="cMode">--</span>
        <b>Minimum duration</b><span id="cDur">--</span>
        <b>Accuracy limit</b><span id="cAcc">--</span>
        <b>ECEF X</b><span id="cX">--</span>
        <b>ECEF Y</b><span id="cY">--</span>
        <b>ECEF Z</b><span id="cZ">--</span>
        <b>Firmware</b><span id="cVer">--</span>
        <b>Build date</b><span id="cBd">--</span>
        <b>Last module reply</b><span id="cMsg">--</span>
      </div>

      <div id="svinStatus" style="display:none;margin-top:12px">
        <b style="color:var(--mu);font-weight:normal">Survey-in progress</b>
        <div class="prog"><i id="cSvinBar"></i></div>
        <div class="kv" style="margin-top:6px">
          <b>State</b><span id="sState">--</span>
          <b>Observations</b><span id="sObs">--</span>
          <b>Mean accuracy</b><span id="sAcc">--</span>
          <b>Mean ECEF</b><span id="sMean">--</span>
        </div>
        <button class="btn" id="sAdopt" onclick="adoptSvin()">Adopt result as fixed position</button>
        <div class="note" id="cSvinTxt"></div>
      </div>
    </div>
  </div>

  <div class="box">
    <h3>Broadcast Station Position (RTCM 1005)</h3>
    <div class="cols">
      <div class="kv">
        <b>Reference station ID</b><span id="bcId">--</span>
        <b>ECEF X</b><span id="bcX">--</span>
        <b>ECEF Y</b><span id="bcY">--</span>
        <b>ECEF Z</b><span id="bcZ">--</span>
        <b>Last 1005</b><span id="bcAge">--</span>
        <b>Unchanged for</b><span id="bcSt">--</span>
      </div>
      <div class="kv">
        <b>Latitude</b><span id="bcLat">--</span>
        <b>Longitude</b><span id="bcLon">--</span>
        <b>Ellipsoidal height</b><span id="bcHgt">--</span>
        <b>Offset from live fix</b><span id="bcDiff">--</span>
      </div>
    </div>
    <div class="note">
      Decoded from the stream the rovers actually receive, not from the module's
      configuration. In survey-in mode the receiver switches to the surveyed
      coordinates by itself once the survey completes, without rewriting its
      configured mode &mdash; so this panel, not the one above, tells you which
      position is really being sent.
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>Antenna Reference Point</h3>
      <div class="note">
        RTCM 1005 broadcasts the <b>antenna reference point</b>. If you enter the
        coordinates of a surveyed ground mark, give the offset from that mark up
        to the antenna here &mdash; otherwise every rover position is off by the
        antenna height. Leave at zero when the coordinates already refer to the
        antenna itself (survey-in result or &ldquo;use current position&rdquo;).
      </div>
      <label class="f">Marker &rarr; ARP North (m)</label>
      <input type="number" id="aN" step="0.001" value="0">
      <label class="f">Marker &rarr; ARP East (m)</label>
      <input type="number" id="aE" step="0.001" value="0">
      <label class="f">Marker &rarr; ARP Up (m) &mdash; usually the antenna height</label>
      <input type="number" id="aU" step="0.001" value="0">
      <button class="btn" onclick="saveArp()">Save offset</button>
      <div class="msg" id="aOffMsg"></div>
    </div>

    <div class="box">
      <h3>Position Averaging</h3>
      <div class="note">
        Averages the receiver's own fix on the ESP32. Useful when the module is
        not in survey-in mode, or as a sanity check against it. This is not a
        substitute for a surveyed coordinate: it converges on the receiver's
        systematic error, not the truth.
      </div>
      <label class="f">Duration (seconds)</label>
      <input type="number" id="avgSec" min="10" max="86400" step="10" value="300">
      <button class="btn" onclick="avgStart()">Start averaging</button>
      <button class="btn g" onclick="avgStop()">Stop and keep</button>

      <div id="avgRun" style="display:none;margin-top:10px">
        <div class="prog"><i id="avgBar"></i></div>
        <div class="note" id="avgTxt"></div>
      </div>
      <div class="kv" id="avgRes" style="display:none;margin-top:10px">
        <b>Latitude</b><span id="avgLat">--</span>
        <b>Longitude</b><span id="avgLon">--</span>
        <b>Height</b><span id="avgAlt">--</span>
        <b>Samples / spread</b><span id="avgN">--</span>
      </div>
      <button class="btn g" id="avgUse" style="display:none" onclick="useAvg()">Use as fixed coordinates</button>
      <div class="msg" id="avgMsg"></div>
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>RTCM Output</h3>
      <label class="f">Observation messages ($PAIR432)</label>
      <select id="bRtcm" onchange="baseApi('rtcm', this.value)">
        <option value="1">MSM7 &ndash; 1077 / 1087 / 1097 / 1117 / 1127</option>
        <option value="0">MSM4 &ndash; 1074 / 1084 / 1094 / 1114 / 1124</option>
        <option value="-1">Disabled</option>
      </select>

      <label class="f">Stationary RTK reference station ARP &ndash; 1005 ($PAIR434)</label>
      <select id="bArp" onchange="baseApi('arp', this.value)">
        <option value="1">Enabled</option><option value="0">Disabled</option>
      </select>

      <label class="f">Satellite ephemerides &ndash; 1019 / 1020 / 1042 / 1044 / 1046 ($PAIR436)</label>
      <select id="bEph" onchange="baseApi('eph', this.value)">
        <option value="1">Enabled</option><option value="0">Disabled</option>
      </select>

      <div class="note">MSM7 carries full-resolution observations; MSM4 is smaller and
      enough for most rovers. Changes apply immediately and are re-applied on boot.</div>
    </div>

    <div class="box">
      <h3>Persistence</h3>
      <div class="note">
        Survey-in / fixed settings live in the module's own non-volatile memory.
        They are only written when you press Save, so a reboot never silently
        restarts a survey.
      </div>
      <button class="btn" onclick="baseApi('save')">Save to module NVM ($PQTMSAVEPAR)</button>
      <button class="btn r" onclick="confirmRestore()">Restore module defaults ($PQTMRESTOREPAR)</button>
    </div>
  </div>
</section>

<!-- ================= DATA OUTPUT ================= -->
<section class="page" id="p-output">
  <div class="box">
    <h3>Endpoints</h3>
    <div class="kv">
      <b>UDP unicast (lowest latency)</b><span id="epUdp">--</span>
      <b>TCP raw / NTRIP caster</b><span id="epTcp">--</span>
      <b>NTRIP mountpoint</b><span id="epMount">--</span>
      <b>USB serial</b><span id="epUsb">--</span>
      <b>UDP datagrams received</b><span id="epUdpRx">--</span>
      <b>Uplink address (if joined)</b><span id="epSta">--</span>
    </div>
    <div class="note">
      Associate the rover with this device's own access point and use the AP
      address: that is a single wireless hop. Going through a router adds a
      second hop for every correction frame.
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>UDP Output</h3>
      <label class="f">Enabled</label>
      <select id="oUdpEn"><option value="1">Yes</option><option value="0">No</option></select>
      <label class="f">Listen port</label>
      <input type="number" id="oUdpPort" min="1" max="65535" value="2102">
      <label class="f">Fixed destination IP (optional &mdash; leave empty to require registration)</label>
      <input type="text" id="oUdpDst" placeholder="192.168.4.20">
      <label class="f">Fixed destination port</label>
      <input type="number" id="oUdpDstP" min="0" max="65535" value="0">
      <label class="f">Broadcast to the subnet as well</label>
      <select id="oUdpBc"><option value="0">No</option><option value="1">Yes</option></select>
      <div class="note">
        No connection state and no retransmission, so a lost frame costs one
        epoch instead of stalling the stream behind a TCP retransmit. Unicast
        rather than broadcast on purpose: 802.11 acknowledges and retries
        unicast frames, while broadcast goes out unacknowledged at the lowest
        basic rate.<br><br>
        <b>To subscribe</b>, the rover sends any UDP datagram to this port and
        repeats it at least every 30 s. Its source address then receives one
        datagram per RTCM frame.<br><br>
        Ground-station software such as Mission Planner only listens and never
        registers, so registration alone will never deliver anything to it.
        <b>Use the fixed destination</b>: enter the receiver's address and the
        port it listens on. That is plain unicast and it is the only option that
        also reaches a socket which has <i>connected</i> to this device, the
        shape most GCS UDP clients use.<br><br>
        <b>Broadcast is a weaker fallback.</b> It only reaches sockets bound to
        the listen port above and left unconnected, it goes out unacknowledged
        at the lowest WiFi rate, and it is sent whether or not anyone listens.
        Prefer the fixed destination whenever you know the address.<br><br>
        The counter on the Endpoints panel shows whether anything has ever
        contacted this port, which tells you at once if the other side is
        silent.
      </div>
    </div>

    <div class="box">
      <h3>TCP Raw &amp; NTRIP Caster</h3>
      <label class="f">Enabled</label>
      <select id="oTcpEn"><option value="1">Yes</option><option value="0">No</option></select>
      <label class="f">Port</label>
      <input type="number" id="oTcpPort" min="1" max="65535" value="2101">
      <label class="f">Accept mode</label>
      <select id="oAccept">
        <option value="0">Auto &ndash; detect NTRIP or raw per connection</option>
        <option value="1">NTRIP caster only</option>
        <option value="2">Raw stream only</option>
      </select>
      <label class="f">Mountpoint</label>
      <input type="text" id="oMount" maxlength="23" value="RTK">
      <label class="f">NTRIP user (empty = no authentication)</label>
      <input type="text" id="oUser" maxlength="23">
      <label class="f">NTRIP password</label>
      <input type="text" id="oPass" maxlength="23">
      <button class="btn" onclick="saveOutput()">Save output settings</button>
      <div class="msg" id="oMsg"></div>
      <div class="note">
        In auto mode a connection that starts with an HTTP <code>GET</code> is
        answered as NTRIP (v1 <code>ICY 200 OK</code> or v2 depending on the
        client) and anything else is served the raw stream, so existing rover
        setups keep working. <code>GET /</code> returns the source table.
      </div>
    </div>
  </div>

  <div class="box">
    <h3>USB Serial Output</h3>
    <div class="cols">
      <div>
        <label class="f">Enabled</label>
        <select id="oUsbEn"><option value="0">No</option><option value="1">Yes</option></select>
        <label class="f">Baud rate</label>
        <select id="oUsbBaud">
          <option value="115200">115200</option>
          <option value="230400">230400</option>
          <option value="460800">460800</option>
          <option value="921600">921600</option>
        </select>
        <button class="btn" onclick="saveOutput()">Save output settings</button>
        <div class="msg" id="oUsbMsg"></div>
      </div>
      <div>
        <div class="kv">
          <b>State</b><span id="usbState">--</span>
          <b>Frames sent</b><span id="usbFr">--</span>
          <b>Bytes sent</b><span id="usbTx">--</span>
          <b>Frames dropped</b><span id="usbDrop">--</span>
        </div>
      </div>
    </div>
    <div class="note">
      Streams the same CRC-checked RTCM frames down the USB cable that flashes
      the board, so a receiver on the PC needs no network at all. Point
      u&#8209;center, RTKLIB or Mission Planner at the board's serial port
      (<code>/dev/cu.usbserial-*</code>, <code>/dev/ttyUSB0</code> or
      <code>COMn</code>) at the baud rate set here.<br><br>
      <b>The console log is muted while this is on.</b> UART0 carries both, and
      a log line printed between two frames would be spliced into the stream.
      Turn this off to get boot and diagnostic messages back; OTA updates and
      the web interface are unaffected either way.<br><br>
      <b>Opening the port reboots the board.</b> That is the dev board's own
      auto-reset circuit, driven by DTR/RTS, not something the firmware can
      refuse. The setting is stored, so the stream resumes by itself &mdash; but
      a survey-in restarts with it, so start the PC software before starting a
      survey, or use one of the network outputs instead.<br><br>
      <b>Frames dropped</b> counts frames skipped because the host was not
      draining the port fast enough. A steady climb means the baud rate is too
      low for the message set &mdash; 115200 carries roughly 11 kB/s, which is
      ample for MSM7 on four constellations at 1 Hz, but raise it if you have
      also raised the message rates.
    </div>
  </div>

  <div class="box">
    <h3>NTRIP Server &mdash; push to a remote caster</h3>
    <div class="cols">
      <div>
        <label class="f">Enabled</label>
        <select id="pEn"><option value="0">No</option><option value="1">Yes</option></select>
        <label class="f">Caster host</label>
        <input type="text" id="pHost" placeholder="rtk2go.com">
        <label class="f">Port</label>
        <input type="number" id="pPort" min="1" max="65535" value="2101">
        <label class="f">Mountpoint</label>
        <input type="text" id="pMount" maxlength="23">
        <label class="f">Password</label>
        <input type="text" id="pPass" maxlength="31">
        <button class="btn" onclick="savePush()">Save and connect</button>
        <div class="msg" id="pMsg"></div>
      </div>
      <div>
        <div class="kv">
          <b>State</b><span id="pState">--</span>
          <b>Detail</b><span id="pDetail">--</span>
          <b>Streaming for</b><span id="pUp">--</span>
          <b>Pushed</b><span id="pSent">--</span>
          <b>Reconnects</b><span id="pRetry">0</span>
        </div>
        <div class="note">
          This is the outbound direction: the base connects <i>out</i> to a caster
          and uploads, instead of waiting for rovers to connect in. It needs a
          WiFi uplink (Network tab) &mdash; the access point alone has no route
          off the device.<br><br>
          NTRIP v1 server protocol (<code>SOURCE</code>), which is what public
          casters accept. Reconnects back off from 5&nbsp;s to 60&nbsp;s so a
          wrong mountpoint does not hammer someone else's server.
        </div>
      </div>
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>Connected Consumers</h3>
      <div class="scroll">
        <table id="clTable">
          <thead><tr><th>Address</th><th>Transport</th><th>Connected</th><th>Sent</th>
            <th>Rover fix</th><th>Baseline</th><th>Reported</th></tr></thead>
          <tbody></tbody>
        </table>
      </div>
      <div class="note" id="clNote"></div>
    </div>

    <div class="box">
      <h3>RTCM Messages</h3>
      <div class="kv">
        <b>Frames forwarded</b><span id="rsF">0</span>
        <b>Throughput</b><span id="rsBps">0 B/s</span>
        <b>CRC errors</b><span id="rsCrc">0</span>
        <b>Last frame</b><span id="rsAge">--</span>
      </div>
      <div class="scroll" style="margin-top:8px">
        <table id="rsTable">
          <thead><tr><th>Type</th><th>Message</th><th>Count</th><th>Interval</th><th>Jitter</th></tr></thead>
          <tbody></tbody>
        </table>
      </div>
      <div class="note">
        Only complete, CRC-24Q verified frames are forwarded; NMEA never reaches
        the rover, which keeps the radio link free for corrections.
      </div>
    </div>
  </div>
</section>

<!-- ================= NETWORK ================= -->
<section class="page" id="p-network">
  <div class="cols">
    <div class="box">
      <h3>Access Point (rover link)</h3>
      <div class="kv">
        <b>Status</b><span id="nApState">--</span>
        <b>Address</b><span id="nApIp">--</span>
        <b>Associated stations</b><span id="nApN">0</span>
      </div>

      <label class="f">SSID</label>
      <input type="text" id="nSsid" maxlength="32">
      <label class="f">Password (empty = open, otherwise 8+ characters)</label>
      <input type="text" id="nPass" maxlength="63">
      <label class="f">Channel</label>
      <select id="nCh"></select>
      <label class="f">Hidden SSID</label>
      <select id="nHide"><option value="0">No</option><option value="1">Yes</option></select>
      <button class="btn" onclick="saveAp()">Apply access point settings</button>
      <div class="msg" id="nMsg"></div>
      <div class="note">
        Applying restarts the access point, so every associated client &mdash;
        including this browser &mdash; reconnects. Pick a channel that is quiet
        where you fly; a congested channel is the most common cause of
        correction latency spikes.
      </div>
    </div>

    <div class="box">
      <h3>WiFi Uplink (optional)</h3>
      <div class="kv">
        <b>Status</b><span id="nStaState">--</span>
        <b>SSID</b><span id="nStaSsid">--</span>
        <b>Address</b><span id="nStaIp">--</span>
        <b>Signal</b><span id="nStaRssi">--</span>
      </div>

      <button class="btn g" onclick="scanNets()">Scan networks</button>
      <label class="f">Found networks</label>
      <select id="nList" onchange="document.getElementById('nJoinSsid').value=this.value"></select>
      <label class="f">SSID</label>
      <input type="text" id="nJoinSsid">
      <label class="f">Password</label>
      <input type="text" id="nJoinPass">
      <button class="btn" onclick="joinNet()">Join network</button>
      <button class="btn r" onclick="forgetNet()">Forget</button>
      <div class="note">
        Purely optional. The access point stays up either way, so joining a
        network only adds a management route and internet-side access; it is not
        needed for RTCM output and the rover should not use it if latency
        matters.
      </div>
    </div>
  </div>

  <div class="box">
    <h3>Tailscale</h3>
    <div class="cols">
      <div>
        <div class="kv">
          <b>State</b><span id="tsState">--</span>
          <b>Tailnet address</b><span id="tsIp">--</span>
          <b>Peers</b><span id="tsPeers">--</span>
        </div>
        <label class="f">Enabled</label>
        <select id="tsEn"><option value="0">No</option><option value="1">Yes</option></select>
        <label class="f">Device name on the tailnet</label>
        <input type="text" id="tsName" maxlength="31" placeholder="rtk-base">
        <label class="f">Auth key (leave blank to keep the stored one)</label>
        <input type="text" id="tsKey" maxlength="95" placeholder="tskey-auth-...">
        <button class="btn" onclick="saveTailscale()">Apply Tailscale settings</button>
        <div class="msg" id="tsMsg"></div>
      </div>
      <div class="note">
        Puts this device on your tailnet, so the whole interface is reachable
        from any other machine signed into the same Tailscale account &mdash; no
        port forwarding, nothing exposed to the internet. The address shown
        above is what you open in a browser.<br><br>
        <b>Tailscale has no username and password for a device.</b> Enrolment
        uses an auth key: in the admin console open
        <i>Settings &rarr; Keys &rarr; Generate auth key</i>, tick
        <i>Reusable</i> and <i>Ephemeral</i>, and paste the
        <code>tskey-auth-&hellip;</code> string here. The key is stored on the
        device and is never sent back to this page.<br><br>
        <b>Needs the WiFi uplink.</b> The coordination server is on the
        internet, so the station interface has to be joined; the access point
        alone has no route off the device. The rover link and every RTCM output
        are unaffected either way.<br><br>
        <b>Changes take effect after a reboot.</b> The client reads its
        settings once when it starts, and stopping it in place is not safe
        here: the shutdown path blocks for three seconds and cannot reliably
        reclaim its 42 kB of task stacks, so restarting in place would both
        stall the base station and leak memory each time.<br><br>
        <b>This costs base station capacity.</b> The client holds a 32 kB map
        buffer and four task stacks for as long as it runs, on a board with
        320 kB of RAM and no PSRAM. To make room, simultaneous RTCM consumers
        are capped at two TCP/NTRIP and two UDP, the history window is six hours
        rather than twelve, and peer capacity is five tunnels. Everything else
        &mdash; every RTCM output, the NTRIP push, the rover link &mdash; runs
        exactly as it does with this off.
      </div>
    </div>
  </div>
</section>

<!-- ================= TERMINAL ================= -->
<!-- ================= HISTORY ================= -->
<section class="page" id="p-history">
  <div class="box">
    <h3>Recent History</h3>
    <div class="hsum" id="hsSummary"></div>
    <div class="note">
      Sampled every 30 seconds since the device booted, held in memory only:
      a reboot starts the window again. The window is six hours; it was twelve
      before the Tailscale client needed the memory. Move the pointer across any chart to
      read every value at that moment.
    </div>
  </div>

  <div class="box">
    <h3>Where the receiver thinks it is</h3>
    <canvas id="hPos" width="900" height="210"></canvas>
    <div class="legend">
      <span><i class="sw" style="background:#2f6fad"></i>North</span>
      <span><i class="sw" style="background:#c0392b"></i>East</span>
      <span><i class="sw" style="background:#2f8f47"></i>Up</span>
    </div>
    <div class="note">
      Movement of the receiver's own live solution, in centimetres from where it
      was when the window opened &mdash; <b id="hRefTxt">--</b>. The station is
      not moving, so everything here is measurement error. A flat trace with
      small noise is healthy; a slow ramp over hours is normal for a standalone
      solution and does not affect the fixed coordinate being broadcast. A sharp
      step means something actually changed: the antenna was touched, or the
      receiver switched modes.
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>Satellites in the solution</h3>
      <canvas id="hSats" width="440" height="150"></canvas>
      <div class="legend">
        <span><i class="sw" style="background:#2f6fad"></i>Used in the fix</span>
        <span><i class="sw" style="background:#9aa8b5"></i>Tracked</span>
      </div>
      <div class="note">
        The same two figures as the header, counted from GSV and GSA. Fewer
        satellites means weaker geometry, and a daily pattern usually means
        something is blocking part of the sky.<br><br>
        <b>Both are floors, not exact counts.</b> An NMEA GSA sentence carries at
        most twelve satellites and this module sends one per constellation, so
        GPS and BeiDou sit at twelve whenever the receiver is actually using
        more. The receiver's own figure in GGA runs higher still &mdash; higher
        than the number of satellites GSV reports as tracked &mdash; so on this
        dual-frequency module it is counting signals rather than satellites, and
        is not plotted here.
      </div>
    </div>
    <div class="box">
      <h3>Signal strength (mean C/N0)</h3>
      <canvas id="hCn0" width="440" height="150"></canvas>
      <div class="note">
        Averaged over every signal being tracked, weak low-elevation ones
        included, so the absolute number is lower than a single satellite's and
        is not meant to be compared against a fixed threshold. The dashed line
        is this window's own median: what matters is the trend away from it. A
        slow decline over days is the classic sign of a connector or cable
        letting water in.
      </div>
    </div>
  </div>

  <div class="cols">
    <div class="box">
      <h3>Position dilution (HDOP)</h3>
      <canvas id="hHdop" width="440" height="150"></canvas>
      <div class="note">Lower is better; under 1.0 is good geometry.</div>
    </div>
    <div class="box">
      <h3>Correction output</h3>
      <canvas id="hBps" width="440" height="150"></canvas>
      <div class="note">
        RTCM bytes per second leaving the device. A drop to zero is an outage
        rovers would have felt.
      </div>
    </div>
  </div>

  <div class="box">
    <h3>Fix quality and interference over time</h3>
    <div class="hstrip"><span class="hlab">Fix</span><canvas id="hFix" width="900" height="26"></canvas></div>
    <div class="hstrip"><span class="hlab">L1 band</span><canvas id="hJ1" width="900" height="26"></canvas></div>
    <div class="hstrip"><span class="hlab">L5 band</span><canvas id="hJ5" width="900" height="26"></canvas></div>
    <canvas id="hAxis" width="900" height="22"></canvas>
    <div class="legend" id="hStripLegend"></div>
    <div class="note">
      Each band is one continuous strip of colour, so an outage or a burst of
      interference is visible as a block rather than as a spike you have to
      catch. Interference showing up at the same time each day points at
      something on a timer nearby rather than at the receiver.
    </div>
  </div>
</section>

<section class="page" id="p-terminal">
  <div class="box">
    <h3>Serial Port Terminal</h3>
    <div class="termbar">
      <input type="text" id="cmdInput" placeholder="$PQTMVERNO  (checksum is added automatically)">
      <button class="btn" style="margin:0" onclick="sendCommand()">Send</button>
      <button class="btn g" style="margin:0" onclick="clearTerm()">Clear</button>
    </div>
    <label style="display:flex;align-items:center;gap:6px;font-size:12px;color:var(--mu);margin-bottom:8px">
      <input type="checkbox" id="termAll" style="width:auto"> Show periodic status messages
      ($PQTMSVINSTATUS, $PAIRSPF) &mdash; about three lines per second
    </label>
    <div id="term"></div>
    <div class="note">
      Commands starting with <code>$</code> get an NMEA checksum appended when
      none is present. Only module replies and proprietary sentences are shown;
      the GGA/GSA/GSV observation stream is consumed by the telemetry pages.
    </div>
  </div>
</section>

<!-- ================= ADMIN ================= -->
<section class="page" id="p-admin">
  <div class="cols">
    <div class="box">
      <h3>Device</h3>
      <div class="kv">
        <b>GNSS module</b><span id="aModel">--</span>
        <b>Module firmware</b><span id="aVer">--</span>
        <b>Module build date</b><span id="aBd">--</span>
        <b>ESP32 firmware</b><span id="aEsp">--</span>
        <b>Uptime</b><span id="aUp">--</span>
        <b>Free heap</b><span id="aHeap">--</span>
        <b>CPU load core 0 / core 1</b><span id="aCpu">--</span>
        <b>PPS</b><span id="aPps">--</span>
      </div>
      <div class="note">
        Serial2 to the module runs at 115200 8N1 on GPIO17 (RX) / GPIO16 (TX),
        with the PPS pulse on GPIO27.
      </div>
    </div>
    <div class="box">
      <h3>Actions</h3>
      <div class="note">Reboot restarts the ESP32 only; the GNSS module keeps its own configuration.</div>
      <button class="btn g" onclick="baseApi('query')">Re-read module configuration</button>
      <button class="btn r" onclick="doReboot()">Reboot ESP32</button>
      <div class="msg" id="aMsg"></div>
    </div>
  </div>

  <div class="box">
    <h3>Settings Backup</h3>
    <div class="cols">
      <div>
        <button class="btn" onclick="downloadBackup()">Download settings as a file</button>
        <div class="note" style="margin-top:8px">
          Saves everything this device holds: access point and station
          credentials, all four output transports, the NTRIP caster and push
          settings, and the antenna reference point offset. The station
          coordinate and survey settings are included as well &mdash; those live
          in the module's own memory, and this is the only copy on the ESP32
          side.
        </div>
      </div>
      <div>
        <label class="f">Restore from a backup file</label>
        <input type="file" id="aRestoreFile" accept=".json,application/json">
        <button class="btn r" onclick="uploadBackup()">Restore these settings</button>
        <div class="msg" id="aBkMsg"></div>
        <div class="note" style="margin-top:8px">
          Applies every field present in the file and reboots. Anything the file
          omits is left alone. The station coordinate is written back to the
          module, so restoring a backup taken at a different site will move the
          base &mdash; check the file before applying it.
        </div>
      </div>
    </div>
    <div class="note">
      <b>The file contains passwords in plain text</b> &mdash; the WiFi key, the
      NTRIP caster password and the push password. That is what makes it a
      usable backup, but keep it somewhere you would keep those passwords.
    </div>
  </div>
</section>

</main>

<script>
// Mirrors the GnssSys table in src/gnss/GNSS_Core.cpp - order matters.
const SYS = [
  {n:'GPS',     c:'G', col:'#7b5fd6', bands:['L1','L2','L5']},
  {n:'GLONASS', c:'R', col:'#f5901e', bands:['G1','G2']},
  {n:'Galileo', c:'E', col:'#a78bfa', bands:['E1','E5a','E5b']},
  {n:'BeiDou',  c:'C', col:'#2e9e4f', bands:['B1','B2','B3']},
  {n:'QZSS',    c:'J', col:'#e0479e', bands:['L1','L2','L5']},
  {n:'SBAS',    c:'S', col:'#4169e1', bands:['L1']},
  {n:'NavIC',   c:'I', col:'#00a0a0', bands:['L5','S']}
];
const BAND_COL = ['#4caf50','#1565c0','#e53935'];
const IONO_SHELL = 350;
// Fixed half-window for the ionosphere map, degrees of latitude. At a 350 km
// shell the pierce point sits 1.6 deg from the station for a satellite at 60
// deg elevation, 4.8 at 30 deg and 9.6 at 13 deg, so ten degrees covers the
// whole visible constellation. Fixing it stops the frame resizing every time a
// satellite rises or sets.
const IONO_HALF = 10;
const FIXQ = ['Invalid','GPS fix','DGNSS','PPS fix','RTK fixed','RTK float','Estimated'];
const BASE_MODE = {'-1':'Unknown','0':'Disabled','1':'Survey-In','2':'Fixed'};
const PUSH_ST = ['off','waiting','connecting','handshake','streaming','error'];
const SVIN_V  = ['Invalid','In progress','Valid'];
const JAM_TXT = ['unknown','clean','warning','critical'];

let D = null, page = 'overview';
// The device sends the satellite and ionosphere arrays every other tick to keep
// frames small; carry the last ones forward on the ticks that omit them.
let lastSig = [], lastIo = [], lastRx = 0, staleTimer = null;
let baseFormLoaded = false, outFormLoaded = false, netFormLoaded = false;
let tsFormLoaded = false, termLines = [];
let scatter = [];   // {e, n, h} in metres relative to the running mean

/* ---------------- routing ---------------- */
function route(){
  let p = (location.hash || '#overview').slice(1);
  if(!document.getElementById('p-' + p)) p = 'overview';
  page = p;
  document.querySelectorAll('.page').forEach(s => s.classList.toggle('on', s.id === 'p-' + p));
  document.querySelectorAll('nav a').forEach(a => a.classList.toggle('on', a.dataset.p === p));
  document.getElementById('crumb').textContent =
    ({overview:'Overview', gnss:'GNSS > Satellites and Signals', base:'Base Mode > Position Mode',
      output:'Data Output > RTCM Distribution', network:'Network > Interfaces',
      history:'History > Recent',
      terminal:'Terminal', admin:'Admin'})[p];
  if(p === 'history') loadHistory(true);
  if(D) render();
}
window.addEventListener('hashchange', route);

/* A canvas draws into a bitmap sized by its width/height attributes. On a
   HiDPI screen that bitmap is stretched to twice as many physical pixels, so
   every label rendered into it goes soft - measured here as devicePixelRatio 2
   with a 1:1 backing store, and worse again on the flex-stretched strips. Size
   the buffer in device pixels, pin the CSS box to the layout size, and scale
   the context once; every drawing routine below then keeps working in CSS
   pixels without knowing about any of this. */
function hidpi(cv){
  if(!cv) return null;
  const dpr = Math.min(window.devicePixelRatio || 1, 3);
  // Design size from the markup, captured once: re-reading it after the buffer
  // has been resized would compound on every draw.
  if(!cv.__w){ cv.__w = cv.width; cv.__h = cv.height; }
  const r = cv.getBoundingClientRect();
  // A hidden page measures zero, and flex-stretched canvases measure wider
  // than the markup says.
  const W = r.width  > 10 ? Math.round(r.width)  : cv.__w;
  const H = r.height >  4 ? Math.round(r.height) : cv.__h;
  if(cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)){
    cv.width  = Math.round(W * dpr);
    cv.height = Math.round(H * dpr);
    cv.style.width  = W + 'px';
    cv.style.height = H + 'px';
  }
  const ctx = cv.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return {ctx, W, H};
}

// Pinning the CSS box stops a canvas growing back when the window does, so the
// pins are dropped on resize and the next draw re-measures.
let hidpiTimer = null;
window.addEventListener('resize', () => {
  clearTimeout(hidpiTimer);
  hidpiTimer = setTimeout(() => {
    document.querySelectorAll('canvas').forEach(c => {
      c.style.width = ''; c.style.height = '';
    });
    if(typeof D !== 'undefined' && D) render();
    if(page === 'history') drawHistory();
  }, 180);
});

/* ---------------- helpers ---------------- */
function $(id){ return document.getElementById(id); }
function txt(id, v){ const e = $(id); if(e) e.textContent = v; }
function esc(s){ return String(s).replace(/[&<>"']/g, c =>
  ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }
function dur(s){
  s = Math.floor(s);
  const d = Math.floor(s/86400), h = Math.floor(s%86400/3600),
        m = Math.floor(s%3600/60), ss = s%60;
  return (d ? d + 'd ' : '') + String(h).padStart(2,'0') + ':' +
         String(m).padStart(2,'0') + ':' + String(ss).padStart(2,'0');
}
function dms(v, isLat){
  if(!isFinite(v)) return '--';
  const hemi = isLat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E');
  v = Math.abs(v);
  const d = Math.floor(v), m = Math.floor((v - d) * 60), s = ((v - d) * 60 - m) * 60;
  return hemi + d + '°' + String(m).padStart(2,'0') + "'" + s.toFixed(4) + '"';
}
function dot(id, cls, title){
  const e = $(id); if(!e) return;
  e.className = 'dot' + (cls ? ' ' + cls : '');
  if(title) e.title = title;
}

/* ---------------- websocket ---------------- */
let sock;
function initWs(){
  // location.host keeps the port; hostname would silently drop it and only
  // happen to work because the device serves HTTP on 80.
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  sock = new WebSocket(`${proto}://${location.host}/ws`);
  sock.onopen = () => {
    $('link').innerHTML = '&#9679; connected';
    $('link').style.color = '#2e7d32';
    log('sy', 'Connection established.');
  };
  sock.onclose = () => {
    $('link').innerHTML = '&#9679; reconnecting';
    $('link').style.color = '#c0392b';
    log('sy', 'Connection lost, retrying...');
    setTimeout(initWs, 2000);
  };
  sock.onmessage = onMessage;

  // A late frame is a lost WiFi packet being retransmitted, not a dead device.
  // Saying so beats a page that silently stops changing.
  if(staleTimer) clearInterval(staleTimer);
  staleTimer = setInterval(() => {
    if(!lastRx || sock.readyState !== 1) return;
    const age = (Date.now() - lastRx) / 1000;
    if(age > 2.5){
      $('link').innerHTML = '&#9679; link slow, ' + age.toFixed(0) + ' s';
      $('link').style.color = '#b8860b';
    }
  }, 500);
}
function onMessage(ev){
  const s = ev.data;
  if(typeof s !== 'string') return;
  if(s.startsWith('TERMP:')){          // periodic status, hidden unless asked for
    const box = $('termAll');
    if(box && box.checked) log('rx', s.slice(6));
    return;
  }
  if(s.startsWith('TERM:')){ log('rx', s.slice(5)); return; }
  if(s.startsWith('TXCMD:')){ log('tx', s.slice(6)); return; }
  let n;
  try { n = JSON.parse(s); } catch(e){ return; }
  if(n.sig) lastSig = n.sig; else n.sig = lastSig;
  if(n.io)  lastIo  = n.io;  else n.io  = lastIo;
  D = n;
  lastRx = Date.now();
  $('link').innerHTML = '&#9679; connected';
  $('link').style.color = '#2e7d32';
  render();
}

/* ---------------- terminal ---------------- */
function log(kind, msg){
  const t = new Date().toLocaleTimeString('en-GB', {hour12:false});
  const tag = kind === 'tx' ? 'TX' : kind === 'rx' ? 'RX' : '--';
  termLines.push({kind, html: `<span style="color:#5b6b7a">[${t}]</span> <span class="${kind}">${tag}</span> ${esc(msg)}`});
  if(termLines.length > 300) termLines.shift();
  const box = $('term');
  if(!box) return;
  const atBottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
  const div = document.createElement('div');
  div.innerHTML = termLines[termLines.length - 1].html;
  box.appendChild(div);
  while(box.children.length > 300) box.removeChild(box.firstChild);
  if(atBottom) box.scrollTop = box.scrollHeight;
}
function clearTerm(){ termLines = []; $('term').innerHTML = ''; }
function sendCommand(){
  const input = $('cmdInput');
  let cmd = input.value.trim();
  if(!cmd) return;
  if(cmd.startsWith('$') && !cmd.includes('*')){
    let cs = 0;
    for(let i = 1; i < cmd.length; i++) cs ^= cmd.charCodeAt(i);
    cmd += '*' + cs.toString(16).toUpperCase().padStart(2, '0');
  }
  fetch('/cmd?c=' + encodeURIComponent(cmd))
    .then(r => { if(r.ok) log('tx', cmd); else log('sy', 'Command rejected.'); })
    .catch(() => log('sy', 'Module unreachable.'));
  input.value = '';
}
document.addEventListener('keyup', e => {
  if(e.key === 'Enter' && e.target && e.target.id === 'cmdInput') sendCommand();
});

/* ---------------- render ---------------- */
function render(){
  renderHeader();
  if(page === 'overview') renderOverview();
  else if(page === 'gnss') renderGnss();
  else if(page === 'base') renderBase();
  else if(page === 'output') renderOutput();
  else if(page === 'network'){ renderNetwork(); renderTailscale(); }
  else if(page === 'admin') renderAdmin();
  // Fetched rather than pushed: the ring buffer only advances every 30 s, so
  // there is nothing to gain from putting it on the 1 Hz telemetry message.
  else if(page === 'history') loadHistory(false);
}

// NetState: 0 AP only, 1 connecting, 2 station up, 3 reconnecting.
function staUp(){ return D.net === 2 && D.ip && D.ip !== '0.0.0.0'; }
function trackedTotal(){ return D.cons.reduce((a, c) => a + c[0], 0); }
function usedTotal(){ return D.cons.reduce((a, c) => a + c[1], 0); }
function meanCn0(){
  if(!D.sig.length) return 0;
  return D.sig.reduce((a, s) => a + s[4], 0) / D.sig.length;
}

function renderHeader(){
  txt('hModel', D.model);
  txt('hRx', D.model);
  // The AP address is the one that matters: it is where the rover connects.
  txt('hIp', D.ap);
  txt('hUp', dur(D.up));
  txt('hLat', D.vloc ? dms(D.lat, true) : '--');
  txt('hLon', D.vloc ? dms(D.lon, false) : '--');
  txt('hHgt', D.valt ? D.alt.toFixed(3) + ' m' : '--');
  txt('hTrk', trackedTotal() + ' / ' + usedTotal() + ' in fix');
  txt('hTime', D.time);
  txt('hBase', operatingState());

  const q = FIXQ[D.fq] || 'Invalid';
  txt('lFix', q);
  dot('dFix', D.fq === 4 ? 'rtk' : D.fq === 5 ? 'ok' : D.fq >= 1 ? 'warn' : 'bad');

  const overall = qualityScore();
  dot('dQual', overall >= 7 ? 'ok' : overall >= 4 ? 'warn' : 'bad', overall + '/10');

  const corr = D.base.rtcm >= 0 && D.base.m > 0 && D.rtcm > 0;
  dot('dCorr', corr ? 'ok' : D.base.m > 0 ? 'warn' : 'bad');
  txt('lCorr', corr ? D.rtcm + ' pkt/s out' : 'No corrections');

  // The access point never goes down, so this is always green; the label just
  // says whether an uplink is also joined.
  dot('dNet', 'ok');
  txt('lNet', staUp() ? 'AP + STA ' + D.ip : 'AP ' + D.ap);

  const consumers = D.tcp + D.udp;
  dot('dTcp', consumers > 0 ? 'ok' : '');
  txt('lTcp', consumers + ' consumer' + (consumers === 1 ? '' : 's'));

  dot('dPps', D.pps ? 'ok' : 'bad');

  // $PAIR391 interference status; 0 unknown, 1 clean, 2 warning, 3 critical.
  const j = D.jam;
  if(j.feat === 2){
    dot('dJam', '');
    txt('lJam', 'Spectrum n/a');
  } else {
    const worst = Math.max(j.l1 || 0, j.l5 >= 0 ? j.l5 : 0);
    dot('dJam', worst === 1 ? 'ok' : worst === 2 ? 'warn' : worst === 3 ? 'bad' : '');
    txt('lJam', ['Spectrum unknown','Spectrum clean','Interference warning','Interference critical'][worst] || 'Spectrum');
  }

  const p = D.push;
  dot('dPush', p.st === 4 ? 'ok' : p.st === 0 ? '' : p.st === 5 ? 'bad' : 'warn');
  txt('lPush', !p.en ? 'No push' : p.st === 4 ? 'Pushing to caster' : 'Push: ' + PUSH_ST[p.st]);
}

function qualityScore(){
  const cn = meanCn0(), trk = trackedTotal();
  let s = 0;
  s += Math.min(4, Math.round(trk / 6));
  s += Math.min(3, Math.round((cn - 25) / 5));
  s += D.fq >= 4 ? 3 : D.fq >= 1 ? 1 : 0;
  return Math.max(0, Math.min(10, s));
}

function bars(score){
  let h = '<div class="bars">';
  const cls = score >= 7 ? 'f' : score >= 4 ? 'w' : 'b';
  for(let i = 0; i < 5; i++) h += `<i class="${i < Math.round(score / 2) ? cls : ''}"></i>`;
  return h + '</div>';
}
function qIcon(kind){
  const c = {sat:'#7b5fd6', sig:'#3a9d43', cpu:'#e8a33d', rtcm:'#2f6fad', all:'#5c7089'}[kind];
  if(kind === 'sat')  return `<svg width="40" height="34" viewBox="0 0 40 34"><circle cx="20" cy="17" r="6" fill="${c}"/><ellipse cx="20" cy="17" rx="17" ry="7" fill="none" stroke="${c}" stroke-width="2" transform="rotate(-25 20 17)"/></svg>`;
  if(kind === 'sig')  return `<svg width="40" height="34" viewBox="0 0 40 34">${[0,1,2,3].map(i=>`<rect x="${8+i*7}" y="${26-i*5}" width="5" height="${4+i*5}" fill="${c}"/>`).join('')}</svg>`;
  if(kind === 'cpu')  return `<svg width="40" height="34" viewBox="0 0 40 34"><rect x="12" y="9" width="16" height="16" rx="2" fill="${c}"/>${[0,1,2].map(i=>`<rect x="${15+i*5}" y="4" width="2" height="5" fill="${c}"/><rect x="${15+i*5}" y="25" width="2" height="5" fill="${c}"/>`).join('')}</svg>`;
  if(kind === 'rtcm') return `<svg width="40" height="34" viewBox="0 0 40 34"><path d="M8 24 L20 8 L32 24" fill="none" stroke="${c}" stroke-width="3"/><circle cx="20" cy="26" r="3" fill="${c}"/></svg>`;
  return `<svg width="40" height="34" viewBox="0 0 40 34"><circle cx="20" cy="17" r="12" fill="none" stroke="${c}" stroke-width="3"/><path d="M14 17 l4 4 l8 -9" fill="none" stroke="${c}" stroke-width="3"/></svg>`;
}

function renderOverview(){
  const cn = meanCn0(), trk = trackedTotal();
  const cpuScore = 10 - Math.round(Math.max(D.cpu0, D.cpu1) / 10);
  const rtcmScore = D.base.m <= 0 ? 0 : D.rtcm > 0 ? 10 : 2;
  const items = [
    ['all',  'Overall',        qualityScore()],
    ['sat',  'Satellites',     Math.min(10, Math.round(trk / 3))],
    ['sig',  'Signal level',   Math.max(0, Math.min(10, Math.round((cn - 20) / 2.5)))],
    ['cpu',  'CPU headroom',   Math.max(0, cpuScore)],
    ['rtcm', 'RTCM output',    rtcmScore]
  ];
  $('qi').innerHTML = items.map(([k, name, sc]) =>
    `<div>${qIcon(k)}<div class="t">${name}</div>${bars(sc)}<div class="v">${sc}/10</div></div>`).join('');

  updateFan('fan');

  txt('oBase', operatingState());
  txt('oRtcm', D.rtcm + ' pkt/s');
  txt('oMsm', D.base.rtcm === 1 ? 'MSM7' : D.base.rtcm === 0 ? 'MSM4' : D.base.rtcm === -1 ? 'Disabled' : 'Unknown');
  txt('oArp', D.base.arp === 1 ? 'Enabled' : D.base.arp === 0 ? 'Disabled' : 'Unknown');
  txt('oEph', D.base.eph === 1 ? 'Enabled' : D.base.eph === 0 ? 'Disabled' : 'Unknown');
  txt('oTcp', D.tcp);

  const sv = D.svin;
  if(surveyRunning() && D.base.dur > 0){
    $('oSvin').style.display = 'block';
    // Prefer the receiver's own observation count over the ESP-side timer, so
    // this panel and the Base Mode page never disagree.
    const live = sv.feat === 1 && sv.dur > 0;
    $('oSvinBar').style.width =
      Math.min(100, (live ? sv.obs / sv.dur : D.base.el / D.base.dur) * 100).toFixed(1) + '%';
    txt('oSvinTxt', live
      ? `Survey-in running: ${sv.obs} of ${sv.dur} observations, mean accuracy ${sv.acc.toFixed(2)} m.`
      : `Survey-in running: ${dur(D.base.el)} of ${dur(D.base.dur)} minimum, target accuracy ${D.base.acc} m.`);
  } else if(D.base.m === 1){
    $('oSvin').style.display = 'block';
    $('oSvinBar').style.width = '100%';
    txt('oSvinTxt', 'Survey-in complete \u2014 the receiver is broadcasting the surveyed ' +
      'position. Its configured mode is still survey-in, so a power cycle starts the ' +
      'averaging again. Use "Adopt result as fixed position" on the Base Mode page, then ' +
      'Save, to make it permanent.');
  } else {
    $('oSvin').style.display = 'none';
  }

  txt('oLat', D.vloc ? D.lat.toFixed(8) + '°' : '--');
  txt('oLon', D.vloc ? D.lon.toFixed(8) + '°' : '--');
  txt('oHgt', D.valt ? D.alt.toFixed(3) + ' m' : '--');
  txt('oSep', D.sep ? D.sep.toFixed(3) + ' m' : '--');
  // GGA reports height above mean sea level; the ellipsoidal height that base
  // coordinates are expressed in is that plus the geoid separation. Labelling
  // the MSL value "ellipsoidal" would invite a ~36 m error when someone copies
  // it into the fixed-mode form.
  txt('oHgtE', D.valt ? (D.alt + (D.sep || 0)).toFixed(3) + ' m' : '--');
  txt('oMode', (D.ft === 3 ? '3D' : D.ft === 2 ? '2D' : 'No fix') + ' – ' + (FIXQ[D.fq] || 'Invalid'));
  txt('oSiu', D.siu + ' (GGA) / ' + usedTotal() + ' (GSA)');
  txt('oDop', [D.pdop, D.hdop, D.vdop].map(v => v ? v.toFixed(2) : '--').join(' / '));

  pushScatter();
  drawScatter();
  drawIono();
}

const REDUCE_MOTION = window.matchMedia &&
  window.matchMedia('(prefers-reduced-motion: reduce)').matches;

// Small satellite glyph: body plus two solar panels, centred on (x, y).
function satGlyph(id, i, x, y){
  return `<g id="${id}-s${i}" class="fansat">
    <rect x="${x-2.3}" y="${y-2.3}" width="4.6" height="4.6" rx="1"/>
    <rect x="${x-7.4}" y="${y-1.6}" width="3.8" height="3.2" rx=".6"/>
    <rect x="${x+3.6}" y="${y-1.6}" width="3.8" height="3.2" rx=".6"/>
    <rect x="${x-3.6}" y="${y-.35}" width="1.3" height=".7"/>
    <rect x="${x+2.3}" y="${y-.35}" width="1.3" height=".7"/>
  </g>`;
}

// Static skeleton. Ids are prefixed with the host element so the Overview and
// GNSS copies never collide.
function fanSvg(id){
  const n = SYS.length, h = 34 + n * 21, gx = 46, gy = h / 2, sx = 150, ex = 74;
  let out = `<svg viewBox="0 0 470 ${h}" style="width:100%;max-width:480px;display:block;margin:0 auto">`;
  out += `<defs><radialGradient id="${id}-gl" cx="34%" cy="30%">
            <stop offset="0%" stop-color="#fbfdff"/>
            <stop offset="62%" stop-color="#d8e7f4"/>
            <stop offset="100%" stop-color="#b3cadd"/>
          </radialGradient></defs>`;

  // Every link is drawn satellite -> globe, so the sweep travels in the same
  // direction the data does. pathLength normalises the dash pattern so the
  // short middle links and the long outer ones pulse at the same rate.
  for (let i = 0; i < n; i++) {
    const y = 22 + i * 21;
    const geom = `x1="${sx}" y1="${y}" x2="${ex}" y2="${gy}" pathLength="100"`;
    out += `<line id="${id}-b${i}" class="fanbase"  ${geom}/>`;
    out += `<line id="${id}-l${i}" class="fanlive"  ${geom}/>`;
    out += `<line id="${id}-g${i}" class="fanglow"  ${geom} style="animation-delay:-${(i*0.27).toFixed(2)}s"/>`;
    out += `<line id="${id}-f${i}" class="fansweep" ${geom} style="animation-delay:-${(i*0.27).toFixed(2)}s"/>`;
  }

  out += `<circle id="${id}-ring" cx="${gx}" cy="${gy}" r="27" fill="none"
            stroke="#63d47e" stroke-width="2" opacity="0">` +
         (REDUCE_MOTION ? '' :
          `<animate attributeName="r" values="27;35" dur="1.9s" repeatCount="indefinite"/>
           <animate attributeName="opacity" values="0.35;0" dur="1.9s" repeatCount="indefinite"/>`) +
         `</circle>`;

  out += `<circle cx="${gx}" cy="${gy}" r="27" fill="url(#${id}-gl)" stroke="#7d99b3" stroke-width="1.3"/>`;
  out += `<g fill="none" stroke="#7d99b3" stroke-width=".9" opacity=".75">
            <path d="M${gx} ${gy-27} v54"/>
            <ellipse cx="${gx}" cy="${gy}" rx="13" ry="27"/>
            <path d="M${gx-27} ${gy} h54"/>
            <path d="M${gx-24.5} ${gy-11} h49"/>
            <path d="M${gx-24.5} ${gy+11} h49"/>
          </g>`;
  out += `<text id="${id}-mode" x="${gx}" y="${h-6}" text-anchor="middle"
            font-family="Arial" font-size="11" fill="#6c7f93"></text>`;

  for (let i = 0; i < n; i++) {
    const y = 22 + i * 21;
    out += satGlyph(id, i, 165, y);
    out += `<text id="${id}-t${i}" x="176" y="${y+4}" font-family="Arial" font-size="12" fill="#20344a"></text>`;
  }
  return out + '</svg>';
}

function updateFan(hostId){
  const host = $(hostId);
  if(!host) return;
  if(host.dataset.built !== '1'){
    host.innerHTML = fanSvg(hostId);
    host.dataset.built = '1';
  }

  let anyActive = false;
  SYS.forEach((s, i) => {
    const c = D.cons[i] || [0, 0];
    const on = c[0] > 0;
    if(on) anyActive = true;

    // SVG elements need setAttribute: .className is read-only there.
    const set = (suffix, base) => {
      const el = $(hostId + suffix + i);
      if(el) el.setAttribute('class', base + (on ? ' on' : ''));
    };
    set('-l', 'fanlive');
    set('-g', 'fanglow');
    set('-f', 'fansweep');
    set('-s', 'fansat');

    const lab = $(hostId + '-t' + i);
    if(lab) lab.textContent = `${s.n} (Position: ${c[1]}, Track: ${c[0]})`;
  });

  const mode = $(hostId + '-mode');
  if(mode) mode.textContent = FIXQ[D.fq] || 'No fix';

  const ring = $(hostId + '-ring');
  if(ring) ring.style.display = anyActive ? '' : 'none';
}

/* ---------------- GNSS page ---------------- */
function renderGnss(){
  updateFan('fan2');
  renderBandTable();
  drawSky();
  buildSysSelect();
  drawCn0();
  drawCnEl();
  buildSatTable();

  $('skyLegend').innerHTML = SYS.map((s, i) =>
    (D.cons[i] && D.cons[i][0] ? `<span><i class="sw" style="background:${s.col}"></i>${s.n}</span>` : '')).join('');
  txt('skyCount', ` Tracking ${trackedTotal()} satellites / ${D.sig.length} signals, ${usedTotal()} in position.`);
}

function renderBandTable(){
  const t = $('bandTable');
  let sigTotal = 0;
  t.tBodies[0].innerHTML = SYS.map((s, i) => {
    const c = D.cons[i] || [0, 0], b = D.bands[i] || [];
    if(!c[0]) return '';
    const cells = b.map((n, k) =>
      `<span style="display:inline-block;min-width:64px"><i class="sw" style="display:inline-block;background:${BAND_COL[k]};margin-right:4px;vertical-align:-1px"></i>${s.bands[k]}: <b>${n}</b></span>`).join(' ');
    sigTotal += b.reduce((a, n) => a + n, 0);
    return `<tr><td><b style="color:${s.col}">${s.n}</b></td><td>${c[0]}</td><td>${c[1]}</td>
            <td style="text-align:left">${cells}</td></tr>`;
  }).join('');
  t.tFoot.innerHTML = `<tr><th>Total</th><th>${trackedTotal()}</th><th>${usedTotal()}</th>
      <th style="text-align:left">${sigTotal} signals</th></tr>`;
}

function drawSky(){
  const hp = hidpi($('sky')); if(!hp) return;
  const {ctx, W, H} = hp, cx = W/2, cy = H/2, R = cx - 30;
  ctx.clearRect(0, 0, W, H);

  ctx.strokeStyle = '#7ba7d4'; ctx.lineWidth = 1.4;
  ctx.beginPath(); ctx.arc(cx, cy, R, 0, 2*Math.PI); ctx.stroke();
  ctx.strokeStyle = '#ccd7e2'; ctx.lineWidth = 1;
  [R*2/3, R/3].forEach(r => { ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2*Math.PI); ctx.stroke(); });
  ctx.beginPath(); ctx.moveTo(cx, cy-R); ctx.lineTo(cx, cy+R);
  ctx.moveTo(cx-R, cy); ctx.lineTo(cx+R, cy); ctx.stroke();

  ctx.fillStyle = '#6c7f93'; ctx.font = '11px Arial'; ctx.textAlign = 'center';
  ctx.fillText('N', cx, cy-R-10); ctx.fillText('S', cx, cy+R+16);
  ctx.fillText('E', cx+R+12, cy+4); ctx.fillText('W', cx-R-12, cy+4);

  // One marker per satellite: pick the strongest band so dual-frequency
  // satellites are not drawn twice.
  const best = new Map();
  D.sig.forEach(s => {
    if(s[2] === 0 && s[3] === 0) return; // no orbital position yet
    const k = s[0] + ':' + s[1];
    const prev = best.get(k);
    if(!prev || s[4] > prev[4]) best.set(k, s);
  });

  best.forEach(s => {
    const sys = SYS[s[0]] || SYS[0];
    const rr = R * (1 - Math.min(90, Math.max(0, s[2])) / 90);
    const ang = (s[3] - 90) * Math.PI / 180;
    const x = cx + rr * Math.cos(ang), y = cy + rr * Math.sin(ang);
    const used = s[6] === 1;

    ctx.strokeStyle = sys.col;
    ctx.globalAlpha = used ? 1 : 0.45;
    ctx.lineWidth = used ? 4 : 2.5;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(x-7, y); ctx.lineTo(x+7, y);
    ctx.moveTo(x, y-7); ctx.lineTo(x, y+7);
    ctx.stroke();
    ctx.globalAlpha = 1;

    ctx.fillStyle = '#20344a'; ctx.font = 'bold 11px Arial'; ctx.textAlign = 'left';
    ctx.fillText(sys.c + String(s[1]).padStart(2, '0'), x + 9, y - 6);
  });
}

function buildSysSelect(){
  const sel = $('cnSys');
  if(sel.dataset.built) return;
  sel.dataset.built = '1';
  sel.innerHTML = '<option value="-1">All</option>' +
    SYS.map((s, i) => `<option value="${i}">${s.n}</option>`).join('');
  sel.value = '0';
  sel.onchange = drawCn0;
}

function drawCn0(){
  const hp = hidpi($('cn0')); if(!hp) return;
  const {ctx, W, H} = hp;
  ctx.clearRect(0, 0, W, H);
  if(!D) return;

  const filter = parseInt($('cnSys').value, 10);
  let list = D.sig.filter(s => filter < 0 || s[0] === filter);
  list.sort((a, b) => a[0] - b[0] || a[1] - b[1] || a[5] - b[5]);

  const L = 34, B = 34, top = 10;
  const plotW = W - L - 10, plotH = H - B - top;

  ctx.strokeStyle = '#e6ecf1'; ctx.fillStyle = '#6c7f93';
  ctx.font = '10px Arial'; ctx.textAlign = 'right';
  for(let v = 0; v <= 60; v += 10){
    const y = top + plotH - v / 60 * plotH;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W-10, y); ctx.stroke();
    ctx.fillText(v, L - 5, y + 3);
  }

  if(!list.length){
    ctx.textAlign = 'center'; ctx.fillStyle = '#9aa8b5';
    ctx.fillText('No signals tracked', W/2, H/2);
    $('cnLegend').innerHTML = '';
    return;
  }

  // Group by satellite so the bands of one satellite sit next to each other.
  const groups = [];
  list.forEach(s => {
    const key = s[0] + ':' + s[1];
    let g = groups.find(g => g.key === key);
    if(!g){ g = {key, sys: s[0], prn: s[1], sigs: []}; groups.push(g); }
    g.sigs.push(s);
  });

  const slot = plotW / groups.length;
  const bw = Math.max(3, Math.min(11, slot / 4));

  groups.forEach((g, gi) => {
    const base = L + gi * slot + slot/2 - (g.sigs.length * bw) / 2;
    g.sigs.forEach((s, si) => {
      const h = Math.min(60, s[4]) / 60 * plotH;
      ctx.fillStyle = BAND_COL[s[5]] || '#888';
      ctx.fillRect(base + si * bw, top + plotH - h, bw - 1, h);
    });
    if(slot > 16){
      ctx.save();
      ctx.translate(L + gi * slot + slot/2, H - B + 8);
      ctx.rotate(-Math.PI/2.6);
      ctx.fillStyle = '#6c7f93'; ctx.font = '10px Arial'; ctx.textAlign = 'right';
      ctx.fillText((SYS[g.sys] ? SYS[g.sys].c : '?') + String(g.prn).padStart(2, '0'), 0, 0);
      ctx.restore();
    }
  });

  // Band labels are constellation specific, so only name them when a single
  // system is selected; otherwise fall back to the frequency slot number.
  const usedBands = [...new Set(list.map(s => s[5]))].sort();
  $('cnLegend').innerHTML = usedBands.map(b =>
    `<span><i class="sw" style="background:${BAND_COL[b]}"></i>${
      filter < 0 ? 'Band ' + (b + 1) : (SYS[filter].bands[b] || 'Band ' + (b + 1))}</span>`).join('') +
    '<span style="color:#6c7f93">C/N0 in dB-Hz</span>';
}

function buildSatTable(){
  const rows = D.sig.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1] || a[5] - b[5]);
  $('satTable').tBodies[0].innerHTML = rows.map(s => {
    const sys = SYS[s[0]] || SYS[0];
    return `<tr>
      <td><b style="color:${sys.col}">${sys.c}${String(s[1]).padStart(2,'0')}</b></td>
      <td>${sys.n}</td><td>${sys.bands[s[5]] || '-'}</td>
      <td>${s[2]}°</td><td>${s[3]}°</td><td>${s[4]}</td>
      <td>${s[6] ? 'yes' : '-'}</td></tr>`;
  }).join('');
}

/* ---------------- Base mode page ---------------- */
// The module leaves its configured mode at "survey-in" even after the survey
// finishes and it starts transmitting the surveyed coordinates. Operating state
// is therefore derived from what is on the wire, not from the configuration.
// Single source of truth for "is a survey actually in progress". Configured
// mode alone is not enough: the module finishes the survey and starts
// broadcasting the surveyed coordinates without ever rewriting its
// configuration, so anything keyed on base.m alone claims it is still
// surveying forever.
function surveyRunning(){
  const b = D.base, sv = D.svin, bc = D.bc;
  if(b.m !== 1) return false;
  // $PQTMSVINSTATUS is authoritative when the firmware provides it.
  if(sv.feat === 1) return sv.v !== 2;
  // Otherwise infer it: 1005 is emitted throughout the survey, so only a
  // coordinate that has stopped changing means the survey has settled.
  return !(bc.v && bc.age >= 0 && bc.age < 30 && bc.st > 30);
}

function operatingState(){
  const b = D.base, bc = D.bc;
  if(b.m === 2) return 'Fixed';
  if(b.m === 1){
    if(surveyRunning()) return 'Survey-In running';
    return D.svin.feat === 1 ? 'Fixed (survey-in complete)'
                             : 'Fixed (station position settled)';
  }
  if(b.m === 0) return bc.v ? 'Broadcasting, mode disabled' : 'Disabled';
  return 'Unknown';
}

function renderBase(){
  const b = D.base, sv = D.svin, bc = D.bc;
  txt('cOper', operatingState());
  txt('cMode', BASE_MODE[b.m] || 'Unknown');

  txt('bcId', bc.v ? bc.id : '--');
  txt('bcX', bc.v ? bc.x.toFixed(4) + ' m' : '--');
  txt('bcY', bc.v ? bc.y.toFixed(4) + ' m' : '--');
  txt('bcZ', bc.v ? bc.z.toFixed(4) + ' m' : '--');
  txt('bcAge', bc.age < 0 ? 'never seen' : bc.age + ' s ago');
  txt('bcSt', bc.v ? dur(bc.st) : '--');
  txt('bcLat', bc.v ? bc.lat.toFixed(8) + '°' : '--');
  txt('bcLon', bc.v ? bc.lon.toFixed(8) + '°' : '--');
  txt('bcHgt', bc.v ? bc.hgt.toFixed(3) + ' m' : '--');
  if(bc.v && D.vloc){
    // Rough local metres; good enough to spot a wrong station coordinate.
    const dN = (D.lat - bc.lat) * 111320;
    const dE = (D.lon - bc.lon) * 111320 * Math.cos(bc.lat * Math.PI / 180);
    txt('bcDiff', Math.hypot(dN, dE).toFixed(2) + ' m horizontal');
  } else {
    txt('bcDiff', '--');
  }
  txt('cDur', b.m === 1 ? dur(b.dur) + ' (' + b.dur + ' s)' : '–');
  txt('cAcc', b.m === 1 ? (b.acc > 0 ? b.acc + ' m' : 'no limit') : '–');
  txt('cX', b.m === 2 ? b.x.toFixed(4) + ' m' : '–');
  txt('cY', b.m === 2 ? b.y.toFixed(4) + ' m' : '–');
  txt('cZ', b.m === 2 ? b.z.toFixed(4) + ' m' : '–');
  txt('cVer', b.ver || '--');
  txt('cBd', b.bd || '--');
  txt('cMsg', b.msg || '--');

  // Prefer the receiver's own survey-in report; fall back to the ESP-side
  // elapsed timer when the firmware does not emit $PQTMSVINSTATUS.
  const live = sv.feat === 1;
  if(b.m === 1){
    $('svinStatus').style.display = 'block';
    if(live){
      const pct = sv.dur ? Math.min(100, sv.obs / sv.dur * 100) : 0;
      $('cSvinBar').style.width = pct.toFixed(1) + '%';
      txt('sState', SVIN_V[sv.v] || 'Unknown');
      txt('sObs', sv.obs + (sv.dur ? ' of ' + sv.dur + ' required' : ''));
      txt('sAcc', sv.acc > 0 ? sv.acc.toFixed(2) + ' m' : '--');
      txt('sMean', sv.x ? sv.x.toFixed(3) + ', ' + sv.y.toFixed(3) + ', ' + sv.z.toFixed(3) : '--');
      txt('cSvinTxt', 'Live from the receiver ($PQTMSVINSTATUS).');
      $('sAdopt').style.display = sv.v === 2 ? '' : 'none';
    } else {
      const pct = b.dur ? Math.min(100, b.el / b.dur * 100) : 0;
      $('cSvinBar').style.width = pct.toFixed(1) + '%';
      txt('sState', sv.feat === 2 ? 'Not reported by this firmware' : 'Waiting for report');
      txt('sObs', '--');
      txt('sAcc', '--');
      txt('sMean', '--');
      txt('cSvinTxt', `${dur(b.el)} elapsed of ${dur(b.dur)} minimum. This firmware does
        not emit $PQTMSVINSTATUS, so the bar is the time since the ESP32 last saw
        survey-in configured; a power cycle restarts the module's own averaging.`);
      $('sAdopt').style.display = 'none';
    }
  } else {
    $('svinStatus').style.display = 'none';
  }

  // -2 / -1 mean "not read back yet"; leave the controls alone in that case.
  if(b.rtcm >= -1) $('bRtcm').value = String(b.rtcm);
  if(b.arp >= 0)   $('bArp').value = String(b.arp);
  if(b.eph >= 0)   $('bEph').value = String(b.eph);

  if(!baseFormLoaded && b.m >= 0){
    baseFormLoaded = true;
    const r = document.querySelector(`input[name=bm][value="${b.m}"]`);
    if(r) r.checked = true;
    if(b.m === 1){ $('bDur').value = b.dur; $('bAcc').value = b.acc; }
    if(b.m === 2){ $('bX').value = b.x; $('bY').value = b.y; $('bZ').value = b.z; }
    $('aN').value = D.arp.n; $('aE').value = D.arp.e; $('aU').value = D.arp.u;
    baseModeChanged();
  }

  renderAvg();
}

function renderAvg(){
  const a = D.avg;
  $('avgRun').style.display = a.run ? 'block' : 'none';
  if(a.run){
    $('avgBar').style.width = Math.min(100, a.el / a.tgt * 100).toFixed(1) + '%';
    txt('avgTxt', `${dur(a.el)} of ${dur(a.tgt)}, ${a.n} samples collected.`);
  }
  const show = a.have && !a.run;
  $('avgRes').style.display = show ? 'grid' : 'none';
  $('avgUse').style.display = show ? '' : 'none';
  if(show){
    txt('avgLat', a.lat.toFixed(8) + '°');
    txt('avgLon', a.lon.toFixed(8) + '°');
    txt('avgAlt', a.alt.toFixed(3) + ' m');
    txt('avgN', a.n + ' samples, ' + (a.rms * 100).toFixed(1) + ' cm RMS');
  }
}

function adoptSvin(){
  if(!confirm('Write the completed survey-in position into fixed mode?')) return;
  fetch('/api/base?action=adopt').then(r => r.json()).then(j => {
    showMsg('bMsg', j.ok ? 'Fixed position set from the survey-in result. Save to keep it across reboots.'
                         : (j.msg || 'Rejected.'), !!j.ok);
    baseFormLoaded = false;
  }).catch(() => showMsg('bMsg', 'Device unreachable.', false));
}

function saveArp(){
  const q = new URLSearchParams({action:'arpoffset', n:$('aN').value, e:$('aE').value, u:$('aU').value});
  fetch('/api/base?' + q).then(r => r.json())
    .then(j => showMsg('aOffMsg', j.ok ? 'Offset saved. It is applied to geodetic coordinates you enter.'
                                       : (j.msg || 'Rejected.'), !!j.ok))
    .catch(() => showMsg('aOffMsg', 'Device unreachable.', false));
}

function avgStart(){
  const s = parseInt($('avgSec').value, 10);
  if(!(s >= 10 && s <= 86400)){ showMsg('avgMsg', 'Duration must be 10-86400 s.', false); return; }
  fetch('/api/base?action=avgstart&s=' + s).then(r => r.json())
    .then(j => showMsg('avgMsg', j.ok ? 'Averaging started.' : 'Rejected.', !!j.ok))
    .catch(() => showMsg('avgMsg', 'Device unreachable.', false));
}
function avgStop(){
  fetch('/api/base?action=avgstop').then(r => r.json())
    .then(j => showMsg('avgMsg', j.ok ? 'Averaging stopped, result kept.' : 'Rejected.', !!j.ok))
    .catch(() => showMsg('avgMsg', 'Device unreachable.', false));
}
function useAvg(){
  const a = D.avg;
  if(!a.have){ showMsg('avgMsg', 'No averaged result yet.', false); return; }
  const r = document.querySelector('input[name=bm][value="2"]');
  if(r) r.checked = true;
  baseModeChanged();
  $('bCoordMode').value = 'lla';
  coordModeChanged();
  $('bLat').value = a.lat.toFixed(8);
  $('bLon').value = a.lon.toFixed(8);
  $('bHgt').value = a.alt.toFixed(3);
  showMsg('avgMsg', 'Copied into the fixed-mode form. These refer to the antenna, so zero the ARP offset before applying.', true);
}

function selectedBaseMode(){
  const r = document.querySelector('input[name=bm]:checked');
  return r ? parseInt(r.value, 10) : -1;
}
function baseModeChanged(){
  const m = selectedBaseMode();
  $('svinBlock').style.display  = m === 1 ? 'block' : 'none';
  $('fixedBlock').style.display = m === 2 ? 'block' : 'none';
}
function coordModeChanged(){
  const lla = $('bCoordMode').value === 'lla';
  $('llaFields').style.display = lla ? 'block' : 'none';
  $('ecefFields').style.display = lla ? 'none' : 'block';
}
function useCurrentPos(){
  if(!D || !D.vloc){ showMsg('bMsg', 'No valid position yet.', false); return; }
  $('bLat').value = D.lat.toFixed(8);
  $('bLon').value = D.lon.toFixed(8);
  $('bHgt').value = (D.alt + (D.sep || 0)).toFixed(3);
  showMsg('bMsg', 'Filled from the current fix. Only use surveyed coordinates for a production base.', true);
}
function showMsg(id, text, ok){
  const e = $(id);
  e.className = 'msg ' + (ok ? 'ok' : 'err');
  e.textContent = text;
  setTimeout(() => { e.className = 'msg'; }, 8000);
}

function applyBaseMode(){
  const m = selectedBaseMode();
  if(m < 0){ showMsg('bMsg', 'Pick a receiver mode first.', false); return; }

  let q = 'action=svin&mode=' + m;
  if(m === 1){
    const d = parseInt($('bDur').value, 10), a = parseFloat($('bAcc').value);
    if(!(d >= 0 && d <= 86400)){ showMsg('bMsg', 'Duration must be 0-86400 seconds.', false); return; }
    if(!(a >= 0)){ showMsg('bMsg', 'Accuracy limit must be 0 or greater.', false); return; }
    q += `&dur=${d}&acc=${a}`;
  } else if(m === 2){
    if($('bCoordMode').value === 'lla'){
      q += `&lat=${$('bLat').value}&lon=${$('bLon').value}&hgt=${$('bHgt').value}`;
    } else {
      q += `&x=${$('bX').value}&y=${$('bY').value}&z=${$('bZ').value}`;
    }
  }
  fetch('/api/base?' + q).then(r => r.json()).then(j => {
    showMsg('bMsg', j.ok ? 'Command sent. Watch the module reply below, then Save to keep it across reboots.' : (j.msg || 'Rejected.'), !!j.ok);
  }).catch(() => showMsg('bMsg', 'Device unreachable.', false));
}
function baseApi(action, v){
  let q = 'action=' + action + (v !== undefined ? '&v=' + encodeURIComponent(v) : '');
  fetch('/api/base?' + q).then(r => r.json()).then(j => {
    showMsg('bMsg', j.ok ? 'Sent: ' + action : (j.msg || 'Rejected.'), !!j.ok);
  }).catch(() => showMsg('bMsg', 'Device unreachable.', false));
}
function confirmRestore(){
  if(confirm('Restore all $PQTM parameters in the module to factory defaults?')) baseApi('restore');
}

/* ---------------- History: last 12 hours ---------------- */
/* The device serves its ring buffer as fixed-width binary rather than JSON:
   23 kB of records would have cost roughly 58 kB of text and the heap to build
   it, for something the browser can read directly. Offsets mirror
   src/system/History.cpp. */
let hist = null, histBusy = false, histLastFetch = 0;

const FIXCOL  = {0:'#c0392b', 1:'#7f9ab5', 2:'#4a90c4', 4:'#2f8f47', 5:'#e8a33d'};
const FIXTXT  = {0:'no fix', 1:'single', 2:'DGPS', 4:'RTK fixed', 5:'RTK float'};
const JAMCOL  = {0:'#dde4ea', 1:'#2f8f47', 2:'#e8a33d', 3:'#c0392b'};
const JAMTXT  = {0:'unknown', 1:'clean', 2:'warning', 3:'critical'};

function parseHistory(buf){
  const d = new DataView(buf);
  if(d.getUint32(0, true) !== 0x484B5452) return null;
  const count = d.getUint16(4, true), iv = d.getUint16(6, true);   // cap = ring size
  const head = d.getUint16(8, true), filled = d.getUint16(10, true);
  const up = d.getUint32(12, true);
  const ref = {lat: d.getFloat64(24, true), lon: d.getFloat64(32, true),
               alt: d.getFloat64(40, true)};
  // Oldest first, so index maps straight to time.
  const start = filled < count ? 0 : head;
  const rows = [];
  for(let k = 0; k < filled; k++){
    const o = 48 + ((start + k) % count) * 16;
    const v = d.getUint8(o + 14);
    if(!v) continue;
    const f = d.getUint8(o + 4), io = d.getUint8(o + 5), hd = d.getUint16(o + 2, true);
    rows.push({
      sats: d.getUint8(o), cn0: d.getUint8(o + 1),
      hdop: hd === 0xFFFF ? null : hd / 100,
      fix: f & 0x0F, j1: (f >> 4) & 3, j5: (f >> 6) & 3,
      iono: io === 255 ? null : io,
      tracked: d.getUint8(o + 15),
      n: v === 1 ? d.getInt16(o + 6, true) : null,
      e: v === 1 ? d.getInt16(o + 8, true) : null,
      u: v === 1 ? d.getInt16(o + 10, true) : null,
      bps: d.getUint16(o + 12, true)
    });
  }
  // Age in seconds of each sample, counting back from the newest.
  rows.forEach((r, i) => r.age = (rows.length - 1 - i) * iv);
  return {iv, up, ref, rows, cap: count};
}

function loadHistory(force){
  if(histBusy) return;
  // The window only advances every 30 s; refetching faster is wasted work.
  if(!force && Date.now() - histLastFetch < 30000) return;
  histBusy = true;
  fetch('/api/history').then(r => r.arrayBuffer()).then(b => {
    hist = parseHistory(b);
    histLastFetch = Date.now();
    drawHistory();
  }).catch(() => {}).finally(() => { histBusy = false; });
}

/* Shared plot frame: a time axis running right-to-left in hours, horizontal
   gridlines, and the value axis labelled in the series' own unit. */
function hFrame(ctx, W, H, lo, hi, unit, span){
  const L = 46, R = 10, T = 20, B = 20;   // T leaves room for the unit label
  const pw = W - L - R, ph = H - T - B;
  ctx.clearRect(0, 0, W, H);
  ctx.font = '10px Arial'; ctx.strokeStyle = '#eef2f6';
  ctx.fillStyle = '#8a99a8'; ctx.textAlign = 'right';
  for(let i = 0; i <= 4; i++){
    const y = T + ph - i / 4 * ph, v = lo + (hi - lo) * i / 4;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillText(Math.abs(hi - lo) < 4 ? v.toFixed(1) : Math.round(v), L - 5, y + 3);
  }
  // Left-aligned in the margin above the scale, so it never lands on the
  // topmost gridline value.
  ctx.textAlign = 'left';
  ctx.fillStyle = '#8a99a8';
  ctx.fillText(unit, 2, 10);
  // Hour marks, oldest on the left.
  ctx.textAlign = 'center';
  const hours = Math.max(1, Math.ceil(span / 3600));
  const stepH = hours > 8 ? 2 : 1;
  for(let h = 0; h <= hours; h += stepH){
    const x = L + pw - (h * 3600 / Math.max(span, 1)) * pw;
    if(x < L - 1) continue;
    ctx.strokeStyle = '#f4f7fa';
    ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + ph); ctx.stroke();
    ctx.fillStyle = '#8a99a8';
    ctx.fillText(h === 0 ? 'now' : '-' + h + 'h', x, H - 6);
  }
  return {L, R, T, B, pw, ph};
}

function hLine(ctx, fr, rows, span, pick, lo, hi, col, fill){
  const pts = [];
  rows.forEach(r => {
    const v = pick(r);
    if(v === null || v === undefined) { pts.push(null); return; }
    pts.push({x: fr.L + fr.pw - (r.age / Math.max(span, 1)) * fr.pw,
              y: fr.T + fr.ph - (v - lo) / Math.max(hi - lo, 1e-6) * fr.ph});
  });
  if(fill){
    ctx.fillStyle = fill; ctx.beginPath(); let open = false;
    pts.forEach(p => {
      if(!p){ if(open){ ctx.lineTo(ctx.__lx, fr.T + fr.ph); ctx.closePath(); ctx.fill(); open = false; } return; }
      if(!open){ ctx.beginPath(); ctx.moveTo(p.x, fr.T + fr.ph); open = true; }
      ctx.lineTo(p.x, p.y); ctx.__lx = p.x;
    });
    if(open){ ctx.lineTo(ctx.__lx, fr.T + fr.ph); ctx.closePath(); ctx.fill(); }
  }
  ctx.strokeStyle = col; ctx.lineWidth = 1.6; ctx.lineJoin = 'round';
  ctx.beginPath(); let pen = false;
  pts.forEach(p => {
    if(!p){ pen = false; return; }
    if(!pen){ ctx.moveTo(p.x, p.y); pen = true; } else ctx.lineTo(p.x, p.y);
  });
  ctx.stroke();
}

function hRange(rows, picks, pad){
  let lo = Infinity, hi = -Infinity;
  rows.forEach(r => picks.forEach(p => {
    const v = p(r);
    if(v === null || v === undefined) return;
    if(v < lo) lo = v; if(v > hi) hi = v;
  }));
  if(lo === Infinity) return [0, 1];
  if(hi - lo < pad){ const m = (lo + hi) / 2; lo = m - pad / 2; hi = m + pad / 2; }
  const m = (hi - lo) * 0.12;
  return [lo - m, hi + m];
}

function hStrip(id, rows, span, pick, palette){
  const hp = hidpi($(id)); if(!hp) return;
  const {ctx, W, H} = hp;
  ctx.clearRect(0, 0, W, H);
  if(!rows.length) return;
  // One rectangle per sample, widened slightly so rounding never leaves gaps.
  const w = Math.max(1, W / rows.length + 0.6);
  rows.forEach(r => {
    const x = W - (r.age / Math.max(span, 1)) * W;
    ctx.fillStyle = palette[pick(r)] || '#dde4ea';
    ctx.fillRect(x - w, 3, w, H - 6);
  });
}

function drawHistory(){
  if(!hist || !hist.rows.length){
    $('hsSummary').innerHTML =
      '<div style="color:#9aa8b5">Collecting &mdash; the first sample lands 30 seconds after boot.</div>';
    return;
  }
  const rows = hist.rows, span = Math.max(rows[0].age, hist.iv);
  const last = rows[rows.length - 1];
  const cover = span / 3600;

  const fixPct = rows.filter(r => r.fix === 4).length / rows.length * 100;
  const cn0s = rows.map(r => r.cn0).filter(v => v > 0);
  const pos = rows.filter(r => r.n !== null);
  let spread = 0;
  if(pos.length){
    const mn = pos.reduce((a, r) => a + r.n, 0) / pos.length;
    const me = pos.reduce((a, r) => a + r.e, 0) / pos.length;
    pos.forEach(r => { spread = Math.max(spread, Math.hypot(r.n - mn, r.e - me)); });
  }
  $('hsSummary').innerHTML = [
    ['Window covered', cover < 1 ? Math.round(span / 60) + ' min' : cover.toFixed(1) + ' h'],
    ['Samples', rows.length + ' of ' + hist.cap],
    ['Horizontal spread', pos.length ? spread.toFixed(0) + ' cm' : '&ndash;'],
    ['Mean C/N0 now', last.cn0 ? last.cn0 + ' dB-Hz' : '&ndash;'],
    ['Lowest C/N0 seen', cn0s.length ? Math.min.apply(null, cn0s) + ' dB-Hz' : '&ndash;'],
    ['Time with RTK fix', fixPct.toFixed(0) + '%'],
    ['Output stopped', rows.filter(r => r.bps === 0).length * hist.iv + ' s']
  ].map(x => `<div><b>${x[0]}</b>${x[1]}</div>`).join('');

  txt('hRefTxt', hist.ref.lat ? hist.ref.lat.toFixed(7) + ', ' + hist.ref.lon.toFixed(7)
                              : 'no reference yet');

  // Position: one frame, three series, shared scale so they stay comparable.
  const ph_ = hidpi($('hPos')); if(!ph_) return;
  const pc = ph_.ctx;
  const [plo, phi] = hRange(rows, [r => r.n, r => r.e, r => r.u], 20);
  const pf = hFrame(pc, ph_.W, ph_.H, plo, phi, 'cm', span);
  hLine(pc, pf, rows, span, r => r.n, plo, phi, '#2f6fad');
  hLine(pc, pf, rows, span, r => r.e, plo, phi, '#c0392b');
  hLine(pc, pf, rows, span, r => r.u, plo, phi, '#2f8f47');

  const sh_ = hidpi($('hSats')); if(!sh_) return;
  const sc = sh_.ctx;
  const [slo, shi] = hRange(rows, [r => r.sats, r => r.tracked], 6);
  const sf = hFrame(sc, sh_.W, sh_.H, Math.max(0, slo), shi, 'sv', span);
  hLine(sc, sf, rows, span, r => r.tracked, Math.max(0, slo), shi, '#9aa8b5');
  hLine(sc, sf, rows, span, r => r.sats, Math.max(0, slo), shi, '#2f6fad', 'rgba(47,111,173,.13)');

  const ch_ = hidpi($('hCn0')); if(!ch_) return;
  const cc = ch_.ctx;
  const [clo, chi] = hRange(rows, [r => r.cn0 || null], 6);
  const cf = hFrame(cc, ch_.W, ch_.H, clo, chi, 'dB-Hz', span);
  // The window's own median, not a fixed threshold. This average covers every
  // tracked signal including weak low-elevation ones, so its absolute value
  // says little; what matters is whether today sits below the recent normal.
  if(cn0s.length){
    const srt = cn0s.slice().sort((a, b) => a - b);
    const med = srt[srt.length >> 1];
    if(med > clo && med < chi){
      const y = cf.T + cf.ph - (med - clo) / (chi - clo) * cf.ph;
      cc.strokeStyle = '#c2ccd6'; cc.setLineDash([4, 3]);
      cc.beginPath(); cc.moveTo(cf.L, y); cc.lineTo(cf.L + cf.pw, y); cc.stroke();
      cc.setLineDash([]);
      cc.fillStyle = '#8a99a8'; cc.font = '9px Arial'; cc.textAlign = 'left';
      cc.fillText('median ' + med, cf.L + 3, y - 3);
    }
  }
  hLine(cc, cf, rows, span, r => r.cn0 || null, clo, chi, '#2f8f47', 'rgba(47,143,71,.13)');

  const hh_ = hidpi($('hHdop')); if(!hh_) return;
  const hc = hh_.ctx;
  const [hlo, hhi] = hRange(rows, [r => r.hdop], 0.5);
  const hf = hFrame(hc, hh_.W, hh_.H, Math.max(0, hlo), hhi, 'HDOP', span);
  hLine(hc, hf, rows, span, r => r.hdop, Math.max(0, hlo), hhi, '#8e5bd6');

  const bh_ = hidpi($('hBps')); if(!bh_) return;
  const bc = bh_.ctx;
  const [blo, bhi] = hRange(rows, [r => r.bps], 200);
  const bf = hFrame(bc, bh_.W, bh_.H, 0, Math.max(bhi, 100), 'B/s', span);
  hLine(bc, bf, rows, span, r => r.bps, 0, Math.max(bhi, 100), '#ef9421', 'rgba(239,148,33,.15)');

  hStrip('hFix', rows, span, r => r.fix, FIXCOL);
  hStrip('hJ1',  rows, span, r => r.j1,  JAMCOL);
  hStrip('hJ5',  rows, span, r => r.j5,  JAMCOL);

  const ah_ = hidpi($('hAxis'));
  if(ah_){
    const ac = ah_.ctx;
    ac.clearRect(0, 0, ah_.W, ah_.H);
    ac.font = '10px Arial'; ac.fillStyle = '#8a99a8'; ac.textAlign = 'center';
    const hours = Math.max(1, Math.ceil(span / 3600)), step = hours > 8 ? 2 : 1;
    for(let h = 0; h <= hours; h += step){
      const x = ah_.W - (h * 3600 / span) * ah_.W;
      if(x < 0) continue;
      ac.fillText(h === 0 ? 'now' : '-' + h + 'h', Math.min(x, ah_.W - 12), 12);
    }
  }

  const seen = k => [...new Set(rows.map(r => r[k]))];
  $('hStripLegend').innerHTML =
    seen('fix').sort().map(v => `<span><i class="sw" style="background:${FIXCOL[v] || '#dde4ea'}"></i>${FIXTXT[v] || 'q' + v}</span>`).join('') +
    [...new Set(seen('j1').concat(seen('j5')))].sort().map(v =>
      `<span><i class="sw" style="background:${JAMCOL[v]}"></i>${JAMTXT[v]}</span>`).join('');
}

/* ---------------- position scatter (no tiles, no internet) ---------------- */
let scRef = null;
function pushScatter(){
  if(!D.vloc) return;
  if(!scRef) scRef = {lat: D.lat, lon: D.lon, alt: D.alt};
  const mPerDegLat = 111320;
  const mPerDegLon = 111320 * Math.cos(scRef.lat * Math.PI / 180);
  scatter.push({
    e: (D.lon - scRef.lon) * mPerDegLon,
    n: (D.lat - scRef.lat) * mPerDegLat,
    h: D.alt - scRef.alt
  });
  if(scatter.length > 600) scatter.shift();
}

function drawScatter(){
  const hp = hidpi($('scatter')); if(!hp) return;
  const {ctx, W, H} = hp, cx = W/2, cy = H/2;
  ctx.clearRect(0, 0, W, H);

  if(!scatter.length){
    ctx.fillStyle = '#9aa8b5'; ctx.font = '12px Arial'; ctx.textAlign = 'center';
    ctx.fillText('Waiting for a valid position', cx, cy);
    return;
  }

  const me = scatter.reduce((a, p) => a + p.e, 0) / scatter.length;
  const mn = scatter.reduce((a, p) => a + p.n, 0) / scatter.length;
  const mh = scatter.reduce((a, p) => a + p.h, 0) / scatter.length;

  let maxR = 0.05;
  scatter.forEach(p => { maxR = Math.max(maxR, Math.hypot(p.e - me, p.n - mn)); });
  const span = maxR * 1.15;
  const sc = (cx - 24) / span;

  ctx.strokeStyle = '#e6ecf1'; ctx.fillStyle = '#9aa8b5';
  ctx.font = '10px Arial'; ctx.textAlign = 'left';
  [1, 2/3, 1/3].forEach(f => {
    ctx.beginPath(); ctx.arc(cx, cy, (cx - 24) * f, 0, 2*Math.PI); ctx.stroke();
    ctx.fillText((span * f * 100).toFixed(1) + ' cm', cx + 3, cy - (cx - 24) * f + 11);
  });
  ctx.strokeStyle = '#ccd7e2';
  ctx.beginPath(); ctx.moveTo(cx, 12); ctx.lineTo(cx, H-12);
  ctx.moveTo(12, cy); ctx.lineTo(W-12, cy); ctx.stroke();
  ctx.fillStyle = '#6c7f93'; ctx.textAlign = 'center';
  ctx.fillText('N', cx, 10); ctx.fillText('E', W-6, cy - 4);

  scatter.forEach((p, i) => {
    const age = i / scatter.length;              // newest samples are opaque
    ctx.fillStyle = `rgba(47,111,173,${0.15 + age * 0.75})`;
    ctx.beginPath();
    ctx.arc(cx + (p.e - me) * sc, cy - (p.n - mn) * sc, 2.2, 0, 2*Math.PI);
    ctx.fill();
  });
  ctx.strokeStyle = '#ef9421'; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.arc(cx, cy, 5, 0, 2*Math.PI); ctx.stroke();

  // Where the broadcast coordinate sits relative to this cloud. The cloud is
  // deliberately still centred on its own mean, because that is what keeps the
  // noise legible; this marker is what reveals a station coordinate that is
  // simply wrong, which a self-referenced plot can never show.
  let bcTxt = '--';
  const bc = D.bc;
  if(bc && bc.v && scRef){
    const mLon = 111320 * Math.cos(scRef.lat * Math.PI / 180);
    const bcE = (bc.lon - scRef.lon) * mLon - me;
    const bcN = (bc.lat - scRef.lat) * 111320 - mn;
    const dist = Math.hypot(bcE, bcN);
    bcTxt = dist < 1 ? (dist * 100).toFixed(1) + ' cm' : dist.toFixed(2) + ' m';

    const bx = cx + bcE * sc, by = cy - bcN * sc;
    if(dist <= span){
      ctx.strokeStyle = '#c0392b'; ctx.lineWidth = 1.8;
      ctx.beginPath();
      ctx.moveTo(bx - 6, by); ctx.lineTo(bx + 6, by);
      ctx.moveTo(bx, by - 6); ctx.lineTo(bx, by + 6);
      ctx.stroke();
    } else {
      // Off the plotted area: point at it from the rim so a gross coordinate
      // error is still obvious instead of silently invisible.
      const ang = Math.atan2(-bcN, bcE);
      const rx = cx + Math.cos(ang) * (cx - 26), ry = cy + Math.sin(ang) * (cx - 26);
      ctx.fillStyle = '#c0392b';
      ctx.beginPath();
      ctx.moveTo(rx + Math.cos(ang) * 7, ry + Math.sin(ang) * 7);
      ctx.lineTo(rx + Math.cos(ang + 2.5) * 7, ry + Math.sin(ang + 2.5) * 7);
      ctx.lineTo(rx + Math.cos(ang - 2.5) * 7, ry + Math.sin(ang - 2.5) * 7);
      ctx.closePath(); ctx.fill();
      ctx.font = '9px Arial'; ctx.textAlign = 'center';
      ctx.fillText(bcTxt, rx - Math.cos(ang) * 14, ry - Math.sin(ang) * 14 + 3);
    }
  }
  txt('scBc', bcTxt);

  const rms = Math.sqrt(scatter.reduce((a, p) =>
    a + (p.e - me) ** 2 + (p.n - mn) ** 2, 0) / scatter.length);
  const es = scatter.map(p => p.e), ns = scatter.map(p => p.n), hs = scatter.map(p => p.h);
  txt('scN', scatter.length);
  txt('scRms', (rms * 100).toFixed(1) + ' cm');
  txt('scPp', ((Math.max(...es) - Math.min(...es)) * 100).toFixed(1) + ' / ' +
              ((Math.max(...ns) - Math.min(...ns)) * 100).toFixed(1) + ' cm');
  txt('scH', ((Math.max(...hs) - Math.min(...hs)) * 100).toFixed(1) + ' cm');
}

/* ---------------- ionosphere ---------------- */
// Diverging scale: blue where the delay has fallen, red where it has risen.
function ionoColour(d, span){
  const t = Math.max(-1, Math.min(1, d / span));
  if(t >= 0) return `rgba(${Math.round(60 + 195 * t)},${Math.round(120 - 70 * t)},${Math.round(120 - 80 * t)},0.95)`;
  return `rgba(${Math.round(60 + 20 * -t)},${Math.round(120 - 10 * -t)},${Math.round(120 + 115 * -t)},0.95)`;
}

function drawIono(){
  const hp = hidpi($('iono')); if(!hp) return;
  const {ctx, W, H} = hp;
  ctx.clearRect(0, 0, W, H);

  const arcs = (D.io || []).filter(a => a[2] > 0 && a[6] > 0 && (a[7] || a[8]));
  if(!arcs.length || !D.vloc){
    ctx.fillStyle = '#9aa8b5'; ctx.font = '12px Arial'; ctx.textAlign = 'center';
    ctx.fillText('Waiting for pierce points', W/2, H/2);
    $('ionoLegend').innerHTML = '';
    txt('ioN', '--'); txt('ioD', '--');
    return;
  }

  const CELL = 2;                                   // graticule cell, degrees
  const lat0 = D.lat, lon0 = D.lon;
  const kx = Math.cos(lat0 * Math.PI / 180);        // keep the aspect true

  // Fixed window centred on the station. The longitude half-span follows from
  // the canvas aspect so the map fills it without distorting distances.
  const pad = 18;
  const sc = (H - pad * 2) / (IONO_HALF * 2);
  const lonHalf = (W - pad * 2) / (2 * kx * sc);
  const latMin = lat0 - IONO_HALF, latMax = lat0 + IONO_HALF;
  const lonMin = lon0 - lonHalf,   lonMax = lon0 + lonHalf;

  const ox = W/2 - lon0 * kx * sc;
  const oy = H/2 + lat0 * sc;
  const px = lon => ox + lon * kx * sc;
  const py = lat => oy - lat * sc;

  ctx.fillStyle = '#f7fafc';
  ctx.fillRect(px(lonMin), py(latMax), (lonMax-lonMin)*kx*sc, (latMax-latMin)*sc);

  // Every filled cell below is one real measurement; the rest stay empty on
  // purpose, because there is nothing between the pierce points to interpolate.
  const inView = a => a[7] >= latMin && a[7] <= latMax && a[8] >= lonMin && a[8] <= lonMax;
  const shown = arcs.filter(inView);
  const span = Math.max(0.05, ...shown.map(a => Math.abs(a[5] / 100)));
  const cells = new Map();
  shown.forEach(a => {
    const cy2 = Math.floor(a[7] / CELL) * CELL, cx2 = Math.floor(a[8] / CELL) * CELL;
    const k = cy2 + ':' + cx2;
    const c = cells.get(k) || {lat: cy2, lon: cx2, sum: 0, n: 0};
    c.sum += a[5] / 100; c.n++;
    cells.set(k, c);
  });
  cells.forEach(c => {
    ctx.fillStyle = ionoColour(c.sum / c.n, span);
    ctx.globalAlpha = 0.75;
    ctx.fillRect(px(c.lon), py(c.lat + CELL), CELL * kx * sc, CELL * sc);
    ctx.globalAlpha = 1;
  });

  // Graticule sits on whole multiples of CELL, not on the station, so the lines
  // stay put instead of sliding with the metre-level wobble of the live fix.
  ctx.strokeStyle = '#cfdae4'; ctx.lineWidth = 0.6;
  for(let la = Math.ceil(latMin / CELL) * CELL; la <= latMax; la += CELL){
    ctx.beginPath(); ctx.moveTo(px(lonMin), py(la)); ctx.lineTo(px(lonMax), py(la)); ctx.stroke();
  }
  for(let lo = Math.ceil(lonMin / CELL) * CELL; lo <= lonMax; lo += CELL){
    ctx.beginPath(); ctx.moveTo(px(lo), py(latMin)); ctx.lineTo(px(lo), py(latMax)); ctx.stroke();
  }

  shown.forEach(a => {
    const x = px(a[8]), y = py(a[7]);
    ctx.fillStyle = ionoColour(a[5] / 100, span);
    ctx.beginPath(); ctx.arc(x, y, 3.4, 0, 2*Math.PI); ctx.fill();
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 1; ctx.stroke();
  });

  const bx = px(lon0), by = py(lat0);
  ctx.strokeStyle = '#20344a'; ctx.lineWidth = 1.6;
  ctx.beginPath();
  ctx.moveTo(bx-5, by); ctx.lineTo(bx+5, by);
  ctx.moveTo(bx, by-5); ctx.lineTo(bx, by+5);
  ctx.stroke();

  ctx.fillStyle = '#6c7f93'; ctx.font = '9px Arial'; ctx.textAlign = 'left';
  ctx.fillText(latMax.toFixed(0) + '°N', px(lonMin) + 2, py(latMax) + 10);
  ctx.fillText(lonMin.toFixed(0) + '°E', px(lonMin) + 2, py(latMin) - 3);
  ctx.textAlign = 'right';
  ctx.fillText(lonMax.toFixed(0) + '°E', px(lonMax) - 2, py(latMin) - 3);

  $('ionoLegend').innerHTML =
    `<span><i class="sw" style="background:${ionoColour(-span, span)}"></i>-${span.toFixed(2)} m</span>` +
    `<span><i class="sw" style="background:${ionoColour(0, span)}"></i>0</span>` +
    `<span><i class="sw" style="background:${ionoColour(span, span)}"></i>+${span.toFixed(2)} m</span>` +
    `<span style="color:#6c7f93">${CELL}° cells at ${IONO_SHELL} km</span>`;

  txt('ioN', shown.length === arcs.length
      ? `${shown.length} pierce points in ${cells.size} cells`
      : `${shown.length} of ${arcs.length} shown, ${cells.size} cells`);
  txt('ioD', (D.iond).toFixed(3) + ' m');
}

/* ---------------- Data Output ---------------- */
const RTCM_NAMES = {
  1005:'Stationary RTK reference station ARP', 1019:'GPS ephemerides',
  1020:'GLONASS ephemerides', 1042:'BeiDou ephemerides', 1044:'QZSS ephemerides',
  1046:'Galileo I/NAV ephemerides',
  1074:'GPS MSM4', 1077:'GPS MSM7', 1084:'GLONASS MSM4', 1087:'GLONASS MSM7',
  1094:'Galileo MSM4', 1097:'Galileo MSM7', 1114:'QZSS MSM4', 1117:'QZSS MSM7',
  1124:'BeiDou MSM4', 1127:'BeiDou MSM7'
};
function bytes(n){
  return n > 1048576 ? (n/1048576).toFixed(1) + ' MB'
       : n > 1024 ? (n/1024).toFixed(1) + ' KB' : n + ' B';
}
function apAddr(){ return D.ap; }

function downloadBackup(){
  // Straight to the endpoint rather than through fetch: the response carries
  // Content-Disposition, so the browser handles naming and saving.
  const a = document.createElement('a');
  a.href = '/api/backup';
  a.download = 'rtk-base-settings.json';
  document.body.appendChild(a); a.click(); a.remove();
  showMsg('aBkMsg', 'Settings file requested.', true);
}

function uploadBackup(){
  const f = $('aRestoreFile').files[0];
  if(!f){ showMsg('aBkMsg', 'Pick a backup file first.', false); return; }
  const r = new FileReader();
  r.onload = () => {
    // Explicit content type: an urlencoded body would be parsed away as
    // request parameters and never reach the handler.
    fetch('/api/restore', {method: 'POST',
                           headers: {'Content-Type': 'application/json'},
                           body: r.result})
      .then(x => x.json())
      .then(j => showMsg('aBkMsg', j.ok
        ? `Restored ${j.applied} section(s). Rebooting; reconnect in a few seconds.`
        : (j.msg || 'Rejected.'), !!j.ok))
      // The device reboots as it answers, so a dropped connection here is the
      // expected case rather than a failure.
      .catch(() => showMsg('aBkMsg', 'Sent; the device is rebooting.', true));
  };
  r.readAsText(f);
}

// GGA field 6. Only 4 means the corrections were used and resolved; 5 means
// they were used but the ambiguities are still float.
const FIXNAME = {0:'no fix', 1:'single', 2:'DGPS', 3:'PPS', 4:'RTK fixed',
                 5:'RTK float', 6:'dead reckoning', 7:'manual', 8:'simulated'};
const FIXCLS  = {0:'r', 1:'r', 2:'n', 4:'g', 5:'n'};

function renderOutput(){
  const o = D.out;
  const udpMode = [];
  if(o.udpDst) udpMode.push('fixed ' + o.udpDst + ':' + o.udpDstP);
  if(o.udpBc)  udpMode.push('broadcast :' + o.udpPort);
  udpMode.push('registered subscribers');
  txt('epUdp', !o.udpEn ? 'disabled'
      : apAddr() + ':' + o.udpPort + '  (' + udpMode.join(', ') + ')');
  txt('epTcp', o.tcpEn ? apAddr() + ':' + o.tcpPort + '  (TCP)' : 'disabled');
  txt('epMount', o.tcpEn ? '/' + o.mount + (o.auth ? '  (authenticated)' : '  (open)') : '–');
  txt('epUdpRx', o.udpRx ? `${o.udpRx} (last from ${o.udpFrom}, ${o.udpAge} s ago)`
                         : 'none - nothing has contacted the UDP port');
  txt('epSta', staUp() ? D.ip : 'not joined');

  txt('epUsb', o.usbEn ? o.usbBaud + ' baud 8N1  (console log muted)' : 'disabled');
  txt('usbState', !o.usbEn ? 'disabled'
      : o.usbFr ? 'streaming at ' + o.usbBaud + ' baud'
                : 'enabled, no frames sent yet');
  txt('usbFr', o.usbFr);
  txt('usbTx', bytes(o.usbTx));
  txt('usbDrop', o.usbDrop ? o.usbDrop + ' (host too slow or baud too low)' : '0');

  if(!outFormLoaded){
    outFormLoaded = true;
    $('oUdpEn').value = o.udpEn ? '1' : '0';
    $('oUdpPort').value = o.udpPort;
    $('oUdpDst').value = o.udpDst || '';
    $('oUdpDstP').value = o.udpDstP || 0;
    $('oUdpBc').value = o.udpBc ? '1' : '0';
    $('oTcpEn').value = o.tcpEn ? '1' : '0';
    $('oTcpPort').value = o.tcpPort;
    $('oAccept').value = String(o.accept);
    $('oMount').value = o.mount;
    $('oUser').value = o.user;
    $('oUsbEn').value = o.usbEn ? '1' : '0';
    $('oUsbBaud').value = String(o.usbBaud);
  }

  const cl = D.cl.map(c => {
    const age = c[8];
    const fix = age < 0 ? '<span style="color:#9aa8b5">not reported</span>'
              : `<span class="pill ${FIXCLS[c[4]] || 'r'}">${FIXNAME[c[4]] || 'no fix'}</span>`
                + (c[5] ? ` <span style="color:#6c7f93">${c[5]} sv` +
                          (c[6] ? `, HDOP ${c[6].toFixed(2)}` : '') + '</span>' : '');
    const bl = c[7] < 0 ? '&ndash;'
             : c[7] < 1000 ? c[7].toFixed(1) + ' m' : (c[7] / 1000).toFixed(2) + ' km';
    const rep = age < 0 ? '&ndash;' : age < 2 ? 'now' : age + ' s ago';
    return `<tr><td>${esc(c[0])}</td><td><span class="pill ${c[1] === 'ntrip' ? 'n' : 'r'}">${c[1].toUpperCase()}</span></td>
     <td>${dur(c[2])}</td><td>${bytes(c[3])}</td><td>${fix}</td><td>${bl}</td><td>${rep}</td></tr>`;
  });
  const ul = D.ul.map(c =>
    `<tr><td>${esc(c[0])}:${c[1]}</td><td><span class="pill n">UDP</span></td>
     <td>${dur(c[2])}</td><td>${bytes(c[3])}</td>
     <td colspan="3" style="color:#9aa8b5">no back channel</td></tr>`);
  $('clTable').tBodies[0].innerHTML = cl.concat(ul).join('') ||
    '<tr><td colspan="7" style="color:#9aa8b5">No consumers connected</td></tr>';
  const fixed = D.cl.filter(c => c[4] === 4).length;
  const reporting = D.cl.filter(c => c[8] >= 0).length;
  txt('clNote', `${D.tcp} TCP/NTRIP and ${D.udp} UDP consumers` +
      (reporting ? `; ${reporting} reporting, ${fixed} with an RTK fix.` : '.'));

  renderPush();

  const r = D.rst;
  txt('rsF', r.f);
  txt('rsBps', bytes(r.bps) + '/s');
  txt('rsCrc', r.crc);
  txt('rsAge', r.age < 0 ? 'never' : r.age + ' s ago');
  $('rsTable').tBodies[0].innerHTML = r.ty.length
    ? r.ty.slice().sort((a, b) => a[0] - b[0]).map(t =>
        `<tr><td>${t[0]}</td><td style="text-align:left">${RTCM_NAMES[t[0]] || '—'}</td><td>${t[1]}</td>
         <td>${t[2] ? (t[2] / 1000).toFixed(2) + ' s' : '–'}</td>
         <td>${t[3] ? t[3] + ' ms' : '–'}</td></tr>`).join('')
    : '<tr><td colspan="5" style="color:#9aa8b5">No RTCM frames yet</td></tr>';
}

function saveOutput(){
  const q = new URLSearchParams({
    action:'save',
    udpEn: $('oUdpEn').value, udpPort: $('oUdpPort').value,
    udpDst: $('oUdpDst').value, udpDstP: $('oUdpDstP').value,
    udpBc: $('oUdpBc').value,
    tcpEn: $('oTcpEn').value, tcpPort: $('oTcpPort').value,
    accept: $('oAccept').value, mount: $('oMount').value,
    user: $('oUser').value, pass: $('oPass').value,
    usbEn: $('oUsbEn').value, usbBaud: $('oUsbBaud').value
  });
  // One form, two Save buttons: whichever one was pressed, the result belongs
  // next to both.
  const say = (m, ok) => { showMsg('oMsg', m, ok); showMsg('oUsbMsg', m, ok); };
  fetch('/api/output?' + q).then(r => r.json()).then(j => {
    say(j.ok ? 'Saved. Listeners restarted; reconnect any consumer.' : (j.msg || 'Rejected.'), !!j.ok);
    outFormLoaded = false;
  }).catch(() => say('Device unreachable.', false));
}

function drawCnEl(){
  const hp = hidpi($('cnel')); if(!hp) return;
  const {ctx, W, H} = hp, L = 38, B = 34, T = 10, R = 12;
  const pw = W - L - R, ph = H - B - T;
  ctx.clearRect(0, 0, W, H);

  ctx.strokeStyle = '#e6ecf1'; ctx.fillStyle = '#6c7f93'; ctx.font = '10px Arial';
  ctx.textAlign = 'right';
  for(let v = 0; v <= 60; v += 10){
    const y = T + ph - v / 60 * ph;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillText(v, L - 5, y + 3);
  }
  ctx.textAlign = 'center';
  for(let e = 0; e <= 90; e += 15){
    const x = L + e / 90 * pw;
    ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + ph); ctx.stroke();
    ctx.fillText(e + '°', x, H - B + 14);
  }
  ctx.fillText('Elevation', L + pw / 2, H - 6);
  ctx.save(); ctx.translate(11, T + ph / 2); ctx.rotate(-Math.PI / 2);
  ctx.fillText('C/N0 dB-Hz', 0, 0); ctx.restore();

  const bands = new Set();
  D.sig.forEach(sg => {
    const e = Math.max(0, Math.min(90, sg[2]));
    const x = L + e / 90 * pw;
    const y = T + ph - Math.min(60, sg[4]) / 60 * ph;
    bands.add(sg[5]);
    ctx.fillStyle = BAND_COL[sg[5]] || '#888';
    ctx.globalAlpha = sg[6] ? 0.95 : 0.4;
    ctx.beginPath(); ctx.arc(x, y, 3.2, 0, 2 * Math.PI); ctx.fill();
  });
  ctx.globalAlpha = 1;

  // Reference curve: a healthy setup rises roughly 20 dB from horizon to zenith.
  ctx.strokeStyle = '#c9d4de'; ctx.setLineDash([4, 4]); ctx.lineWidth = 1.5;
  ctx.beginPath();
  for(let e = 0; e <= 90; e++){
    const cn = 30 + 18 * Math.sin(e * Math.PI / 180);
    const x = L + e / 90 * pw, y = T + ph - Math.min(60, cn) / 60 * ph;
    e ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  }
  ctx.stroke(); ctx.setLineDash([]);

  $('cnelLegend').innerHTML = [...bands].sort().map(b =>
    `<span><i class="sw" style="background:${BAND_COL[b]}"></i>Band ${b + 1}</span>`).join('') +
    '<span style="color:#6c7f93">solid = used in fix, dashed line = typical trend</span>';
}

/* ---------------- NTRIP push ---------------- */
let pushFormLoaded = false;
function renderPush(){
  const p = D.push;
  txt('pState', p.en ? PUSH_ST[p.st] : 'disabled');
  txt('pDetail', p.msg || '--');
  txt('pUp', p.st === 4 ? dur(p.up) : '–');
  txt('pSent', p.st === 4 ? bytes(p.sent) : '–');
  txt('pRetry', p.retry);

  if(!pushFormLoaded){
    pushFormLoaded = true;
    $('pEn').value = p.en ? '1' : '0';
    $('pHost').value = p.host;
    $('pPort').value = p.port;
    $('pMount').value = p.mount;
  }
}
function savePush(){
  const q = new URLSearchParams({en:$('pEn').value, host:$('pHost').value,
    port:$('pPort').value, mount:$('pMount').value, pass:$('pPass').value});
  fetch('/api/push?' + q).then(r => r.json()).then(j => {
    showMsg('pMsg', j.ok ? 'Saved. Watch the state on the right.' : (j.msg || 'Rejected.'), !!j.ok);
    pushFormLoaded = false;
  }).catch(() => showMsg('pMsg', 'Device unreachable.', false));
}

/* ---------------- Network ---------------- */
function renderNetwork(){
  const a = D.apinfo;
  txt('nApState', `up on channel ${a.ch}, ${a.sec ? 'WPA2' : 'open'}${a.hide ? ', hidden' : ''}`);
  txt('nApIp', D.ap);
  txt('nApN', a.n);

  txt('nStaState', ['not joined','connecting','connected','reconnecting'][D.net] || '--');
  txt('nStaSsid', D.ssid || '–');
  txt('nStaIp', D.net === 2 ? D.ip : '–');
  txt('nStaRssi', D.net === 2 ? D.rssi + ' dBm' : '–');

  if(!netFormLoaded){
    netFormLoaded = true;
    const ch = $('nCh');
    ch.innerHTML = '';
    for(let i = 1; i <= 13; i++){
      const o = document.createElement('option');
      o.value = i; o.textContent = 'Channel ' + i;
      ch.appendChild(o);
    }
    ch.value = a.ch;
    $('nSsid').value = a.ssid;
    $('nHide').value = a.hide ? '1' : '0';
    setNetOptions([], 'Scan first...');
  }
}

function setNetOptions(labels, placeholder){
  const sel = $('nList');
  sel.innerHTML = '';
  const head = document.createElement('option');
  head.value = ''; head.textContent = placeholder;
  sel.appendChild(head);
  labels.forEach(l => {          // textContent: SSIDs are arbitrary text
    const o = document.createElement('option');
    o.value = l; o.textContent = l;
    sel.appendChild(o);
  });
}

function scanNets(attempt){
  attempt = attempt || 0;
  if(attempt === 0) setNetOptions([], 'Scanning...');
  fetch('/scan').then(r => r.json()).then(d => {
    if(!Array.isArray(d)){
      if(attempt < 12){ setTimeout(() => scanNets(attempt + 1), 1500); return; }
      throw new Error('timeout');
    }
    setNetOptions(d, d.length ? 'Select a network...' : 'No networks found');
  }).catch(() => setNetOptions([], 'Scan failed'));
}

const TS_STATE = ['Idle','Waiting for WiFi','Connecting','Registering',
                  'Connected','Reconnecting','Error'];

function renderTailscale(){
  const t = D.ts; if(!t) return;
  txt('tsState', !t.buf ? 'unavailable (no memory)'
                : t.fail ? t.msg
                : t.rb ? 'settings saved - reboot to apply'
                : !t.en ? 'disabled'
                : (TS_STATE[t.st] || '?') + (t.msg && t.msg !== TS_STATE[t.st] ? ' - ' + t.msg : ''));
  txt('tsIp', t.ip || '--');
  txt('tsPeers', t.en ? t.peers : '--');
  if(!tsFormLoaded){
    tsFormLoaded = true;
    $('tsEn').value = t.en ? '1' : '0';
    $('tsName').value = t.name || '';
    // Never echoed back by the device; the field only says whether one is held.
    $('tsKey').placeholder = t.haveKey ? '(key stored - leave blank to keep it)'
                                       : 'tskey-auth-...';
  }
}

function saveTailscale(){
  const q = new URLSearchParams({action:'save', en:$('tsEn').value,
                                 name:$('tsName').value, key:$('tsKey').value});
  fetch('/api/tailscale?' + q).then(r => r.json()).then(j => {
    showMsg('tsMsg', j.ok ? 'Saved. The client restarts with the new settings.'
                          : (j.msg || 'Rejected.'), !!j.ok);
    $('tsKey').value = '';
    tsFormLoaded = false;
  }).catch(() => showMsg('tsMsg', 'Device unreachable.', false));
}

function saveAp(){
  const pass = $('nPass').value;
  if(pass.length > 0 && pass.length < 8){
    showMsg('nMsg', 'WPA2 needs at least 8 characters, or leave it empty for an open AP.', false);
    return;
  }
  if(!confirm('Restart the access point with the new settings? Every associated client, including this browser, will drop.')) return;
  const q = new URLSearchParams({action:'ap', ssid:$('nSsid').value, pass:pass,
                                ch:$('nCh').value, hide:$('nHide').value});
  fetch('/api/net?' + q).then(r => r.json()).then(j => {
    showMsg('nMsg', j.ok ? 'Applied. Reconnect to the access point with the new settings.' : (j.msg || 'Rejected.'), !!j.ok);
    netFormLoaded = false;
  }).catch(() => showMsg('nMsg', 'Device unreachable.', false));
}

function joinNet(){
  const ssid = $('nJoinSsid').value.trim();
  if(!ssid){ showMsg('nMsg', 'Enter an SSID first.', false); return; }
  const q = new URLSearchParams({action:'join', ssid:ssid, pass:$('nJoinPass').value});
  fetch('/api/net?' + q).then(r => r.json()).then(j => {
    showMsg('nMsg', j.ok ? 'Connecting... the access point stays up regardless.' : (j.msg || 'Rejected.'), !!j.ok);
  }).catch(() => showMsg('nMsg', 'Device unreachable.', false));
}

function forgetNet(){
  if(!confirm('Forget the saved uplink network? The access point is unaffected.')) return;
  fetch('/api/net?action=forget').then(r => r.json())
    .then(j => showMsg('nMsg', 'Uplink credentials cleared.', !!j.ok))
    .catch(() => showMsg('nMsg', 'Device unreachable.', false));
}

/* ---------------- Admin ---------------- */
function renderAdmin(){
  txt('aModel', D.model);
  txt('aVer', D.base.ver || '--');
  txt('aBd', D.base.bd || '--');
  txt('aEsp', D.esp);
  txt('aUp', dur(D.up));
  txt('aHeap', (D.heap / 1024).toFixed(1) + ' KB');
  txt('aCpu', D.cpu0 + ' % / ' + D.cpu1 + ' %');
  txt('aPps', D.pps ? 'Locked' : 'No pulse');
}
function doReboot(){
  if(!confirm('Reboot the ESP32 now? The RTCM stream drops for a few seconds.')) return;
  fetch('/reboot').then(() => showMsg('aMsg', 'Rebooting...', true))
                  .catch(() => showMsg('aMsg', 'Rebooting...', true));
}

route();
initWs();
</script>
</body>
</html>
)rawliteral";
