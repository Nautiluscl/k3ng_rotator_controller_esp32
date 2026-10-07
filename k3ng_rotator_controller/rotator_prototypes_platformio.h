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

#endif // rotator_prototypes_platformio_h
