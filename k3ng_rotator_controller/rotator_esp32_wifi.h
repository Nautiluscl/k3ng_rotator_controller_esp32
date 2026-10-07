/* rotator_esp32_wifi.h

   WiFi para el perfil ESP32 (FEATURE_WIFI):
     - conexión a una red existente (modo estación), con reconexión automática no bloqueante
     - puerto serie virtual por TCP (WIFI_TCP_PORT, por defecto 23): mismos comandos y
       respuestas que el puerto de control USB (GS-232, Easycom y comandos \)
     - nombre en la red por mDNS (WIFI_HOSTNAME.local)
     - hora UTC por NTP, que alimenta el reloj del firmware (FEATURE_CLOCK)
     - comandos \W para ver y cambiar la red sin recompilar

   Este archivo contiene definiciones de funciones y variables: se incluye una sola vez,
   al final de k3ng_rotator_controller.ino, para tener acceso a todo el estado del sketch.
   Los prototipos están en rotator_prototypes_platformio.h.
*/

#if defined(FEATURE_WIFI)

#define WIFI_RAW_LINE_SIZE 96

#include <lwip/sockets.h>

WiFiServer wifi_tcp_server(WIFI_TCP_PORT);
WiFiClient wifi_tcp_client;
byte wifi_tcp_write_failures = 0;

char wifi_ssid[33] = "";
char wifi_password[65] = "";
byte wifi_connected = 0;
byte wifi_mdns_started = 0;
unsigned long wifi_last_connect_attempt = 0;
unsigned long wifi_connected_since = 0;

byte ntp_synced = 0;
unsigned long ntp_last_sync = 0;
unsigned long ntp_last_check = 0;

// Copia de la línea recibida conservando mayúsculas y minúsculas. El firmware pasa a
// mayúsculas todo lo que entra, lo que estropearía el SSID y la contraseña de \WS y \WP.
// Hay una copia por origen (puerto serie y TCP) para que no se mezclen.
#define WIFI_RAW_SOURCE_SERIAL 0
#define WIFI_RAW_SOURCE_TCP 1

struct wifi_raw_line_t {
  char line[WIFI_RAW_LINE_SIZE];
  byte index;
  char last_line[WIFI_RAW_LINE_SIZE];
};
wifi_raw_line_t wifi_raw[2];

// --------------------------------------------------------------
void wifi_raw_line_feed(byte incoming_byte, byte source){

  wifi_raw_line_t * raw = &wifi_raw[source ? 1 : 0];

  if ((incoming_byte == 13) || (incoming_byte == 10)) {
    if (raw->index > 0) {
      raw->line[raw->index] = 0;
      strcpy(raw->last_line, raw->line);
      raw->index = 0;
    }
    return;
  }
  if (raw->index < (WIFI_RAW_LINE_SIZE - 1)) {
    raw->line[raw->index] = incoming_byte;
    raw->index++;
  }

}

// --------------------------------------------------------------
void wifi_raw_line_reset(byte source){

  // se llama cuando el intérprete descarta su buffer, para que la copia cruda no se desfase
  wifi_raw[source ? 1 : 0].index = 0;
  wifi_raw[source ? 1 : 0].last_line[0] = 0;

}

// --------------------------------------------------------------
void wifi_raw_line_close(byte source){

  // el intérprete procesa una línea sin CR (buffer lleno): se cierra también la copia cruda
  wifi_raw_line_feed(13, source);

}

// --------------------------------------------------------------
#if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
void station_location_load(){

  // Ubicación de la estación para el cálculo del Sol y la Luna. En el firmware original vive
  // solo en RAM (latitude/longitude) y vuelve a DEFAULT_LATITUDE/LONGITUDE en cada arranque.
  Preferences prefs;
  if (prefs.begin("location", true)) {
    if (prefs.isKey("lat") && prefs.isKey("lon")) {
      latitude = prefs.getDouble("lat", DEFAULT_LATITUDE);
      longitude = prefs.getDouble("lon", DEFAULT_LONGITUDE);
    }
    prefs.end();
  }

}

// --------------------------------------------------------------
void station_location_save(){

  Preferences prefs;
  if (prefs.begin("location", false)) {
    prefs.putDouble("lat", latitude);
    prefs.putDouble("lon", longitude);
    prefs.end();
  }

}

