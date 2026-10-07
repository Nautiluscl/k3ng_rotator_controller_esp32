# Bitácora de desarrollo: port a ESP32

Registro de las etapas del port del K3NG Rotator Controller a ESP32-WROOM-32 con WiFi,
servidor web y sensor BNO055. Las entradas van en orden cronológico; cada etapa termina
con una compilación limpia y un commit en la rama `esp32-port`.

Documentos relacionados:
- `PLAN_PORT_ESP32.md`: plan técnico de partida
- `requerimientos.txt`: requisitos de esta implementación

---

## 2026-10-07: etapa 0, análisis y plan

- Se analiza el firmware (`k3ng_rotator_controller/`, unas 29 000 líneas, sketch Arduino
  para AVR). Ya incluye un servidor TCP (`FEATURE_ETHERNET` + `service_ethernet()`) que
  pasa los bytes a los intérpretes GS-232, Easycom y `\`. Ese camino se reutiliza para el
  puerto serie virtual por WiFi.
- El azimut con HH-12 ya está soportado (`FEATURE_AZ_POSITION_HH12_AS5045_SSI`). El BNO055
  no lo está y se añade siguiendo el patrón del ADXL345 de Adafruit.
- Requisitos fijados en `requerimientos.txt`:
  - Solo ESP32-WROOM-32 estándar; no se mantiene la compilación para Arduino Mega.
  - Servidor web ligero para smartphone: monitorización, 4 botones de movimiento,
    seguimiento de Sol y Luna, y parada.
  - Pruebas de código con subagentes Sonnet.
  - Implementación por etapas en el fork `Nautiluscl/k3ng_rotator_controller_esp32`.

## 2026-10-07: etapa 1, estructura PlatformIO y compilación base

**Cambios**
- `platformio.ini` (nuevo): entorno `esp32` (`esp32dev`), `src_dir` en la carpeta del
  sketch y `lib_dir = libraries`. Se excluyen `Nextion/` y `rotator_k3ngdisplay.cpp`.
  Partición `min_spiffs.csv`, para dejar sitio a OTA en el futuro.
- Nuevo perfil `HARDWARE_ESP32_WIFI`, activado desde `build_flags`:
  - `rotator_features_esp32.h`, `rotator_pins_esp32.h` y `rotator_settings_esp32.h`,
    copiados de los genéricos y adaptados.
  - Enlazado en `rotator_hardware.h` (rama `ARDUINO_ARCH_ESP32` y `HARDWARE_CUSTOM`), en
    el `.ino`, en `rotator_debug.h` y en `rotator_k3ngdisplay.h`.
- Mapa de pines (detalle en `rotator_pins_esp32.h`):
  - Relés CW, CCW, UP y DOWN en GPIO 25, 26, 32 y 33.
  - HH-12: CLK en GPIO 18, CS en 17 y DO en 19.
  - I2C: SDA en GPIO 21 y SCL en 22.
  - Se evitan los pines de flash, los de arranque y GPIO14, que emite PWM al arrancar.
- Azimut: HH-12 en lugar de potenciómetro. Rango inicial de 0° a 360°
  (`AZIMUTH_STARTING_POINT = 0`, `ROTATION_CAPABILITY = 360`), propio de un encoder
  absoluto de una vuelta.
- Ajustes de plataforma en el `.ino`:
  - `avr/pgmspace.h` solo en AVR.
  - `TWBR` sustituido por `Wire.setClock()`.
  - El chequeo de memoria libre de `DEBUG_DUMP` usa `ESP.getFreeHeap()`.
  - `get_analog_pin()` no usa `A1`/`A2`, que el core ESP32 no define.
- EEPROM emulada: `EEPROM.begin(ESP32_EEPROM_SIZE)` en la primera lectura y
  `EEPROM.commit()` al guardar. Un `static_assert` comprueba que la configuración cabe.
- `rotator_dependencies.h`: `#error` si se activan en ESP32 funciones no adaptadas
  (encoders incrementales, entradas de pulsos, steppers, LCD y seguimiento de satélites).

**Problemas encontrados**
- El fork activaba en `rotator_settings.h` `FEATURE_DS3231_RTC` y dos steppers con
  enable en los pines 8 y 9, que en el ESP32 pertenecen a la flash. Se desactivan en el
  perfil ESP32.
- PlatformIO genera los prototipos del `.ino` al principio del archivo sin respetar los
  `#if`, y el prototipo de `read_magnetometer_raw()` quedaba antes de su tipo de retorno.
  Se movió `struct MagnetometerReading` a `rotator.h`, con guarda.
- `rotator_k3ngdisplay.h` fuerza `FEATURE_4_BIT_LCD_DISPLAY` y el `.cpp` se compila
  siempre, lo que exige `LiquidCrystal.h`. Se excluye del build en este perfil.

**Resultado:** compila sin errores ni avisos. RAM al 7,4 % y flash al 18,5 %.

## 2026-10-07: etapa 2, elevación con Bosch BNO055

**Cambios**
- Nueva función `FEATURE_EL_POSITION_BNO055`, activa en el perfil ESP32 junto a
  `FEATURE_ELEVATION_CONTROL`. Sigue el patrón del ADXL345 de Adafruit: include, instancia,
  `begin()` en `initialize_peripherals()` y bloque en `read_elevation()`.
- El sensor trabaja en modo **IMUPLUS** (acelerómetro y giróscopo, sin magnetómetro).
  La elevación se calcula como `atan2(numerador, denominador)` sobre `VECTOR_GRAVITY`.
  Los ejes y el signo se configuran con macros en `rotator_settings_esp32.h`
  (`BNO055_ELEVATION_NUMERATOR/DENOMINATOR/INVERT`).
- Sobre la lectura se aplican la corrección (`FEATURE_ELEVATION_CORRECTION`), el offset de
  configuración y un suavizado propio en `float`. El suavizado original guarda el valor
  anterior en un `unsigned int` y perdería los decimales.
- I2C a 100 kHz con `Wire.setTimeOut()`, porque el BNO055 usa clock stretching. Reset por
  hardware opcional con `BNO055_RESET_PIN`.
- Detección de fallos:
  - Un vector de gravedad fuera de 7 a 12,5 m/s² cuenta como lectura inválida (con el
    sensor desconectado, la librería devuelve ceros).
  - Tras `BNO055_FAIL_THRESHOLD` lecturas inválidas seguidas, el sensor pasa a `FAULT` y
    se detiene la elevación (`REQUEST_KILL`, `DBG_BNO055_SENSOR_FAULT`).
  - Mientras el sensor no esté operativo, se cancela cualquier orden de elevación.
  - Cada 10 s se reintenta la inicialización, solo con el rotor parado, porque `begin()`
    puede bloquear cerca de 1 s.
- Calibración, con offsets guardados en `Preferences` (namespace `bno055`) y restaurados al
  arrancar:
  - `\XB`: muestra estado, nivel de calibración (SYS, G, A), si hay offsets restaurados y
    la elevación sin procesar.
  - `\XBS`: guarda los offsets; exige acelerómetro y giróscopo en nivel 3.
  - `\XBC`: borra los offsets guardados.
- `rotator_dependencies.h`: el BNO055 se añade a las listas de sensores válidos y de
  activación de Wire, y es incompatible con otros sensores de elevación.

**Resultado:** compila sin avisos. RAM al 7,5 % y flash al 19,1 %.
