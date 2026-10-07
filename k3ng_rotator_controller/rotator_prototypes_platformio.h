/* rotator_prototypes_platformio.h

   Prototipos que el conversor .ino -> .cpp de PlatformIO no genera por sí solo.
   No detecta funciones definidas con sangría (dentro de bloques #if indentados) ni
   algunas que devuelven punteros con la forma "char * nombre()". Arduino IDE sí las
   detecta, por eso el código original compila allí sin estos prototipos.

   Si al activar una función aparece "'xxx' was not declared in this scope" para una
   función que sí existe en el .ino, hay que añadir aquí su prototipo.
*/

#if !defined(rotator_prototypes_platformio_h)
#define rotator_prototypes_platformio_h

#if defined(FEATURE_CLOCK)
  char * timezone_modified_clock_string();
  char * zulu_clock_string();
#endif

#if defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING)
  char *coordinates_to_maidenhead(float latitude_degrees, float longitude_degrees);
#endif

#if defined(FEATURE_MOON_TRACKING)
  void update_moon_position();
#endif

// funciones de rotator_esp32_wifi.h y rotator_esp32_web.h (definidas al final del sketch)
#if defined(FEATURE_WIFI)
  void initialize_wifi();
  void service_wifi();
  void wifi_raw_line_feed(byte incoming_byte, byte source);
  void wifi_tcp_print(char * print_this);
  void wifi_tcp_send(const char * text, byte add_newline);
  void wifi_backslash_command(byte input_buffer[], int input_buffer_index, byte source_port, char * return_string);
#endif

#if defined(FEATURE_WIFI) && (defined(FEATURE_MOON_TRACKING) || defined(FEATURE_SUN_TRACKING))
  void station_location_load();
  void station_location_save();
  byte station_location_set_from_grid(const char * grid_in, char * grid_out);
#endif

#if defined(FEATURE_WEB_SERVER)
  void initialize_web_server();
  void service_web_server();
#endif

#if defined(ARDUINO_ARCH_ESP32)
  const char * esp32_reset_reason_text();
  void esp32_report_reset_reason();
#endif

#if defined(FEATURE_SIMULATION)
  extern byte simulation_active;
  void initialize_simulation();
  void service_simulation();
  void simulation_override_azimuth();
  void simulation_override_elevation();
  byte simulation_is_motor_pin(uint8_t pin);
  byte simulation_inactive_value(uint8_t pin);
  void simulation_set(byte on, byte save);
  void simulation_backslash_command(byte input_buffer[], int input_buffer_index, char * return_string);
  #define SIMULATION_IS_ACTIVE() (simulation_active)
#else
  #define SIMULATION_IS_ACTIVE() (0)
#endif

// Elevación bloqueada por el BNO055: solo con OPTION_BNO055_FAULT_STOPS_ELEVATION y nunca en
// simulación (allí no se usa el sensor)
#if defined(FEATURE_EL_POSITION_BNO055) && defined(OPTION_BNO055_FAULT_STOPS_ELEVATION)
  #define BNO055_BLOCKS_ELEVATION() ((bno055_state != BNO055_STATE_OK) && !SIMULATION_IS_ACTIVE())
#else
  #define BNO055_BLOCKS_ELEVATION() (0)
#endif

#endif // rotator_prototypes_platformio_h
