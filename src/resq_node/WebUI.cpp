#include "WebUI.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "Calibration.h"

static WebServer server(80);

String g_webui_debug = "Boot OK";
uint32_t webui_wants_target_id = 0;
volatile bool webui_wants_ring = false;

static RingState s_ring_state = RING_IDLE;
void webui_set_ring_state(RingState s) { s_ring_state = s; }

volatile uint8_t webui_wants_found = 0;
static FoundState s_found_state = FOUND_IDLE;
void webui_set_found_state(FoundState s) { s_found_state = s; }

// State variables for WebUI
static uint32_t s_target_id = 0;
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

void webui_set_target(uint32_t id, uint8_t /*mode*/, float dist, float angle) {
  s_target_id = id;
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
<html lang="th">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <title>ResQ-Locator</title>
  <style>
    :root{
      --bg:#1a1a1a; --panel:#252525; --surface:#2d2d2d; --raised:#353535; --active:#404040;
      --input:#181818; --divider:#0e0e0e; --border:#3a3a3a;
      --text:#d4d4d4; --dim:#909090; --muted:#6a6a6a;
      --accent:#5dade2; --accent-hover:#7bbeed; --accent-pressed:#3d8fc4;
      --green:#5fb878; --yellow:#d4a235; --red:#d04545;
    }
    *{box-sizing:border-box;}
    body{margin:0;background:var(--bg);color:var(--text);
      font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans Thai",sans-serif;
      font-size:13px;-webkit-font-smoothing:antialiased;}
    .mono{font-variant-numeric:tabular-nums;font-feature-settings:"tnum" 1;}
    .wrap{max-width:460px;margin:0 auto;padding:10px;display:flex;flex-direction:column;gap:8px;}

    /* top bar */
    .topbar{display:flex;align-items:center;justify-content:space-between;height:34px;
      padding:0 10px;background:var(--surface);border:1px solid var(--border);}
    .brand{font-size:11px;font-weight:700;letter-spacing:.14em;text-transform:uppercase;color:var(--accent);}
    .dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--green);
      margin-right:7px;box-shadow:0 0 6px var(--green);vertical-align:middle;}
    .topstat{font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:.09em;}

    /* Premiere panel grammar */
    .panel{background:var(--panel);border:1px solid var(--border);}
    .panel-header{display:flex;align-items:center;justify-content:space-between;height:26px;
      padding:0 10px;background:var(--surface);color:var(--dim);border-bottom:1px solid var(--divider);
      font-size:10px;font-weight:600;text-transform:uppercase;letter-spacing:.1em;}
    .panel-header .hval{color:var(--text);font-variant-numeric:tabular-nums;}
    .panel-body{padding:10px;}

    .row{display:flex;justify-content:space-between;align-items:center;padding:5px 0;}
    .row .label{color:var(--dim);font-size:12px;}
    .row .val{font-size:15px;font-weight:600;color:var(--text);font-variant-numeric:tabular-nums;}
    .val.accent{color:var(--accent);}

    /* radar (responsive) */
    .radar{position:relative;width:100%;max-width:260px;aspect-ratio:1/1;margin:4px auto 10px;border-radius:50%;
      background:radial-gradient(circle at center, rgba(93,173,226,.08) 0%, var(--input) 78%);
      border:1px solid var(--border);overflow:hidden;}
    .ring{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);
      border:1px dashed rgba(255,255,255,.10);border-radius:50%;}
    .r1{width:33%;height:33%;} .r2{width:66%;height:66%;} .r3{width:99%;height:99%;}
    .cross-h{position:absolute;top:50%;left:0;right:0;height:1px;background:rgba(255,255,255,.06);}
    .cross-v{position:absolute;top:0;bottom:0;left:50%;width:1px;background:rgba(255,255,255,.06);}
    .sweep{position:absolute;top:50%;left:50%;width:0;height:0;border-radius:50%;
      border:2px solid var(--accent);transform:translate(-50%,-50%);
      animation:radar 2.4s linear infinite;box-sizing:border-box;}
    @keyframes radar{0%{width:0;height:0;opacity:.9}100%{width:99%;height:99%;opacity:0}}
    .blip{position:absolute;top:50%;left:50%;width:14px;height:14px;border-radius:50%;
      background:var(--red);transform:translate(-50%,-50%);display:none;
      box-shadow:0 0 12px var(--red);transition:top .3s ease,left .3s ease;}

    .dist-big{text-align:center;font-size:30px;font-weight:700;color:var(--accent);
      font-variant-numeric:tabular-nums;line-height:1;}
    .dist-sub{text-align:center;font-size:10px;color:var(--muted);text-transform:uppercase;
      letter-spacing:.11em;margin-top:5px;}

    .photo{display:none;width:84px;height:84px;object-fit:cover;border:1px solid var(--border);
      margin:0 auto 8px;}
    .med{display:none;font-size:12px;line-height:1.55;padding:8px;background:var(--input);
      border:1px solid var(--divider);margin-top:8px;}

    .btn{display:inline-flex;align-items:center;justify-content:center;width:100%;height:32px;padding:0 10px;
      font-size:12px;font-weight:600;text-transform:uppercase;letter-spacing:.06em;cursor:pointer;
      color:var(--text);background:var(--raised);border:1px solid var(--border);border-radius:2px;}
    .btn:active{background:var(--active);}
    .btn.accent{background:var(--accent-pressed);border-color:var(--accent-pressed);color:#fff;}
    .btn.accent:active{background:var(--accent);}
    .btn-sm{height:24px;padding:0 10px;font-size:10px;font-weight:600;text-transform:uppercase;
      cursor:pointer;color:var(--text);background:var(--raised);border:1px solid var(--border);border-radius:2px;}
    .btn-sm.locked{background:var(--green);border-color:var(--green);color:#0e1a12;}

    .tbl-scroll{overflow-x:auto;}
    table{width:100%;border-collapse:collapse;}
    th{text-align:left;padding:6px 8px;font-size:10px;text-transform:uppercase;letter-spacing:.08em;
      color:var(--muted);border-bottom:1px solid var(--border);white-space:nowrap;}
    td{padding:8px;font-size:12px;border-bottom:1px solid var(--divider);vertical-align:middle;}
    tr:last-child td{border-bottom:none;}

    input[type=file]{width:100%;font-size:12px;color:var(--dim);margin:2px 0 8px;}
    .hint{font-size:11px;color:var(--muted);margin-top:8px;line-height:1.5;}
    details.panel>summary{list-style:none;cursor:pointer;}
    details.panel>summary::-webkit-details-marker{display:none;}
    .cal-in{display:grid;grid-template-columns:1fr auto;gap:6px;margin-bottom:6px;}
    .cal-in input{height:32px;padding:0 8px;font-size:14px;color:var(--text);background:var(--input);
      border:1px solid var(--border);border-radius:2px;width:100%;}
    .cal-in .btn{width:auto;}
    .cal-btns{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:8px;}
    .ok{color:var(--green);} .warn{color:var(--yellow);}
    #debug_msg{font-size:10px;color:var(--muted);text-align:center;word-break:break-all;padding:2px 4px;}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="topbar">
      <span><span class="dot"></span><span class="brand">ResQ-Locator</span></span>
      <span class="topstat" id="topstat">Searching…</span>
    </div>

    <div class="panel">
      <div class="panel-header"><span>Target</span><span class="hval" id="target-id">None</span></div>
      <div class="panel-body">
        <div class="radar">
          <div class="ring r1"></div><div class="ring r2"></div><div class="ring r3"></div>
          <div class="cross-h"></div><div class="cross-v"></div>
          <div class="sweep"></div><div class="blip" id="blip"></div>
        </div>
        <div class="dist-big" id="distance">--</div>
        <div class="dist-sub">Distance to wearer</div>
        <div class="med" id="target-medical"></div>
        <button class="btn accent" id="ring-btn" onclick="ringBand()" style="margin-top:10px;display:none;">🔔 ส่งเสียงที่กำไล</button>
        <div class="hint" id="ring-status" style="text-align:center;"></div>
        <div id="found-box" style="display:none;margin-top:10px;">
          <div class="hint" style="margin:0 0 6px;">แจ้งผลให้ศูนย์ (ส่งผ่าน LoRa ใน slot ของเครื่องนี้)</div>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;">
            <button class="btn accent" onclick="reportFound(0)">✅ ช่วยออกมาแล้ว</button>
            <button class="btn" onclick="reportFound(1)">⚫ เสียชีวิต</button>
            <button class="btn" onclick="reportFound(2)">❓ ไม่พบคน</button>
            <button class="btn" onclick="reportFound(3)">🆘 ขอกำลังเสริม</button>
          </div>
        </div>
        <div class="hint" id="found-status" style="text-align:center;"></div>
      </div>
    </div>

    <div class="panel" id="vitals-panel" style="display:none;">
      <div class="panel-header"><span>Wearer Vitals</span></div>
      <div class="panel-body">
        <img id="target-photo" class="photo" alt="">
        <div class="row"><span class="label">Heart Rate</span><span class="val accent" id="v-hr">--</span></div>
        <div class="row"><span class="label">Triage</span><span class="val" id="v-triage">--</span></div>
      </div>
    </div>

    <div class="panel">
      <div class="panel-header"><span>Detected Bands</span><span class="hval" id="band-count">0</span></div>
      <div class="panel-body" style="padding:0;">
        <div class="tbl-scroll">
          <table>
            <thead><tr><th>Band</th><th>HR</th><th>RSSI</th><th></th></tr></thead>
            <tbody id="band-list">
              <tr><td colspan="4" style="text-align:center;color:var(--muted);padding:14px;">No bands detected</td></tr>
            </tbody>
          </table>
        </div>
      </div>
    </div>

    <details class="panel" id="cal-panel">
      <summary class="panel-header"><span>ปรับเทียบระยะ ▾</span><span class="hval" id="cal-model">--</span></summary>
      <div class="panel-body">
        <div class="hint" style="margin:0 0 8px;">
          1) ล็อกกำไลที่จะใช้ วางกำไลนิ่ง ๆ สูงพอ ๆ กับเครื่องนี้<br>
          2) ยืนห่างตามระยะที่วัดด้วยตลับเมตร ใส่ระยะแล้วกด <b>วัด</b> เครื่องจะเก็บสัญญาณ 3 รอบ (~30 วิ) ห้ามขยับ<br>
          3) ทำซ้ำหลายระยะ เช่น 1, 2, 5, 10, 15, 20, 30 ม. (สูงสุด 10 จุด) แล้วกด <b>คำนวณและบันทึก</b>
        </div>
        <div class="row"><span class="label">RSSI กำไลที่ล็อกตอนนี้</span><span class="val mono" id="cal-live">--</span></div>
        <div class="cal-in">
          <input id="cal-d" type="number" inputmode="decimal" min="0.3" step="0.1" placeholder="ระยะ (เมตร)">
          <button class="btn accent" id="cal-rec" onclick="calAdd()">วัด</button>
        </div>
        <div class="hint" id="cal-status" style="margin:0 0 6px;"></div>
        <div class="tbl-scroll">
          <table>
            <thead><tr><th>#</th><th>ระยะ</th><th>RSSI</th><th>ประมาณจากสูตรใหม่</th></tr></thead>
            <tbody id="cal-pts"><tr><td colspan="4" style="text-align:center;color:var(--muted);padding:10px;">ยังไม่มีจุด</td></tr></tbody>
          </table>
        </div>
        <div class="hint" id="cal-fit"></div>
        <div class="cal-btns">
          <button class="btn accent" onclick="calCmd('apply')" style="grid-column:1/3;">คำนวณและบันทึก</button>
          <button class="btn" onclick="calCmd('undo')">ลบจุดล่าสุด</button>
          <button class="btn" onclick="calCmd('clear','ล้างจุดที่วัดทั้งหมด?')">ล้างทั้งหมด</button>
          <button class="btn" onclick="calCmd('reset','คืนค่าเริ่มต้นจากโรงงาน?')" style="grid-column:1/3;">คืนค่าเริ่มต้น</button>
        </div>
      </div>
    </details>

    <div class="panel">
      <div class="panel-header"><span>Roster Sync · Offline</span><span class="hval" id="roster-count">--</span></div>
      <div class="panel-body">
        <input type="file" id="roster-file" accept=".json,application/json">
        <button class="btn accent" onclick="uploadRoster()">Upload roster.json</button>
        <div class="hint" id="roster-status">Export จากเว็บ dispatcher แล้วเลือกไฟล์ roster.json ที่นี่</div>
      </div>
    </div>

    <div id="debug_msg">Booting…</div>
  </div>

  <script>
    let rosterCache = {};

    function loadRoster(){
      return fetch('/api/roster').then(r=>r.json()).then(data=>{
        rosterCache = data || {};
        document.getElementById('roster-count').innerText = Object.keys(rosterCache).length + ' คน';
      }).catch(()=>{ document.getElementById('roster-count').innerText = '0 คน'; });
    }
    loadRoster();

    // Upload roster.json exported from the dispatcher. Same-origin http POST, so
    // no mixed-content/CORS block. Multipart so the ESP32 streams it to flash.
    function uploadRoster(){
      const st = document.getElementById('roster-status');
      const f = document.getElementById('roster-file').files[0];
      if(!f){ st.innerText='เลือกไฟล์ roster.json ก่อน'; return; }
      st.innerText='กำลังอัปโหลด…';
      const r = new FileReader();
      r.onload = function(){
        try{ JSON.parse(r.result); }catch(e){ st.innerText='ไฟล์ไม่ใช่ JSON ที่ถูกต้อง'; return; }
        const fd = new FormData();
        fd.append('file', f, 'roster.json');
        fetch('/api/roster', { method:'POST', body:fd })
          .then(res=>res.json())
          .then(d=>{ st.innerText = d.success ? ('✅ บันทึกแล้ว '+Math.round((d.bytes||0)/1024)+' KB (ออฟไลน์ได้)') : 'ล้มเหลว'; return loadRoster(); })
          .catch(e=>{ st.innerText='ล้มเหลว: '+e; });
      };
      r.readAsText(f);
    }

    function assignTarget(id){
      fetch('/api/assign?id='+id, { method:'POST' }).catch(()=>{});
    }

    function ringBand(){
      document.getElementById('ring-status').innerText = 'กำลังส่งคำสั่ง…';
      fetch('/api/ring', { method:'POST' }).catch(()=>{});
    }
    const FOUND_LABEL = ['ช่วยออกมาแล้ว', 'เสียชีวิต', 'ไม่พบคน', 'ขอกำลังเสริม'];
    function reportFound(outcome){
      if(outcome !== 2 && outcome !== 3 && !confirm('ยืนยันแจ้งศูนย์: ' + FOUND_LABEL[outcome] + ' ?')) return;
      fetch('/api/found?outcome=' + outcome, { method:'POST' }).catch(()=>{});
    }
    const FOUND_TEXT = ['', 'กำลังแจ้งศูนย์… (ส่งซ้ำ 3 รอบ)', '✅ แจ้งศูนย์แล้ว'];
    const RING_TEXT = ['', 'กำลังเรียกกำไล… (รอกำไลตื่น สูงสุด ~12 วิ)', '✅ กำไลกำลังส่งเสียง', '⚠️ กำไลไม่ตอบ (อยู่นอกระยะหรือปิดอยู่)'];

    const TRIAGE_LABEL = ['SAFE','WATCH','CRITICAL','NO PULSE','NO DATA'];
    const TRIAGE_COLOR = ['#5fb878','#d4a235','#d04545','#909090','#6b7280'];

    let calMsg = '';
    function calAdd(){
      const d = parseFloat(document.getElementById('cal-d').value);
      if(!(d > 0)){ calMsg = 'ใส่ระยะเป็นเมตรก่อน'; return; }
      calMsg = '';
      fetch('/api/cal/add?d='+d, { method:'POST' }).then(r=>r.json())
        .then(x=>{ calMsg = x.success ? '' : ('⚠️ '+x.error); }).catch(()=>{});
    }
    function calCmd(cmd, ask){
      if(ask && !confirm(ask)) return;
      fetch('/api/cal/'+cmd, { method:'POST' }).then(r=>r.json())
        .then(x=>{ calMsg = x.success ? (cmd==='apply' ? '✅ บันทึกแล้ว เครื่องใช้ค่านี้คำนวณระยะตั้งแต่ตอนนี้' : '') : ('⚠️ '+x.error); }).catch(()=>{});
    }
    function renderCal(c, data){
      if(!c) return;
      document.getElementById('cal-model').innerText =
        c.ref.toFixed(1)+' dBm · n '+c.n.toFixed(2)+(c.def ? ' (ค่าเริ่มต้น)' : ' (ปรับเทียบแล้ว)');
      const tb = (data.bands||[]).find(b=>b.id===data.target_id);
      document.getElementById('cal-live').innerText =
        !data.target_id ? 'ยังไม่ล็อกกำไล' : (tb && tb.rssi > -127 ? tb.rssi+' dBm' : 'ยังไม่ได้ยิน');
      const rec = document.getElementById('cal-rec');
      rec.disabled = c.cap || !data.target_id;
      rec.innerText = c.cap ? ('กำลังวัด '+c.got+'/'+c.need+'…') : 'วัด';
      document.getElementById('cal-status').innerText = c.cap
        ? 'กำลังเก็บสัญญาณที่ '+c.capd+' ม. ('+c.got+'/'+c.need+') — อย่าขยับ'
        : (c.err ? '⚠️ '+c.err : calMsg);
      const f = c.fit;
      const est = r => f.ok ? Math.pow(10,(f.ref-r)/(10*f.n)).toFixed(1)+' ม.' : '--';
      document.getElementById('cal-pts').innerHTML = c.pts.length
        ? c.pts.map((p,i)=>'<tr><td>'+(i+1)+'</td><td class="mono">'+p[0]+' ม.</td><td class="mono">'+p[1].toFixed(1)+'</td><td class="mono">'+est(p[1])+'</td></tr>').join('')
        : '<tr><td colspan="4" style="text-align:center;color:var(--muted);padding:10px;">ยังไม่มีจุด</td></tr>';
      const fe = document.getElementById('cal-fit');
      if(f.ok){
        const q = f.r2 >= 0.9 ? '<span class="ok">ดีมาก</span>' : f.r2 >= 0.75 ? '<span class="warn">พอใช้</span>' : '<span class="warn">แกว่งมาก ลองวัดเพิ่ม</span>';
        fe.innerHTML = 'ผลคำนวณ: RSSI@1m <b>'+f.ref.toFixed(1)+'</b> dBm · n <b>'+f.n.toFixed(2)+'</b> · R² '+f.r2.toFixed(2)+' ('+q+') — ยังไม่ใช้จนกว่าจะกดบันทึก';
      } else {
        fe.innerText = c.pts.length ? f.err : '';
      }
    }

    function updateData(signal){
      return fetch('/api/data', { signal }).then(r=>r.json()).then(data=>{
        document.getElementById('debug_msg').innerText = data.debug || '';
        const bands = data.bands || [];
        document.getElementById('band-count').innerText = bands.length;

        if(!data.target_id || data.target_id === 0){
          document.getElementById('target-id').innerText = 'None';
          document.getElementById('topstat').innerText = 'Searching…';
          document.getElementById('target-medical').style.display = 'none';
          document.getElementById('distance').innerText = '--';
          document.getElementById('blip').style.display = 'none';
          document.getElementById('vitals-panel').style.display = 'none';
          document.getElementById('ring-btn').style.display = 'none';
          document.getElementById('ring-status').innerText = '';
          document.getElementById('found-box').style.display = 'none';
        } else {
          document.getElementById('found-box').style.display = 'block';
          document.getElementById('ring-btn').style.display = 'flex';
          document.getElementById('ring-status').innerText = RING_TEXT[data.ring] || '';
          const hexId = data.target_id.toString(16).toUpperCase().padStart(8,'0');
          const pInfo = rosterCache[hexId];
          document.getElementById('target-id').innerText = pInfo ? pInfo.name : hexId;
          document.getElementById('topstat').innerText = 'Locked';

          const ph = document.getElementById('target-photo');
          if(pInfo && pInfo.photo){ ph.src = pInfo.photo; ph.style.display='block'; } else { ph.style.display='none'; }

          let medHtml = '';
          if(pInfo){
            if(pInfo.age) medHtml += 'Age '+pInfo.age;
            if(pInfo.medical && pInfo.medical.bloodType) medHtml += ' · Blood <b style="color:var(--red)">'+pInfo.medical.bloodType+'</b>';
            if(pInfo.medical && pInfo.medical.conditions && pInfo.medical.conditions.length)
              medHtml += '<div style="margin-top:4px"><span style="color:var(--red)">Cond:</span> '+pInfo.medical.conditions.join(', ')+'</div>';
            if(pInfo.medical && pInfo.medical.allergies && pInfo.medical.allergies.length)
              medHtml += '<div style="margin-top:4px"><span style="color:var(--yellow)">Allergy:</span> '+pInfo.medical.allergies.join(', ')+'</div>';
          }
          const md = document.getElementById('target-medical');
          if(medHtml){ md.innerHTML = medHtml; md.style.display='block'; } else { md.style.display='none'; }

          document.getElementById('vitals-panel').style.display = 'block';
          document.getElementById('v-hr').innerText = (data.hr>0 ? data.hr+' bpm' : '--');
          const vt = document.getElementById('v-triage');
          vt.innerText = TRIAGE_LABEL[data.triage] || '--';
          vt.style.color = TRIAGE_COLOR[data.triage] || '#909090';

          // Distance comes from the firmware (RSSI path-loss estimate). <0 = no signal.
          const d = data.distance_m;
          const blip = document.getElementById('blip');
          if(typeof d === 'number' && d >= 0){
            document.getElementById('distance').innerText = d.toFixed(1)+' m';
            const R = (document.querySelector('.radar').clientWidth || 240) / 2;
            const px = Math.min(R-9, (d/20.0)*(R-9));   // 20 m spans the radar
            blip.style.display = 'block';
            blip.style.left = R + 'px';
            blip.style.top  = (R - px) + 'px';          // farther => nearer the edge
          } else {
            document.getElementById('distance').innerText = 'No Signal';
            blip.style.display = 'none';
          }
        }

        const tbody = document.getElementById('band-list');
        if(bands.length === 0){
          tbody.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--muted);padding:14px;">No bands detected</td></tr>';
        } else {
          tbody.innerHTML = bands.map(b=>{
            const locked = (b.id === data.target_id);
            const btn = locked ? '<button class="btn-sm locked">Locked</button>'
                               : '<button class="btn-sm" onclick="assignTarget('+b.id+')">Assign</button>';
            const hexId = b.id.toString(16).toUpperCase().padStart(8,'0');
            const pInfo = rosterCache[hexId];
            const nm = pInfo ? ('<span style="font-weight:600">'+pInfo.name+'</span><br><span style="font-size:10px;color:var(--muted)">'+hexId+'</span>')
                             : '<span class="mono">'+hexId+'</span>';
            let badge = '';
            if(pInfo && pInfo.medical && pInfo.medical.conditions && pInfo.medical.conditions.length)
              badge = '<span style="display:inline-block;margin-top:3px;padding:1px 4px;font-size:9px;background:rgba(208,69,69,.2);color:var(--red);">'+pInfo.medical.conditions[0]+'</span>';
            const hr = '<span style="color:var(--red)">&#9829;</span> '+(b.hr||'--');
            return '<tr><td>'+nm+badge+'</td><td>'+hr+'</td><td class="mono">'+(b.rssi<=-127?'—':b.rssi)+'</td><td>'+btn+'</td></tr>';
          }).join('');
        }
        document.getElementById('found-status').innerText = FOUND_TEXT[data.found] || '';
        renderCal(data.cal, data);
        setLink(true);
      }).catch(()=>setLink(false));
    }

    // Poll sequentially: wait for each reply (or a 3 s timeout) before the next
    // request. A fixed setInterval piled requests onto the single-threaded ESP32
    // web server whenever it was busy, and they failed in bursts.
    function setLink(ok){
      const s = document.getElementById('topstat');
      if(!ok){ s.innerText = 'Reconnecting…'; s.style.color = 'var(--yellow)'; }
      else { s.style.color = ''; }
    }
    function poll(){
      const ctl = new AbortController();
      const t = setTimeout(()=>ctl.abort(), 3000);
      updateData(ctl.signal).finally(()=>{ clearTimeout(t); setTimeout(poll, 700); });
    }
    poll();
  </script>
