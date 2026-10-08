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
- `println()` al cliente TCP podía bloquear el loop si el cliente dejaba de leer. *Esta
  primera corrección (`setConnectionTimeout()` a 50 ms) resultó ineficaz; ver la revisión
  final.*
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

## 2026-10-07: correcciones de la etapa 4 tras la revisión

Correcciones a partir de la revisión con un subagente Sonnet:
- **Seguimiento con el sensor recién caído:** la tarea web validaba la hora, el BNO055 y el
  arranque con una copia del estado de hasta 200 ms de antigüedad. Ahora el loop lo vuelve a
  comprobar con el estado real antes de activar el seguimiento. Además, cuando el BNO055
  entra en `FAULT` se desactiva cualquier seguimiento.
- **Keepalive atrasado:** el navegador usa varias conexiones, y un keepalive en vuelo podía
  llegar después del release y volver a mover el eje durante 1 s. Ahora cada carga de la
  página tiene un `sid` aleatorio y cada pulsación un `seq` creciente. El firmware descarta
  las órdenes de una pulsación ya soltada.
- **STOP con prioridad:** ya no pasa por la cola, que podía estar llena. La tarea web levanta
  una marca y el loop la atiende antes que cualquier otra orden.
- **Multitouch:** soltar un botón que ya no es el activo no detiene el que sigue pulsado.
- **Sin conexión:** las peticiones de la página tienen un timeout de 3 s. Si la WiFi cae con
  una conexión colgada, la página sigue consultando y avisa "Sin conexión".
- **Hombre muerto:** cuando vence, el eje solo se para si sigue moviéndose en la dirección
  que pidió la web. Si otro puerto lo ha tomado entretanto, no se interfiere.
- **Locator:** las funciones `isdigit`, `isalpha` y `toupper` reciben `unsigned char`, porque
  un byte UTF-8 negativo es comportamiento indefinido.
- `tools/web_preview.py` aplica la misma regla de `sid`/`seq` y se probó el descarte del
  keepalive atrasado.

## 2026-10-07: revisión final (etapa 5, web y README)

Correcciones a partir de la revisión con un subagente Sonnet:
- **Escritura TCP bloqueante (riesgo de reinicio por watchdog):** `NetworkClient::write()`
  del core espera hasta 1 s fijo por intento y reintenta hasta 10 veces. El tiempo que
  configura `setConnectionTimeout()` no limita esa espera, así que la corrección de la
  etapa 3 no servía. Con un cliente que deja de leer, una respuesta podía bloquear el loop
  unos 10 s y el watchdog reiniciaba el ESP32. Ahora `wifi_tcp_send()` comprueba con
  `select()` sin espera que el socket admite datos y envía con `send(..., MSG_DONTWAIT)`.
  Si no cabe, la respuesta se descarta, y tras 3 fallos seguidos se desconecta al cliente.
- **STOP desde otro móvil:** el siguiente keepalive del móvil que mantenía pulsado volvía a
  arrancar el eje. Ahora la página marca con `first=1` la primera orden de cada pulsación,
  y un keepalive (`first=0`) solo mantiene un movimiento web en curso: nunca arranca un eje
  parado por STOP, por el hombre muerto o por un release.
- **Clientes sin sesión** (curl, scripts): con `sid=0` el filtro descartaba todas las
  órdenes. Ahora `sid=0` significa sin filtro, y cada orden cuenta como primera.
- **STOP frente a la cola:** un seguimiento encolado justo antes de STOP se activaba
  después. Ahora STOP vacía la cola (`xQueueReset`).
- **Hombre muerto:** vuelve a parar siempre el eje al vencer. La versión anterior no lo
  paraba si otro puerto parecía haber tomado el eje, pero con arranque suave un cambio de
  sentido pasa por estados del sentido contrario más de 1 s y el eje podía quedar sin
  control. Es preferible parar un movimiento ajeno a dejar uno propio sin control.
- README: la tabla de la API documenta `axis` y explica que `sid`, `seq` y `first` son
  opcionales para scripts.

**Riesgo residual conocido:** `Adafruit_BNO055::begin()` espera el `CHIP_ID` sin límite de
tiempo después de su reset por software. Antes de llamarlo se sondea el chip, así que solo se
colgaría si el sensor desaparece en los pocos milisegundos entre el sondeo y `begin()`. En el
loop terminaría en reinicio por watchdog; en `setup()`, que va antes de activar el watchdog,
el ESP32 se quedaría colgado hasta reiniciarlo a mano.

