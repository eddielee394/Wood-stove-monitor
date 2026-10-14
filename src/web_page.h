// The phone/browser page served at "/". Compiled into the firmware so a
// single OTA update covers both. Polls /api/status every 2s.
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Stove Monitor</title>
<style>
:root{--bg:#0E141B;--card:#141C25;--text:#E8EEF4;--muted:#9FB2C8;--subtle:#7F93AA;--border:#243040;
--green:#4FD18B;--amber:#F5B547;--cool:#8DBDF0;--accent:#1271EB;--warn:#F29A1F;--warnfg:#1A1205;
--crit:#B0102C;--critfg:#FFF1F3;--reload:#1271EB}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:600 16px/1.35 system-ui,-apple-system,"Segoe UI",sans-serif;
font-variant-numeric:tabular-nums}
main{max-width:640px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
h1{font-size:20px;margin:0;font-weight:800}
.fresh{font-size:14px;color:var(--muted)}
.fresh.stale{color:var(--amber)}
.banner{display:none;border-radius:12px;padding:14px 16px;margin-bottom:12px;align-items:center;gap:12px;justify-content:space-between}
.banner.on{display:flex}
.banner b{font-size:18px;font-weight:800;display:block}
.banner span{font-size:14px}
.banner.warning{background:var(--warn);color:var(--warnfg)}
.banner.critical,.banner.chimney{background:var(--crit);color:var(--critfg)}
.banner.fault{background:#231A0A;color:var(--amber);border:2px solid var(--amber)}
.banner.reload{background:var(--reload);color:#F2F7FF}
.banner.silenced{background:var(--card);color:var(--amber);border:1px solid var(--border)}
button{font:inherit;font-weight:800;border-radius:999px;border:0;padding:12px 18px;min-height:44px;cursor:pointer}
.banner button{background:rgba(0,0,0,.25);color:inherit;white-space:nowrap}
.banner.warning button{background:var(--warnfg);color:var(--warn)}
.banner.critical button,.banner.chimney button{background:var(--critfg);color:var(--crit)}
.cards{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:14px 16px}
.card.warning{border-color:var(--warn)}
.card.critical{border-color:#E0304E}
.card.fault{border-color:var(--amber)}
.name{font-weight:800;letter-spacing:1px;color:var(--muted)}
.where{font-size:13px;color:var(--subtle)}
.temp{font-size:56px;font-weight:800;line-height:1.1;letter-spacing:-2px;margin-top:4px}
.temp small{font-size:18px;color:var(--muted);letter-spacing:0;vertical-align:top}
.rate{color:var(--muted);font-size:15px}
.nosig{color:var(--amber);font-size:20px;font-weight:800;margin-top:14px}
.zone{margin-top:12px}
.zone .status{font-weight:800}
.ideal{color:var(--green)}.coolz{color:var(--cool)}.hot,.chimcool{color:var(--amber)}.na{color:var(--muted)}
details{margin-top:20px;background:var(--card);border:1px solid var(--border);border-radius:12px;padding:4px 16px}
summary{cursor:pointer;font-weight:800;padding:12px 0;min-height:44px}
fieldset{border:0;padding:0;margin:0 0 12px}
legend{color:var(--muted);font-size:13px;letter-spacing:1px;padding:8px 0}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid #1C2733}
input[type=number]{width:110px;font:inherit;padding:10px;border-radius:8px;border:1px solid #2E3D4F;background:#17212C;color:var(--text);text-align:right}
input[type=checkbox]{width:24px;height:24px}
.save{background:var(--accent);color:#fff;margin:8px 0 16px}
#msg,#locmsg{font-size:14px;margin-left:8px}
.err{color:var(--amber)}.ok{color:var(--green)}
.outside{color:var(--muted);font-size:15px;margin:-4px 0 12px}
.hint{color:var(--subtle);font-size:13px;margin:4px 0 8px}
input.loc{width:100%;font:inherit;padding:10px;border-radius:8px;border:1px solid #2E3D4F;background:#17212C;color:var(--text)}
footer{color:var(--subtle);font-size:13px;margin-top:16px}
@media (max-width:420px){.cards{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
<header><h1>Stove Monitor</h1><span class="fresh" id="fresh">connecting…</span></header>
<div class="outside" id="outside"></div>
<div class="banner" id="banner"><div><b id="bTitle"></b><span id="bText"></span></div>
<button id="bBtn" type="button">Silence</button></div>
<section class="cards">
<div class="card" id="c0"><div class="name">STOVE <span class="where">collar pipe</span></div><div id="v0"></div></div>
<div class="card" id="c1"><div class="name">FLUE <span class="where">chimney exit</span></div><div id="v1"></div></div>
</section>
<div class="card zone">BURN ZONE · <span class="status" id="zone">—</span></div>
<details>
<summary>Settings</summary>
<form id="form">
<fieldset><legend>STOVE (°F)</legend>
<label>Warning <input type="number" name="stove_warn" step="25"></label>
<label>Critical <input type="number" name="stove_crit" step="25"></label></fieldset>
<fieldset><legend>FLUE (°F)</legend>
<label>Warning <input type="number" name="flue_warn" step="25"></label>
<label>Critical <input type="number" name="flue_crit" step="25"></label>
<label>Rate-of-rise alarm (°F/min) <input type="number" name="flue_rate"></label>
<label>…only above (°F) <input type="number" name="flue_rate_min" step="25"></label></fieldset>
<fieldset><legend>ALERTS</legend>
<label>Reload alert <input type="checkbox" name="reload_on"></label>
<label>Reload below (°F) <input type="number" name="reload" step="25"></label>
<label>Quiet mode (Critical always sounds) <input type="checkbox" name="quiet"></label>
<label>Silence warnings (min) <input type="number" name="silence_min"></label>
<label>Silence critical (min) <input type="number" name="silence_crit"></label>
<label>Hysteresis (°F) <input type="number" name="hyst"></label></fieldset>
<fieldset><legend>IDEAL BURN (°F)</legend>
<label>Show indicator <input type="checkbox" name="ideal_on"></label>
<label>Stove from <input type="number" name="ideal_lo" step="25"></label>
<label>Stove to <input type="number" name="ideal_hi" step="25"></label>
<label>Chimney exit at least <input type="number" name="ideal_exit" step="25"></label></fieldset>
<fieldset><legend>DISPLAY</legend>
<label>Show °C <input type="checkbox" name="units_c"></label>
<label>Brightness (10–255) <input type="number" name="brightness"></label></fieldset>
<button class="save" type="submit">Save</button><span id="msg"></span>
</form>
<form id="loc">
<fieldset><legend>LOCATION (OUTSIDE TEMPERATURE)</legend>
<p class="hint">In Google Maps, right-click your house (long-press on a phone) and copy the coordinates, then paste them here. Leave empty to turn the outside temperature off. Stored only on the monitor.</p>
<input class="loc" type="text" name="coords" autocomplete="off" placeholder="e.g. 41.87811, -87.62980"></fieldset>
<button class="save" type="submit">Save location</button><span id="locmsg"></span>
</form>
</details>
<footer id="foot"></footer>
</main>
<script>
const $=id=>document.getElementById(id);
let unitsC=false, lastOk=0;
const conv=f=>unitsC?(f-32)*5/9:f, U=()=>unitsC?'°C':'°F';
function card(i,ch,lvl,fault){
  const c=$('c'+i); c.className='card'+(fault?' fault':lvl==='CRITICAL'?' critical':lvl==='WARNING'?' warning':'');
  if(!ch.valid){$('v'+i).innerHTML='<div class="nosig">'+(ch.present?'No signal':'No sensor')+'</div><div class="rate">Check the probe at the sensor box</div>';return}
  let r='Measuring';
  if(ch.rate!==null){const v=Math.round(unitsC?ch.rate*5/9:ch.rate);r=(v>3?'▲ +':v<-3?'▼ ':'► ')+v+U()+'/min'}
  $('v'+i).innerHTML='<div class="temp">'+Math.round(conv(ch.temp))+'<small>'+U()+'</small></div><div class="rate">'+r+'</div>';
}
function banner(s){
  const a=s.alarms,b=$('banner');let cls='',t='',x='',btn='Silence';
  const P=['STOVE','FLUE'];
  if(a.silenced&&a.highest!=='NONE'){cls='silenced';t='Alarm silenced';x='Returns in '+Math.floor(a.silence_left/60)+':'+String(a.silence_left%60).padStart(2,'0');btn=''}
  else if(a.rate_high){cls='chimney';t='POSSIBLE CHIMNEY FIRE';x='Close the door and air control. Get everyone out. Call 911.'}
  else if(a.temp.includes('CRITICAL')){const i=a.temp[1]==='CRITICAL'?1:0;cls='critical';t=P[i]+' CRITICAL';x=Math.round(conv(s.ch[i].temp))+U()}
  else if(a.temp.includes('WARNING')){const i=a.temp[1]==='WARNING'?1:0;cls='warning';t=P[i]+' WARNING';x=Math.round(conv(s.ch[i].temp))+U()}
  else if(a.fault[0]||a.fault[1]){cls='fault';t='PROBE FAULT';x=(a.fault[0]&&a.fault[1]?'Both probes':a.fault[0]?'Stove probe':'Flue probe')+' · no reading'}
  else if(a.reload){cls='reload';t='Time to reload';x='Stove is cooling down';btn='OK'}
  b.className='banner'+(cls?' on '+cls:'');$('bTitle').textContent=t;$('bText').textContent=x;
  $('bBtn').style.display=btn?'':'none';$('bBtn').textContent=btn;
}
function zone(s){
  const z=$('zone'),st=s.ch[0],fl=s.ch[1],g=s.settings;let t,c;
  if(!st.valid){t='Unavailable · needs the stove probe';c='na'}
  else if(s.alarms.ideal){t='Ideal burn';c='ideal'}
  else if(st.temp<g.ideal_lo){t='Too cool';c='coolz'}
  else if(st.temp>g.ideal_hi){t='Running hot';c='hot'}
  else if(fl.valid&&fl.temp<g.ideal_exit){t='Chimney too cool · creosote risk';c='chimcool'}
  else{t='Ideal burn';c='ideal'}
  z.textContent=t;z.className='status '+c;
}
const h12=t=>{if(!t)return'';let[h,m]=t.split(':').map(Number);const ap=h<12?'am':'pm';h=h%12||12;return h+':'+String(m).padStart(2,'0')+ap};
function outside(o){
  const el=$('outside');
  if(!o.located){el.textContent='';return}
  let t=o.valid?'Outside '+Math.round(conv(o.temp))+U()+(o.sky?' · '+o.sky:''):'Outside temperature unavailable'+(o.error?' ('+o.error+')':'');
  if(o.sunrise&&o.sunset)t+=' · sunrise '+h12(o.sunrise)+', sunset '+h12(o.sunset);
  el.textContent=t;
}
let locLoaded=false;
let settingsLoaded=false;
function fillSettings(g){
  for(const el of $('form').elements){if(!el.name||!(el.name in g))continue;
    if(el.type==='checkbox')el.checked=!!g[el.name];else el.value=g[el.name]}
  settingsLoaded=true;
}
async function poll(){
  try{
    const s=await (await fetch('/api/status',{cache:'no-store'})).json();
    lastOk=Date.now();unitsC=!!s.settings.units_c;
    for(let i=0;i<2;i++)card(i,s.ch[i],s.alarms.temp[i],s.alarms.fault[i]);
    banner(s);zone(s);
    if(!settingsLoaded)fillSettings(s.settings);
    outside(s.outside);
    if(!locLoaded){if(s.outside.located)$('loc').elements.coords.value=s.outside.lat+', '+s.outside.lon;locLoaded=true}
    $('fresh').textContent='live';$('fresh').className='fresh';
    $('foot').textContent='Firmware '+s.version+' · Wi-Fi '+s.wifi.ssid+' · '+s.wifi.rssi+' dBm · up '+Math.floor(s.uptime/60)+' min';
  }catch(e){
    const ago=Math.round((Date.now()-lastOk)/1000);
    $('fresh').textContent=lastOk?'offline · last update '+ago+'s ago':'cannot reach the monitor';$('fresh').className='fresh stale';
  }
}
$('bBtn').onclick=async()=>{await fetch('/api/silence',{method:'POST'});poll()};
$('form').onsubmit=async e=>{
  e.preventDefault();const body=new URLSearchParams();
  for(const el of e.target.elements){if(!el.name)continue;body.append(el.name,el.type==='checkbox'?(el.checked?1:0):el.value)}
  const r=await fetch('/api/settings',{method:'POST',body});const j=await r.json();
  $('msg').textContent=j.ok?'Saved':j.error;$('msg').className=j.ok?'ok':'err';
  if(j.settings)fillSettings(j.settings);
};
$('loc').onsubmit=async e=>{
  e.preventDefault();const v=e.target.elements.coords.value.trim(),m=v.match(/^(-?[\d.]+)\s*[,\s]\s*(-?[\d.]+)$/);
  if(v&&!m){$('locmsg').textContent='Paste two numbers, e.g. 41.87811, -87.62980';$('locmsg').className='err';return}
  const body=new URLSearchParams({lat:m?m[1]:'',lon:m?m[2]:''});
  const r=await fetch('/api/location',{method:'POST',body});const j=await r.json();
  $('locmsg').textContent=j.ok?'Saved':j.error;$('locmsg').className=j.ok?'ok':'err';poll();
};
poll();setInterval(poll,2000);
</script>
</body>
</html>
)HTML";