</body>
</html>
)rawliteral";

static String cal_json() {
  size_t n = 0;
  const CalPoint* pts = cal_points(&n);
  const CalFit f = cal_fit();
  String j = "{";
  j += "\"ref\":" + String(cal_ref_1m(), 1) + ",\"n\":" + String(cal_n(), 2);
  j += ",\"def\":" + String(cal_is_default() ? "true" : "false");
  j += ",\"cap\":" + String(cal_capturing() ? "true" : "false");
  j += ",\"got\":" + String(cal_captured()) + ",\"need\":" + String(CAL_SAMPLES_PER_POINT);
  j += ",\"capd\":" + String(cal_capture_distance(), 1);
  j += ",\"err\":\"" + String(cal_last_error()) + "\"";
  j += ",\"pts\":[";
  for (size_t i = 0; i < n; i++) {
    if (i) j += ",";
    j += "[" + String(pts[i].distance_m, 1) + "," + String(pts[i].rssi, 1) + "]";
  }
  j += "],\"fit\":{\"ok\":" + String(f.ok ? "true" : "false");
  j += ",\"ref\":" + String(f.ref_1m, 1) + ",\"n\":" + String(f.n, 2) + ",\"r2\":" + String(f.r2, 2);
  j += ",\"err\":\"" + String(f.error) + "\"}}";
  return j;
}

