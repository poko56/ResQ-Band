#include "WebUI.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);

String g_webui_debug = "Boot OK";
bool webui_wants_mode_switch = false;
uint32_t webui_wants_target_id = 0;

// State variables for WebUI
static uint32_t s_target_id = 0;
static uint8_t  s_mode = 0; // 0=LoRa, 1=UWB
static float    s_distance_m = -1.0f;
static float    s_angle_deg = 0.0f;
static uint8_t  s_v_hr = 0, s_v_spo2 = 0, s_v_batt = 0, s_v_triage = 0;

#define MAX_BANDS 10
static SeenBand s_bands[MAX_BANDS];
static size_t s_bands_count = 0;

void webui_set_bands(SeenBand* bands, size_t count) {
  s_bands_count = count > MAX_BANDS ? MAX_BANDS : count;
  for (size_t i = 0; i < s_bands_count; i++) {
    s_bands[i] = bands[i];
  }
}

void webui_set_target(uint32_t id, uint8_t mode, float dist, float angle) {
  s_target_id = id;
  s_mode = mode;
  s_distance_m = dist;
  s_angle_deg = angle;
}

void webui_set_vitals(uint8_t hr, uint8_t spo2, uint8_t batt, uint8_t triage) {
  s_v_hr = hr;
  s_v_spo2 = spo2;
  s_v_batt = batt;
  s_v_triage = triage;
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>ResQ-Locator Dashboard</title>
  <style>
    :root {
      --bg: #0f172a;
      --panel-bg: rgba(30, 41, 59, 0.7);
      --accent: #3b82f6;
      --accent-hover: #2563eb;
      --success: #10b981;
      --danger: #ef4444;
      --text: #f8fafc;
      --text-muted: #94a3b8;
    }
    body { 
      background-color: var(--bg); color: var(--text); 
      font-family: 'Inter', system-ui, sans-serif; 
      margin: 0; padding: 16px; 
      overflow-x: hidden;
    }
    h1 { margin: 0 0 16px 0; font-size: 24px; color: var(--accent); font-weight: 600; text-align: center; }
    #debug_msg {
      text-align: center;
      color: #888;
      font-size: 12px;
      margin-bottom: 16px;
    }
    
    /* Radar */
    .radar-container { 
      position: relative; width: 280px; height: 280px; margin: 0 auto 20px; 
      border-radius: 50%; background: radial-gradient(circle at center, rgba(59,130,246,0.1) 0%, rgba(15,23,42,1) 100%); 
      border: 2px solid rgba(59,130,246,0.3); overflow: hidden; 
      box-shadow: 0 0 30px rgba(59,130,246,0.1);
    }
    .circle { position: absolute; border: 1px dashed rgba(255,255,255,0.15); border-radius: 50%; top: 50%; left: 50%; transform: translate(-50%, -50%); }
    .c1 { width: 90px; height: 90px; }
    .c2 { width: 180px; height: 180px; }
    .c3 { width: 270px; height: 270px; }
    .crosshair-h { position: absolute; top: 50%; left: 0; right: 0; height: 1px; background-color: rgba(255,255,255,0.1); }
    .crosshair-v { position: absolute; top: 0; bottom: 0; left: 50%; width: 1px; background-color: rgba(255,255,255,0.1); }
    .pulse { 
      position: absolute; top: 50%; left: 50%; width: 100%; height: 100%; border-radius: 50%; 
      border: 2px solid var(--accent); transform: translate(-50%, -50%); 
      animation: radar-scan 2s linear infinite; box-sizing: border-box; 
    }
    @keyframes radar-scan {
      0% { width: 0; height: 0; opacity: 1; }
      100% { width: 100%; height: 100%; opacity: 0; }
    }
    .blip { 
      position: absolute; width: 16px; height: 16px; background-color: var(--danger); 
      border-radius: 50%; top: 50%; left: 50%; transform: translate(-50%, -50%); 
      display: none; box-shadow: 0 0 15px var(--danger); transition: all 0.3s ease; 
    }
    
    /* Target Info */
    .glass-panel {
      background: var(--panel-bg);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.05);
      border-radius: 16px;
      padding: 16px;
      margin-bottom: 16px;
      box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1);
    }
    .status-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
    .status-label { color: var(--text-muted); font-size: 14px; }
    .status-val { font-size: 18px; font-weight: 600; color: var(--accent); }
    .val-red { color: var(--danger); }
    
    .btn { 
      background-color: var(--accent); color: white; border: none; 
      padding: 12px; font-size: 16px; font-weight: 600; border-radius: 8px; 
      cursor: pointer; width: 100%; transition: 0.2s; 
    }
    .btn:active { transform: scale(0.98); }
    .btn.outline { background: transparent; border: 1px solid var(--accent); color: var(--accent); }
    
    /* Band Table */
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th { text-align: left; padding: 8px; color: var(--text-muted); font-size: 12px; border-bottom: 1px solid rgba(255,255,255,0.1); }
    td { padding: 12px 8px; border-bottom: 1px solid rgba(255,255,255,0.05); font-size: 14px; }
    tr:last-child td { border-bottom: none; }
    .btn-sm { padding: 6px 12px; font-size: 12px; width: auto; background-color: rgba(255,255,255,0.1); color: var(--text); border: none; border-radius: 4px; cursor: pointer; }
    .btn-sm.active { background-color: var(--success); color: #000; font-weight: 600; }
  </style>
</head>
<body>
  <h1>ResQ-Locator Dashboard</h1>
  <div id="debug_msg">Booting...</div>
  
  <div class="radar-container">
    <div class="circle c1"></div>
    <div class="circle c2"></div>
    <div class="circle c3"></div>
    <div class="crosshair-h"></div>
    <div class="crosshair-v"></div>
    <div class="pulse"></div>
    <div class="blip" id="blip"></div>
  </div>
  
  <div class="glass-panel">
    <div class="status-row">
      <span class="status-label">Current Mode:</span>
      <span class="status-val" id="mode-text">Searching...</span>
    </div>
    <div class="status-row">
      <span class="status-label">Locked Target:</span>
      <span class="status-val" id="target-id">None</span>
    </div>
    <div class="status-row">
      <span class="status-label">Distance:</span>
      <span class="status-val" id="distance">--</span>
    </div>
    <button class="btn outline" style="margin-top: 8px;" onclick="switchMode()" id="btn-mode">Switch to BLE Mode</button>
  </div>

  <div class="glass-panel" id="vitals-panel" style="display:none;">
    <h3 style="margin: 0 0 8px; font-size: 16px; color: var(--text-muted);">Wearer Vitals</h3>
    <div class="status-row"><span class="status-label">Heart Rate</span><span class="status-val" id="v-hr">--</span></div>
    <div class="status-row"><span class="status-label">Triage</span><span class="status-val" id="v-triage">--</span></div>
  </div>

  <div class="glass-panel">
    <h3 style="margin: 0 0 8px; font-size: 16px; color: var(--text-muted);">Detected Bands</h3>
    <table>
      <thead>
        <tr>
          <th>Band ID</th>
          <th>Vitals</th>
          <th>RSSI</th>
          <th>Action</th>
        </tr>
      </thead>
      <tbody id="band-list">
        <tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No bands detected</td></tr>
      </tbody>
    </table>
  </div>

  <script>
    let currentMode = 0;
    
    function assignTarget(id) {
      fetch('/api/assign?id=' + id, { method: 'POST' })
        .then(res => res.json())
        .then(data => updateData())
        .catch(err => console.error(err));
    }
    
    function switchMode() {
      fetch('/api/switch_mode', { method: 'POST' })
        .then(res => res.json())
        .then(data => updateData())
        .catch(err => console.error(err));
    }
    
    function updateData() {
      fetch('/api/data')
        .then(res => res.json())
        .then(data => {
          document.getElementById('debug_msg').innerText = data.debug || "";
          currentMode = data.mode;
          document.getElementById('mode-text').innerText = (data.mode === 0 ? 'LoRa Sweep' : 'BLE Pinpoint');
          document.getElementById('btn-mode').innerText = (data.mode === 0 ? 'Switch to BLE Mode' : 'Switch to LoRa Mode');
          
          if (data.target_id === 0) {
            document.getElementById('target-id').innerText = 'None';
            document.getElementById('distance').innerText = '--';
            document.getElementById('blip').style.display = 'none';
            document.getElementById('vitals-panel').style.display = 'none';
          } else {
            document.getElementById('target-id').innerText = data.target_id.toString(16).toUpperCase();

            // Wearer vitals panel (from dispatch or LoRa heartbeat)
            document.getElementById('vitals-panel').style.display = 'block';
            document.getElementById('v-hr').innerText   = (data.hr > 0 ? data.hr + ' bpm' : '--');
            const tlab = ['SAFE','WATCH','CRITICAL','DECEASED'][data.triage] || '--';
            const tcol = ['#10b981','#eab308','#ef4444','#94a3b8'][data.triage] || '#94a3b8';
            const vt = document.getElementById('v-triage');
            vt.innerText = tlab; vt.style.color = tcol;
            
            // Find target in bands to get its RSSI
            let targetBand = data.bands.find(b => b.id === data.target_id);
            let trssi = targetBand ? targetBand.rssi : -100;
            
            let dist_px = 0;
            let show_blip = true;

            if (data.mode === 0) {
              // LoRa sweep: only estimate distance when we actually have this
              // target's RSSI. Without it, the path-loss formula fed -100 dBm
              // spits out a meaningless ~158 m, so show "No Signal" instead.
              if (!targetBand) {
                document.getElementById('distance').innerText = 'No Signal';
                show_blip = false;
              } else {
                let est_dist = Math.pow(10, (-45 - trssi) / 25.0);
                document.getElementById('distance').innerText = '~' + est_dist.toFixed(1) + ' m';
                dist_px = Math.min(140, (est_dist / 100.0) * 140.0);
              }
            } else {
              if (data.distance_m < 0) {
                document.getElementById('distance').innerText = 'Out of Range';
                show_blip = false;
              } else {
                document.getElementById('distance').innerText = data.distance_m.toFixed(2) + ' m';
                dist_px = Math.min(140, (data.distance_m / 20.0) * 140.0);
              }
            }
            
            let blip = document.getElementById('blip');
            if (show_blip) {
              blip.style.display = 'block';
              let angle = data.mode === 0 ? 0 : data.angle_deg;
              let rad = (angle - 90) * (Math.PI / 180);
              let cx = 140 + dist_px * Math.cos(rad); // 140 is radius
              let cy = 140 + dist_px * Math.sin(rad);
              blip.style.left = cx + 'px';
              blip.style.top = cy + 'px';
            } else {
              blip.style.display = 'none';
            }
          }
          
          // Render Band List
          let tbody = document.getElementById('band-list');
          if (data.bands.length === 0) {
            tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No bands detected</td></tr>';
          } else {
            tbody.innerHTML = data.bands.map(b => {
              let isLocked = (b.id === data.target_id);
              let btnHtml = isLocked 
                ? '<button class="btn-sm active">Locked</button>'
                : '<button class="btn-sm" onclick="assignTarget('+b.id+')">Assign</button>';
              let vitals = '<span style="color:var(--danger)">\u2665</span>' + (b.hr||'--');
              return '<tr><td>'+b.id.toString(16).toUpperCase()+'</td><td>'+vitals+'</td><td>'+b.rssi+'</td><td>'+btnHtml+'</td></tr>';
            }).join('');
          }
        })
        .catch(err => console.error(err));
    }
    
    setInterval(updateData, 500);
    updateData(); // initial call
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleApiData() {
  String json = "{";
  json += "\"mode\":" + String(s_mode) + ",";
  json += "\"target_id\":" + String(s_target_id) + ",";
  json += "\"distance_m\":" + String(s_distance_m) + ",";
  json += "\"angle_deg\":" + String(s_angle_deg) + ",";
  json += "\"hr\":" + String(s_v_hr) + ",";
  json += "\"spo2\":" + String(s_v_spo2) + ",";
  json += "\"batt\":" + String(s_v_batt) + ",";
  json += "\"triage\":" + String(s_v_triage) + ",";
  
  String extDebug = g_webui_debug + " | T:" + String(s_target_id, HEX);
  extDebug += " [";
  for (size_t i = 0; i < s_bands_count; i++) {
    extDebug += String(s_bands[i].id, HEX) + "(" + String(millis() - s_bands[i].last_seen_ms) + "ms) ";
  }
  extDebug += "]";
  json += "\"debug\":\"" + extDebug + "\",";
  
  json += "\"bands\":[";
  for (size_t i = 0; i < s_bands_count; i++) {
    json += "{";
    json += "\"id\":" + String(s_bands[i].id) + ",";
    json += "\"rssi\":" + String(s_bands[i].rssi) + ",";
    json += "\"hr\":" + String(s_bands[i].hr) + ",";
    json += "\"spo2\":" + String(s_bands[i].spo2) + ",";
    json += "\"age_ms\":" + String(millis() - s_bands[i].last_seen_ms);
    json += "}";
    if (i < s_bands_count - 1) json += ",";
  }
  json += "]}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleAssign() {
  if (server.hasArg("id")) {
    webui_wants_target_id = strtoul(server.arg("id").c_str(), NULL, 10);
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleSwitchMode() {
  webui_wants_mode_switch = true;
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void init_web_ui() {
  Serial.println("[WebUI] Starting Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ResQ-Locator", "12345678");
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[WebUI] AP IP address: ");
  Serial.println(IP);
  
  server.on("/", handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/api/data", HTTP_OPTIONS, handleOptions);
  server.on("/api/assign", HTTP_POST, handleAssign);
  server.on("/api/assign", HTTP_OPTIONS, handleOptions);
  server.on("/api/switch_mode", HTTP_POST, handleSwitchMode);
  server.on("/api/switch_mode", HTTP_OPTIONS, handleOptions);
  server.begin();
  Serial.println("[WebUI] HTTP server started");
}

void webui_loop() {
  server.handleClient();
}