// --------------------------------------------------------------
byte station_location_set_from_grid(const char * grid_in, char * grid_out){

  // Locator Maidenhead de 6 caracteres (p. ej. FF46pn). Devuelve 1 si es válido; en grid_out
  // queda normalizado (AA00aa).
  if (strlen(grid_in) != 6) {
    return 0;
  }
  char grid[7];
  for (byte i = 0; i < 6; i++) {
    unsigned char c = (unsigned char)grid_in[i];     // ctype con char negativo (UTF-8) es comportamiento indefinido
    if ((i == 2) || (i == 3)) {
      if (!isdigit(c)) { return 0; }
      grid[i] = c;
    } else {
      if (!isalpha(c)) { return 0; }
      grid[i] = (i < 2) ? toupper(c) : tolower(c);
      if ((i < 2) && (grid[i] > 'R')) { return 0; }     // campo: A-R
      if ((i >= 4) && (grid[i] > 'x')) { return 0; }    // subcuadro: a-x
    }
  }
  grid[6] = 0;

  grid2deg(grid, &longitude, &latitude);
  station_location_save();
  strcpy(grid_out, grid);
  return 1;

}
#endif

// --------------------------------------------------------------
void wifi_load_credentials(){

  Preferences prefs;

  strncpy(wifi_ssid, WIFI_DEFAULT_SSID, sizeof(wifi_ssid) - 1);
  strncpy(wifi_password, WIFI_DEFAULT_PASSWORD, sizeof(wifi_password) - 1);

  if (prefs.begin("wifi", true)) {
    if (prefs.isKey("ssid")) {
      prefs.getString("ssid", wifi_ssid, sizeof(wifi_ssid));
      prefs.getString("pass", wifi_password, sizeof(wifi_password));
    }
    prefs.end();
  }

}

// --------------------------------------------------------------
void wifi_start_connection(){

  wifi_last_connect_attempt = millis();
  if (strlen(wifi_ssid) == 0) {
    return;
  }
  WiFi.disconnect();
  WiFi.begin(wifi_ssid, wifi_password);

}

// --------------------------------------------------------------
void initialize_wifi(){

  wifi_load_credentials();
  #if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
    station_location_load();
  #endif

  WiFi.persistent(false);              // las credenciales se guardan en Preferences, no en la NVS del driver
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.setSleep(false);                // sin ahorro de energía: evita 100-300 ms de latencia por comando
  WiFi.setAutoReconnect(true);

  #if (WIFI_USE_DHCP == 0)
    WiFi.config(IPAddress(WIFI_STATIC_IP), IPAddress(WIFI_STATIC_GATEWAY), IPAddress(WIFI_STATIC_SUBNET), IPAddress(WIFI_STATIC_DNS));
  #endif

  // no se espera a que conecte: el rotor tiene que funcionar por USB aunque no haya WiFi
  wifi_start_connection();

  wifi_tcp_server.begin();
  wifi_tcp_server.setNoDelay(true);

  #if defined(FEATURE_WEB_SERVER)
    initialize_web_server();
  #endif

  control_port->print(F("WiFi: connecting to "));
  control_port->println(wifi_ssid);

}

// --------------------------------------------------------------
void wifi_on_connect(){

  wifi_connected = 1;
  wifi_connected_since = millis();

  control_port->print(F("WiFi: connected, IP "));
  control_port->print(WiFi.localIP());
  control_port->print(F(" RSSI "));
  control_port->println(WiFi.RSSI());

  if (!wifi_mdns_started) {
    if (MDNS.begin(WIFI_HOSTNAME)) {
      MDNS.addService("telnet", "tcp", WIFI_TCP_PORT);
      #if defined(FEATURE_WEB_SERVER)
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
      #endif
      wifi_mdns_started = 1;
    }
  }

  // SNTP corre en segundo plano; la hora se vuelca al reloj del firmware en service_ntp()
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

}