static void send_result(bool ok, const char* error) {
  String r = String("{\"success\":") + (ok ? "true" : "false");
  if (!ok) r += String(",\"error\":\"") + error + "\"";
  r += "}";
  server.send(200, "application/json", r);
}

void handleCalAdd() {
  const float d = server.hasArg("d") ? server.arg("d").toFloat() : 0;
  const char* err = "";
  send_result(cal_start(s_target_id, d, &err), err);
}
void handleCalUndo()  { cal_remove_last(); send_result(true, ""); }
void handleCalClear() { cal_clear(); send_result(true, ""); }
void handleCalReset() { cal_reset(); send_result(true, ""); }
void handleCalApply() {
  const CalFit f = cal_apply();
  send_result(f.ok, f.error);
}

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleApiData() {
  String json = "{";
  json += "\"target_id\":" + String(s_target_id) + ",";
  json += "\"distance_m\":" + String(s_distance_m) + ",";
  json += "\"angle_deg\":" + String(s_angle_deg) + ",";
  json += "\"hr\":" + String(s_v_hr) + ",";
  json += "\"spo2\":" + String(s_v_spo2) + ",";
  json += "\"batt\":" + String(s_v_batt) + ",";
  json += "\"triage\":" + String(s_v_triage) + ",";
  json += "\"ring\":" + String((int)s_ring_state) + ",";
  json += "\"found\":" + String((int)s_found_state) + ",";
  json += "\"cal\":" + cal_json() + ",";
  
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

void handleFound() {
  if (server.hasArg("outcome")) {
    const long o = server.arg("outcome").toInt();
    if (o >= 0 && o <= 3) webui_wants_found = (uint8_t)(o + 1);
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRing() {
  webui_wants_ring = true;
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  server.send(204);
}

void handleRosterGet() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  if (!LittleFS.exists("/roster.json")) {
    server.send(200, "application/json", "{}");
    return;
  }
  File f = LittleFS.open("/roster.json", "r");
  server.streamFile(f, "application/json");
  f.close();
}

// Roster upload is streamed straight to LittleFS. Buffering the whole body in
// RAM (server.arg("plain")) worked for a names-only roster but blows up once
// survivor photos are included - the ESP32 only has ~320 KB with WiFi + BLE
// already resident.
static File s_rosterUpload;
static size_t s_rosterBytes = 0;

void handleRosterUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_rosterBytes = 0;
    s_rosterUpload = LittleFS.open("/roster.json.part", "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_rosterUpload) {
      s_rosterUpload.write(up.buf, up.currentSize);
      s_rosterBytes += up.currentSize;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_rosterUpload) s_rosterUpload.close();
    // Only swap in the new roster once it is fully written, so a dropped
    // upload can't leave a half-parsed file behind.
    LittleFS.remove("/roster.json");
    LittleFS.rename("/roster.json.part", "/roster.json");
    Serial.printf("[WebUI] roster uploaded: %u bytes\n", (unsigned)s_rosterBytes);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_rosterUpload) s_rosterUpload.close();
    LittleFS.remove("/roster.json.part");
  }
}

void handleRosterPostDone() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  String r = "{\"success\":true,\"bytes\":" + String(s_rosterBytes) + "}";
  server.send(200, "application/json", r);
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
  server.on("/api/ring", HTTP_POST, handleRing);
  server.on("/api/found", HTTP_POST, handleFound);
  server.on("/api/ring", HTTP_OPTIONS, handleOptions);
  server.on("/api/cal/add", HTTP_POST, handleCalAdd);
  server.on("/api/cal/undo", HTTP_POST, handleCalUndo);
  server.on("/api/cal/clear", HTTP_POST, handleCalClear);
  server.on("/api/cal/apply", HTTP_POST, handleCalApply);
  server.on("/api/cal/reset", HTTP_POST, handleCalReset);
  // Browsers always ask for a favicon; answer it instead of logging a 404.
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  
  server.on("/api/roster", HTTP_GET, handleRosterGet);
  server.on("/api/roster", HTTP_POST, handleRosterPostDone, handleRosterUpload);
  server.on("/api/roster", HTTP_OPTIONS, handleOptions);
  
  LittleFS.begin(true);
  
  server.begin();
  Serial.println("[WebUI] HTTP server started");
}

void webui_loop() {
  server.handleClient();
}
