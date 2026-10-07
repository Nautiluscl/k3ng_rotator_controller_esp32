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

WiFiServer wifi_tcp_server(WIFI_TCP_PORT);
WiFiClient wifi_tcp_client;

char wifi_ssid[33] = "";
char wifi_password[65] = "";
byte wifi_connected = 0;
byte wifi_mdns_started = 0;
unsigned long wifi_last_connect_attempt = 0;
unsigned long wifi_connected_since = 0;

byte ntp_synced = 0;
unsigned long ntp_last_sync = 0;
unsigned long ntp_last_check = 0;

// Copia de la última línea recibida conservando mayúsculas y minúsculas. El firmware pasa a
// mayúsculas todo lo que entra, lo que estropearía el SSID y la contraseña de \WS y \WP.
char wifi_raw_line[WIFI_RAW_LINE_SIZE] = "";
byte wifi_raw_line_index = 0;
char wifi_raw_last_line[WIFI_RAW_LINE_SIZE] = "";

// --------------------------------------------------------------
void wifi_raw_line_feed(byte incoming_byte){

  if ((incoming_byte == 13) || (incoming_byte == 10)) {
    if (wifi_raw_line_index > 0) {
      wifi_raw_line[wifi_raw_line_index] = 0;
      strcpy(wifi_raw_last_line, wifi_raw_line);
      wifi_raw_line_index = 0;
    }
    return;
  }
  if (wifi_raw_line_index < (WIFI_RAW_LINE_SIZE - 1)) {
    wifi_raw_line[wifi_raw_line_index] = incoming_byte;
    wifi_raw_line_index++;
  }

}

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

  // el driver reintenta solo (setAutoReconnect), pero si se queda en un estado de fallo se fuerza
  if ((millis() - wifi_last_connect_attempt) > WIFI_RECONNECT_INTERVAL_MS) {
    wifi_start_connection();
  }

}

// --------------------------------------------------------------
void service_wifi_tcp(){

  static byte tcp_buffer[COMMAND_BUFFER_SIZE];
  static int tcp_buffer_index = 0;
  static unsigned long last_received_byte = 0;
  static byte telnet_iac_skip = 0;
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
    tcp_buffer_index = 0;
    telnet_iac_skip = 0;
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
  }

  // como mucho 64 bytes por pasada para no retrasar el control de los motores
  byte bytes_this_pass = 0;
  while ((wifi_tcp_client.available() > 0) && (bytes_this_pass < 64)) {

    byte incoming_byte = wifi_tcp_client.read();
    bytes_this_pass++;
    last_received_byte = millis();

    // negociación telnet (IAC = 255, seguido de dos bytes): se ignora
    if (telnet_iac_skip) {
      telnet_iac_skip--;
      continue;
    }
    if (incoming_byte == 255) {
      telnet_iac_skip = 2;
      continue;
    }

    wifi_raw_line_feed(incoming_byte);

    if ((incoming_byte > 96) && (incoming_byte < 123)) {  // a mayúsculas, como el resto de puertos
      incoming_byte = incoming_byte - 32;
    }

    if ((incoming_byte != 10) && (incoming_byte != 13) && (tcp_buffer_index < COMMAND_BUFFER_SIZE)) {
      tcp_buffer[tcp_buffer_index] = incoming_byte;
      tcp_buffer_index++;
    }

    if (((incoming_byte == 13) || (tcp_buffer_index >= COMMAND_BUFFER_SIZE)) && (tcp_buffer_index > 0)) {
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
      wifi_tcp_client.println(return_string);
      tcp_buffer_index = 0;
    }

  }

}

// --------------------------------------------------------------
void wifi_tcp_print(char * print_this){

  if (wifi_tcp_client && wifi_tcp_client.connected()) {
    wifi_tcp_client.print(print_this);
  }

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
void wifi_backslash_command(byte input_buffer[], int input_buffer_index, char * return_string){

  /*
    \WI          - estado: red, IP, RSSI, nombre mDNS y sincronización NTP
    \WS<ssid>    - guarda el SSID (respeta mayúsculas; no se aplica hasta \WR)
    \WP<clave>   - guarda la contraseña (respeta mayúsculas; no se muestra)
    \WR          - reconecta con los datos guardados
    \WD          - vuelve al SSID y la contraseña de rotator_settings_esp32.h
  */

  // argumento con mayúsculas originales: la línea cruda empieza por \WS o \WP
  const char * raw_argument = "";
  if ((strlen(wifi_raw_last_line) >= 3) && ((wifi_raw_last_line[0] == '\\') || (wifi_raw_last_line[0] == '/')) && (toupper(wifi_raw_last_line[1]) == 'W') && (toupper(wifi_raw_last_line[2]) == toupper(input_buffer[2]))) {
    raw_argument = wifi_raw_last_line + 3;
  }

  switch (toupper(input_buffer[2])) {

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