// --------------------------------------------------------------
void service_ntp(){

  #if defined(FEATURE_CLOCK)
    // hasta la primera sincronización se mira cada segundo; después, cada NTP_RESYNC_INTERVAL_MS
    unsigned long interval = ntp_synced ? NTP_RESYNC_INTERVAL_MS : 1000;

    // si se pierde el NTP durante mucho tiempo, el reloj sigue funcionando pero ya no se
    // marca como sincronizado
    if (ntp_synced && ((millis() - ntp_last_sync) > NTP_STALE_AFTER_MS)) {
      clock_status = FREE_RUNNING;
    }

    if ((millis() - ntp_last_check) < interval) {
      return;
    }
    ntp_last_check = millis();

    time_t now_utc = time(nullptr);
    if (now_utc > 1700000000) {        // si SNTP aún no respondió, time() vale pocos segundos desde 1970
      setTime((unsigned long)now_utc);
      clock_status = NTP_SYNC;
      if (!ntp_synced) {
        control_port->print(F("NTP: clock set "));
        control_port->println(zulu_clock_string());
      }
      ntp_synced = 1;
      ntp_last_sync = millis();
    }
  #endif

}

// --------------------------------------------------------------
void service_wifi_connection(){

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_connected) {
      wifi_on_connect();
    }
    return;
  }

  if (wifi_connected) {
    wifi_connected = 0;
    control_port->println(F("WiFi: connection lost"));
    wifi_last_connect_attempt = millis();
  }

  // el driver reintenta solo (setAutoReconnect); solo se fuerza un intento nuevo si se ha
  // quedado en un estado de fallo, para no cortar un intento que todavía está en curso
  if ((millis() - wifi_last_connect_attempt) > WIFI_RECONNECT_INTERVAL_MS) {
    wl_status_t status = WiFi.status();
    if ((status == WL_CONNECT_FAILED) || (status == WL_NO_SSID_AVAIL) || (status == WL_CONNECTION_LOST) || (status == WL_DISCONNECTED) || (status == WL_STOPPED)) {
      wifi_start_connection();
    } else {
      wifi_last_connect_attempt = millis();
    }
  }

}

// --------------------------------------------------------------
void service_wifi_tcp(){

  static byte tcp_buffer[COMMAND_BUFFER_SIZE];
  static int tcp_buffer_index = 0;
  static unsigned long last_received_byte = 0;
  static byte telnet_state = 0;        // 0 datos, 1 tras IAC, 2 opción de WILL/WONT/DO/DONT, 3 subnegociación, 4 IAC en subnegociación
  char return_string[100] = "";

  // conexión nueva: sustituye a la anterior (un cliente WiFi que desaparece sin cerrar
  // la conexión dejaría el puerto ocupado para siempre)
  if (wifi_tcp_server.hasClient()) {
    WiFiClient new_client = wifi_tcp_server.accept();
    if (wifi_tcp_client && wifi_tcp_client.connected()) {
      wifi_tcp_client.stop();
    }
    wifi_tcp_client = new_client;
    wifi_tcp_client.setNoDelay(true);
    wifi_tcp_write_failures = 0;
    tcp_buffer_index = 0;
    telnet_state = 0;
    wifi_raw_line_reset(WIFI_RAW_SOURCE_TCP);
    #ifdef DEBUG_ETHERNET
      debug.print(F("service_wifi_tcp: client "));
      debug.println(wifi_tcp_client.remoteIP().toString().c_str());
    #endif
  }

  if (!wifi_tcp_client || !wifi_tcp_client.connected()) {
    return;
  }

  // descartar un mensaje a medias si lleva demasiado tiempo sin completarse
  if ((tcp_buffer_index) && ((millis() - last_received_byte) > WIFI_MESSAGE_TIMEOUT_MS)) {
    tcp_buffer_index = 0;
    wifi_raw_line_reset(WIFI_RAW_SOURCE_TCP);
  }

  // como mucho 64 bytes por pasada para no retrasar el control de los motores
  byte bytes_this_pass = 0;
  while ((wifi_tcp_client.available() > 0) && (bytes_this_pass < 64)) {

    byte incoming_byte = wifi_tcp_client.read();
    bytes_this_pass++;
    last_received_byte = millis();

    // negociación telnet: se descarta. IAC (255) va seguido de un comando de un byte, de
    // WILL/WONT/DO/DONT (251-254) + opción, o de SB (250) ... IAC SE (240)
    switch (telnet_state) {
      case 1:
        if ((incoming_byte >= 251) && (incoming_byte <= 254)) {
          telnet_state = 2;
        } else if (incoming_byte == 250) {
          telnet_state = 3;
        } else {
          telnet_state = 0;          // comando de un byte (NOP, AYT...) o IAC IAC: se ignora
        }
        continue;
      case 2:
        telnet_state = 0;
        continue;
      case 3:
        if (incoming_byte == 255) {
          telnet_state = 4;
        }
        continue;
      case 4:
        telnet_state = (incoming_byte == 240) ? 0 : 3;
        continue;
    }
    if (incoming_byte == 255) {
      telnet_state = 1;
      continue;
    }

    // los clientes telnet en modo carácter envían CR NUL: el NUL se descarta
    if (incoming_byte == 0) {
      continue;
    }

    wifi_raw_line_feed(incoming_byte, WIFI_RAW_SOURCE_TCP);

    if ((incoming_byte > 96) && (incoming_byte < 123)) {  // a mayúsculas, como el resto de puertos
      incoming_byte = incoming_byte - 32;
    }

    if ((incoming_byte != 10) && (incoming_byte != 13) && (tcp_buffer_index < COMMAND_BUFFER_SIZE)) {
      tcp_buffer[tcp_buffer_index] = incoming_byte;
      tcp_buffer_index++;
    }

    if (((incoming_byte == 13) || (tcp_buffer_index >= COMMAND_BUFFER_SIZE)) && (tcp_buffer_index > 0)) {
      if (incoming_byte != 13) {
        wifi_raw_line_close(WIFI_RAW_SOURCE_TCP);
      }
      return_string[0] = 0;
      if ((tcp_buffer[0] == '\\') || (tcp_buffer[0] == '/')) {
        process_backslash_command(tcp_buffer, tcp_buffer_index, ETHERNET_PORT0, INCLUDE_RESPONSE_CODE, return_string, SOURCE_CONTROL_PORT);
      } else {
        #ifdef FEATURE_YAESU_EMULATION
          process_yaesu_command(tcp_buffer, tcp_buffer_index, ETHERNET_PORT0, return_string);
        #endif
        #ifdef FEATURE_EASYCOM_EMULATION
          process_easycom_command(tcp_buffer, tcp_buffer_index, ETHERNET_PORT0, return_string);
        #endif
      }
      wifi_tcp_send(return_string, 1);
      tcp_buffer_index = 0;
    }

  }

}

