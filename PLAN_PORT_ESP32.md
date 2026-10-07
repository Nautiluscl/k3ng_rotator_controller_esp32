# Plan de portado: K3NG Rotator Controller a ESP32 con WiFi y BNO055

Documento de trabajo para ejecutarse con Claude Code (extensión de VSCode) dentro
de este repositorio. Las fases están ordenadas por dependencia: cada una debe
compilar y probarse antes de pasar a la siguiente. Se recomienda un commit por
tarea completada.

---

## 1. Objetivo

Portar el firmware `k3ng_rotator_controller/` a un ESP32 de forma que:

1. **Azimut**: se lea con el encoder absoluto **HH-12 (AS5045, SSI)**, ya
   soportado por el firmware (`FEATURE_AZ_POSITION_HH12_AS5045_SSI`).
2. **Elevación (inclinación)**: se lea con un **Bosch BNO055** por I2C. Este
   sensor **no** está soportado y hay que añadirlo. Se usa *solo* para la
   inclinación, nunca para el azimut.
3. **Control remoto**: el ESP32 se conecta a una **red WiFi existente** (modo
   estación) y expone un **puerto serie virtual por TCP/IP**. Un cliente TCP
   (PstRotator, Gpredict, hamlib, socat…) envía comandos Yaesu GS-232, Easycom
   o `\` y recibe las mismas respuestas que por USB-serie.
4. El puerto USB-serie (`Serial`) sigue funcionando como puerto de control y de
   depuración.

## 2. Contexto del código existente (verificado)

- **Sin estructura PlatformIO.** El repositorio no tiene `platformio.ini`. El
  firmware es un único sketch Arduino:
  `k3ng_rotator_controller/k3ng_rotator_controller.ino` (~20 000 líneas) más los
  headers `rotator_*.h`. Las librerías locales están en `libraries/`
  (incluida `libraries/hh12/`).
- **Selección de hardware**: `rotator_hardware.h` define `HARDWARE_*`. Según ese
  define, el `.ino` (alrededor de las líneas 1120-1140 y 1250-1265) incluye
  `rotator_features_*.h`, `rotator_pins_*.h` y `rotator_settings_*.h`. Este es el
  mecanismo que debe usarse para añadir un perfil `HARDWARE_ESP32_WIFI`.
- **La red ya está abstraída.** Con `FEATURE_ETHERNET`:
  - Declaraciones en el `.ino` (~línea 1637): `EthernetClient ethernetclient0`,
    `EthernetServer ethernetserver0(ETHERNET_TCP_PORT_0)` y el par opcional `…1`.
  - Inicialización en `initialize_peripherals()` (~línea 11838):
    `Ethernet.begin(mac, ip, gateway, subnet); ethernetserver0.begin();`
  - El servicio está en `service_ethernet()` (~línea 20123): lee bytes del
    cliente, arma el buffer hasta el CR y llama a `process_yaesu_command`,
    `process_easycom_command` y `process_backslash_command` con
    `ETHERNET_PORT0`.
  - Solo se usan `begin()`, `available()`, `read()`, `print()` y `println()`.
    `WiFiServer` y `WiFiClient` del core ESP32 tienen la misma API.
  - Posible descuido del original: `ethernetserver1.begin()` no se llama en
    ningún sitio.
- **El ADXL345 sirve de plantilla para el BNO055.** El sensor de elevación
  `FEATURE_EL_POSITION_ADXL345_USING_ADAFRUIT_LIB` aparece en:
  - `.ino` ~1197: `#include`
  - `.ino` ~1737: instancia global
  - `.ino` ~11773: `begin()` en `initialize_peripherals()`
  - `.ino` ~10292: lectura en `read_elevation()`, con `atan2`, `correct_elevation()`
    y `configuration.elevation_offset`
  - `rotator_dependencies.h` líneas 40, 65 y 101: validaciones y activación de
    `Wire`
  - `rotator_features*.h`: define comentado
- **HH-12**: la librería `libraries/hh12/` hace SSI por bit-banging con
  `digitalWrite` y `delayMicroseconds(HH12_DELAY)` (100 µs) y es portable a
  ESP32. Se instancia en el `.ino` ~1752 (`hh12 azimuth_hh12;`). Los pines están
  en `rotator_pins.h:107-110` (`az_hh12_clock_pin`, `az_hh12_cs_pin` y
  `az_hh12_data_pin`).
