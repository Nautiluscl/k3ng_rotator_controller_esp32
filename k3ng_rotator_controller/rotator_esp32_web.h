/* rotator_esp32_web.h

   Servidor web ligero para el perfil ESP32 (FEATURE_WEB_SERVER), pensado para usarse desde
   un smartphone en la red local: http://rotor.local/ (o la IP que muestra \WI)

     GET  /             página de control (HTML + CSS + JS embebidos, sin dependencias externas)
     GET  /api/status   estado en JSON: posición, movimiento, seguimiento, WiFi, uptime, hora
     POST /api/move     dir=cw|ccw|up|down|release  movimiento manual mantenido (ver abajo)
     POST /api/stop     para todo: rotación de ambos ejes y cualquier seguimiento
     POST /api/track    target=sun|moon, on=1|0  seguimiento del Sol o de la Luna
     POST /api/locator  grid=XXnnxx  ubicación de la estación (locator Maidenhead de 6 caracteres)

   Movimiento manual mantenido ("hombre muerto"): mientras el botón está pulsado, la página
   repite la orden cada WEB_JOG_KEEPALIVE_MS. Si el firmware deja de recibirla durante
   WEB_JOG_TIMEOUT_MS (móvil bloqueado, WiFi caído, pestaña cerrada), detiene el eje por su
   cuenta. Así un corte de red nunca deja el rotor girando.

   Como rotator_esp32_wifi.h, este archivo se incluye al final de k3ng_rotator_controller.ino.
   La página se puede revisar sin ESP32 con tools/web_preview.py.
*/

#if defined(FEATURE_WEB_SERVER)

