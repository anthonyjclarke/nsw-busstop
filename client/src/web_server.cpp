#include "web_server.h"
#include "bus_api.h"
#include "time_mgr.h"
#include "../include/config.h"
#include "../include/debug.h"
#include "../include/secrets.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static AsyncWebServer server(WEB_PORT);

static void buildLocalStateJson(String& body) {
  // Recalculate minutes so the JSON always reflects current time
  recalcMinutes();

  StaticJsonDocument<2048> doc;

  doc["time"]  = getTimeStr();
  doc["date"]  = getDateStr();
  doc["now"]   = (long)getUTCNow();   // UTC epoch for client-side recalc
  doc["tzOff"] = getLocalTZOffset();  // local TZ offset in seconds (e.g. +39600 for AEDT)

  JsonArray stops = doc.createNestedArray("stops");
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    JsonObject stop = stops.createNestedObject();
    stop["id"]    = stopIds[i];
    stop["name"]  = stopNames[i];
    stop["valid"] = stopData[i].valid;
    stop["fetchAge"] = stopData[i].lastFetchMs
      ? (millis() - stopData[i].lastFetchMs) / 1000 : -1;
    if (stopData[i].hasAlerts) {
      stop["alert"] = stopData[i].alertText;
    }

    JsonArray deps = stop.createNestedArray("departures");
    for (uint8_t j = 0; j < stopData[i].count; j++) {
      const Departure& d = stopData[i].departures[j];
      JsonObject dep = deps.createNestedObject();
      dep["route"]   = d.route;
      dep["clock"]   = d.clockTime;
      dep["minutes"] = d.minutesUntil;
      dep["epoch"]   = (long)d.epochUTC;
      dep["rt"]      = d.isRealtime;
      dep["delay"]   = d.delaySecs;
      if (d.destination[0] != '\0') {
        dep["dest"] = d.destination;
      }
    }
  }

  serializeJson(doc, body);
}

static bool fetchNasJson(const char* path, String& body) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = getNasUrl();
  if (url.endsWith("/") && path[0] == '/') {
    url.remove(url.length() - 1);
  } else if (!url.endsWith("/") && path[0] != '/') {
    url += '/';
  }
  url += path;

  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(5000);

  if (strlen(SECRET_NAS_API_KEY) > 0) {
    http.addHeader("Authorization", String("Bearer ") + SECRET_NAS_API_KEY);
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    DBG_WARN("NAS proxy GET failed: %s -> HTTP %d", url.c_str(), code);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  return true;
}

static bool postNasJson(const char* path, const String& payload, String& body) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = getNasUrl();
  if (url.endsWith("/") && path[0] == '/') {
    url.remove(url.length() - 1);
  } else if (!url.endsWith("/") && path[0] != '/') {
    url += '/';
  }
  url += path;

  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  if (strlen(SECRET_NAS_API_KEY) > 0) {
    http.addHeader("Authorization", String("Bearer ") + SECRET_NAS_API_KEY);
  }

  int code = http.POST(payload);
  if (code != HTTP_CODE_OK) {
    DBG_WARN("NAS proxy POST failed: %s -> HTTP %d", url.c_str(), code);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  return true;
}

static bool getNasDashboardRows(uint8_t& rowsOut) {
  String body;
  if (!fetchNasJson("/api/dashboard-config", body)) {
    return false;
  }

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    DBG_WARN("NAS dashboard-config parse error: %s", err.c_str());
    return false;
  }

  int rows = doc["departuresPerStop"] | 0;
  if (rows < WEBUI_DEPARTURES_MIN || rows > WEBUI_DEPARTURES_MAX) {
    DBG_WARN("NAS dashboard rows out of range: %d", rows);
    return false;
  }

  rowsOut = (uint8_t)rows;
  return true;
}