- **Dependencias específicas de AVR** que hay que neutralizar:
  - `TWBR` (velocidad I2C, `SET_I2C_BUS_SPEED`)
  - `#include <avr/pgmspace.h>` (~línea 1117; el core ESP32 lo emula, solo hay
    que verificarlo)
  - EEPROM sin `begin()`/`commit()`: unos 115 usos, centralizados en las
    funciones de lectura y escritura de la configuración (~líneas 7280-7810) y
    en la zona TLE
  - 8 `attachInterrupt`
  - `rotator_hardware.h` no reconoce `ARDUINO_ARCH_ESP32`

## 3. Decisiones de diseño

| Tema | Decisión | Motivo |
|---|---|---|
| Placa | ESP32 clásico (ESP32-WROOM/DevKit) o PLC ESP32 de Industrial Shields | Las plataformas `espressif32` e `industrialshields-esp32` ya están en `~/.platformio` |
| Red | WiFi en modo estación, servidor TCP en el puerto 23 (y el 24 opcional) | Reutiliza el camino de `FEATURE_ETHERNET` |
| Nombre en la red | mDNS `rotor.local` (configurable) y DHCP por defecto, con IP fija opcional | Así no hace falta conocer la IP |
| Credenciales WiFi | Valores por defecto en `rotator_settings_esp32.h`, sobrescribibles por comandos `\` y guardados en `Preferences` | Se puede cambiar de red sin recompilar. WiFiManager queda como opción posterior |
| Modo del BNO055 | **`OPERATION_MODE_IMUPLUS`** (acelerómetro y giróscopo, sin magnetómetro) | La inclinación se refiere a la gravedad, así que el magnetómetro no aporta nada y los motores lo perturbarían |
| Cálculo de la elevación | A partir del **vector de gravedad** (`VECTOR_GRAVITY`) con `atan2` sobre los ejes elegidos | Más robusto que los ángulos de Euler cerca de los ±90°, donde aparece el gimbal lock en la representación |
| Librería del BNO055 | `adafruit/Adafruit BNO055` (depende de `Adafruit Unified Sensor` y `Adafruit BusIO`) | Es la más usada y coherente con el resto del firmware, que ya usa sensores Adafruit |
| I2C | 100 kHz y `Wire.setTimeOut()` alto | El BNO055 usa clock stretching, con problemas conocidos en ESP32 a 400 kHz |
| Persistencia | `EEPROM` emulada (con `begin`/`commit`) para la configuración existente y `Preferences` para WiFi y offsets del BNO055 | Cambios mínimos sobre el código original |

## 4. Restricciones de hardware a respetar

- **ADC2 no funciona con WiFi activo.** Las entradas analógicas solo pueden ir
  en GPIO 32-39 (ADC1). Con HH-12 y BNO055 no hacen falta entradas analógicas
  de posición, pero hay que tenerlo en cuenta para cualquier otra entrada
  analógica, como un joystick o un potenciómetro de velocidad.
- **Pines a evitar:** 6-11 (flash), 0, 2, 12 y 15 (pines de arranque) y 34-39
  (solo entrada, sin pull-up interno).
- **Niveles lógicos:** el ESP32 trabaja a 3,3 V. El HH-12 suele alimentarse a
  5 V, y su salida de datos (DO) hacia el ESP32 necesita un adaptador de nivel o
  un divisor resistivo. CLK y CS pueden ir directos si el HH-12 los acepta a
  3,3 V; si no, también necesitan adaptador. El módulo BNO055 (Adafruit, GY-BNO055)
  funciona a 3,3 V.
- **Alimentación:** los picos de WiFi (~300 mA) junto al arranque de los
  motores pueden provocar un brownout. La fuente lógica debe ir separada o bien
  desacoplada de la de potencia.
- **Cables I2C largos:** el BNO055 va montado en el mástil o en el boom. Por
  encima de ~1 m conviene usar un extensor I2C diferencial (P82B715, PCA9615)
  o colocar un microcontrolador cerca del sensor. Se deja documentado y no se
  resuelve en este plan.

Mapa de pines propuesto (orientativo, se ajusta a la placa real en la fase 1):

| Función | GPIO | Nota |
|---|---|---|
| I2C SDA (BNO055, LCD) | 21 | Por defecto del core |
| I2C SCL | 22 | Por defecto del core |
| HH-12 CLK (az) | 18 | Salida |
| HH-12 CS (az) | 5 | Salida (pin de arranque débil, aceptable para CS) |
| HH-12 DO (az) | 19 | Entrada, con adaptador de nivel si el HH-12 va a 5 V |
| Rotar CW | 25 | Salida a relé o driver |
| Rotar CCW | 26 | Salida a relé o driver |
| Elevar UP | 27 | Salida |
| Elevar DOWN | 14 | Salida |
| BNO055 RST (opcional) | 23 | Permite reiniciar el sensor si se cuelga el bus I2C |

---

## 5. Fases y tareas

### Fase 1: estructura PlatformIO y compilación mínima (sin red ni sensores)

1. Crear `platformio.ini` en la raíz:
   - `src_dir = k3ng_rotator_controller`
   - `lib_extra_dirs = libraries`
   - `[env:esp32]` con `platform = espressif32`, `board = esp32dev` y
     `framework = arduino`
   - `monitor_speed` igual a `CONTROL_PORT_BAUD_RATE` de los settings
   - Un `[env:megaatmega2560]` de referencia, para comprobar que no se rompe la
     compilación AVR
   - Comprobar que PlatformIO compila el `.ino` (lo convierte a `.cpp`). Si los
     prototipos automáticos dan problemas, documentarlo antes de renombrar nada.
2. En `rotator_hardware.h`:
   - Añadir `#define HARDWARE_ESP32_WIFI`, comentado por defecto o activado
     desde `build_flags` con `-DHARDWARE_ESP32_WIFI`.
   - Añadir la rama `#elif defined(ARDUINO_ARCH_ESP32)` con
     `CONTROL_PORT_SERIAL_PORT_CLASS HardwareSerial`.
   - Incluir `HARDWARE_ESP32_WIFI` en la lista que define `HARDWARE_CUSTOM`.
