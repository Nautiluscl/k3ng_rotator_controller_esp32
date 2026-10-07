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
.loc{display:flex;gap:8px;margin-top:10px}
.loc input{flex:1;min-width:0;font:inherit;color:var(--tx);background:#0f1720;border:1px solid #2f4155;border-radius:10px;padding:8px 10px;text-transform:uppercase}
.loc button{min-height:40px;padding:0 14px;touch-action:auto}
#msg{min-height:1.3em;text-align:center;color:var(--mu);font-size:13px}
#msg.err{color:var(--st)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--st);margin-right:6px;vertical-align:1px}
.dot.ok{background:var(--ok)}
</style></head><body><main>
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
 <form class="loc" id="locf"><input id="loci" maxlength="6" placeholder="Locator (FF46pn)" autocomplete="off" autocapitalize="characters"><button>Guardar</button></form>
</section>
</main>
<script>
const $=id=>document.getElementById(id);let S={},held=null,hbT=null,fails=0;
function msg(t,e){const m=$('msg');m.textContent=t||'';m.className=e?'err':''}
async function post(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b||{})});
 const j=await r.json().catch(()=>({ok:false,msg:'Respuesta inválida'}));if(!j.ok)throw new Error(j.msg||'Error');return j}
function f1(v){return(Math.round(v*10)/10).toFixed(1)+'°'}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function bars(r){return r>=-55?'Excelente':r>=-67?'Buena':r>=-75?'Regular':'Débil'}
function mvTxt(s){return{cw:'girando CW',ccw:'girando CCW',up:'subiendo',down:'bajando'}[s]||''}
function render(j){S=j;
 $('az').textContent=f1(j.az);$('el').textContent=j.el_ok?f1(j.el):'---';
 $('azs').textContent=mvTxt(j.az_mv);$('azs').className='s'+(j.az_mv?' mv':'');
 $('els').textContent=j.el_ok?mvTxt(j.el_mv):'sensor no disponible';$('els').className='s'+(j.el_mv||!j.el_ok?' mv':'');
 $('sun').classList.toggle('on',!!j.sun.trk);$('moon').classList.toggle('on',!!j.moon.trk);
 $('sunp').textContent=j.time_ok?'AZ '+f1(j.sun.az)+'  EL '+f1(j.sun.el):'sin hora';
 $('moonp').textContent=j.time_ok?'AZ '+f1(j.moon.az)+'  EL '+f1(j.moon.el):'sin hora';
 $('wd').className='dot'+(j.wifi.ok?' ok':'');$('ssid').textContent=j.wifi.ssid||'--';
 $('rssi').textContent=j.wifi.ok?j.wifi.rssi+' dBm ('+bars(j.wifi.rssi)+')':'--';$('ip').textContent=j.wifi.ip||'--';
 $('up').textContent=dur(j.uptime);$('rst').textContent=j.rst;$('utc').textContent=j.time_ok?j.utc:'sin sincronizar';
 $('bno').textContent=j.bno;$('grid').textContent=j.grid}
async function poll(){try{const r=await fetch('/api/status',{cache:'no-store'});render(await r.json());
 if(fails){msg('')}fails=0}catch(e){if(++fails>1)msg('Sin conexión con el rotor',1)}
 setTimeout(poll,held?500:1000)}
function release(){if(!held)return;const d=held;held=null;clearInterval(hbT);
 document.querySelectorAll('[data-d]').forEach(b=>b.classList.remove('on'));
 post('/api/move',{dir:'release',axis:(d=='cw'||d=='ccw')?'az':'el'}).catch(e=>msg(e.message,1))}
document.querySelectorAll('[data-d]').forEach(b=>{
 b.addEventListener('pointerdown',e=>{e.preventDefault();if(held)release();held=b.dataset.d;b.classList.add('on');
  b.setPointerCapture(e.pointerId);if(navigator.vibrate)navigator.vibrate(15);
  const go=()=>post('/api/move',{dir:held||'release'}).catch(err=>{msg(err.message,1);release()});
  go();hbT=setInterval(()=>{if(held)go()},%KEEPALIVE%)});
 ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>b.addEventListener(ev,release));
 b.addEventListener('contextmenu',e=>e.preventDefault())});
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});
$('stop').addEventListener('click',async()=>{release();if(navigator.vibrate)navigator.vibrate([30,40,30]);
 try{await post('/api/stop');msg('Movimiento detenido')}catch(e){msg(e.message,1)}});
