#pragma once

#include <Arduino.h>

constexpr char kWebUiHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#10231c">
<title>Heat Pilot</title>
<style>
:root{color-scheme:dark;--bg:#0b1512;--panel:#13231e;--line:#294239;--text:#eef7f2;--muted:#9eb4aa;--green:#59d499;--orange:#f0b35a;--red:#f07b72}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#183328 0,var(--bg) 38rem);color:var(--text);font:16px/1.45 system-ui,-apple-system,sans-serif}
main{width:min(100% - 2rem,58rem);margin:auto;padding:2rem 0 4rem}header{display:flex;align-items:center;justify-content:space-between;margin-bottom:1.5rem}
h1{font-size:clamp(1.6rem,5vw,2.3rem);margin:0;letter-spacing:-.04em}h2{font-size:.78rem;text-transform:uppercase;letter-spacing:.12em;color:var(--muted);margin:0 0 1rem}
.connection{display:flex;align-items:center;gap:.45rem;color:var(--muted);font-size:.82rem}.dot{width:.65rem;height:.65rem;border-radius:50%;background:var(--orange)}.online .dot{background:var(--green)}.offline .dot{background:var(--red)}
.alert{display:none;padding:.8rem 1rem;border:1px solid #7a403c;background:#341d1b;color:#ffd5d1;border-radius:.8rem;margin-bottom:1rem}.alert.show{display:block}
.mode{display:grid;grid-template-columns:1fr 1fr;gap:.6rem;padding:.35rem;background:#0a1411;border:1px solid var(--line);border-radius:1rem;margin-bottom:1rem}
button{min-height:3.2rem;border:0;border-radius:.75rem;background:transparent;color:var(--muted);font:inherit;font-weight:600;cursor:pointer}button.active{background:var(--panel);color:var(--text);box-shadow:0 2px 12px #0005}button[data-mode=automatic].active{color:var(--green)}button:disabled{opacity:.55;cursor:wait}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}.card{min-width:0;background:var(--panel);background:color-mix(in srgb,var(--panel) 92%,transparent);border:1px solid var(--line);border-radius:1rem;padding:1.15rem}
.wide{grid-column:1/-1}.value{font-size:clamp(1.65rem,6vw,2.4rem);font-weight:700;letter-spacing:-.04em}.unit{font-size:.9rem;font-weight:500;color:var(--muted);letter-spacing:0}.sub{color:var(--muted);font-size:.88rem;margin-top:.25rem}
.row{display:flex;align-items:center;justify-content:space-between;gap:1rem;padding:.55rem 0;border-top:1px solid var(--line)}.row:first-of-type{border-top:0}.row span:first-child{color:var(--muted)}
.phases{display:flex;gap:.45rem}.phase{width:1.15rem;height:1.15rem;border-radius:50%;border:2px solid #466057}.phase.on{border-color:var(--orange);background:var(--orange);box-shadow:0 0 12px #f0b35a66}
.badge{display:inline-flex;align-items:center;border:1px solid var(--line);border-radius:99px;padding:.28rem .6rem;font-size:.82rem}.badge.good{color:var(--green);border-color:#36775a}.badge.warn{color:var(--orange);border-color:#7d6136}
#temperatures:empty:after{content:'Keine Sensorwerte';color:var(--muted)}footer{margin-top:1.3rem;text-align:center;color:var(--muted);font-size:.76rem}
@media(max-width:560px){main{width:min(100% - 1rem,58rem);padding-top:1rem}.grid{grid-template-columns:1fr}.wide{grid-column:auto}.card{padding:1rem}header{margin:.35rem .25rem 1.2rem}}
</style>
</head>
<body><main>
<header><div><h1>Heat Pilot</h1><div class="sub" id="state">Status wird geladen …</div></div><div class="connection" id="connection"><i class="dot"></i><span>Verbinden</span></div></header>
<div class="alert" id="alert"></div>
<section class="mode" aria-label="Betriebsart"><button data-mode="disabled">Deaktiviert</button><button data-mode="automatic">Automatik</button></section>
<div class="grid">
<section class="card"><h2>PV-Überschuss</h2><div class="value" id="surplus">–</div><div class="sub" id="surplusSource">Keine Messung</div></section>
<section class="card"><h2>Puffertemperatur</h2><div class="value" id="temperature">–</div><div class="sub" id="temperaturePolicy">Zieltemperatur wird geladen</div></section>
<section class="card"><h2>Heizstab</h2><div class="row"><span>Phasen</span><div class="phases"><i class="phase"></i><i class="phase"></i><i class="phase"></i></div></div><div class="row"><span>Leistung</span><strong id="heaterPower">0 W</strong></div><div class="row"><span>Energie seit Start</span><strong id="energy">0 Wh</strong></div><div class="row"><span>Umwälzpumpe</span><strong id="pump">Aus</strong></div></section>
<section class="card"><h2>Batterie</h2><div class="value" id="soc">–</div><div class="row"><span>Leistung</span><strong id="batteryPower">–</strong></div><div class="row"><span>Daten</span><span class="badge warn" id="batteryFresh">Nicht verfügbar</span></div></section>
<section class="card wide"><h2>Temperaturfühler</h2><div id="temperatures"></div></section>
</div>
<footer id="updated">Noch nicht aktualisiert</footer>
</main>
<script>
const q=s=>document.querySelector(s),qa=s=>document.querySelectorAll(s);let busy=false,currentMode='';
const states={disabled:'Deaktiviert',manual_control:'Manuelle Steuerung',monitoring:'Bereit',heating:'Heizbetrieb',pump_overrun:'Pumpennachlauf',temperature_hold:'Zieltemperatur erreicht',waiting_for_data:'Warte auf Energiedaten',waiting_for_temperature:'Warte auf Temperatur',temperature_fault:'Temperaturfehler',fault:'Fehler',starting:'Startet'};
function number(v,d=0){return Number(v).toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d})}
function power(v){if(v===null||v===undefined)return '–';return Math.abs(v)>=1000?number(v/1000,2)+' kW':number(v)+' W'}
function energy(v){return v>=1000?number(v/1000,3)+' kWh':number(v,2)+' Wh'}
function alertMessage(s){if(!s.output_driver_healthy)return 'Ausgangstreiber nicht verfügbar.';if(s.temperature_fault)return 'Temperaturmessung fehlerhaft – Heizen ist gesperrt.';if(!s.temperature_valid)return 'Keine gültige Temperaturmessung.';if(!s.measurements_valid&&s.mode==='automatic')return 'Automatik wartet auf aktuelle Energie- oder Batteriedaten.';return ''}
function render(s){currentMode=s.mode;q('#state').textContent=states[s.state]||s.state;qa('.mode button').forEach(b=>b.classList.toggle('active',b.dataset.mode===s.mode));
q('#surplus').textContent=power(s.surplus_w);q('#surplusSource').textContent=s.surplus_source==='smart_meter'?'Fronius Smart Meter':s.surplus_source==='simulation'?'Simulation':'Keine aktuelle Messung';
q('#temperature').textContent=s.temperature_valid?number(s.temperature_c,1)+' °C':'–';q('#temperaturePolicy').textContent='Ziel '+number(s.target_temperature_c,0)+' °C · Freigabe ab '+number(s.restart_temperature_c,0)+' °C';qa('.phase').forEach((p,i)=>p.classList.toggle('on',i<s.heater_phases));q('#heaterPower').textContent=power(s.heater_phases*s.heater_phase_power_w);q('#energy').textContent=energy(s.estimated_heater_energy_wh);q('#pump').textContent=s.pump?'Ein':'Aus';
const battery=s.battery||{};q('#soc').textContent=battery.valid?number(battery.state_of_charge_percent,1)+' %':'–';q('#batteryPower').textContent=battery.valid?power(battery.power_w):'–';const fresh=q('#batteryFresh');fresh.textContent=battery.fresh?'Aktuell':'Nicht verfügbar';fresh.className='badge '+(battery.fresh?'good':'warn');
const sensors=q('#temperatures');sensors.replaceChildren();(s.temperature_sensors||[]).forEach((t,i)=>{const r=document.createElement('div');r.className='row';const n=document.createElement('span');n.textContent='Sensor '+(i+1)+' · '+t.address.slice(-4);const v=document.createElement('strong');v.textContent=t.valid?number(t.temperature_c,1)+' °C':'Ungültig';r.append(n,v);sensors.append(r)});
const msg=alertMessage(s),a=q('#alert');a.textContent=msg;a.classList.toggle('show',!!msg);q('#updated').textContent='Aktualisiert '+new Date().toLocaleTimeString('de-DE');const c=q('#connection');c.className='connection online';c.querySelector('span').textContent='Online'}
async function load(){if(busy)return;try{const r=await fetch('/api/v1/status',{cache:'no-store'});if(!r.ok)throw Error();render(await r.json())}catch(e){const c=q('#connection');c.className='connection offline';c.querySelector('span').textContent='Offline';q('#alert').textContent='Verbindung zum Heat Pilot unterbrochen.';q('#alert').classList.add('show')}}
async function setMode(mode){if(busy||mode===currentMode)return;if(mode==='automatic'&&!confirm('Automatik aktivieren? Der Heizstab kann bei ausreichendem Überschuss einschalten.'))return;busy=true;qa('button').forEach(b=>b.disabled=true);try{const r=await fetch('/api/v1/mode',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode})});if(!r.ok)throw Error();render(await r.json())}catch(e){q('#alert').textContent='Betriebsart konnte nicht geändert werden.';q('#alert').classList.add('show')}finally{busy=false;qa('button').forEach(b=>b.disabled=false)}}
qa('.mode button').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode)));load();setInterval(load,2000);
</script></body></html>)HTML";