3. Crear `rotator_features_esp32.h`, `rotator_pins_esp32.h` y
   `rotator_settings_esp32.h`, partiendo de los genéricos, y engancharlos en los
   bloques `#include` del `.ino` (siguiendo el patrón de `HARDWARE_M0UPU`).
   Configuración inicial: `FEATURE_ELEVATION_CONTROL`, el protocolo Yaesu y
   ningún sensor ni red todavía.
4. Ajustes de plataforma:
   - Sustituir el uso de `TWBR` por `Wire.setClock(SET_I2C_BUS_SPEED)` bajo
     `#if defined(ARDUINO_ARCH_ESP32)`, conservando la rama AVR.
   - Proteger `#include <avr/pgmspace.h>` con `#if defined(__AVR__)` si el core
     ESP32 da error (en caso contrario, dejarlo).
   - Marcar con `IRAM_ATTR` las rutinas pasadas a `attachInterrupt` (solo en
     ESP32, mediante una macro `ISR_ATTR` que en AVR quede vacía).
   - Revisar el `free memory check` de `DEBUG_DUMP` (usa símbolos AVR) y
     desactivarlo en ESP32 o sustituirlo por `ESP.getFreeHeap()`.
5. **Criterio de salida:** `pio run -e esp32` compila sin errores y
   `pio run -e megaatmega2560` sigue compilando.

### Fase 2: EEPROM y puerto de control por USB

1. Añadir `EEPROM.begin(EEPROM_SIZE_ESP32)` al principio de `setup()` (o de la
   rutina que lee la configuración), con un tamaño que cubra la configuración y
   la zona TLE. Calcularlo a partir de `sizeof(configuration)` y de
   `tle_file_eeprom_memory_area_start`.
2. Añadir `EEPROM.commit()` al final de la función que escribe la configuración
   (~línea 7801) y de las escrituras TLE (~7820 y ~7880), solo en ESP32.
3. Comprobar en la placa, por USB-serie:
   - Que el equipo arranca y responde a `C`, `\?FV` (versión) y similares.
   - Que un cambio de configuración (offset, calibración) sobrevive a un reinicio.
4. **Criterio de salida:** la configuración persiste tras desconectar la
   alimentación.

### Fase 3: azimut con HH-12