static bool setNasDashboardRows(uint8_t rows) {
  if (rows < WEBUI_DEPARTURES_MIN || rows > WEBUI_DEPARTURES_MAX) {
    return false;
  }

  StaticJsonDocument<64> doc;
  doc["departures_per_stop"] = rows;

  String payload;
  serializeJson(doc, payload);

  String body;
  return postNasJson("/api/dashboard-config", payload, body);
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

// GET /api/state
// Returns the fixed local cache used by the TFT.
static void handleApiState(AsyncWebServerRequest* req) {
  String body;
  buildLocalStateJson(body);
  req->send(200, "application/json", body);
}

// GET /api/dashboard-state
// For the browser dashboard, prefer the NAS dashboard-state feed so the local
// WebUI can show 1-8 rows per stop while the TFT remains fixed at 3.
static void handleApiDashboardState(AsyncWebServerRequest* req) {
  String body;
  if (fetchNasJson("/api/dashboard-state", body)) {
    req->send(200, "application/json", body);
    return;
  }

  buildLocalStateJson(body);
  req->send(200, "application/json", body);
}

// GET /
// Dashboard — polls /api/dashboard-state every 15s, re-renders every 5s client-side.
static const char ROOT_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD BusStop</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#fff;padding:1em;max-width:480px;margin:0 auto}
h1{font-size:1.2em;margin-bottom:.15em}
.hdr{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:.6em}
.ts{color:#888;font-size:.85em}
.upd{color:#555;font-size:.75em}
.alert{background:#442200;border:1px solid #886600;border-radius:4px;padding:.4em .6em;margin-bottom:.5em;font-size:.8em;color:#ffcc44}
.stop{background:#1a1a1a;border-radius:6px;padding:.6em .8em;margin-bottom:.5em}
.sh{display:flex;justify-content:space-between;align-items:center;margin-bottom:.3em}
.sn{color:#0cf;font-weight:700}
.badge{font-size:.7em;padding:.1em .4em;border-radius:3px}
.badge-rt{background:#0a3a0a;color:#0f0;border:1px solid #0a0}
.badge-sched{background:#222;color:#666;border:1px solid #444}
.dep{display:flex;gap:.6em;padding:.15em 0;font-size:.9em;align-items:baseline}
.dr{min-width:2.8em;font-weight:600}
.ds{min-width:4.2em}
.dd{flex:1;color:#888;font-size:.8em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dy{min-width:5.2em;text-align:right}
.mn{min-width:2.8em;text-align:right}
.delay{display:inline-block;font-size:.72em;padding:.1em .4em;border-radius:999px;border:1px solid #6b4a00;background:#2d2100;color:#ffb84d}
.delay-early{display:inline-block;font-size:.72em;padding:.1em .4em;border-radius:999px;border:1px solid #1b5e20;background:#0f2412;color:#7dff8a}
.near{color:#0f0}.far{color:#ff0}.now{color:#f90}.gone{color:#f44}
.ck{color:#aaa}
.nd{color:#555;font-style:italic;font-size:.85em}
.ft{margin-top:.8em;font-size:.75em;color:#444;text-align:center}
.ft a{color:#0cf;text-decoration:none}
</style></head><body>
<h1>Bus Departures</h1>
<div class="hdr"><span class="ts" id="clock">--</span><span class="upd" id="upd">--</span></div>
<div id="alerts"></div>
<div id="stops">Loading...</div>
<div class="ft"><a href="/config">Config</a></div>
<script>
var D=null,fetched=0;
function fetchAndRender(){
  fetch('/api/dashboard-state').then(function(r){return r.json();}).then(function(d){
    D=d;fetched=Date.now();render();
  }).catch(function(){
  });
}
function fmtDelay(s){
  if(!s||s===0)return '';
  var m=Math.round(s/60);
  if(m>=2)return '<span class="delay">+'+m+'m late</span>';
  if(m<=-2)return '<span class="delay-early">'+Math.abs(m)+'m early</span>';
  return '';
}
function isToday(epoch,nowUTC,tzOff){
  var localEp=(epoch+tzOff)*1000;
  var localNow=(nowUTC+tzOff)*1000;
  var d1=new Date(localEp),d2=new Date(localNow);
  return d1.getUTCFullYear()===d2.getUTCFullYear()
      && d1.getUTCMonth()===d2.getUTCMonth()
      && d1.getUTCDate()===d2.getUTCDate();
}
function dayAbbr(epoch,tzOff){
  var days=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
  var d=new Date((epoch+tzOff)*1000);
  return days[d.getUTCDay()];
}
function render(){
  if(!D)return;
  document.getElementById('clock').textContent=D.time+'  '+D.date;
  var age=Math.round((Date.now()-fetched)/1000);
  document.getElementById('upd').textContent='Updated '+age+'s ago';
  var nowUTC=D.now+age;
  var tzOff=D.tzOff||0;
  var ah='';
  D.stops.forEach(function(s){
    if(s.alert)ah+='<div class="alert">&#9888; '+s.alert+'</div>';
  });
  document.getElementById('alerts').innerHTML=ah;
  var h='';
  D.stops.forEach(function(s){
    h+='<div class="stop"><div class="sh"><span class="sn">'+s.name+'</span></div>';
    if(!s.valid||!s.departures.length){h+='<div class="nd">No data</div>';}
    else s.departures.forEach(function(d){
      var m=Math.round((d.epoch-nowUTC)/60);
      var today=isToday(d.epoch,nowUTC,tzOff);
      var cls,label;
      if(!today){
        cls='far';label=dayAbbr(d.epoch,tzOff);
      } else if(m<=0){
        if(m<0){cls='gone';label='Gone';}else{cls='now';label='Now';}
      } else {
        if(m>=60){var hr=Math.floor(m/60),rm=m%60;cls='far';label=rm===0?hr+'h':hr+'h'+rm+'m';}
        else{cls=m<10?'near':'far';label=m+'m';}
      }
      var dl=fmtDelay(d.delay);
      var dest=d.dest||'';
      var badge=d.rt?'<span class="badge badge-rt">LIVE</span>'
                    :'<span class="badge badge-sched">SCHED</span>';
      h+='<div class="dep">'
        +'<span class="dr">'+d.route+'</span>'
        +'<span class="ds">'+badge+'</span>'
        +'<span class="dd">'+dest+'</span>'
        +'<span class="dy">'+dl+'</span>'
        +'<span class="mn '+cls+'">'+label+'</span>'
        +'<span class="ck">'+d.clock+'</span>'
        +'</div>';
    });
    h+='</div>';
  });
  document.getElementById('stops').innerHTML=h;
}
function poll(){
  fetchAndRender();
}

poll();
setInterval(poll,15000);
setInterval(render,5000);
</script>
</body></html>)rawliteral";

static void handleRoot(AsyncWebServerRequest* req) {
  req->send(200, "text/html", ROOT_HTML);
}

// GET /config — device config + stats page
static const char CONFIG_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD BusStop — Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#fff;padding:1em;max-width:480px;margin:0 auto}
h1{font-size:1.2em;margin-bottom:.6em}
h2{font-size:.95em;margin:.8em 0 .4em;color:#0cf}
.card{background:#1a1a1a;border-radius:6px;padding:.8em;margin-bottom:.6em}
.row{display:flex;justify-content:space-between;padding:.2em 0;font-size:.85em;gap:.5em}
.row span:first-child{color:#888}
.row span:last-child{color:#fff;font-family:ui-monospace,Menlo,monospace;text-align:right;word-break:break-all}
.meta{color:#888;font-size:.8em;line-height:1.45;margin-bottom:.6em}
.slider-row{display:flex;justify-content:space-between;align-items:center;gap:.5em;flex-wrap:wrap;margin-bottom:.35em}
.slider-value{color:#0cf;font-weight:700}
label{display:block;color:#888;font-size:.8em;margin-bottom:.25em}
input[type="text"]{width:100%;padding:.5em;border-radius:4px;border:1px solid #333;background:#222;color:#fff;font-family:ui-monospace,Menlo,monospace;font-size:.85em}
input[type="range"]{width:100%;accent-color:#0cf}
button{padding:.5em 1em;border-radius:4px;border:0;background:#0cf;color:#000;font-weight:600;cursor:pointer;margin-top:.5em}
button:hover{background:#0af}
.status{font-size:.75em;margin-top:.4em;min-height:1em}
.ok{color:#0f0}.err{color:#f44}
.ft{margin-top:.8em;font-size:.75em;color:#444;text-align:center}
.ft a{color:#0cf;text-decoration:none;margin:0 .5em}
</style></head><body>
<h1>BusStop Config</h1>

<div class="card">
<h2>NAS Server</h2>
<label for="nasUrl">Server URL</label>
<input id="nasUrl" type="text" placeholder="http://192.168.1.100:8081">
<button onclick="saveNas()">Save</button>
<div id="nasStatus" class="status"></div>
</div>

<div class="card">
<h2>WebUI Display</h2>
<div class="meta">Server-backed rows per stop for the NAS dashboard and this device browser view. The TFT stays fixed at 3 departures.</div>
<div class="slider-row">
<label for="webUiRows">Rows per stop</label>
<div class="slider-value"><span id="webUiRowsValue">3</span> buses</div>
</div>
<input id="webUiRows" type="range" min="1" max="8" value="3" oninput="updateWebUiRowsPreview()">
<button onclick="saveWebUiRows()">Save display setting</button>
<div id="webUiStatus" class="status"></div>
</div>

<div class="card">
<h2>System Stats</h2>
<div id="stats">Loading...</div>
</div>

<div class="ft"><a href="/">Departures</a><a href="/api/dashboard-state">WebUI JSON</a><a href="/api/state">TFT JSON</a></div>

<script>
function flashStatus(id,msg,ok,clearMs){
  var s=document.getElementById(id);
  s.textContent=msg;s.className='status '+(ok?'ok':'err');
  if(clearMs){
    window.setTimeout(function(){if(s.textContent===msg){s.textContent='';s.className='status';}},clearMs);
  }
}
function updateWebUiRowsPreview(){
  document.getElementById('webUiRowsValue').textContent=document.getElementById('webUiRows').value;
}
function load(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.getElementById('nasUrl').value=d.nasUrl||'';
    document.getElementById('webUiRows').value=d.webUiRows||3;
    updateWebUiRowsPreview();
    var rows=[
      ['Uptime',d.uptime],
      ['Firmware',d.build],
      ['WiFi SSID',d.ssid],
      ['IP',d.ip],
      ['RSSI',d.rssi+' dBm'],
      ['MAC',d.mac],
      ['Hostname',d.hostname],
      ['Free Heap',(d.heap/1024).toFixed(1)+' KB'],
      ['Max Block',(d.maxBlk/1024).toFixed(1)+' KB'],
      ['Last Fetch',d.lastFetch],
      ['Chip',d.chip]
    ];
    var h='';
    rows.forEach(function(r){h+='<div class="row"><span>'+r[0]+'</span><span>'+r[1]+'</span></div>';});
    document.getElementById('stats').innerHTML=h;
  });
}
function saveNas(){
  var url=document.getElementById('nasUrl').value.trim();
  var s=document.getElementById('nasStatus');
  s.textContent='Saving...';s.className='status';
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({nasUrl:url})})
    .then(function(r){
      if(r.ok){
        var msg='Saved. Reboot to apply.';
        s.textContent=msg;s.className='status ok';
        window.setTimeout(function(){if(s.textContent===msg){s.textContent='';s.className='status';}},2500);
      }
      else{s.textContent='Save failed';s.className='status err';}
    }).catch(function(){s.textContent='Save failed';s.className='status err';});
}
function saveWebUiRows(){
  var rows=Number(document.getElementById('webUiRows').value);
  document.getElementById('webUiStatus').textContent='Saving...';
  document.getElementById('webUiStatus').className='status';
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({webUiRows:rows})})
    .then(function(r){
      if(!r.ok)throw new Error('save failed');
      return r.json();
    })
    .then(function(d){
      if(d.webUiRows){document.getElementById('webUiRows').value=d.webUiRows;updateWebUiRowsPreview();}
      flashStatus('webUiStatus','Saved. TFT remains fixed at 3.',true,2500);
    }).catch(function(){flashStatus('webUiStatus','Save failed',false,0);});
}
load();
setInterval(load,10000);
</script>
</body></html>)rawliteral";

static void handleConfigPage(AsyncWebServerRequest* req) {
  req->send(200, "text/html", CONFIG_HTML);
}

static void formatUptime(uint32_t ms, char* buf, size_t len) {
  uint32_t s = ms / 1000;
  uint32_t d = s / 86400; s %= 86400;
  uint32_t h = s / 3600;  s %= 3600;
  uint32_t m = s / 60;    s %= 60;
  if (d) snprintf(buf, len, "%ud %uh %um", d, h, m);
  else if (h) snprintf(buf, len, "%uh %um %us", h, m, s);
  else snprintf(buf, len, "%um %us", m, s);
}

// GET /api/config — system stats + current config
static void handleApiConfigGet(AsyncWebServerRequest* req) {
  StaticJsonDocument<1024> doc;

  char upbuf[32];
  formatUptime(millis(), upbuf, sizeof(upbuf));
  doc["uptime"] = upbuf;
  doc["build"]  = __DATE__ " " __TIME__;

  doc["ssid"]     = WiFi.SSID();
  doc["ip"]       = WiFi.localIP().toString();
  doc["rssi"]     = WiFi.RSSI();
  doc["mac"]      = WiFi.macAddress();
  doc["hostname"] = OTA_HOSTNAME;

  doc["heap"]   = ESP.getFreeHeap();
  doc["maxBlk"] = ESP.getMaxAllocHeap();

  char chipBuf[32];
  snprintf(chipBuf, sizeof(chipBuf), "%s rev%d, %d MHz",
           ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  doc["chip"] = chipBuf;

  // Last fetch age from any valid stop (they all update together)
  uint32_t lastMs = 0;
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    if (stopData[i].lastFetchMs > lastMs) lastMs = stopData[i].lastFetchMs;
  }
  if (lastMs) {
    char fbuf[24];
    snprintf(fbuf, sizeof(fbuf), "%us ago", (millis() - lastMs) / 1000);
    doc["lastFetch"] = fbuf;
  } else {
    doc["lastFetch"] = "never";
  }

  doc["nasUrl"] = getNasUrl();
  uint8_t webUiRows = DEPARTURES_PER_STOP;
  if (getNasDashboardRows(webUiRows)) {
    doc["webUiRows"] = webUiRows;
  } else {
    doc["webUiRows"] = DEPARTURES_PER_STOP;
  }

  String body;
  serializeJson(doc, body);
  req->send(200, "application/json", body);
}

// POST /api/config — update NAS URL and/or NAS-backed WebUI rows
static void handleApiConfigPost(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index != 0 || len != total) {
    req->send(400, "application/json", "{\"error\":\"chunked body not supported\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    req->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  bool hasNasUrl = doc.containsKey("nasUrl");
  bool hasWebUiRows = doc.containsKey("webUiRows");
  if (!hasNasUrl && !hasWebUiRows) {
    req->send(400, "application/json", "{\"error\":\"no config fields supplied\"}");
    return;
  }

  if (hasNasUrl) {
    const char* url = doc["nasUrl"];
    if (!url || strlen(url) < 8) {
      req->send(400, "application/json", "{\"error\":\"nasUrl required\"}");
      return;
    }

    if (!setNasUrl(String(url))) {
      req->send(500, "application/json", "{\"error\":\"save failed\"}");
      return;
    }
  }

  int webUiRows = 0;
  if (hasWebUiRows) {
    webUiRows = doc["webUiRows"] | 0;
    if (webUiRows < WEBUI_DEPARTURES_MIN || webUiRows > WEBUI_DEPARTURES_MAX) {
      req->send(400, "application/json", "{\"error\":\"webUiRows out of range\"}");
      return;
    }

    if (!setNasDashboardRows((uint8_t)webUiRows)) {
      req->send(502, "application/json", "{\"error\":\"nas display save failed\"}");
      return;
    }
  }

  StaticJsonDocument<128> resp;
  resp["ok"] = true;
  if (hasNasUrl) {
    resp["nasUrl"] = getNasUrl();
  }
  if (hasWebUiRows) {
    resp["webUiRows"] = webUiRows;
  }

  String body;
  serializeJson(resp, body);
  req->send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

void initWebServer() {
  server.on("/",           HTTP_GET, handleRoot);
  server.on("/config",     HTTP_GET, handleConfigPage);
  server.on("/api/state",  HTTP_GET, handleApiState);
  server.on("/api/dashboard-state", HTTP_GET, handleApiDashboardState);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST,
            [](AsyncWebServerRequest* req) {},
            NULL,
            handleApiConfigPost);

  server.begin();
  DBG_INFO("Web server started — http://%s/", WiFi.localIP().toString().c_str());
}
