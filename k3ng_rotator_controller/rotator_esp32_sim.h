/* rotator_esp32_sim.h

   Modo simulación (FEATURE_SIMULATION): rotor virtual para probar la conectividad y el
   control desde otros programas (PstRotator, Gpredict, hamlib...) sin motores ni sensores.

   Con la simulación activa:
     - las salidas de motor (rotate_cw, rotate_ccw, rotate_up, rotate_down y sus variantes
       PWM) se mantienen inactivas: el rotor real no se mueve;
     - la posición no se lee de los sensores: se integra a partir del estado de rotación que
       decide el propio firmware (CW/CCW/UP/DOWN) a SIMULATION_AZ_DEG_PER_SEC y
       SIMULATION_EL_DEG_PER_SEC, con topes en los límites mecánicos configurados;
     - todo lo demás funciona igual: comandos GS-232/Easycom por USB y TCP, web, seguimiento
       de Sol y Luna, paradas y hombre muerto.

   Comandos:   \XV1  activa     \XV0  desactiva     \XV  estado
   El estado se guarda en flash (Preferences) y se conserva tras reiniciar.

   Como los demás módulos ESP32, se incluye al final de k3ng_rotator_controller.ino.
*/

#if defined(FEATURE_SIMULATION)

byte simulation_active = 0;
float simulation_raw_azimuth = 0;          // misma escala que raw_azimuth (starting_point .. +capability)
float simulation_elevation = 0;
unsigned long simulation_last_update = 0;

// --------------------------------------------------------------
byte simulation_is_motor_pin(uint8_t pin){

  // salidas que mueven motores; los pines a 0 están desactivados y no cuentan
  if (pin == 0) {
    return 0;
  }
  if ((pin == rotate_cw) || (pin == rotate_ccw) || (pin == rotate_cw_ccw) ||
      (pin == rotate_cw_pwm) || (pin == rotate_ccw_pwm) || (pin == rotate_cw_ccw_pwm)) {
    return 1;
  }
  #if defined(FEATURE_ELEVATION_CONTROL)
    if ((pin == rotate_up) || (pin == rotate_down) || (pin == rotate_up_or_down) ||
        (pin == rotate_up_pwm) || (pin == rotate_down_pwm) || (pin == rotate_up_down_pwm)) {
      return 1;
    }
  #endif
  return 0;

}

// --------------------------------------------------------------
byte simulation_inactive_value(uint8_t pin){

  #if defined(FEATURE_ELEVATION_CONTROL)
    if ((pin == rotate_up) || (pin == rotate_down) || (pin == rotate_up_or_down) ||
        (pin == rotate_up_pwm) || (pin == rotate_down_pwm) || (pin == rotate_up_down_pwm)) {
      return ROTATE_PIN_EL_INACTIVE_VALUE;
    }
  #endif
  return ROTATE_PIN_AZ_INACTIVE_VALUE;

}

// --------------------------------------------------------------
void simulation_release_motor_outputs(){

  // deja todas las salidas de motor en reposo (al entrar en simulación)
  uint8_t pins[] = {rotate_cw, rotate_ccw, rotate_cw_ccw, rotate_cw_pwm, rotate_ccw_pwm, rotate_cw_ccw_pwm
    #if defined(FEATURE_ELEVATION_CONTROL)
      , rotate_up, rotate_down, rotate_up_or_down, rotate_up_pwm, rotate_down_pwm, rotate_up_down_pwm
    #endif
  };
  for (byte i = 0; i < sizeof(pins); i++) {
    if (pins[i]) {
      digitalWrite(pins[i], simulation_inactive_value(pins[i]));
    }
  }

}