1. Activar `FEATURE_AZ_POSITION_HH12_AS5045_SSI` en `rotator_features_esp32.h`.
2. Definir `az_hh12_clock_pin`, `az_hh12_cs_pin` y `az_hh12_data_pin` en
   `rotator_pins_esp32.h` según la tabla del apartado 4.
3. Revisar `libraries/hh12/hh12.cpp`:
   - Que no use `digitalWriteFast` ni registros AVR. Lo esperado es que sea
     portable.
   - `HH12_DELAY` (100 µs) bloquea unos 2 ms por lectura (18 bits × 2 ×
     100 µs). Es aceptable, pero se puede bajar a 10-20 µs en ESP32 si hace
     falta, porque el AS5045 admite relojes de hasta 1 MHz.
   - Mantener las opciones `OPTION_REVERSE_AZ_HH12_AS5045` y
     `OPTION_HH12_10_BIT_READINGS`.
4. Comprobar en banco que el azimut sigue al eje en todo el giro, que el sentido
   es correcto y que el offset (`\Ix`/`O`) funciona.
5. **Criterio de salida:** lecturas estables (±0,1°) y sin saltos al girar.

### Fase 4: elevación con BNO055 (nuevo sensor)

Hay que seguir **exactamente** el patrón del ADXL345 con la librería de
Adafruit (ver apartado 2), para que la integración sea coherente con el resto
del firmware.

1. **Define de la funcionalidad:**
   - Añadir `// #define FEATURE_EL_POSITION_BNO055` en `rotator_features.h` y
     en los demás `rotator_features_*.h`, junto a los demás `FEATURE_EL_POSITION_*`
     y comentado.
   - Activarlo en `rotator_features_esp32.h`.
2. **Dependencias** (`rotator_dependencies.h`):
   - Línea ~40: añadirlo a la lista incompatible con
     `FEATURE_EL_POSITION_GET_FROM_REMOTE_UNIT`.
   - Línea ~65: añadirlo a la lista de sensores de elevación válidos. Si no, se
     dispara el `#error` de "falta sensor de elevación".
   - Línea ~101: añadirlo a la lista que activa `Wire`/I2C.
   - Añadir un `#error` si se define junto con otro `FEATURE_EL_POSITION_*`.
3. **Settings** (`rotator_settings.h` y `rotator_settings_esp32.h`):
   ```c
   #define BNO055_I2C_ADDRESS 0x28          // 0x29 si ADR está a VCC
   #define BNO055_ELEVATION_AXIS_A 'Y'      // eje numerador de atan2
   #define BNO055_ELEVATION_AXIS_B 'Z'      // eje denominador de atan2
   #define BNO055_ELEVATION_INVERT 0        // 1 = invertir signo
   #define BNO055_READ_INTERVAL_MS 50       // no leer en cada loop
   #define BNO055_FAIL_THRESHOLD 10         // lecturas fallidas seguidas antes de marcar error
   ```
   Se recomienda resolver los ejes con macros en tiempo de compilación, no con
   un `switch` en tiempo de ejecución. Como alternativa, se puede usar el
   remapeo interno del chip (`setAxisRemap`/`setAxisSign`) y dejar fijo `atan2(y, z)`.
4. **Include e instancia** (`.ino`, junto al bloque del ADXL345 Adafruit):
   ```c
   #ifdef FEATURE_EL_POSITION_BNO055
     #include <Adafruit_BNO055.h>
     Adafruit_BNO055 bno = Adafruit_BNO055(55, BNO055_I2C_ADDRESS, &Wire);
   #endif
   ```
   Añadir a `platformio.ini`, en `lib_deps`: `adafruit/Adafruit BNO055`,
   `adafruit/Adafruit Unified Sensor` y `adafruit/Adafruit BusIO`.
5. **Inicialización** (`initialize_peripherals()`, junto a `accel.begin()`):
   - `Wire.begin(); Wire.setClock(100000);` y en ESP32 `Wire.setTimeOut(…)`.
   - `bno.begin(OPERATION_MODE_IMUPLUS)`. Si falla, informarlo por el puerto de
     control y marcar el sensor como no disponible, sin bloquear el arranque.
   - `bno.setExtCrystalUse(true)` si el módulo tiene cristal de 32 kHz (el de
     Adafruit lo tiene).
   - Restaurar los offsets de calibración guardados (ver el punto 8).
