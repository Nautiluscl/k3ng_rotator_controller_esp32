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

## 2026-10-07: correcciones de la etapa 2 tras la revisión

Correcciones a partir de la revisión con un subagente Sonnet:
- `Adafruit_BNO055::begin()` espera el `CHIP_ID` sin límite de tiempo después del reset por
  software. Si el sensor deja de responder justo en ese momento, el loop se colgaría con los
  relés activos. Ahora `bno055_chip_present()` lee el registro 0x00 por I2C y solo se llama
  a `begin()` si responde 0xA0. El watchdog de la etapa 5 cubre el riesgo que queda.
- El suavizado se sembraba con la lectura cruda, sin corrección ni offset, y no se volvía a
  sembrar tras un fallo. Ahora se siembra con la primera lectura buena ya corregida y se
  vuelve a sembrar después de cada `FAULT`.

## 2026-10-07: etapa 3, WiFi, puerto serie virtual TCP y hora por NTP

**Cambios**
- `FEATURE_WIFI` (solo ESP32), en el nuevo archivo `rotator_esp32_wifi.h`, que se incluye
  al final del `.ino`:
  - Modo estación con conexión **no bloqueante**: el rotor funciona por USB aunque no haya
    WiFi. Reconexión automática y reintento forzado cada 15 s.
  - `WiFi.setSleep(false)` para que la latencia por comando sea baja.
  - mDNS: `rotor.local`, con los servicios `_telnet._tcp` y `_http._tcp`.
  - Servidor TCP en el puerto 23 con los mismos intérpretes que el puerto USB (GS-232,
    Easycom y `\`), escrito de cero: el `service_ethernet()` original no sirve con
    `WiFiServer`, porque `available()` tiene otra semántica en el ESP32. Admite un cliente;
    uno nuevo sustituye al anterior, para que un cliente WiFi caído no bloquee el puerto.
    Filtra la negociación telnet (IAC) y procesa como mucho 64 bytes por pasada.
  - NTP con `configTime()` (SNTP en segundo plano). Cuando hay hora válida se vuelca a
    TimeLib (`setTime`) y `clock_status` pasa a `NTP_SYNC`. Resincroniza cada hora.
  - Comandos `\WI` (estado), `\WS<ssid>`, `\WP<clave>`, `\WR` (reconectar) y `\WD`
    (valores por defecto). Las credenciales se guardan en `Preferences`.
- El firmware pasa a mayúsculas todo lo que recibe, lo que estropearía el SSID y la
  contraseña. Se guarda aparte una copia de la línea con las mayúsculas originales
  (`wifi_raw_line_feed()`), alimentada desde el puerto serie y desde TCP, y `\WS`/`\WP`
  leen de esa copia.
- `COMMAND_BUFFER_SIZE` pasa de 50 a 80 en el perfil ESP32, para admitir contraseñas WPA
  de hasta 63 caracteres.
- Se activan `FEATURE_CLOCK`, `FEATURE_SUN_TRACKING` y `FEATURE_MOON_TRACKING`.
- `rotator_dependencies.h`: `FEATURE_WIFI` exige ESP32 y es incompatible con
  `FEATURE_ETHERNET` y con `FEATURE_ANCILLARY_PIN_CONTROL`, que también usa `\W`.

**Problemas encontrados**
- El conversor `.ino` de PlatformIO no genera prototipos de funciones definidas con sangría
  (`update_moon_position`) ni de algunas que devuelven `char *`. Se crea
  `rotator_prototypes_platformio.h` con los prototipos que faltan.
- El compilador del ESP32 trata como error una función con valor de retorno que puede
  terminar sin `return` (`-Werror=return-type`). Se añaden en `clock_status_string()` y
  `days_in_month()`.
- La librería `WiFi` del core 3.3.12 incluye `"Network.h"` entre comillas y PlatformIO no
  resuelve la dependencia. La librería se llama `Networking`, no `Network`. Se declaran las
  librerías del core en `lib_deps` y se añade la ruta de `Network/src` en `build_flags`.

**Resultado:** compila sin avisos. RAM al 16,0 % y flash al 55,3 % (partición de app de
1,9 MB).

## 2026-10-07: correcciones de la etapa 3 tras la revisión

Correcciones a partir de la revisión con un subagente Sonnet:
- `\W` sin subcomando leía un byte sobrante del comando anterior y podía ejecutar `\WR` o
  `\WD`, que borra las credenciales. Ahora `\W` a secas equivale a `\WI`.
- `\WP` guardaba una contraseña vacía cuando la copia cruda de la línea no coincidía con el
  comando. Ahora la copia cruda tiene que coincidir con el comando completo: misma longitud
  y mismas letras, sin distinguir mayúsculas. Si no coincide, `\WS` y `\WP` dan error y no
  guardan nada.
- La copia cruda se desfasaba del buffer de comandos cuando este se vaciaba (por timeout,
  cliente nuevo o buffer lleno). Ahora hay una copia por origen (serie y TCP) y se
  reinicia o cierra en los mismos puntos que el buffer.
- Los clientes telnet en modo carácter envían CR NUL, y el NUL acababa como primer byte del
  comando siguiente. Ahora se descarta.
- La negociación telnet se filtra con una pequeña máquina de estados: comandos de un byte,
  WILL/WONT/DO/DONT con opción y subnegociación SB…SE.
- `println()` al cliente TCP podía bloquear el loop unos 3 s si el cliente dejaba de leer.
  Se baja el tiempo de escritura a 50 ms por intento con `setConnectionTimeout()`.
- La reconexión forzada solo se hace en estados de fallo, para no cortar un intento de
  conexión en curso.
- Sin NTP durante 24 h, `clock_status` vuelve a `FREE_RUNNING`.

## 2026-10-07: etapa 4, servidor web para smartphone

**Cambios**
- `FEATURE_WEB_SERVER`, en `rotator_esp32_web.h`, usa el `WebServer` del core, sin
  librerías externas. Página única de 7,7 KB con HTML, CSS y JS embebidos, sin recursos
  externos, porque el rotor puede estar en una red sin internet. Diseño oscuro, botones
  grandes y márgenes seguros para iPhone. Dirección: `http://rotor.local/`.
- Funciones pedidas en `requerimientos.txt`:
  - **Monitorización:** azimut, elevación, estado de movimiento por eje, uptime (contador
    de 64 bits), RSSI con valoración, SSID, IP, hora UTC, estado del BNO055 y locator.
  - **Movimiento rápido:** 4 botones en cruz (CW, CCW, arriba y abajo), que mueven el eje
    mientras se mantienen pulsados.
  - **Seguimiento del Sol y de la Luna:** botones que se activan y desactivan con un toque,
    con la posición actual de cada uno. Exigen hora NTP y BNO055 operativo.
  - **STOP:** parada inmediata (`REQUEST_KILL`) de ambos ejes, que además desactiva
    cualquier seguimiento.
- API: `GET /api/status`, y `POST /api/move`, `/api/stop`, `/api/track` y `/api/locator`.
- **Hombre muerto:** mientras se pulsa un botón, la página repite la orden cada 250 ms. Si
  el firmware pasa 1 s sin recibirla (móvil bloqueado, WiFi caído, pestaña cerrada), detiene
  el eje. La página también suelta el botón al perder el foco o al cambiar de pestaña.
- **Aislamiento del control de motores:** `WebServer::handleClient()` puede bloquear hasta
  5 s con un cliente lento, y esos tiempos son fijos en el core. Por eso el servidor corre
  en una tarea de FreeRTOS en el núcleo 0:
  - Las órdenes llegan al `loop()` por una cola, y es el loop quien las aplica con
    `submit_request()`.
  - El loop publica cada 200 ms una copia del estado, protegida con un spinlock.
  - La tarea web nunca toca el estado del rotor.
- Un movimiento manual desactiva el seguimiento, porque si no el seguimiento devolvería el
  rotor a su objetivo. Se respeta `ROTATIONAL_AND_CONFIGURATION_CMD_IGNORE_TIME_MS` tras el
  arranque.
- Contraseña opcional con autenticación básica (`WEB_SERVER_PASSWORD`; vacía por defecto).
- **Ubicación persistente:** en el firmware original la latitud y la longitud solo viven en
  RAM y vuelven al valor por defecto, que está en Pensilvania, en cada arranque. Ahora
  `\G<locator>` y la web la guardan en `Preferences` y se restaura al arrancar.
- `tools/web_preview.py`: extrae la página del firmware y la sirve con un rotor simulado,
  para revisar la interfaz sin ESP32. También se usó para probar la API y el hombre muerto.

**Pruebas:** compilación sin avisos y sintaxis JS validada con Node. Contra la API
simulada se probaron la página, el estado, el movimiento, la parada por hombre muerto a 1,5 s,
el seguimiento, la parada general y el locator válido e inválido.

**Resultado:** RAM al 16,3 % y flash al 57,4 %.

## 2026-10-07: etapa 5, robustez

**Cambios**
- **Watchdog del loop** (`OPTION_ESP32_LOOP_WATCHDOG`): se activa al final de `setup()` con
  `enableLoopWDT()`. Si una iteración de `loop()` tarda más de 5 s
  (`CONFIG_ESP_TASK_WDT_TIMEOUT_S` del core), el ESP32 se reinicia. Con el reinicio los GPIO
  vuelven a ser entradas y los relés se sueltan, siempre que los drivers lleven pull-down.
  Se revisaron los `delay()` largos del firmware: todos pertenecen a funciones desactivadas
  en este perfil (encoder A2 y prueba de pantalla LCD).
- **Motivo del último reinicio** (`esp_reset_reason()`): se muestra por el puerto de control
  al arrancar y en la web. Un `BROWNOUT` repetido apunta a que los motores hunden la
  alimentación del ESP32. El detector de brownout ya viene activado en el core.
- **Reinicio por `\Q`:** el original vuelve a llamar a `setup()` desde el loop. En el ESP32
  eso crearía una segunda tarea web y una segunda cola sobre los mismos objetos. Se
  sustituye por `ESP.restart()`.

**Resultado:** compila sin avisos. RAM al 16,3 % y flash al 57,4 %.