// --------------------------------------------------------------
void simulation_set(byte on, byte save){

  // al cambiar de modo se para todo: la posición "salta" entre la real y la simulada
  submit_request(AZ, REQUEST_KILL, 0, DBG_SIMULATION);
  #if defined(FEATURE_ELEVATION_CONTROL)
    submit_request(EL, REQUEST_KILL, 0, DBG_SIMULATION);
  #endif

  if (on && !simulation_active) {
    // se parte de la última posición conocida, acotada al rango de giro
    simulation_raw_azimuth = raw_azimuth;
    if (simulation_raw_azimuth < configuration.azimuth_starting_point) {
      simulation_raw_azimuth = configuration.azimuth_starting_point;
    }
    if (simulation_raw_azimuth > (configuration.azimuth_starting_point + configuration.azimuth_rotation_capability)) {
      simulation_raw_azimuth = configuration.azimuth_starting_point + configuration.azimuth_rotation_capability;
    }
    #if defined(FEATURE_ELEVATION_CONTROL)
      simulation_elevation = elevation;
      if (simulation_elevation < 0) { simulation_elevation = 0; }
      if (simulation_elevation > ELEVATION_MAXIMUM_DEGREES) { simulation_elevation = ELEVATION_MAXIMUM_DEGREES; }
    #endif
    simulation_last_update = millis();
  }

  simulation_active = on;
  if (on) {
    simulation_release_motor_outputs();
  }

  if (save) {
    Preferences prefs;
    if (prefs.begin("sim", false)) {
      prefs.putUChar("on", on);
      prefs.end();
    }
  }

}

// --------------------------------------------------------------
void initialize_simulation(){

  Preferences prefs;
  byte on = SIMULATION_ACTIVE_AT_BOOT;
  if (prefs.begin("sim", true)) {
    if (prefs.isKey("on")) {
      on = prefs.getUChar("on", on);
    }
    prefs.end();
  }
  if (on) {
    simulation_set(1, 0);
    control_port->println(F("SIMULATION MODE ACTIVE - motor outputs disabled (\\XV0 to exit)"));
  }

}

// --------------------------------------------------------------
void service_simulation(){

  // integra la posición virtual según lo que el firmware está haciendo con cada eje

  if (!simulation_active) {
    return;
  }

  unsigned long now = millis();
  float dt = (now - simulation_last_update) / 1000.0;
  simulation_last_update = now;
  if (dt > 0.5) {
    dt = 0.5;      // tras una pausa larga del loop no se dan saltos grandes
  }

  float az_min = configuration.azimuth_starting_point;
  float az_max = configuration.azimuth_starting_point + configuration.azimuth_rotation_capability;
  switch (current_az_state()) {
    case ROTATING_CW: simulation_raw_azimuth += SIMULATION_AZ_DEG_PER_SEC * dt; break;
    case ROTATING_CCW: simulation_raw_azimuth -= SIMULATION_AZ_DEG_PER_SEC * dt; break;
  }
  if (simulation_raw_azimuth > az_max) { simulation_raw_azimuth = az_max; }     // tope mecánico
  if (simulation_raw_azimuth < az_min) { simulation_raw_azimuth = az_min; }

  #if defined(FEATURE_ELEVATION_CONTROL)
    switch (current_el_state()) {
      case ROTATING_UP: simulation_elevation += SIMULATION_EL_DEG_PER_SEC * dt; break;
      case ROTATING_DOWN: simulation_elevation -= SIMULATION_EL_DEG_PER_SEC * dt; break;
    }
    if (simulation_elevation > ELEVATION_MAXIMUM_DEGREES) { simulation_elevation = ELEVATION_MAXIMUM_DEGREES; }
    if (simulation_elevation < 0) { simulation_elevation = 0; }
  #endif

}

// --------------------------------------------------------------
void simulation_override_azimuth(){

  // llamada al final de la medición de read_azimuth(): sustituye la lectura del sensor
  if (simulation_active) {
    raw_azimuth = simulation_raw_azimuth;
    convert_raw_azimuth_to_real_azimuth();
  }

}

// --------------------------------------------------------------
void simulation_override_elevation(){

  #if defined(FEATURE_ELEVATION_CONTROL)
    if (simulation_active) {
      elevation = simulation_elevation;
    }
  #endif

}

// --------------------------------------------------------------
void simulation_backslash_command(byte input_buffer[], int input_buffer_index, char * return_string){

  if (input_buffer_index >= 4) {
    if (input_buffer[3] == '1') {
      simulation_set(1, 1);
    } else if (input_buffer[3] == '0') {
      simulation_set(0, 1);
    } else {
      strcpy_P(return_string, (const char*) F("Usage: \\XV1 on, \\XV0 off, \\XV status"));
      return;
    }
  }
  if (simulation_active) {
    snprintf(return_string, 96, "Simulation ON (%.0f deg/s az, %.0f deg/s el) - motor outputs disabled",
      (double)SIMULATION_AZ_DEG_PER_SEC, (double)SIMULATION_EL_DEG_PER_SEC);
  } else {
    strcpy_P(return_string, (const char*) F("Simulation OFF"));
  }

}

#endif // FEATURE_SIMULATION