6. **Lectura** (`read_elevation()`, bloque nuevo análogo al del ADXL345):
   - Respetar `BNO055_READ_INTERVAL_MS`. Entre lecturas, devolver el último
     valor válido.
   - `imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);`
   - `elevation = atan2(A, B) * 180.0 / M_PI;` con los ejes configurados y el
     signo según `BNO055_ELEVATION_INVERT`.
   - Aplicar después, en el mismo orden que el ADXL345:
     `correct_elevation()` (si existe `FEATURE_ELEVATION_CORRECTION`),
     `configuration.elevation_offset` (si no hay `FEATURE_CALIBRATION`) y
     `ELEVATION_SMOOTHING_FACTOR`.
   - Añadir trazas bajo `#ifdef DEBUG_ACCEL` o un nuevo `DEBUG_BNO055`.
7. **Detección de fallos (seguridad):**
   - Si `getVector` devuelve todo a cero, o el bus I2C da error durante
     `BNO055_FAIL_THRESHOLD` lecturas seguidas, marcar el sensor de elevación
     como fallido.
   - Si el sensor falla con el rotor en movimiento de elevación, **detener la
     elevación** con la rutina de parada existente (`submit_request` de parada
     o `rotator(DEACTIVATE…)`, siguiendo lo que ya haga el firmware ante un
     fallo de sensor) e informarlo por el puerto de control.
   - Si existe el pin RST, reiniciar el BNO055 por hardware y repetir `begin()`.
8. **Calibración del BNO055:**
   - En `IMUPLUS` hay que calibrar el acelerómetro y el giróscopo. El estado se
     obtiene con `bno.getCalibration(&sys, &gyro, &accel, &mag)`.
   - Comandos `\` nuevos (comprobar en `process_backslash_command` que las
     letras están libres antes de asignarlas):
     - `\XB`: muestra el estado de calibración (sys/gyro/accel, 0-3) y la
       elevación sin procesar.
     - `\XBS`: guarda los offsets (`getSensorOffsets`, `adafruit_bno055_offsets_t`)
       en `Preferences` (namespace `bno055`).
     - `\XBC`: borra los offsets guardados.
   - Al arrancar, si hay offsets guardados, aplicarlos con `setSensorOffsets()`
     **antes** de entrar en el modo `IMUPLUS`, porque la librería lo exige en
     modo CONFIG.
   - Documentar el procedimiento: dejar el sensor quieto en 6 posiciones para
     calibrar el acelerómetro, esperar a que `accel == 3` y guardar con `\XBS`.
9. **Comprobar en banco:**
   - 0°, 45° y 90° comparados con un inclinómetro de referencia.
   - Estabilidad en reposo (que no deriva en 30 min).
   - Que las vibraciones de los motores no producen saltos. Si los hay, subir
     `ELEVATION_SMOOTHING_FACTOR`.
   - Que al desconectar el sensor en caliente se detiene la elevación y se
     informa del fallo.
10. **Criterio de salida:** elevación con error ≤ 0,5° frente a la referencia y
    parada segura ante un fallo del sensor.

### Fase 5: WiFi y puerto serie virtual por TCP

1. **Define de la funcionalidad:**
   - Añadir `FEATURE_WIFI` (solo ESP32) en `rotator_features_esp32.h`.
   - En `rotator_dependencies.h`: hacerlo incompatible con `FEATURE_ETHERNET` y
     exigir `ARDUINO_ARCH_ESP32`.
2. **Reutilizar el camino de Ethernet** sin duplicar `service_ethernet()`:
   - Bajo `FEATURE_WIFI`, incluir `<WiFi.h>` y `<ESPmDNS.h>` y declarar
     `WiFiServer ethernetserver0(ETHERNET_TCP_PORT_0)` y
     `WiFiClient ethernetclient0` (y los `…1`), con los mismos nombres de
     variable.
   - Hacer que los `#ifdef FEATURE_ETHERNET` que envuelven el servicio, las
     respuestas y `ETHERNET_PORT0` se activen también con `FEATURE_WIFI`. La
     forma más limpia es un define común `FEATURE_NETWORK_CONTROL_PORT`, que
     activan tanto `FEATURE_ETHERNET` como `FEATURE_WIFI`, y sustituir por él
     los `#ifdef FEATURE_ETHERNET` del camino del puerto TCP. El enlace
     maestro-esclavo por Ethernet (`ethernetslavelinkclient0`) queda **fuera**:
     no hay que activarlo con WiFi.
   - Revisar todos los usos (`grep -n FEATURE_ETHERNET`) y decidir uno por uno:
     11 en el `.ino` y los comentados en `rotator_debug.cpp`.