['sun','moon'].forEach(t=>$(t).addEventListener('click',async()=>{const on=S[t]&&S[t].trk?0:1;
 try{const j=await post('/api/track',{target:t,on:on});msg(j.msg)}catch(e){msg(e.message,1)}}));
$('locf').addEventListener('submit',async e=>{e.preventDefault();const g=$('loci').value.trim();
 try{const j=await post('/api/locator',{grid:g});msg(j.msg);$('loci').value=''}catch(err){msg(err.message,1)}});
poll();
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
#define WEB_CMD_STOP 3
#define WEB_CMD_TRACK 4
#define WEB_CMD_LOCATOR 5

#define WEB_TARGET_SUN 1
#define WEB_TARGET_MOON 2

struct web_command_t {
  byte type;
  byte axis;          // AZ, EL o 0 = ambos (RELEASE)
  byte request;       // REQUEST_CW/CCW/UP/DOWN
  byte target;        // WEB_TARGET_SUN / WEB_TARGET_MOON
  byte on;
  char grid[7];
};

struct web_snapshot_t {
  float az;
  float el;
  byte el_ok;
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
};

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
    s.el_ok = (bno055_state == BNO055_STATE_OK);
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

  portENTER_CRITICAL(&web_snapshot_mux);
  web_snapshot = s;
  portEXIT_CRITICAL(&web_snapshot_mux);

}

// --------------------------------------------------------------
void web_stop_axis(byte axis){

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
void web_apply_command(web_command_t * cmd){

  switch (cmd->type) {

    case WEB_CMD_MOVE:
      // se vuelve a comprobar aquí: entre la petición y este punto el sensor pudo fallar
      if (!web_rotation_allowed()) { break; }
      #if defined(FEATURE_EL_POSITION_BNO055)
        if ((cmd->axis == EL) && (bno055_state != BNO055_STATE_OK)) { break; }
      #endif
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
      if ((cmd->axis == AZ) || (cmd->axis == 0)) { web_stop_axis(AZ); }
      if ((cmd->axis == EL) || (cmd->axis == 0)) { web_stop_axis(EL); }
      break;

    case WEB_CMD_STOP:
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
      break;

    case WEB_CMD_TRACK:
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
    "{\"az\":%.2f,\"el\":%.2f,\"el_ok\":%d,\"az_mv\":\"%s\",\"el_mv\":\"%s\","
    "\"sun\":{\"az\":%.2f,\"el\":%.2f,\"trk\":%d},\"moon\":{\"az\":%.2f,\"el\":%.2f,\"trk\":%d},"
    "\"wifi\":{\"ok\":%d,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"},"
    "\"uptime\":%lu,\"rst\":\"%s\",\"time_ok\":%d,\"utc\":\"%s\",\"grid\":\"%s\",\"bno\":\"%s\"}",
    (double)s.az, (double)s.el, s.el_ok, web_motion_text(s.az_motion), web_motion_text(s.el_motion),
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

  if ((cmd.axis == EL) && !s.el_ok) {
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

  web_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = WEB_CMD_STOP;
  if (web_queue_command(&cmd)) {
    web_send_result(1, "Movimiento detenido");
  }

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
    if (!s.el_ok) {
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
    else { web_send_result(1, s.sun_visible ? "Siguiendo el Sol" : "Seguimiento del Sol activo (bajo el horizonte: espera a que salga)"); }
  } else {
    if (!cmd.on) { web_send_result(1, "Seguimiento de la Luna desactivado"); }
    else { web_send_result(1, s.moon_visible ? "Siguiendo la Luna" : "Seguimiento de la Luna activo (bajo el horizonte: espera a que salga)"); }
  }

}

// --------------------------------------------------------------
byte web_normalize_grid(const char * grid_in, char * grid_out){

  // Locator Maidenhead de 6 caracteres: AA00aa, campo A-R, subcuadro a-x
  if (strlen(grid_in) != 6) {
    return 0;
  }
  for (byte i = 0; i < 6; i++) {
    char c = grid_in[i];
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
  web_server.onNotFound(web_handle_not_found);
  web_server.begin();

  // núcleo 0 (el de la pila WiFi); el loop() de Arduino corre en el núcleo 1
  xTaskCreatePinnedToCore(web_server_task, "web", WEB_SERVER_TASK_STACK, NULL, 1, NULL, 0);

}

#endif // FEATURE_WEB_SERVER