// --------------------------------------------------------------
static const char web_page_html[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="theme-color" content="#0f1720">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>Rotor</title>
<style>
:root{--bg:#0f1720;--card:#18222e;--line:#263342;--tx:#e6edf3;--mu:#8b9bb0;--ac:#3fa7ff;--ok:#3fbf7f;--wa:#e5a83b;--st:#e5484d}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.35 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
padding:max(12px,env(safe-area-inset-top)) 14px max(16px,env(safe-area-inset-bottom));-webkit-user-select:none;user-select:none}
main{max-width:460px;margin:0 auto;display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px 14px}
.pos{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.pos .lb{color:var(--mu);font-size:12px;letter-spacing:.08em;text-transform:uppercase}
.pos .v{font-size:40px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.1}
.pos .s{color:var(--mu);font-size:13px;min-height:1.3em}
.mv{color:var(--wa)!important}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
button{font:inherit;color:var(--tx);background:#223041;border:1px solid #2f4155;border-radius:14px;min-height:64px;
touch-action:none;cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px}
button small{color:var(--mu);font-size:11px;letter-spacing:.06em}
button .ar{font-size:26px;line-height:1}
button.on{background:var(--ac);border-color:var(--ac);color:#fff}
button.on small{color:#e8f3ff}
button:disabled{opacity:.4}
#stop{background:var(--st);border-color:var(--st);color:#fff;font-weight:700;font-size:17px;letter-spacing:.05em}
.trk{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.trk button{min-height:58px}
.trk .t{font-weight:600}
.info{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px}
.info dt{color:var(--mu)}.info dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.cfg{display:block;margin-top:12px;padding:10px;text-align:center;color:var(--ac);text-decoration:none;border:1px solid #2f4155;border-radius:10px}
#msg{min-height:1.3em;text-align:center;color:var(--mu);font-size:13px}
#msg.err{color:var(--st)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--st);margin-right:6px;vertical-align:1px}
.dot.ok{background:var(--ok)}
.sim{background:var(--wa);color:#111;font-weight:700;text-align:center;border-radius:10px;padding:8px;letter-spacing:.03em}
.cred{color:var(--mu);font-size:12px;text-align:center;line-height:1.5;margin:4px 0 0}.cred a{color:var(--mu)}
</style></head><body><main>
<div id="sim" class="sim" hidden>MODO SIMULACIÓN · los motores no se mueven</div>
<section class="card pos">
 <div><div class="lb">Azimut</div><div class="v" id="az">---</div><div class="s" id="azs"></div></div>
 <div><div class="lb">Elevación</div><div class="v" id="el">---</div><div class="s" id="els"></div></div>
</section>
<section class="pad" aria-label="Movimiento manual">
 <span></span><button data-d="up"><span class="ar">▲</span><small>ARRIBA</small></button><span></span>
 <button data-d="ccw"><span class="ar">◀</span><small>CCW</small></button>
 <button id="stop">STOP</button>
 <button data-d="cw"><span class="ar">▶</span><small>CW</small></button>
 <span></span><button data-d="down"><span class="ar">▼</span><small>ABAJO</small></button><span></span>
</section>
<section class="trk">
 <button id="sun"><span class="t">☀ Seguir Sol</span><small id="sunp">--</small></button>
 <button id="moon"><span class="t">☾ Seguir Luna</span><small id="moonp">--</small></button>
</section>
<div id="msg"></div>
<section class="card">
 <dl class="info">
  <dt>Red</dt><dd><span class="dot" id="wd"></span><span id="ssid">--</span></dd>
  <dt>Señal</dt><dd id="rssi">--</dd>
  <dt>IP</dt><dd id="ip">--</dd>
  <dt>Uptime</dt><dd id="up">--</dd>
  <dt>Último reinicio</dt><dd id="rst">--</dd>
  <dt>Hora UTC</dt><dd id="utc">--</dd>
  <dt>Sensor EL</dt><dd id="bno">--</dd>
  <dt>Locator</dt><dd id="grid">--</dd>
 </dl>
 <a class="cfg" href="/config">⚙ Configuración</a>
</section>
<footer class="cred">Basado en el <a href="https://github.com/k3ng/k3ng_rotator_controller">K3NG Rotator Controller</a> de Anthony Good, K3NG,<br>a través del fork de <a href="https://github.com/X9X0/k3ng_rotator_controller">X9X0</a>. Licencia GPL v3.</footer>
</main>
<script>
const $=id=>document.getElementById(id);let S={},held=null,heldBtn=null,hbT=null,fails=0,seq=0;
const sid=1+Math.floor(Math.random()*2e9);
function msg(t,e){const m=$('msg');m.textContent=t||'';m.className=e?'err':''}
async function req(u,o){const c=new AbortController(),t=setTimeout(()=>c.abort(),3000);
 try{return await fetch(u,Object.assign({signal:c.signal,cache:'no-store'},o||{}))}finally{clearTimeout(t)}}
async function post(u,b){const r=await req(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b||{})});
 const j=await r.json().catch(()=>({ok:false,msg:'Respuesta inválida'}));if(!j.ok)throw new Error(j.msg||'Error');return j}
function f1(v){return(Math.round(v*10)/10).toFixed(1)+'°'}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function bars(r){return r>=-55?'Excelente':r>=-67?'Buena':r>=-75?'Regular':'Débil'}
function mvTxt(s){return{cw:'girando CW',ccw:'girando CCW',up:'subiendo',down:'bajando'}[s]||''}
function render(j){S=j;
 $('az').textContent=f1(j.az);$('el').textContent=f1(j.el);$('sim').hidden=!j.sim;
 $('azs').textContent=mvTxt(j.az_mv);$('azs').className='s'+(j.az_mv?' mv':'');
 $('els').textContent=j.el_ok?mvTxt(j.el_mv):'sin sensor'+(j.el_mv?' · '+mvTxt(j.el_mv):'');$('els').className='s'+(j.el_mv||!j.el_ok?' mv':'');
 $('sun').classList.toggle('on',!!j.sun.trk);$('moon').classList.toggle('on',!!j.moon.trk);
 $('sunp').textContent=j.time_ok?'AZ '+f1(j.sun.az)+'  EL '+f1(j.sun.el):'sin hora';
 $('moonp').textContent=j.time_ok?'AZ '+f1(j.moon.az)+'  EL '+f1(j.moon.el):'sin hora';
 $('wd').className='dot'+(j.wifi.ok?' ok':'');$('ssid').textContent=j.wifi.ssid||'--';
 $('rssi').textContent=j.wifi.ok?j.wifi.rssi+' dBm ('+bars(j.wifi.rssi)+')':'--';$('ip').textContent=j.wifi.ip||'--';
 $('up').textContent=dur(j.uptime);$('rst').textContent=j.rst;$('utc').textContent=j.time_ok?j.utc:'sin sincronizar';
 $('bno').textContent=j.bno;$('grid').textContent=j.grid}
async function poll(){try{const r=await req('/api/status');render(await r.json());
 if(fails){msg('')}fails=0}catch(e){if(++fails>1)msg('Sin conexión con el rotor',1)}
 setTimeout(poll,held?500:1000)}
// sid+seq: cada pulsación lleva un número creciente; el firmware descarta un keepalive
// atrasado que llegue después de soltar ese mismo botón
function release(){if(!held)return;const d=held;held=null;heldBtn=null;clearInterval(hbT);hbT=null;
 document.querySelectorAll('[data-d]').forEach(b=>b.classList.remove('on'));
 post('/api/move',{dir:'release',axis:(d=='cw'||d=='ccw')?'az':'el',sid:sid,seq:seq}).catch(e=>msg(e.message,1))}
document.querySelectorAll('[data-d]').forEach(b=>{
 b.addEventListener('pointerdown',e=>{e.preventDefault();if(held)release();held=b.dataset.d;heldBtn=b;seq++;b.classList.add('on');
  b.setPointerCapture(e.pointerId);if(navigator.vibrate)navigator.vibrate(15);
  const d=held,n=seq,go=f=>{if(held===d&&seq===n)post('/api/move',{dir:d,sid:sid,seq:n,first:f?1:0}).catch(err=>{msg(err.message,1);release()})};
  go(1);hbT=setInterval(()=>go(0),%KEEPALIVE%)});
 // con varios dedos, soltar un botón que ya no es el activo no detiene el otro
 ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>b.addEventListener(ev,()=>{if(heldBtn===b)release()}));
 b.addEventListener('contextmenu',e=>e.preventDefault())});
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});
$('stop').addEventListener('click',async()=>{release();if(navigator.vibrate)navigator.vibrate([30,40,30]);
 try{await post('/api/stop',{sid:sid,seq:seq});msg('Movimiento detenido')}catch(e){msg(e.message,1)}});
['sun','moon'].forEach(t=>$(t).addEventListener('click',async()=>{const on=S[t]&&S[t].trk?0:1;
 try{const j=await post('/api/track',{target:t,on:on});msg(j.msg)}catch(e){msg(e.message,1)}}));
poll();
</script></body></html>)rawliteral";

// --------------------------------------------------------------
// Página de configuración (/config): ajustes que el firmware permite cambiar sin recompilar
static const char web_config_html[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0f1720">
<title>Rotor · Configuración</title>
<style>
:root{--bg:#0f1720;--card:#18222e;--line:#263342;--tx:#e6edf3;--mu:#8b9bb0;--ac:#3fa7ff;--ok:#3fbf7f;--wa:#e5a83b;--st:#e5484d}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
padding:max(12px,env(safe-area-inset-top)) 14px max(24px,env(safe-area-inset-bottom))}
main{max-width:460px;margin:0 auto;display:grid;gap:12px}
header{display:flex;align-items:center;gap:10px}
header a{color:var(--ac);text-decoration:none;font-size:15px;padding:6px 0}
header h1{font-size:18px;margin:0;flex:1;text-align:right;font-weight:650}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px 14px;display:grid;gap:10px}
h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--mu);margin:0;font-weight:600}
label{display:grid;grid-template-columns:1fr 7.5em;align-items:center;gap:10px;font-size:14px}
label small{display:block;color:var(--mu);font-size:12px}
input{font:inherit;color:var(--tx);background:var(--bg);border:1px solid #2f4155;border-radius:10px;padding:8px 10px;width:100%;min-width:0}
input[type=number]{text-align:right;font-variant-numeric:tabular-nums}
input.wide{grid-column:1/-1}
button{font:inherit;color:var(--tx);background:#223041;border:1px solid #2f4155;border-radius:12px;min-height:44px;padding:0 14px;cursor:pointer}
button.pri{background:var(--ac);border-color:var(--ac);color:#fff;font-weight:600}
button.dan{background:transparent;border-color:var(--st);color:var(--st)}
.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1}
.sw{display:flex;align-items:center;justify-content:space-between;gap:12px}
.sw input{display:none}
.sw i{width:52px;height:30px;border-radius:15px;background:#2f4155;position:relative;flex:none;transition:.2s}
.sw i:after{content:"";position:absolute;width:24px;height:24px;border-radius:50%;background:#fff;top:3px;left:3px;transition:.2s}
.sw input:checked+i{background:var(--wa)}.sw input:checked+i:after{left:25px}
.note{color:var(--mu);font-size:12.5px;margin:0}
.warn{color:var(--wa)}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0;font-size:14px}
dt{color:var(--mu)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
#msg{position:sticky;bottom:max(10px,env(safe-area-inset-bottom));min-height:0;text-align:center;font-size:14px;border-radius:10px;padding:0}
#msg.show{padding:10px;background:#223041;border:1px solid #2f4155}
#msg.err{border-color:var(--st);color:var(--st)}
.lvl{display:inline-block;min-width:1.6em;padding:0 5px;border-radius:6px;background:#2f4155;text-align:center}
.lvl.ok{background:var(--ok);color:#111}
.cred{color:var(--mu);font-size:12px;text-align:center;line-height:1.5;margin:4px 0 0}.cred a{color:var(--mu)}
</style></head><body><main>
<header><a href="/">‹ Control</a><h1>Configuración</h1></header>

<section class="card">
 <label class="sw"><span>Modo simulación<small>Rotor virtual: los motores no se mueven. Para probar PstRotator u otros programas.</small></span>
  <input type="checkbox" id="sim"><i></i></label>
</section>

<form class="card" id="fcfg">
 <h2>Rotor</h2>
 <label>Punto de inicio de azimut (°)<input type="number" id="az_start" min="0" max="359" step="1" required></label>
 <label>Rango de giro de azimut (°)<input type="number" id="az_cap" min="90" max="720" step="1" required></label>
 <label>Offset de elevación (°)<small>Se suma a la lectura del BNO055</small><input type="number" id="el_offset" min="-90" max="90" step="0.1" required></label>
 <h2>Estación</h2>
 <label>Zona horaria (h)<small>Solo para la hora local del puerto de control</small><input type="number" id="tz" min="-12" max="14" step="0.5" required></label>
 <h2>Seguimiento del Sol</h2>
 <label>Intervalo de cálculo (ms)<input type="number" id="sun_check" min="100" max="60000" step="100" required></label>
 <label>Intervalo mínimo entre giros (ms)<input type="number" id="sun_min" min="0" max="600000" step="100" required></label>
 <label>Umbral para girar (°)<input type="number" id="sun_thr" min="0.1" max="20" step="0.1" required></label>
 <h2>Seguimiento de la Luna</h2>
 <label>Intervalo de cálculo (ms)<input type="number" id="moon_check" min="100" max="60000" step="100" required></label>
 <label>Intervalo mínimo entre giros (ms)<input type="number" id="moon_min" min="0" max="600000" step="100" required></label>
 <label>Umbral para girar (°)<input type="number" id="moon_thr" min="0.1" max="20" step="0.1" required></label>
 <button class="pri">Guardar ajustes</button>
</form>

<form class="card" id="floc">
 <h2>Ubicación</h2>
 <dl><dt>Locator</dt><dd id="grid">--</dd><dt>Latitud / longitud</dt><dd id="latlon">--</dd></dl>
 <div class="row"><input id="loci" maxlength="6" placeholder="Nuevo locator (FF46pn)" autocomplete="off" autocapitalize="characters"><button>Guardar</button></div>
</form>

<section class="card" id="cbno">
 <h2>Sensor de elevación (BNO055)</h2>
 <dl><dt>Estado</dt><dd id="bno">--</dd>
  <dt>Calibración</dt><dd>Sist <span class="lvl" id="cs">-</span> Giro <span class="lvl" id="cg">-</span> Acel <span class="lvl" id="ca">-</span></dd>
  <dt>Calibración guardada</dt><dd id="cofs">--</dd>
  <dt>Elevación sin offset</dt><dd id="craw">--</dd></dl>
 <p class="note">Quieto unos segundos calibra el giroscopio; seis posiciones distintas, quieto en cada una, calibran el acelerómetro. Con Giro y Acel en 3 se puede guardar.</p>
 <div class="row"><button id="bsave">Guardar calibración</button><button id="bclear" class="dan">Borrar</button></div>
</section>

<form class="card" id="fwifi">
 <h2>Red WiFi</h2>
 <dl><dt>Red actual</dt><dd id="ssid">--</dd><dt>Señal</dt><dd id="rssi">--</dd><dt>IP</dt><dd id="ip">--</dd></dl>
 <input class="wide" id="nssid" maxlength="32" placeholder="Nueva red (SSID)" autocomplete="off" autocapitalize="none">
 <input class="wide" id="npass" type="password" maxlength="63" placeholder="Contraseña" autocomplete="new-password">
 <p class="note warn">Al cambiar de red el rotor se desconecta de esta. Si la red nueva no funciona, se recupera por USB con \WD (vuelve a la red de compilación).</p>
 <button class="pri">Guardar y reconectar</button>
</form>

<section class="card">
 <h2>Sistema</h2>
 <dl><dt>Firmware</dt><dd id="ver">--</dd><dt>Uptime</dt><dd id="up">--</dd><dt>Último reinicio</dt><dd id="rst">--</dd><dt>Memoria libre</dt><dd id="heap">--</dd></dl>
 <button id="brst" class="dan">Reiniciar el controlador</button>
</section>
<div id="msg"></div>
<footer class="cred">Basado en el <a href="https://github.com/k3ng/k3ng_rotator_controller">K3NG Rotator Controller</a> de Anthony Good, K3NG,<br>a través del fork de <a href="https://github.com/X9X0/k3ng_rotator_controller">X9X0</a>. Licencia GPL v3.</footer>
</main>
<script>
const $=id=>document.getElementById(id);let C={},mt=null;
const F=['az_start','az_cap','el_offset','tz','sun_check','sun_min','sun_thr','moon_check','moon_min','moon_thr'];
function msg(t,e){const m=$('msg');m.textContent=t||'';m.className=t?('show'+(e?' err':'')):'';clearTimeout(mt);if(t)mt=setTimeout(()=>msg(''),5000)}
async function req(u,o){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);
 try{return await fetch(u,Object.assign({signal:c.signal,cache:'no-store'},o||{}))}finally{clearTimeout(t)}}
async function post(u,b){const r=await req(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b||{})});
 const j=await r.json().catch(()=>({ok:false,msg:'Respuesta inválida'}));if(!j.ok)throw new Error(j.msg||'Error');return j}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function lvl(id,v){$(id).textContent=v;$(id).className='lvl'+(v==3?' ok':'')}
function render(j,first){C=j;
 $('sim').checked=!!j.sim;
 if(first)F.forEach(k=>{$(k).value=j.cfg[k]});
 $('grid').textContent=j.grid;$('latlon').textContent=j.lat.toFixed(4)+', '+j.lon.toFixed(4);
 $('cbno').hidden=!j.bno.present;
 $('bno').textContent=j.bno.text;lvl('cs',j.bno.sys);lvl('cg',j.bno.gyro);lvl('ca',j.bno.accel);
 $('cofs').textContent=j.bno.saved?'sí':'no';$('craw').textContent=j.bno.raw.toFixed(2)+'°';
 $('ssid').textContent=j.wifi.ssid;$('rssi').textContent=j.wifi.ok?j.wifi.rssi+' dBm':'--';$('ip').textContent=j.wifi.ip||'--';
 $('ver').textContent=j.ver;$('up').textContent=dur(j.uptime);$('rst').textContent=j.rst;$('heap').textContent=Math.round(j.heap/1024)+' KB'}
async function load(first){try{const r=await req('/api/config');render(await r.json(),first)}catch(e){msg('Sin conexión con el rotor',1)}}
$('sim').addEventListener('change',async e=>{const on=e.target.checked?1:0;
 try{const j=await post('/api/sim',{on:on});msg(j.msg)}catch(err){msg(err.message,1);e.target.checked=!on}});
$('fcfg').addEventListener('submit',async e=>{e.preventDefault();const b={};F.forEach(k=>b[k]=$(k).value);
 try{const j=await post('/api/config',b);msg(j.msg);setTimeout(()=>load(true),600)}catch(err){msg(err.message,1)}});
$('floc').addEventListener('submit',async e=>{e.preventDefault();
 try{const j=await post('/api/locator',{grid:$('loci').value.trim()});msg(j.msg);$('loci').value='';setTimeout(load,600)}catch(err){msg(err.message,1)}});
$('bsave').addEventListener('click',async()=>{try{const j=await post('/api/bno055',{action:'save'});msg(j.msg)}catch(e){msg(e.message,1)}});
$('bclear').addEventListener('click',async()=>{if(!window.confirm('¿Borrar la calibración guardada del BNO055?'))return;
 try{const j=await post('/api/bno055',{action:'clear'});msg(j.msg)}catch(e){msg(e.message,1)}});
$('fwifi').addEventListener('submit',async e=>{e.preventDefault();const s=$('nssid').value;
 if(!s){msg('Falta el nombre de la red',1);return}
 if(!window.confirm('El rotor se conectará a "'+s+'" y dejará esta red. ¿Continuar?'))return;
 try{const j=await post('/api/wifi',{ssid:s,pass:$('npass').value});msg(j.msg);$('npass').value=''}catch(err){msg(err.message,1)}});
$('brst').addEventListener('click',async()=>{if(!window.confirm('¿Reiniciar el controlador? Se detiene cualquier movimiento.'))return;
 try{const j=await post('/api/restart');msg(j.msg);setTimeout(()=>location.reload(),12000)}catch(e){msg(e.message,1)}});
load(true);setInterval(()=>load(false),2000);
</script></body></html>)rawliteral";

/*
   Concurrencia
   ------------
   WebServer::handleClient() puede bloquear hasta 5 s esperando a un cliente lento (los
   tiempos HTTP_MAX_* del core son fijos). Para que eso nunca frene el control de los motores,
   el servidor web corre en su propia tarea de FreeRTOS en el núcleo 0, y el loop() del
   firmware sigue en el núcleo 1:

     - tarea web -> loop: las órdenes (mover, parar, seguir, locator) van por una cola y es
       el loop quien las aplica con submit_request()/change_tracking(), como cualquier otro
       puerto de control. La tarea web nunca toca el estado del rotor.
     - loop -> tarea web: el loop publica cada WEB_SNAPSHOT_INTERVAL_MS una copia del estado
       (web_snapshot), protegida por un spinlock; /api/status solo lee esa copia.

   Si la tarea web se queda bloqueada, los keepalive dejan de llegar a la cola y el loop
   detiene el eje por el temporizador de hombre muerto.
*/

#define WEB_CMD_MOVE 1
#define WEB_CMD_RELEASE 2
#define WEB_CMD_TRACK 4
#define WEB_CMD_LOCATOR 5
#define WEB_CMD_CONFIG 6
#define WEB_CMD_SIM 7
#define WEB_CMD_BNO055 8
#define WEB_CMD_WIFI 9
#define WEB_CMD_RESTART 10

#define WEB_CONFIG_FIELDS 10      // mismo orden que web_config_names[]

#define WEB_TARGET_SUN 1
#define WEB_TARGET_MOON 2

struct web_command_t {
  byte type;
  byte axis;          // AZ, EL o 0 = ambos (RELEASE)
  byte request;       // REQUEST_CW/CCW/UP/DOWN
  byte target;        // WEB_TARGET_SUN / WEB_TARGET_MOON
  byte on;
  char grid[7];
  uint32_t sid;       // sesión de la página (aleatoria por carga); 0 = cliente sin sesión (curl, scripts)
  uint32_t seq;       // número de pulsación dentro de la sesión
  byte first;         // 1 = primera orden de la pulsación; 0 = keepalive
  float values[WEB_CONFIG_FIELDS];   // WEB_CMD_CONFIG
  char text1[33];     // WEB_CMD_WIFI: SSID; WEB_CMD_BNO055: acción
  char text2[65];     // WEB_CMD_WIFI: contraseña
};

// Ajustes de /api/config: nombre, mínimo y máximo (se validan en la tarea web)
struct web_config_field_t {
  const char * name;
  float min_value;
  float max_value;
};
const web_config_field_t web_config_fields[WEB_CONFIG_FIELDS] = {
  {"az_start", 0, 359},           // configuration.azimuth_starting_point
  {"az_cap", 90, 720},            // configuration.azimuth_rotation_capability
  {"el_offset", -90, 90},         // configuration.elevation_offset
  {"tz", -12, 14},                // configuration.clock_timezone_offset
  {"sun_check", 100, 60000},      // configuration.tracking_sun_check_frequency_ms
  {"sun_min", 0, 600000},         // configuration.tracking_sun_minimum_rotation_interval_ms
  {"sun_thr", 0.1, 20},           // configuration.tracking_sun_degrees_difference_threshold
  {"moon_check", 100, 60000},     // configuration.tracking_moon_check_frequency_ms
  {"moon_min", 0, 600000},        // configuration.tracking_moon_minimum_rotation_interval_ms
  {"moon_thr", 0.1, 20},          // configuration.tracking_moon_degrees_difference_threshold
};

struct web_snapshot_t {
  float az;
  float el;
  byte el_ok;         // sensor de elevación operativo (o simulación)
  byte el_blocked;    // elevación bloqueada por el sensor (OPTION_BNO055_FAULT_STOPS_ELEVATION)
  byte sim;
  byte az_motion;
  byte el_motion;
  float sun_az, sun_el, moon_az, moon_el;
  byte sun_trk, moon_trk, sun_visible, moon_visible;
  byte time_ok;
  byte rotation_allowed;
  byte wifi_ok;
  int rssi;
  unsigned long uptime;
  char utc[24];
  char grid[10];
  char ip[16];
  char ssid[33];
  const char * bno_text;
  const char * reset_reason;
  // datos de /api/config
  float cfg[WEB_CONFIG_FIELDS];
  double lat, lon;
  byte bno_present, bno_sys, bno_gyro, bno_accel, bno_saved;
  float bno_raw;
  uint32_t heap;
};

// acciones diferidas: la respuesta HTTP tiene que salir antes de reconectar o reiniciar
unsigned long web_pending_wifi_reconnect_at = 0;
unsigned long web_pending_restart_at = 0;

WebServer web_server(WEB_SERVER_PORT);
QueueHandle_t web_command_queue = NULL;
web_snapshot_t web_snapshot;
portMUX_TYPE web_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
unsigned long web_snapshot_last_update = 0;

// estado del movimiento mantenido; solo lo usa el loop
byte web_jog_az_request = REQUEST_STOP;
byte web_jog_el_request = REQUEST_STOP;
unsigned long web_jog_az_last_keepalive = 0;
unsigned long web_jog_el_last_keepalive = 0;

// última pulsación soltada por eje (0 = AZ, 1 = EL): un keepalive atrasado de esa misma
// pulsación, que llegue después del release, se descarta en lugar de volver a mover el eje
uint32_t web_jog_released_sid[2] = {0, 0};
uint32_t web_jog_released_seq[2] = {0, 0};

// STOP no pasa por la cola (que podría estar llena): la tarea web levanta esta marca y el
// loop la atiende antes que cualquier otra orden
volatile byte web_stop_pending = 0;
volatile uint32_t web_stop_sid = 0;
volatile uint32_t web_stop_seq = 0;

// ==============================================================
//  Lado del loop (núcleo 1)
// ==============================================================

byte web_rotation_allowed(){

  // Mismo criterio que los comandos Yaesu: ignorar órdenes de giro justo después del arranque
  #if defined(OPTION_ALLOW_ROTATIONAL_AND_CONFIGURATION_CMDS_AT_BOOT_UP)
    return 1;
  #else
    return (millis() > ROTATIONAL_AND_CONFIGURATION_CMD_IGNORE_TIME_MS);
  #endif

}

// --------------------------------------------------------------
void web_update_snapshot(){

  web_snapshot_t s;
  memset(&s, 0, sizeof(s));

  s.az = azimuth;
  s.el = elevation;
  s.el_ok = 1;
  s.az_motion = current_az_state();
  s.el_motion = NOT_DOING_ANYTHING;
  s.bno_text = "-";
  s.rotation_allowed = web_rotation_allowed();

  #if defined(FEATURE_ELEVATION_CONTROL)
    s.el_motion = current_el_state();
  #endif

  #if defined(FEATURE_EL_POSITION_BNO055)
    s.el_ok = (bno055_state == BNO055_STATE_OK) || SIMULATION_IS_ACTIVE();
    s.el_blocked = BNO055_BLOCKS_ELEVATION();
    switch (bno055_state) {
      case BNO055_STATE_OK: s.bno_text = "BNO055 OK"; break;
      case BNO055_STATE_FAULT: s.bno_text = "BNO055 FALLO"; break;
      default: s.bno_text = "BNO055 no detectado"; break;
    }
  #endif

  #if defined(FEATURE_SUN_TRACKING)
    s.sun_az = sun_azimuth;
    s.sun_el = sun_elevation;
    s.sun_trk = sun_tracking_active;
    s.sun_visible = sun_visible;
  #endif
  #if defined(FEATURE_MOON_TRACKING)
    s.moon_az = moon_azimuth;
    s.moon_el = moon_elevation;
    s.moon_trk = moon_tracking_active;
    s.moon_visible = moon_visible;
  #endif

  #if defined(FEATURE_CLOCK)
    s.time_ok = ntp_synced;
    if (s.time_ok) {
      snprintf(s.utc, sizeof(s.utc), "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    }
  #endif

  #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
    strncpy(s.grid, coordinates_to_maidenhead(latitude, longitude), sizeof(s.grid) - 1);
  #endif

  s.wifi_ok = wifi_connected;
  if (wifi_connected) {
    s.rssi = WiFi.RSSI();
    strncpy(s.ip, WiFi.localIP().toString().c_str(), sizeof(s.ip) - 1);
  }

  // el SSID lo pone el usuario: se descartan comillas, barras y controles para no romper el JSON
  byte j = 0;
  for (byte i = 0; (wifi_ssid[i] != 0) && (j < sizeof(s.ssid) - 1); i++) {
    if ((wifi_ssid[i] != '"') && (wifi_ssid[i] != '\\') && ((byte)wifi_ssid[i] >= 32)) {
      s.ssid[j++] = wifi_ssid[i];
    }
  }

  s.uptime = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  s.reset_reason = esp32_reset_reason_text();
  s.heap = ESP.getFreeHeap();
  s.lat = latitude;
  s.lon = longitude;

  s.cfg[0] = configuration.azimuth_starting_point;
  s.cfg[1] = configuration.azimuth_rotation_capability;
  s.cfg[2] = configuration.elevation_offset;
  s.cfg[3] = configuration.clock_timezone_offset;
  #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
    s.cfg[4] = configuration.tracking_sun_check_frequency_ms;
    s.cfg[5] = configuration.tracking_sun_minimum_rotation_interval_ms;
    s.cfg[6] = configuration.tracking_sun_degrees_difference_threshold;
    s.cfg[7] = configuration.tracking_moon_check_frequency_ms;
    s.cfg[8] = configuration.tracking_moon_minimum_rotation_interval_ms;
    s.cfg[9] = configuration.tracking_moon_degrees_difference_threshold;
  #endif

  #if defined(FEATURE_EL_POSITION_BNO055)
    // getCalibration() es una lectura I2C: como mucho cada 2 s
    static uint8_t cal_sys = 0, cal_gyro = 0, cal_accel = 0;
    static unsigned long last_cal_read = 0;
    if (bno055_state != BNO055_STATE_OK) {
      cal_sys = cal_gyro = cal_accel = 0;
    } else if ((millis() - last_cal_read) > 2000) {
      uint8_t cal_mag = 0;
      bno.getCalibration(&cal_sys, &cal_gyro, &cal_accel, &cal_mag);
      last_cal_read = millis();
    }
    s.bno_present = 1;
    s.bno_sys = cal_sys;
    s.bno_gyro = cal_gyro;
    s.bno_accel = cal_accel;
    s.bno_saved = bno055_offsets_restored;
    s.bno_raw = bno055_raw_elevation;
  #endif
  s.sim = SIMULATION_IS_ACTIVE();

  portENTER_CRITICAL(&web_snapshot_mux);
  web_snapshot = s;
  portEXIT_CRITICAL(&web_snapshot_mux);

}

// --------------------------------------------------------------
void web_stop_axis(byte axis){

  // Se para siempre, aunque otro puerto haya tomado el eje en el último segundo: no se puede
  // saber con certeza (con arranque suave, un cambio de sentido pasa por estados del sentido
  // contrario) y es preferible parar un movimiento ajeno a dejar uno propio sin control
  if (axis == AZ) {
    if (web_jog_az_request != REQUEST_STOP) {
      submit_request(AZ, REQUEST_STOP, 0, DBG_WEB_INTERFACE);
      web_jog_az_request = REQUEST_STOP;
    }
  } else {
    #if defined(FEATURE_ELEVATION_CONTROL)
      if (web_jog_el_request != REQUEST_STOP) {
        submit_request(EL, REQUEST_STOP, 0, DBG_WEB_INTERFACE);
        web_jog_el_request = REQUEST_STOP;
      }
    #endif
  }

}

// --------------------------------------------------------------
void web_apply_stop(uint32_t sid, uint32_t seq){

  // parada inmediata (sin rampa) de ambos ejes y fin de cualquier seguimiento
  #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
    change_tracking(DEACTIVATE_ALL);
  #endif
  #ifdef FEATURE_PARK
    deactivate_park();
  #endif
  #ifdef FEATURE_TIMED_BUFFER
    clear_timed_buffer();
  #endif
  submit_request(AZ, REQUEST_KILL, 0, DBG_WEB_STOP);
  #if defined(FEATURE_ELEVATION_CONTROL)
    submit_request(EL, REQUEST_KILL, 0, DBG_WEB_STOP);
  #endif
  web_jog_az_request = REQUEST_STOP;
  web_jog_el_request = REQUEST_STOP;
  for (byte i = 0; i < 2; i++) {
    web_jog_released_sid[i] = sid;
    web_jog_released_seq[i] = seq;
  }

}

// --------------------------------------------------------------
void web_apply_command(web_command_t * cmd){

  switch (cmd->type) {

    case WEB_CMD_MOVE:
      {
        byte i = (cmd->axis == AZ) ? 0 : 1;
        if ((cmd->sid != 0) && (cmd->sid == web_jog_released_sid[i]) && (cmd->seq <= web_jog_released_seq[i])) {
          break;      // orden de una pulsación ya soltada (llegó por otra conexión, tarde)
        }
        // un keepalive solo mantiene un movimiento web en curso; si el eje está parado (STOP
        // desde otro móvil, hombre muerto, release) no lo vuelve a arrancar
        byte jog_request = (cmd->axis == AZ) ? web_jog_az_request : web_jog_el_request;
        if (!cmd->first && (jog_request == REQUEST_STOP)) {
          break;
        }
      }
      // se vuelve a comprobar aquí: entre la petición y este punto el sensor pudo fallar
      if (!web_rotation_allowed()) { break; }
      if ((cmd->axis == EL) && BNO055_BLOCKS_ELEVATION()) { break; }
      if (cmd->axis == AZ) {
        web_jog_az_last_keepalive = millis();
        if (web_jog_az_request != cmd->request) {     // la orden se envía al cambiar; las repeticiones son keepalive
          #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
            change_tracking(DEACTIVATE_ALL);          // si no, el seguimiento devolvería el rotor a su objetivo
          #endif
          #ifdef FEATURE_PARK
            deactivate_park();
          #endif
          submit_request(AZ, cmd->request, 0, DBG_WEB_INTERFACE);
          web_jog_az_request = cmd->request;
        }
      }
      #if defined(FEATURE_ELEVATION_CONTROL)
        if (cmd->axis == EL) {
          web_jog_el_last_keepalive = millis();
          if (web_jog_el_request != cmd->request) {
            #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
              change_tracking(DEACTIVATE_ALL);
            #endif
            #ifdef FEATURE_PARK
              deactivate_park();
            #endif
            submit_request(EL, cmd->request, 0, DBG_WEB_INTERFACE);
            web_jog_el_request = cmd->request;
          }
        }
      #endif
      break;

    case WEB_CMD_RELEASE:
      if ((cmd->axis == AZ) || (cmd->axis == 0)) {
        web_stop_axis(AZ);
        web_jog_released_sid[0] = cmd->sid;
        web_jog_released_seq[0] = cmd->seq;
      }
      if ((cmd->axis == EL) || (cmd->axis == 0)) {
        web_stop_axis(EL);
        web_jog_released_sid[1] = cmd->sid;
        web_jog_released_seq[1] = cmd->seq;
      }
      break;

    case WEB_CMD_TRACK:
      // la tarea web validó con una copia del estado de hasta WEB_SNAPSHOT_INTERVAL_MS; se
      // repite aquí con el estado real antes de activar
      if (cmd->on) {
        if (!web_rotation_allowed()) { break; }
        #if defined(FEATURE_CLOCK)
          if (!ntp_synced) { break; }
        #endif
        if (BNO055_BLOCKS_ELEVATION()) { break; }
      }
      web_jog_az_request = REQUEST_STOP;
      web_jog_el_request = REQUEST_STOP;
      #if defined(FEATURE_SUN_TRACKING)
        if (cmd->target == WEB_TARGET_SUN) {
          if (cmd->on) {
            change_tracking(ACTIVATE_SUN_TRACKING);
          } else {
            change_tracking(DEACTIVATE_SUN_TRACKING);
            stop_rotation();
          }
        }
      #endif
      #if defined(FEATURE_MOON_TRACKING)
        if (cmd->target == WEB_TARGET_MOON) {
          if (cmd->on) {
            change_tracking(ACTIVATE_MOON_TRACKING);
          } else {
            change_tracking(DEACTIVATE_MOON_TRACKING);
            stop_rotation();
          }
        }
      #endif
      break;

    case WEB_CMD_CONFIG:
      configuration.azimuth_starting_point = (int)cmd->values[0];
      configuration.azimuth_rotation_capability = (long)cmd->values[1];
      configuration.elevation_offset = cmd->values[2];
      configuration.clock_timezone_offset = cmd->values[3];
      #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
        configuration.tracking_sun_check_frequency_ms = (unsigned int)cmd->values[4];
        configuration.tracking_sun_minimum_rotation_interval_ms = (unsigned int)cmd->values[5];
        configuration.tracking_sun_degrees_difference_threshold = cmd->values[6];
        configuration.tracking_moon_check_frequency_ms = (unsigned int)cmd->values[7];
        configuration.tracking_moon_minimum_rotation_interval_ms = (unsigned int)cmd->values[8];
        configuration.tracking_moon_degrees_difference_threshold = cmd->values[9];
      #endif
      write_settings_to_eeprom();     // se guarda ya, sin esperar a EEPROM_WRITE_DIRTY_CONFIG_TIME
      // la posición se recalcula con el punto de inicio y el offset nuevos
      read_azimuth(1);
      #if defined(FEATURE_ELEVATION_CONTROL)
        read_elevation(1);
      #endif
      break;

    case WEB_CMD_SIM:
      #if defined(FEATURE_SIMULATION)
        simulation_set(cmd->on, 1);
      #endif
      web_jog_az_request = REQUEST_STOP;
      web_jog_el_request = REQUEST_STOP;
      break;

    case WEB_CMD_BNO055:
      #if defined(FEATURE_EL_POSITION_BNO055)
        if (cmd->text1[0] == 's') {
          bno055_save_offsets();
        } else if (cmd->text1[0] == 'c') {
          bno055_clear_offsets();
        }
      #endif
      break;

    case WEB_CMD_WIFI:
      strncpy(wifi_ssid, cmd->text1, sizeof(wifi_ssid) - 1);
      wifi_ssid[sizeof(wifi_ssid) - 1] = 0;
      strncpy(wifi_password, cmd->text2, sizeof(wifi_password) - 1);
      wifi_password[sizeof(wifi_password) - 1] = 0;
      wifi_save_credentials();
      web_pending_wifi_reconnect_at = millis() + 1500;
      if (web_pending_wifi_reconnect_at == 0) { web_pending_wifi_reconnect_at = 1; }
      control_port->print(F("WiFi: new network from web, reconnecting to "));
      control_port->println(wifi_ssid);
      break;

    case WEB_CMD_RESTART:
      submit_request(AZ, REQUEST_KILL, 0, DBG_WEB_STOP);
      #if defined(FEATURE_ELEVATION_CONTROL)
        submit_request(EL, REQUEST_KILL, 0, DBG_WEB_STOP);
      #endif
      web_pending_restart_at = millis() + 1500;
      if (web_pending_restart_at == 0) { web_pending_restart_at = 1; }
      break;

    case WEB_CMD_LOCATOR:
      #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
        {
          char grid[10];
          station_location_set_from_grid(cmd->grid, grid);
        }
      #endif
      break;

  }

  // el estado cambia: se publica ya, sin esperar al siguiente intervalo
  web_snapshot_last_update = 0;

}

// --------------------------------------------------------------
void service_web_server(){

  // llamado desde el loop (service_wifi): aplica órdenes, hombre muerto y publica el estado

  if (web_command_queue == NULL) {
    return;
  }

  if (web_stop_pending) {
    web_stop_pending = 0;
    // STOP gana: se descartan las órdenes que ya estaban en la cola (un seguimiento encolado
    // antes no debe activarse justo después de la parada)
    xQueueReset(web_command_queue);
    web_apply_stop(web_stop_sid, web_stop_seq);
    web_snapshot_last_update = 0;
  }

  web_command_t cmd;
  while (xQueueReceive(web_command_queue, &cmd, 0) == pdTRUE) {
    web_apply_command(&cmd);
  }

  // hombre muerto: sin keepalive de la página, el eje se detiene solo
  if ((web_jog_az_request != REQUEST_STOP) && ((millis() - web_jog_az_last_keepalive) > WEB_JOG_TIMEOUT_MS)) {
    web_stop_axis(AZ);
    #ifdef DEBUG_ETHERNET
      debug.println(F("service_web_server: az jog keepalive timeout"));
    #endif
  }
  if ((web_jog_el_request != REQUEST_STOP) && ((millis() - web_jog_el_last_keepalive) > WEB_JOG_TIMEOUT_MS)) {
    web_stop_axis(EL);
    #ifdef DEBUG_ETHERNET
      debug.println(F("service_web_server: el jog keepalive timeout"));
    #endif
  }

  if (web_pending_wifi_reconnect_at && ((long)(millis() - web_pending_wifi_reconnect_at) >= 0)) {
    web_pending_wifi_reconnect_at = 0;
    wifi_connected = 0;
    wifi_start_connection();
  }
  if (web_pending_restart_at && ((long)(millis() - web_pending_restart_at) >= 0)) {
    control_port->println(F("Restart requested from web"));
    control_port->flush();
    ESP.restart();
  }

  if ((web_snapshot_last_update == 0) || ((millis() - web_snapshot_last_update) > WEB_SNAPSHOT_INTERVAL_MS)) {
    web_update_snapshot();
    web_snapshot_last_update = millis();
    if (web_snapshot_last_update == 0) {
      web_snapshot_last_update = 1;
    }
  }

}

// ==============================================================
//  Lado de la tarea web (núcleo 0): solo lee web_snapshot y encola órdenes
// ==============================================================

void web_get_snapshot(web_snapshot_t * s){

  portENTER_CRITICAL(&web_snapshot_mux);
  *s = web_snapshot;
  portEXIT_CRITICAL(&web_snapshot_mux);

}

// --------------------------------------------------------------
byte web_check_auth(){

  // Autenticación básica opcional (WEB_SERVER_PASSWORD vacío = sin contraseña)
  if (strlen(WEB_SERVER_PASSWORD) == 0) {
    return 1;
  }
  if (web_server.authenticate(WEB_SERVER_USER, WEB_SERVER_PASSWORD)) {
    return 1;
  }
  web_server.requestAuthentication(BASIC_AUTH, "Rotor");
  return 0;

}

// --------------------------------------------------------------
void web_send_result(byte ok, const char * message){

  char json[160];
  char escaped[120];
  byte j = 0;

  for (byte i = 0; (message[i] != 0) && (j < sizeof(escaped) - 2); i++) {
    if ((message[i] == '"') || (message[i] == '\\')) {
      escaped[j++] = '\\';
    }
    escaped[j++] = message[i];
  }
  escaped[j] = 0;

  snprintf(json, sizeof(json), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", escaped);
  web_server.sendHeader("Cache-Control", "no-store");
  web_server.send(ok ? 200 : 400, "application/json", json);

}

// --------------------------------------------------------------
uint32_t web_arg_u32(const char * name){

  return (uint32_t)strtoul(web_server.arg(name).c_str(), NULL, 10);

}

// --------------------------------------------------------------
byte web_queue_command(web_command_t * cmd){

  if (xQueueSend(web_command_queue, cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
    web_send_result(0, "Rotor ocupado, reintenta");
    return 0;
  }
  return 1;

}

// --------------------------------------------------------------
void web_handle_root(){

  if (!web_check_auth()) {
    return;
  }

  // la página lleva el intervalo de keepalive incrustado como %KEEPALIVE%
  String page = FPSTR(web_page_html);
  page.replace("%KEEPALIVE%", String(WEB_JOG_KEEPALIVE_MS));
  web_server.sendHeader("Cache-Control", "no-cache");
  web_server.send(200, "text/html; charset=utf-8", page);

}

// --------------------------------------------------------------
const char * web_motion_text(byte state){

  switch (state) {
    case ROTATING_CW: return "cw";
    case ROTATING_CCW: return "ccw";
    case ROTATING_UP: return "up";
    case ROTATING_DOWN: return "down";
    default: return "";
  }

}

// --------------------------------------------------------------
void web_handle_status(){

  if (!web_check_auth()) {
    return;
  }

  web_snapshot_t s;
  web_get_snapshot(&s);
  char json[720];

  snprintf(json, sizeof(json),
    "{\"az\":%.2f,\"el\":%.2f,\"el_ok\":%d,\"sim\":%d,\"az_mv\":\"%s\",\"el_mv\":\"%s\","
    "\"sun\":{\"az\":%.2f,\"el\":%.2f,\"trk\":%d},\"moon\":{\"az\":%.2f,\"el\":%.2f,\"trk\":%d},"
    "\"wifi\":{\"ok\":%d,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"},"
    "\"uptime\":%lu,\"rst\":\"%s\",\"time_ok\":%d,\"utc\":\"%s\",\"grid\":\"%s\",\"bno\":\"%s\"}",
    (double)s.az, (double)s.el, s.el_ok, s.sim, web_motion_text(s.az_motion), web_motion_text(s.el_motion),
    (double)s.sun_az, (double)s.sun_el, s.sun_trk, (double)s.moon_az, (double)s.moon_el, s.moon_trk,
    s.wifi_ok, s.ssid, s.rssi, s.ip,
    s.uptime, s.reset_reason ? s.reset_reason : "-", s.time_ok, s.utc, s.grid, s.bno_text ? s.bno_text : "-");

  web_server.sendHeader("Cache-Control", "no-store");
  web_server.send(200, "application/json", json);

}

// --------------------------------------------------------------
void web_handle_move(){

  if (!web_check_auth()) {
    return;
  }

  web_snapshot_t s;
  web_get_snapshot(&s);
  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_MOVE;
  cmd.sid = web_arg_u32("sid");
  cmd.seq = web_arg_u32("seq");
  // sin sesión (curl, scripts) cada orden cuenta como primera: arranca y mantiene el eje,
  // pero hay que repetirla antes de WEB_JOG_TIMEOUT_MS
  cmd.first = (cmd.sid == 0) ? 1 : (web_server.arg("first") == "1");

  String dir = web_server.arg("dir");
  if (dir == "cw") { cmd.axis = AZ; cmd.request = REQUEST_CW; }
  else if (dir == "ccw") { cmd.axis = AZ; cmd.request = REQUEST_CCW; }
  else if (dir == "up") { cmd.axis = EL; cmd.request = REQUEST_UP; }
  else if (dir == "down") { cmd.axis = EL; cmd.request = REQUEST_DOWN; }
  else if (dir == "release") {
    cmd.type = WEB_CMD_RELEASE;
    String axis = web_server.arg("axis");
    cmd.axis = (axis == "az") ? AZ : ((axis == "el") ? EL : 0);
    if (web_queue_command(&cmd)) {
      web_send_result(1, "");
    }
    return;
  } else {
    web_send_result(0, "Dirección no válida");
    return;
  }

  #if !defined(FEATURE_ELEVATION_CONTROL)
    if (cmd.axis == EL) {
      web_send_result(0, "Sin control de elevación");
      return;
    }
  #endif

  if ((cmd.axis == EL) && s.el_blocked) {
    web_send_result(0, "Sensor de elevación no disponible");
    return;
  }

  if (!s.rotation_allowed) {
    web_send_result(0, "Arrancando, espera unos segundos");
    return;
  }

  if (web_queue_command(&cmd)) {
    web_send_result(1, "");
  }

}

// --------------------------------------------------------------
void web_handle_stop(){

  if (!web_check_auth()) {
    return;
  }

  // sin cola: no puede fallar por cola llena
  web_stop_sid = web_arg_u32("sid");
  web_stop_seq = web_arg_u32("seq");
  web_stop_pending = 1;
  web_send_result(1, "Movimiento detenido");

}

// --------------------------------------------------------------
void web_handle_track(){

  if (!web_check_auth()) {
    return;
  }

  web_snapshot_t s;
  web_get_snapshot(&s);
  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_TRACK;
  cmd.on = (web_server.arg("on") == "1");

  String target = web_server.arg("target");
  #if defined(FEATURE_SUN_TRACKING)
    if (target == "sun") { cmd.target = WEB_TARGET_SUN; }
  #endif
  #if defined(FEATURE_MOON_TRACKING)
    if (target == "moon") { cmd.target = WEB_TARGET_MOON; }
  #endif
  if (cmd.target == 0) {
    web_send_result(0, "Objetivo no válido");
    return;
  }

  if (cmd.on) {
    if (!s.time_ok) {
      web_send_result(0, "Sin hora NTP: no se puede calcular la posición");
      return;
    }
    if (s.el_blocked) {
      web_send_result(0, "Sensor de elevación no disponible");
      return;
    }
    if (!s.rotation_allowed) {
      web_send_result(0, "Arrancando, espera unos segundos");
      return;
    }
  }

  if (!web_queue_command(&cmd)) {
    return;
  }

  if (cmd.target == WEB_TARGET_SUN) {
    if (!cmd.on) { web_send_result(1, "Seguimiento del Sol desactivado"); }
    else { web_send_result(1, s.sun_visible ? "Siguiendo el Sol" : "Seguimiento del Sol activo (bajo el horizonte: el rotor espera en su azimut, a 0° de elevación)"); }
  } else {
    if (!cmd.on) { web_send_result(1, "Seguimiento de la Luna desactivado"); }
    else { web_send_result(1, s.moon_visible ? "Siguiendo la Luna" : "Seguimiento de la Luna activo (bajo el horizonte: el rotor espera en su azimut, a 0° de elevación)"); }
  }

}

// --------------------------------------------------------------
byte web_normalize_grid(const char * grid_in, char * grid_out){

  // Locator Maidenhead de 6 caracteres: AA00aa, campo A-R, subcuadro a-x
  if (strlen(grid_in) != 6) {
    return 0;
  }
  for (byte i = 0; i < 6; i++) {
    unsigned char c = (unsigned char)grid_in[i];     // ctype con char negativo (UTF-8) es comportamiento indefinido
    if ((i == 2) || (i == 3)) {
      if (!isdigit(c)) { return 0; }
      grid_out[i] = c;
    } else {
      if (!isalpha(c)) { return 0; }
      grid_out[i] = (i < 2) ? toupper(c) : tolower(c);
      if ((i < 2) && (grid_out[i] > 'R')) { return 0; }
      if ((i >= 4) && (grid_out[i] > 'x')) { return 0; }
    }
  }
  grid_out[6] = 0;
  return 1;

}

// --------------------------------------------------------------
void web_handle_locator(){

  if (!web_check_auth()) {
    return;
  }

  #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
    web_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = WEB_CMD_LOCATOR;
    if (!web_normalize_grid(web_server.arg("grid").c_str(), cmd.grid)) {
      web_send_result(0, "Locator no válido (formato XXnnxx, p. ej. FF46pn)");
      return;
    }
    if (web_queue_command(&cmd)) {
      char message[48];
      snprintf(message, sizeof(message), "Ubicación guardada: %s", cmd.grid);
      web_send_result(1, message);
    }
  #else
    web_send_result(0, "Sin seguimiento de Sol/Luna");
  #endif

}

// --------------------------------------------------------------
void web_handle_config_page(){

  if (!web_check_auth()) {
    return;
  }
  web_server.sendHeader("Cache-Control", "no-cache");
  web_server.send_P(200, "text/html; charset=utf-8", web_config_html);

}

// --------------------------------------------------------------
void web_handle_config_get(){

  if (!web_check_auth()) {
    return;
  }

  web_snapshot_t s;
  web_get_snapshot(&s);

  // la contraseña WiFi no se devuelve nunca
  String json;
  json.reserve(900);
  json = "{\"cfg\":{";
  for (byte i = 0; i < WEB_CONFIG_FIELDS; i++) {
    if (i) { json += ","; }
    json += "\"";
    json += web_config_fields[i].name;
    json += "\":";
    json += String(s.cfg[i], 2);
  }
  char buffer[420];
  snprintf(buffer, sizeof(buffer),
    "},\"sim\":%d,\"grid\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,"
    "\"bno\":{\"present\":%d,\"text\":\"%s\",\"sys\":%d,\"gyro\":%d,\"accel\":%d,\"saved\":%d,\"raw\":%.2f},"
    "\"wifi\":{\"ok\":%d,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"},"
    "\"ver\":\"%s\",\"uptime\":%lu,\"rst\":\"%s\",\"heap\":%lu}",
    s.sim, s.grid, s.lat, s.lon,
    s.bno_present, s.bno_text ? s.bno_text : "-", s.bno_sys, s.bno_gyro, s.bno_accel, s.bno_saved, (double)s.bno_raw,
    s.wifi_ok, s.ssid, s.rssi, s.ip,
    CODE_VERSION " ESP32 " __DATE__, s.uptime, s.reset_reason ? s.reset_reason : "-", (unsigned long)s.heap);
  json += buffer;

  web_server.sendHeader("Cache-Control", "no-store");
  web_server.send(200, "application/json", json);

}

// --------------------------------------------------------------
void web_handle_config_post(){

  if (!web_check_auth()) {
    return;
  }

  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_CONFIG;
  char message[96];

  for (byte i = 0; i < WEB_CONFIG_FIELDS; i++) {
    String text = web_server.arg(web_config_fields[i].name);
    text.trim();
    char * end = NULL;
    float value = strtof(text.c_str(), &end);
    if ((text.length() == 0) || (end == NULL) || (*end != 0) || isnan(value) ||
        (value < web_config_fields[i].min_value) || (value > web_config_fields[i].max_value)) {
      snprintf(message, sizeof(message), "Valor no válido en %s (%g a %g)",
        web_config_fields[i].name, (double)web_config_fields[i].min_value, (double)web_config_fields[i].max_value);
      web_send_result(0, message);
      return;
    }
    cmd.values[i] = value;
  }

  if (web_queue_command(&cmd)) {
    web_send_result(1, "Ajustes guardados");
  }

}

// --------------------------------------------------------------
void web_handle_sim(){

  if (!web_check_auth()) {
    return;
  }

  #if defined(FEATURE_SIMULATION)
    web_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = WEB_CMD_SIM;
    cmd.on = (web_server.arg("on") == "1");
    if (web_queue_command(&cmd)) {
      web_send_result(1, cmd.on ? "Simulación activada: los motores no se moverán" : "Simulación desactivada");
    }
  #else
    web_send_result(0, "Firmware compilado sin FEATURE_SIMULATION");
  #endif

}

// --------------------------------------------------------------
void web_handle_bno055(){

  if (!web_check_auth()) {
    return;
  }

  #if defined(FEATURE_EL_POSITION_BNO055)
    web_snapshot_t s;
    web_get_snapshot(&s);
    web_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = WEB_CMD_BNO055;
    String action = web_server.arg("action");
    if (action == "save") {
      if ((s.bno_gyro < 3) || (s.bno_accel < 3)) {
        web_send_result(0, "Falta calibrar: Giro y Acel tienen que estar en 3");
        return;
      }
      strcpy(cmd.text1, "save");
    } else if (action == "clear") {
      strcpy(cmd.text1, "clear");
    } else {
      web_send_result(0, "Acción no válida");
      return;
    }
    if (web_queue_command(&cmd)) {
      web_send_result(1, (cmd.text1[0] == 's') ? "Calibración guardada" : "Calibración borrada");
    }
  #else
    web_send_result(0, "Firmware compilado sin BNO055");
  #endif

}

// --------------------------------------------------------------
void web_handle_wifi(){

  if (!web_check_auth()) {
    return;
  }

  String ssid = web_server.arg("ssid");
  String pass = web_server.arg("pass");
  if ((ssid.length() == 0) || (ssid.length() > 32)) {
    web_send_result(0, "El nombre de la red debe tener de 1 a 32 caracteres");
    return;
  }
  if ((pass.length() > 63) || ((pass.length() > 0) && (pass.length() < 8))) {
    web_send_result(0, "La contraseña WPA debe tener de 8 a 63 caracteres (o vacía si la red es abierta)");
    return;
  }

  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_WIFI;
  strncpy(cmd.text1, ssid.c_str(), sizeof(cmd.text1) - 1);
  strncpy(cmd.text2, pass.c_str(), sizeof(cmd.text2) - 1);
  if (web_queue_command(&cmd)) {
    web_send_result(1, "Red guardada. El rotor se reconecta en unos segundos: búscalo en la red nueva");
  }

}

// --------------------------------------------------------------
void web_handle_restart(){

  if (!web_check_auth()) {
    return;
  }

  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_RESTART;
  if (web_queue_command(&cmd)) {
    web_send_result(1, "Reiniciando… la página se recarga sola");
  }

}

// --------------------------------------------------------------
void web_handle_not_found(){

  web_server.send(404, "text/plain", "404");

}

// --------------------------------------------------------------
void web_server_task(void * parameter){

  for (;;) {
    web_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }

}

// --------------------------------------------------------------
void initialize_web_server(){

  web_command_queue = xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(web_command_t));
  web_update_snapshot();

  web_server.on("/", HTTP_GET, web_handle_root);
  web_server.on("/api/status", HTTP_GET, web_handle_status);
  web_server.on("/api/move", HTTP_POST, web_handle_move);
  web_server.on("/api/stop", HTTP_POST, web_handle_stop);
  web_server.on("/api/track", HTTP_POST, web_handle_track);
  web_server.on("/api/locator", HTTP_POST, web_handle_locator);
  web_server.on("/config", HTTP_GET, web_handle_config_page);
  web_server.on("/api/config", HTTP_GET, web_handle_config_get);
  web_server.on("/api/config", HTTP_POST, web_handle_config_post);
  web_server.on("/api/sim", HTTP_POST, web_handle_sim);
  web_server.on("/api/bno055", HTTP_POST, web_handle_bno055);
  web_server.on("/api/wifi", HTTP_POST, web_handle_wifi);
  web_server.on("/api/restart", HTTP_POST, web_handle_restart);
  web_server.onNotFound(web_handle_not_found);
  web_server.begin();

  // núcleo 0 (el de la pila WiFi); el loop() de Arduino corre en el núcleo 1
  xTaskCreatePinnedToCore(web_server_task, "web", WEB_SERVER_TASK_STACK, NULL, 1, NULL, 0);

}

#endif // FEATURE_WEB_SERVER