3. **Settings** (`rotator_settings_esp32.h`):
   ```c
   #define WIFI_DEFAULT_SSID "mi_red"
   #define WIFI_DEFAULT_PASSWORD "mi_clave"
   #define WIFI_HOSTNAME "rotor"            // mDNS: rotor.local
   #define WIFI_USE_DHCP 1                  // 0 = usar ETHERNET_IP_ADDRESS/GATEWAY/SUBNET
   #define WIFI_RECONNECT_INTERVAL_MS 10000
   #define ETHERNET_TCP_PORT_0 23
   // #define ETHERNET_TCP_PORT_1 24
   ```
4. **Inicialización** (`initialize_peripherals()`, en lugar de `Ethernet.begin`):
   - Leer el SSID y la contraseña de `Preferences` (namespace `wifi`). Si no hay,
     usar los valores por defecto.
   - `WiFi.mode(WIFI_STA); WiFi.setHostname(…); WiFi.setSleep(false);`. Sin
     `setSleep(false)`, la latencia de respuesta sube a 100-300 ms.
   - Si `WIFI_USE_DHCP == 0`, llamar a `WiFi.config(ip, gateway, subnet)`.
   - `WiFi.begin(ssid, pass)` **sin bloquear**. No hay que esperar en `setup()`
     a que conecte: el rotor tiene que funcionar por USB aunque no haya WiFi.
   - Llamar a `ethernetserver0.begin()` y, si existe el puerto 1,
     `ethernetserver1.begin()`, que falta en el original.
   - `ethernetserver0.setNoDelay(true)` para que las respuestas cortas salgan
     sin esperar.
5. **Gestión de la conexión** (nueva función `service_wifi()`, llamada desde
   `loop()` junto a `service_ethernet()`):
   - Máquina de estados no bloqueante: si `WiFi.status() != WL_CONNECTED` y ha
     pasado `WIFI_RECONNECT_INTERVAL_MS`, llamar a `WiFi.reconnect()`.
   - Al conectar: arrancar mDNS (`MDNS.begin(WIFI_HOSTNAME)`,
     `MDNS.addService("telnet", "tcp", 23)`) e informar la IP por el puerto de
     control.
   - Ninguna rutina de red debe bloquear más de unos pocos ms. El control de
     motores no puede depender del estado de la red.
6. **Clientes TCP:**
   - En `service_ethernet()`, comprobar que, si un cliente se desconecta
     (`!ethernetclient0.connected()`), se hace `stop()` y se acepta uno nuevo.
   - En el core 3.x, `WiFiServer::available()` está obsoleto y se recomienda
     `accept()`. Usar `accept()` bajo `ARDUINO_ARCH_ESP32` si da aviso.
   - La política del original es de un cliente por puerto. Si llega un segundo
     cliente al puerto 23, se rechaza. Hay que documentarlo.