// --------------------------------------------------------------
void wifi_tcp_send(const char * text, byte add_newline){

  // NetworkClient::write() del core espera hasta 1 s por intento y reintenta hasta 10 veces
  // (valores fijos): con un cliente que deja de leer bloquearía el loop ~10 s y saltaría el
  // watchdog. Aquí se comprueba con select() sin espera que el socket admite datos y se envía
  // con MSG_DONTWAIT; si no cabe, la respuesta se descarta. Tras WIFI_TCP_MAX_WRITE_FAILURES
  // fallos seguidos se da el cliente por perdido.

  if (!wifi_tcp_client || !wifi_tcp_client.connected()) {
    return;
  }

  int fd = wifi_tcp_client.fd();
  if (fd < 0) {
    return;
  }

  char buffer[112];
  size_t length = strlen(text);
  if (length > sizeof(buffer) - 3) {
    length = sizeof(buffer) - 3;
  }
  memcpy(buffer, text, length);
  if (add_newline) {
    buffer[length++] = '\r';
    buffer[length++] = '\n';
  }

  fd_set write_set;
  FD_ZERO(&write_set);
  FD_SET(fd, &write_set);
  struct timeval no_wait = {0, 0};

  ssize_t sent = -1;
  if (select(fd + 1, NULL, &write_set, NULL, &no_wait) > 0) {
    sent = send(fd, buffer, length, MSG_DONTWAIT);
  }

  if (sent == (ssize_t)length) {
    wifi_tcp_write_failures = 0;
    return;
  }

  if (++wifi_tcp_write_failures >= WIFI_TCP_MAX_WRITE_FAILURES) {
    control_port->println(F("WiFi: TCP client not reading - disconnected"));
    wifi_tcp_client.stop();
    wifi_tcp_write_failures = 0;
  }

}

// --------------------------------------------------------------
void wifi_tcp_print(char * print_this){

  wifi_tcp_send(print_this, 0);

}

// --------------------------------------------------------------
void service_wifi(){

  service_wifi_connection();
  if (wifi_connected) {
    service_ntp();
  }
  service_wifi_tcp();
  #if defined(FEATURE_WEB_SERVER)
    service_web_server();
  #endif

}