**Resultado:** compila sin avisos. RAM al 16,3 % y flash al 57,5 %. JS validado con Node.
Probados con el simulador el STOP desde otro móvil, el keepalive posterior y el script sin
`sid`.

## 2026-10-07: ajustes privados fuera del repositorio

- `rotator_settings_esp32.h` incluye `rotator_settings_esp32_local.h` si existe
  (`__has_include`). `WIFI_DEFAULT_SSID`, `WIFI_DEFAULT_PASSWORD`, `WIFI_HOSTNAME`,
  `WEB_SERVER_USER`, `WEB_SERVER_PASSWORD` y `DEFAULT_LATITUDE`/`DEFAULT_LONGITUDE` pasan a
  `#ifndef`, de modo que el archivo local tiene prioridad.
- El archivo local está en `.gitignore`. El repositorio lleva la plantilla
  `rotator_settings_esp32_local.h.example`, porque el repositorio va a ser público y no
  debe contener credenciales.
- La ubicación por defecto del repositorio pasa a ser la Plaza de Armas de Santiago de Chile
  (−33,4378, −70,6505; FF46qn). Sustituye a la de Pensilvania del firmware original.
- Se comprobó que sin archivo local el firmware lleva los valores de ejemplo, y que con él
  lleva los locales. La contraseña no aparece en ningún archivo versionado ni en el
  historial de git.

## 2026-10-07: primera prueba sobre hardware real

Placa: ESP32-D0WDQ6 rev. 1.0 (ESP32-WROOM-32), cristal de 40 MHz, en `/dev/ttyUSB0`. Solo la
placa: **sin HH-12, sin BNO055 y sin relés conectados**.

**Defecto encontrado y corregido**
- El core del ESP32 escribía sus mensajes de log (`[E][esp32-hal-i2c-ng.c] i2cWrite() …`
  cada 10 s al reintentar el BNO055, y `[E][Preferences.cpp] nvs_open failed` en el primer
  arranque) por `Serial`, que es el puerto de control GS-232. Un programa de seguimiento
  conectado por USB habría recibido esas líneas mezcladas con las respuestas. Se añade
  `-DCORE_DEBUG_LEVEL=0` en `platformio.ini`.
- La basura que aparece al arrancar la emite el cargador de arranque ROM del chip a
  115200 baudios. Es normal y no se puede evitar.

**Pruebas superadas** (scripts en `tools/`)
- Arranque: conecta a la red WiFi en unos 3 s, sincroniza NTP y anuncia `rotor.local`.
  `BNO055: not found` es correcto, porque el sensor no está conectado.
- `tools/test_red.py`, 25 de 25 pruebas:
  - **mDNS y puerto TCP 23:** `C2` responde en 11 ms. Funcionan las minúsculas, `\WI` y
    `\W` sin argumento. La negociación telnet (IAC) y el CR NUL se filtran bien. Un segundo
    cliente sustituye al primero.
  - **Web:** página de 8,4 KB, `/api/status` en 47 ms, hora NTP, Sol y Luna calculados.
  - **Movimiento mantenido sobre el firmware real:** arranca el eje, el keepalive lo
    mantiene y el hombre muerto lo para a 1 s. Un keepalive tardío o una orden atrasada no
    vuelven a arrancarlo, y el STOP de otro cliente gana al keepalive.
  - **Rechazos:** sin BNO055 se rechazan la elevación y el seguimiento, igual que una
    dirección inválida o un locator inválido.