7. **Comandos `\` de red** (verificar que las letras están libres):
   - `\WS<ssid>`: guarda el SSID.
   - `\WP<password>`: guarda la contraseña (no se muestra al consultarla).
   - `\WR`: reconecta con los datos nuevos.
   - `\WI`: muestra el estado, el SSID, la IP, el RSSI y el hostname.
8. **Comprobar:**
   - `nc rotor.local 23` y luego `C2`, `M180`, `W090 045` y `S`, con respuestas
     idénticas a las del USB.
   - Cortar el router: el rotor sigue funcionando por USB y se reconecta solo
     al volver el router.
   - Desconectar el cliente TCP de forma brusca y comprobar que acepta otro.
   - Medir la latencia (comando → respuesta): debe estar por debajo de 50 ms en
     LAN.
9. **Criterio de salida:** PstRotator o hamlib controlan el rotor por WiFi
   durante 1 h sin cortes.

### Fase 6: robustez

1. Activar el watchdog de tareas (`esp_task_wdt_init` y `esp_task_wdt_add`
   sobre la tarea del loop), con un timeout de 5-10 s, y alimentarlo en
   `loop()`.
2. Habilitar el detector de brownout (activo por defecto en el core) y
   registrar el motivo del último reinicio (`esp_reset_reason()`), visible con
   `\WI` o `\?`.
3. Opcional: OTA con `ArduinoOTA`, activable con `FEATURE_WIFI_OTA` y protegido
   con contraseña definida en los settings.
4. Opcional: WiFiManager, que levanta un AP de configuración si no consigue
   conectar en N intentos. Se deja como mejora posterior.

### Fase 7: lado del PC (documentación, sin código de firmware)

Crear `docs/ESP32_WIFI.md` (o una sección del README) con:

- **Conexión TCP directa:**
  - hamlib: `rotctld -m 603 -r rotor.local:23` (GS-232B) o `-m 601`/`602`
    según el protocolo.
  - PstRotator: configuración TCP/IP.
  - Gpredict: a través de `rotctld`.
- **COM virtual:**
  - Windows: HW VSP3, o com0com con hub4com.
  - Linux: `socat pty,link=/dev/ttyROT,raw,echo=0 tcp:rotor.local:23`.
- Aviso de seguridad: el puerto no tiene autenticación. Solo debe usarse en la
  LAN y nunca debe abrirse a internet sin VPN.
- El procedimiento de calibración del BNO055 (fase 4, punto 8) y los comandos
  `\W*` y `\XB*`.

### Fase 8: herramienta de configuración (`k3ng_config_tool/`)

1. Añadir `data/board_definitions/esp32.json`, siguiendo el formato de
   `arduino_mega.json`, con:
   - Los pines válidos, marcando los de solo entrada (34-39), los de arranque y
     los de ADC2, con un aviso de incompatibilidad con WiFi.
   - Los buses I2C y SPI por defecto.
2. Registrarla en `boards/board_database.py`.
3. Dar de alta `FEATURE_EL_POSITION_BNO055`, `FEATURE_WIFI` y sus settings
   (`BNO055_*`, `WIFI_*`) en los parsers y en las reglas de validación
   (`data/validation_rules`), con las mismas incompatibilidades que en
   `rotator_dependencies.h`.
4. **Criterio de salida:** la herramienta genera unos `rotator_*_esp32.h`
   válidos que compilan.

---

## 6. Reglas para la implementación

- **No romper AVR.** Todo cambio específico de ESP32 va bajo
  `#if defined(ARDUINO_ARCH_ESP32)` o bajo los defines nuevos. Al final de cada
  fase hay que compilar también `env:megaatmega2560`.
- **Seguir el estilo del firmware:** bloques `#ifdef FEATURE_…` / `#endif //
  FEATURE_…` con el comentario de cierre, trazas bajo `DEBUG_*` y mensajes con
  `F("…")`.
- **No reformatear** el `.ino` ni mover funciones: los diffs deben ser mínimos y
  revisables.
- **Nada bloqueante en `loop()`**: ni esperas activas de WiFi ni `delay()` en
  rutinas de red o del sensor.
- Comentarios nuevos en español neutro, en forma impersonal ("se lee", "hay que
  ajustar").
- Un commit por tarea, con un mensaje que indique fase y tarea (p. ej. `ESP32
  F4.6: lectura de elevación con BNO055 (vector gravedad)`).

## 7. Riesgos conocidos

| Riesgo | Mitigación |
|---|---|
| Clock stretching del BNO055 con el I2C del ESP32 | 100 kHz, `setTimeOut` alto, pin RST para reinicio por hardware y core `espressif32` reciente |
| Cable I2C largo hasta el sensor en el boom | Extensor I2C diferencial o cable corto y apantallado. Se valida en la fase 4 |
| Vibración de los motores en la lectura de elevación | `ELEVATION_SMOOTHING_FACTOR` y el filtrado propio del modo `IMUPLUS` |
| Nivel de 5 V del HH-12 | Adaptador de nivel en DO (y en CLK/CS si hace falta) |
| Brownout al arrancar los motores con WiFi transmitiendo | Fuentes separadas o desacoplo y registro de `esp_reset_reason()` |
| Prototipos automáticos del `.ino` en PlatformIO | Comprobarlo en la fase 1. Si falla, añadir los prototipos necesarios o pasar a `.cpp` con un header de declaraciones |
| Puerto TCP sin autenticación | Uso solo en la LAN, documentado en la fase 7 |