// --------------------------------------------------------------
const char * wifi_status_text(){

  switch (WiFi.status()) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_CONNECT_FAILED: return "FAILED";
    case WL_CONNECTION_LOST: return "LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    default: return "UNKNOWN";
  }

}

// --------------------------------------------------------------
byte wifi_save_credentials(){

  Preferences prefs;
  if (!prefs.begin("wifi", false)) {
    return 0;
  }
  prefs.putString("ssid", wifi_ssid);
  prefs.putString("pass", wifi_password);
  prefs.end();
  return 1;

}

// --------------------------------------------------------------
void wifi_backslash_command(byte input_buffer[], int input_buffer_index, byte source_port, char * return_string){

  /*
    \WI          - estado: red, IP, RSSI, nombre mDNS y sincronización NTP
    \WS<ssid>    - guarda el SSID (respeta mayúsculas; no se aplica hasta \WR)
    \WP<clave>   - guarda la contraseña (respeta mayúsculas; no se muestra)
    \WR          - reconecta con los datos guardados
    \WD          - vuelve al SSID y la contraseña de rotator_settings_esp32.h
  */

  // \W a secas: input_buffer[2] tendría restos del comando anterior
  char subcommand = (input_buffer_index >= 3) ? toupper(input_buffer[2]) : 'I';

  // argumento con mayúsculas originales: la línea cruda del mismo origen tiene que ser
  // exactamente este comando (misma longitud y mismas letras sin distinguir mayúsculas)
  const char * raw_line = wifi_raw[(source_port == CONTROL_PORT0) ? WIFI_RAW_SOURCE_SERIAL : WIFI_RAW_SOURCE_TCP].last_line;
  const char * raw_argument = NULL;
  if (((int)strlen(raw_line) == input_buffer_index) && (input_buffer_index >= 3)) {
    byte matches = 1;
    for (int i = 0; i < input_buffer_index; i++) {
      if (toupper(raw_line[i]) != toupper(input_buffer[i])) {
        matches = 0;
        break;
      }
    }
    if (matches) {
      raw_argument = raw_line + 3;
    }
  }

  if (((subcommand == 'S') || (subcommand == 'P')) && (raw_argument == NULL)) {
    strcpy_P(return_string, (const char*) F("Error: line not available, retry"));
    return;
  }

  switch (subcommand) {

    case 'S':
      if ((strlen(raw_argument) == 0) || (strlen(raw_argument) > 32)) {
        strcpy_P(return_string, (const char*) F("Error: SSID 1-32 chars"));
        return;
      }
      strcpy(wifi_ssid, raw_argument);
      if (wifi_save_credentials()) {
        snprintf(return_string, 96, "WiFi SSID saved: %s (\\WR to apply)", wifi_ssid);
      } else {
        strcpy_P(return_string, (const char*) F("Error: cannot save"));
      }
      return;

    case 'P':
      if (strlen(raw_argument) > 63) {
        strcpy_P(return_string, (const char*) F("Error: password max 63 chars"));
        return;
      }
      strcpy(wifi_password, raw_argument);
      if (wifi_save_credentials()) {
        strcpy_P(return_string, (const char*) F("WiFi password saved (\\WR to apply)"));
      } else {
        strcpy_P(return_string, (const char*) F("Error: cannot save"));
      }
      return;

    case 'R':
      wifi_connected = 0;
      wifi_start_connection();
      snprintf(return_string, 96, "WiFi reconnecting to %s", wifi_ssid);
      return;

    case 'D':
      {
        Preferences prefs;
        if (prefs.begin("wifi", false)) {
          prefs.clear();
          prefs.end();
        }
      }
      wifi_load_credentials();
      snprintf(return_string, 96, "WiFi defaults restored: %s (\\WR to apply)", wifi_ssid);
      return;

    case 'I':
    default:
      if (WiFi.status() == WL_CONNECTED) {
        snprintf(return_string, 96, "WiFi %s SSID:%s IP:%s RSSI:%d %s.local NTP:%s",
          wifi_status_text(), wifi_ssid, WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
          WIFI_HOSTNAME, ntp_synced ? "OK" : "NO");
      } else {
        snprintf(return_string, 96, "WiFi %s SSID:%s", wifi_status_text(), wifi_ssid);
      }
      return;

  }

}

#endif // FEATURE_WIFI