- `tools/test_sol_y_tcp.py`:
  - **Sol:** la posición calculada por el firmware para FF46pi coincide con un cálculo
    NOAA independiente (diferencia de 0,00° en azimut y 0,01° en elevación). Esto valida a
    la vez el NTP, la ubicación y `sunpos`.
  - **Cliente TCP que no lee:** se enviaron unos 2900 comandos en 20 s sin leer las
    respuestas. El ESP32 no se bloquea ni se reinicia, la web sigue respondiendo en menos
    de 75 ms y el firmware desconecta al cliente ("TCP client not reading -
    disconnected"). Queda confirmada la corrección de escritura TCP sin bloqueo.

**Pendiente de probar con el hardware conectado:** HH-12 (lectura de azimut), BNO055
(elevación, calibración, detección de fallo en caliente), salida real de los relés en
GPIO 25/26/32/33 y el procedimiento completo de "Puesta en marcha" del README.

## 2026-10-07: modo simulación y elevación sin bloqueo por sensor

**Cambios pedidos**
- **Sin bloqueo por falta de sensor:** la elevación ya no se impide ni se detiene cuando el
  BNO055 no está o falla. El bloqueo pasa a ser opcional
  (`OPTION_BNO055_FAULT_STOPS_ELEVATION`, desactivado por defecto) y se aplica en un solo
  sitio, la macro `BNO055_BLOCKS_ELEVATION()`: en la lectura del sensor, en las órdenes web
  y en la activación del seguimiento. Sin bloqueo, ante un fallo la lectura queda
  congelada en el último valor válido y se avisa por el puerto de control. La web muestra
  siempre la elevación, con la marca "sin sensor".
- **Modo simulación** (`FEATURE_SIMULATION`, `rotator_esp32_sim.h`):
  - `\XV1`/`\XV0`/`\XV`, con el estado guardado en `Preferences`.
  - `digitalWriteEnhanced()` y `analogWriteEnhanced()` mantienen inactivas las salidas de
    motor (y al entrar en simulación se ponen en reposo).
  - `service_simulation()` integra la posición virtual según `current_az_state()` y
    `current_el_state()`, a 6 °/s en azimut y 3 °/s en elevación, con topes en el rango
    configurado.
  - Al final de cada medición de `read_azimuth()` y `read_elevation()` la posición virtual
    sustituye a la del sensor, sea cual sea el tipo de sensor.
  - Al entrar o salir se paran ambos ejes, porque la posición salta entre la real y la
    virtual.
  - La web muestra la banda "MODO SIMULACIÓN" y la simulación ignora el bloqueo por sensor.

**Pruebas en la placa real** (sin sensores ni relés)
- `tools/test_simulacion.py` por TCP, 9 de 9 pruebas. `M090` y `M150` llegan; `W200 030`
  llega en los dos ejes a la vez; `S` detiene a mitad de recorrido; `R`/`A` y `U`/`E` mueven
  el eje correspondiente.
- Con la simulación apagada y sin BNO055, la web ya permite subir la elevación y activar el
  seguimiento del Sol.
- La simulación se conserva tras un reinicio y se avisa al arrancar.

## 2026-10-07: página de configuración web

**Cambios**
- Nueva página `/config` (10 KB, embebida), enlazada desde la principal. El formulario
  del locator pasa de la página principal a esta.
- Ajustes editables, todos de la estructura `configuration` que el firmware ya guardaba en
  EEPROM:
  - punto de inicio y rango de giro de azimut;
  - offset de elevación;
  - zona horaria;
  - intervalos y umbrales del seguimiento del Sol y de la Luna.

  Se validan por rango en la tarea web. El loop los aplica por la cola, los guarda en el
  acto con `write_settings_to_eeprom()` y vuelve a leer la posición.
- Además: interruptor de simulación, locator, estado y calibración del BNO055 (guardar o
  borrar), cambio de red WiFi (la reconexión se difiere 1,5 s para que la respuesta HTTP
  llegue antes de cortar) y reinicio (con parada de ejes y `ESP.restart()` diferido).
- La contraseña WiFi nunca sale por la API.
- Se deja fuera `azimuth_offset` del K3NG: `apply_azimuth_offset()` solo actúa con valores
  negativos y los duplica, así que expuesto en un formulario confundiría. El ajuste
  equivalente es el punto de inicio.
- `tools/web_preview.py` sirve también `/config` con una API simulada.

**Pruebas en la placa real**
- `tools/test_config.py`, 15 de 15:
  - Carga de la página y del JSON, y comprobación de que la contraseña no aparece.
  - Aplicación de los ajustes.
  - Rechazo de valores fuera de rango, de texto no numérico, de una red vacía y de una
    contraseña WPA corta.
  - Simulación desde la web.
  - Reinicio desde la web: vuelve con `rst=SOFTWARE`, con los ajustes y la simulación
    conservados.
  - Restauración de los valores originales.
- Regresión: `test_red.py` 25 de 25 y `test_simulacion.py` 9 de 9. En `test_red.py` se
  actualizaron las dos pruebas que esperaban el bloqueo por falta de BNO055, que se quitó en
  la entrega anterior a petición del usuario.
