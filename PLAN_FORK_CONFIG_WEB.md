# Plan del fork: K3NG Rotator ESP32 configurable por web

Documento de trabajo para el usuario y para futuras sesiones de Claude Code. Cada etapa es ejecutable por separado. Al empezar una sesión se leen este archivo, `BITACORA.md` (sección «Próximo paso») y `docs/DECISIONES.md`.

Convención: **[VERIFICADO]** = comprobado en el código del port; **[SUPUESTO]** = estimación o dato no comprobado, que debe confirmarse en la etapa indicada.

---

## 1. Objetivo y alcance

**Objetivo:** un único firmware para ESP32-WROOM-32 (4 MB) en el que sensores, pantallas, pines, salidas, protocolos y opciones se configuran por web en tiempo de ejecución, sin VSCode ni recompilar. Etapa final: instalación desde el navegador con ESP Web Tools (como https://richonguzman.github.io/lora-tracker-web-flasher/installer.html).

**Punto de partida [VERIFICADO]:** rama `esp32-port` de este repositorio. `k3ng_rotator_controller/k3ng_rotator_controller.ino` (~24 000 líneas, 1884 directivas `#if*`, 193 identificadores `FEATURE_`/`OPTION_`), perfil `HARDWARE_ESP32_WIFI` (`rotator_features_esp32.h` con 36 `#define` activos, `rotator_pins_esp32.h`, `rotator_settings_esp32.h` con 266 `#define`), web en `rotator_esp32_web.h`, WiFi en `rotator_esp32_wifi.h`, simulación en `rotator_esp32_sim.h`. Pruebas contra placa en `tools/test_*.py`.

### Entra
- Sensores de posición, pantallas, entradas locales, salidas de motor, reloj (NTP/GPS/RTC), protocolos (Yaesu GS-232A/B, Easycom, DCU-1, extendidos K3NG), seguimiento Sol/Luna, park/autopark, límites, calibración. Detalle en la sección 4.
- Configuración completa por web, exportar/importar, modo seguro, reset de fábrica, OTA, Improv, portal cautivo, flasheador web.

### Se descarta (motivo)
| Elemento | Motivo |
|---|---|
| `#ifdef __AVR__`, `OPTION_SAVE_MEMORY_*`, TimerOne/TimerFive, digitalWriteFast | Exclusivos de AVR, no compilan en ESP32 |
| Perfiles `rotator_*_m0upu/wb6kcn/test.h` y genéricos | Solo ESP32 en el fork |
| `FEATURE_ETHERNET` (W5100) | El WiFi lo reemplaza; quizá W5500/LAN8720 vía `ETH.h` en el futuro |
| Maestro/esclavo serie y Ethernet (pines > 99, `GET_FROM_REMOTE_UNIT`) | ~170 bloques `#if`, cambia la semántica de pines; un futuro «remoto TCP» entre ESP32 lo sustituiría |
| HMC5883L Jarzebski, DFRobot QMC5883, Adafruit LSM303, ADXL345 Love Electronics, PJRC Encoder, MEMSIC 2125, A2/SEI | Duplicados, obsoletos, librería ausente, pines fijos en el constructor o hardware raro |
| LCD Adafruit RGB, Midas/ST7036, FaBo, variantes YourDuino/SainSmart/RFRobot/YwRobot | `hd44780_I2Cexp` cubre los PCF8574; el resto choca entre sí o está abandonado |
| RTC PCF8583, `FEATURE_ADC_RESOLUTION12`, `OPTION_EXTERNAL_ANALOG_REFERENCE` | Obsoletos o sin sentido en ESP32 |

---

## 2. Decisión de arquitectura

Se evaluaron tres propuestas (valoradas por dos jueces con criterios de riesgo y de usuario):

| Propuesta | Nota riesgo / usuario | Por qué no o por qué sí |
|---|---|---|
| **In situ** (mantener el `.ino`, `#ifdef` → `if (cfg.x)`) | 5 / 6,5 | Diff mínimo contra upstream, pero un monolito de 24 000 líneas lleno de `if` es inmanejable para subagentes con contexto limitado; etapa XL mal acotada |
| **Reescritura** (firmware nuevo con tareas FreeRTOS) | 4 / 6,5 | Arquitectura limpia, pero reimplementa años de casos límite del K3NG (overlap, rampas, matices GS-232); no mueve el rotor hasta muy tarde; carreras y watchdog |
| **Núcleo K3NG + capa de drivers** | **7 / 8** | **Elegida.** Conserva la lógica probada, aísla cada librería en su `.cpp`, permite encargos pequeños y verificables |

**Injertos incorporados de las otras propuestas:**
- De *in situ*: truco de macros (`#define rotate_cw (cfg.pins.rotate_cw)`, settings → `cfg.*`) para tocar pocas líneas; conversión de `#if` opción por opción con script, empezando por las de un solo uso; modularización **gradual** (`.ino` → `.cpp` primero, extraer módulos solo cuando se tocan); `config_t` con layout fijo desde el principio; aceptación de E0 «igual que v1.0»; prefijo y disciplina de diff mínimo en el núcleo.
- De *reescritura*: punto único de escritura de motores (`digitalWriteEnhanced`/`analogWriteEnhanced` → módulo `outputs`); `PinManager`, `LedcAllocator`, `I2CBus` con mutex; pruebas nativas con casos sacados del K3NG antes de tocar el núcleo; formularios generados desde `GET /api/schema`; portal AP con asistente completo y prueba de movimiento; **red, recuperación y OTA adelantadas antes de los drivers**; etapa de beta con usuarios reales y guía de migración antes del flasheador.

---

## 3. Arquitectura

### 3.1 Estructura de archivos (estado objetivo)

```
src/
  main.cpp                 safe_init → load_config → validate → registry.create() → k3ng_setup()
  k3ng_core.cpp            el .ino convertido a .cpp (se va adelgazando)
  k3ng_prototypes.h        generado por tools/gen_prototypes.py
  config/  schema.cpp      tabla única {clave, tipo, defecto, min, max, reinicio, ayuda}
           store.cpp       LittleFS /config.json + /config.bak, NVS para calibraciones
           validate.cpp    reglas de pines ESP32 + exclusiones de rotator_dependencies.h
           migrate.cpp     schema N→N+1, importación de config_t y NVS del port
  hal/     pin_manager.cpp, i2c_bus.cpp (Wire único + mutex), ledc_alloc.cpp, uart_alloc.cpp
  drivers/position/  pos_pot, pos_hh12, pos_bno055, pos_incremental, pos_pulse, pos_sim, (fase 2) pos_rotary, pos_qmc5883, pos_hmc5883l, pos_adxl345, pos_lsm303
  drivers/output/    out_relay, out_pwm_ledc, out_brake, (fase 2) out_stepper
  drivers/display/   disp_hd44780_i2c, disp_oled_u8x8, (fase 2) disp_lcd4bit, nextion
  drivers/input/     buttons, pots, preset_encoder, leds, limits
  drivers/clock/     ntp, gps_tinygps, rtc_ds3231, rtc_ds1307
  protocol/          port_router.cpp (despacho por puerto), y los parsers que hoy están en rotator_command_processing.h
  net/               wifi, web, api, captive, improv, ota
  recovery/          boot_counter, safe_mode, factory_reset
include/   IPositionDriver.h, IOutputDriver.h, ITextDisplay.h, IClockSource.h
data/      web (HTML/JS/CSS en gzip) → imagen LittleFS
test/      test_native/ (Unity), test_embedded/
tools/     pruebas Python actuales + gen_prototypes.py + convert_ifdef.py
docs/      UPSTREAM.md, DECISIONES.md, MATRIZ_HW.md, MIGRACION.md
flasher/   index.html + manifest.json (GitHub Pages)
legacy/    referencia del perfil original (no compila)
```

La estructura se alcanza de forma gradual: en E1 solo se renombra el `.ino` a `.cpp`; los módulos se extraen en las etapas que los tocan.

### 3.2 Interfaces

```cpp
class IPositionDriver {                    // un eje
public:
  virtual bool begin(const JsonObjectConst& cfg, I2CBus& bus) = 0;
  virtual void end() {}                    // detachInterrupt, liberar pines
  virtual bool read(float& grados_crudos) = 0;  // no bloqueante
  virtual PosStatus status() const = 0;    // OK, NOT_FOUND, COMM_ERROR, NOT_CALIBRATED, OUT_OF_RANGE
  virtual void service() {}                // sondeo
  virtual bool calibrate(CalStep paso) { return false; }
  virtual void describeSettings(JsonArray& out) const {}
  virtual ~IPositionDriver() {}
};
struct IOutputDriver { virtual void setDirection(Dir d)=0; virtual void setSpeed(uint8_t)=0; virtual void brake(bool)=0; };
struct ITextDisplay  { virtual bool begin(uint8_t c, uint8_t r)=0; virtual void setCursor(uint8_t,uint8_t)=0;
                       virtual void print(const char*)=0; virtual void clear()=0; virtual void backlight(bool){} };
```

- **Registro:** `DriverFactory { id, ejes, bus, crear() }[]`. En `setup()` se hace `new` del driver elegido por eje.
- **Lo común fuera del driver:** offset, inversión, multiplicador, wrap 360/450, filtrado y conversión a `raw_azimuth`/`elevation`. Sustituye los ~26 bloques `#ifdef` de `read_azimuth()` (`.ino:8655`) y `read_elevation()` (`.ino:10218`).
- **Objetos globales en conflicto** pasan a miembros de su driver: `rtc` (`.ino:1249`, `:1818`), `accel` (`:1770`, `:1784`), `compass` (`:1758`, `:1792`), `bno` (`:1774`), `gps` (`:1809`), `myGNSS` (`:1812`), `Encoder` (`:1859/1865`), las 7 variantes de `lcd` (`rotator_k3ngdisplay.cpp:17-90`).
- **Pantalla:** `K3NGdisplay` conserva búfer y API y escribe en un `ITextDisplay*`. Las 88 llamadas del `.ino` mantienen su firma, pero todas están dentro de `#if FEATURE_*_DISPLAY` y hay que convertirlas en `if (cfg.display.tipo != NONE)`. Además, `rotator_k3ngdisplay.h:34-71` incluye los perfiles `HARDWARE_*` y está acoplado a `rotator_features.h:81`: hay que desacoplarlo (ver E7).
- **Salidas:** solo `digitalWriteEnhanced()` (`.ino:14112`) y `analogWriteEnhanced()` (`.ino:14169`) tocan pines de motor. Ahí se centralizan polaridad, simulación (`simulation_is_motor_pin()` comparará contra `cfg.pins`) y LEDC.
- **ISR:** `attachInterruptArg(pin, isr, this, modo)`, `IRAM_ATTR`, variables `volatile` con `portENTER_CRITICAL_ISR`.

### 3.3 Modelo de configuración

**Esquema:** tabla `const` única en `config/schema.cpp`, con `{clave, tipo, defecto, min, max, requiere_reinicio, ayuda}`. Los defectos salen de `rotator_settings_esp32.h` y `rotator_pins_esp32.h`. La misma tabla genera el formulario web (`GET /api/schema`), valida y rellena claves que falten.

**Conversión del código:**
- Pines: `int8_t`, `PIN_NONE = -1`, macro `PIN_OK(p)`. `#define rotate_cw 25` → `#define rotate_cw (cfg.pins.rotate_cw)`; los `if (rotate_cw)` pasan a `if (PIN_OK(rotate_cw))`. Corregir `#ifdef blink_led` (`.ino:2722`), que se excluiría sin aviso. Todo `pinMode` en `initialize_pins()` (`.ino:11354`).
- Detección de restos: con `PIN_NONE = -1`, un `if (rotate_cw)` sin convertir da verdadero en silencio. Hay que añadir `tools/check_pins.py` en CI, que falle si queda un `if (<nombre_de_pin>)` sin `PIN_OK` o un `#ifdef <nombre_de_pin>`. Como defensa adicional, `digitalWriteEnhanced()`, `analogWriteEnhanced()`, `digitalReadEnhanced()` y `pinMode` (vía `pin_manager`) ignoran los pines negativos.
- Settings numéricos (~235): `#define X (cfg.x)`.
- Settings que dimensionan arrays (`COMMAND_BUFFER_SIZE`, `TIMED_INTERVAL_ARRAY_SIZE`, `SATELLITE_TLE_CHAR_SIZE`, `LCD_ROWS`, los 12 `LCD_*_ROW`): constante al máximo (4×20), valor de ejecución como límite.
- Settings dentro de `#if` (14: `ANALOG_AZ_OVERLAP_DEGREES`, `DEFAULT_LATITUDE/LONGITUDE`, `CONTROL_PORT_MAPPED_TO`, `BNO055_ELEVATION_INVERT`, `BNO055_RESET_PIN`, `WIFI_DEFAULT_SSID/PASSWORD`, `WIFI_HOSTNAME`, `WIFI_USE_DHCP`, `WEB_SERVER_USER/PASSWORD`, `SIMULATION_ACTIVE_AT_BOOT`, `ETHERNET_TCP_PORT_1`): reescribir como `if`.
- FEATURE/OPTION de comportamiento (~55): compilados siempre, `if (cfg.feat.x)`. `FEATURE_ELEVATION_CONTROL` siempre compilado + `cfg.has_elevation`.
- `OPTION_DISPLAY_*`: máscara de bits `cfg.display.opt`.

**Almacenamiento:**
| Qué | Dónde |
|---|---|
| Configuración del equipo (hardware, pines, opciones, protocolos) | `/config.json` en LittleFS, con `"schema": N`, `"fw"`, `"board"`; `/config.bak` = última que arrancó bien |
| Calibraciones y estado (`last_az`, mín/máx pot, offsets BNO055, cero del encoder, WiFi) | NVS, espacio único `k3ng`, claves sueltas; escrituras frecuentes limitadas como hoy |
| `config_t` (`.ino:1393`) | Layout fijo con todos los campos; la «subversión» (`calculated_configuration_struct_subversion()`) deja de depender de FEATURE, si no cada cambio por web borraría la calibración |

**Migración:** `migrate()` aplica N→N+1 paso a paso; claves que faltan → defecto; desconocidas → se ignoran. Primer arranque del fork: importar `config_t` de la EEPROM emulada (1024 B, `ESP32_EEPROM_SIZE`) y los espacios NVS actuales `wifi`, `location`, `sim`, `bno055`.

**Validación de pines (firmware = fuente de verdad, repetida en JS):**
| Regla | Pines | Acción |
|---|---|---|
| Flash interna | 6-11 | Rechazar |
| UART0 | 1, 3 | Rechazar |
| Inexistentes o no expuestos en WROOM-32 | 20, 24, 28-31, 37, 38, >39 | Rechazar |
| GPIO0 (botón BOOT) | 0 | Rechazar en cualquier uso (salida, botón, LED, límite, entrada con pull-up externo): rompería el gesto BOOT de 5/10 s y el arranque |
| Solo entrada, sin pull-up | 34-39 | Rechazar como salida; avisar en botones sin pull-up externo |
| Strapping | 2, 5, 12, 15 | Avisar; GPIO12 alto al arrancar impide arrancar |
| ADC2 | 2, 4, 12-15, 25-27 | Rechazar como analógico (no funciona con WiFi); analógicos solo en ADC1 (32-39) |
| Duplicados | — | Rechazar, salvo el bus I2C compartido |
| Exclusiones | — | Reglas de `rotator_dependencies.h` (un sensor por eje, BNO055 requiere elevación, Luna/Sol requieren elevación y reloj, `AUTOPARK` requiere `PARK`, un solo RTC, etc.) y direcciones I2C repetidas |

**Caliente frente a reinicio:**
- En caliente: offsets, límites, velocidades, rampas, park/autopark, ubicación, zona horaria, umbrales de seguimiento, simulación, opciones de pantalla.
- Con reinicio («Guardar y reiniciar», `ESP.restart()`): pines, drivers, buses, polaridad, protocolo por puerto, WiFi.

**Exportar/importar:** `GET /config.json` (sin clave WiFi salvo casilla); `POST /api/config` valida todo y, si falla, devuelve lista de errores sin tocar la configuración vigente.

**Modo seguro y reset de fábrica:**
1. Primera instrucción de `setup()`: salidas en estado inactivo.
2. Contador de arranques en NVS (o RTC RAM); se pone a 0 tras 60 s estables con la web respondiendo, y entonces se copia `config.json` → `config.bak`.
3. Configuración inválida o 3 fallos seguidos → restaurar `config.bak`; si tampoco sirve → **modo seguro**: sin motores ni sensores, AP `K3NG-Setup-XXXX` y web.
4. BOOT (GPIO0) pulsado 5 s al arrancar → modo seguro con AP; 10 s → reset de fábrica (borra `config.json`; calibración conservada opcionalmente).
5. Comando `\FACTORY` por serie; por TCP solo con confirmación (`\FACTORY <código>` mostrado en la respuesta anterior) y con contraseña web definida.
6. Página mínima de recuperación (WiFi + OTA) incrustada en la app, por si falta LittleFS.

### 3.4 Primera puesta en marcha sin WiFi compilada
0. **Baudios:** hoy `Serial` va a 9600 (`rotator_settings_esp32.h:359` `CONTROL_PORT_BAUD_RATE`, `platformio.ini:18`) y ESP Web Tools habla Improv a 115200, así que el instalador no detectaría la placa. Decisión: el puerto USB pasa a **115200 por defecto**, configurable por web (9600 sigue disponible), con aviso en README, MIGRACION.md y notas de versión para usuarios de PstRotator y similares. Alternativa descartada: autodetectar baudios al arrancar (frágil con tramas GS-232 cortas).
1. **Improv Wi-Fi por serie** (librería `jnthas/Improv WiFi Library` o implementación propia). Demultiplexado en `Serial`: tramas con cabecera `IMPROV` → Improv; resto → GS-232. Activo solo sin WiFi guardada o en los primeros 60 s. Devuelve `http://<ip>/` para «Visit device».
2. **AP con portal cautivo:** si no hay credenciales o STA falla ~30 s. `K3NG-Rotor-XXXX` (MAC), `DNSServer`, mismo `WebServer`, respuestas a `/generate_204` y `/hotspot-detect.html`. Sirve el asistente completo. **Es nuevo:** `rotator_esp32_wifi.h` no tiene `softAP`; se crea desde cero. El AP lleva clave WPA2 derivada de la MAC, impresa por serie al arrancar y en la etiqueta/README (no AP abierto).
3. **Cambio de WiFi:** durante el cambio se usa AP+STA (web y TCP 23 accesibles por ambas interfaces); si la red nueva no conecta en 30 s, se vuelve a la anterior o al AP, sin perder acceso.

### 3.4 bis Seguridad
- Hoy `WEB_SERVER_PASSWORD` está vacío por defecto (`rotator_settings_esp32.h:487`): `/update`, `/api/config`, `/api/factory` y `\FACTORY` por TCP quedarían abiertos en la LAN o el AP.
- El asistente inicial obliga a fijar una contraseña; hasta entonces quedan bloqueados OTA, fábrica e importación de configuración (solo se permite configurar WiFi y la contraseña).
- Los POST exigen token CSRF (o cabecera propia `X-K3NG-Token` obtenida tras autenticarse). Basic Auth viaja en claro por HTTP: se documenta como limitación de red local de confianza.

### 3.5 Web
- **Páginas:** Estado (az/el, mover, parar), Asistente inicial (WiFi → ubicación → sensor AZ → sensor EL → salidas → prueba de movimiento → pantalla), Sensores, Salidas, Pantalla y entradas, Protocolos y puertos, Tiempo (NTP/GPS/RTC), Calibración (asistentes paso a paso), Sistema (exportar/importar, OTA, reinicio, fábrica, versión, licencia y enlace al código).
- **API:** `GET /api/schema`, `GET/POST /api/config`, `GET /config.json`, `GET /api/status`, `POST /api/move`, `GET /api/i2cscan`, `POST /api/calibrate`, `POST /update` (Basic Auth, detiene motores antes de grabar), `POST /api/restart`, `POST /api/factory`.
- **Escáner I2C** que propone el driver según las direcciones (0x28/0x29 BNO055, 0x3C/0x3D OLED, 0x20-0x27/0x38-0x3F expansor LCD, 0x68 RTC, 0x0D/0x1E magnetómetros).
- Sensores elegidos con *radio buttons* (uno por eje).

---

## 4. Hardware soportado

| Grupo | Fase 1 | Fase 2 | Descartado |
|---|---|---|---|
| Posición | Potenciómetro AZ/EL (ADC1) [ya funciona], HH-12/AS5045 absoluto y relativo [ya funciona], BNO055 EL [ya funciona], encoder incremental A/B/Z, entrada de pulsos, simulado | Rotary k3ng (sondeo), QMC5883 (Mecha), HMC5883L (librería local), ADXL345 (Adafruit), LSM303 (Pololu) | Ver sección 1 |
| Salidas | Relés CW/CCW/UP/DOWN con polaridad, PWM LEDC con frecuencia configurable, rampas, frenos | Paso a paso (`hw_timer` o RMT), salidas de frecuencia | TimerOne/Five |
| Pantallas | LCD I2C 16x2/20x4 (`hd44780_I2Cexp`, autodetección), OLED SSD1306/SH1106 (U8x8) | LCD 4 bits, Nextion por UART2 | Ver sección 1 |
| Entradas | Botones (CW, CCW, UP, DOWN, STOP, preset, Sol, Luna), pots de velocidad y preset (ADC1), LEDs, límites, detección de bloqueo | Encoders de preset | — |
| Reloj | NTP [ya funciona], GPS TinyGPS en `Serial2`, RTC DS3231/DS1307 | — | PCF8583 |
| Protocolos | Yaesu GS-232A/B, Easycom, DCU-1 por puerto (USB, TCP 23, TCP 24) + extendidos K3NG | — | Maestro/esclavo |
| Seguimiento | Sol, Luna, espera en el horizonte (`41a09e9`), park/autopark, autocorrect | Satélites (P13, TLE por web o Celestrak) | — |

[SUPUESTO] `\W` (WiFi) choca con `ANCILLARY_PIN_CONTROL` (`rotator_dependencies.h:192`): se reasigna el comando de WiFi para que ambos convivan.

---

## 5. Presupuesto de flash/RAM y particiones

- Hoy [VERIFICADO]: `board_build.partitions = min_spiffs.csv` (app0/app1 de 0x1E0000 = 1 966 080 B, SPIFFS 128 KB, coredump 64 KB). Binario ~1,108 MB (56 %), RAM estática ~53 KB (dato del encargo, se reconfirma en E0).
- Estimación [SUPUESTO]: +0,35 a 0,6 MB → **1,45-1,7 MB**; RAM estática 70-90 KB. El mayor desconocido son las ramas hoy eliminadas por el preprocesador.
- **Particiones propuestas (se fijan en E2 y no se cambian después de publicar):**
```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1C0000,
app1,     app,  ota_1,   0x1D0000, 0x1C0000,
littlefs, data, spiffs,  0x390000, 0x60000,
coredump, data, coredump,0x3F0000, 0x10000,
```
  App 1,75 MB ×2, LittleFS 384 KB.
- **Regla de decisión:** si la medición de E0 supera 1,6 MB, se mantiene `min_spiffs` con la web incrustada en gzip (PROGMEM) y se retira o recorta el coredump.
- Control en CI: fallo si la app supera el 90 % de la ranura. Meta en placa: `ESP.getMinFreeHeap()` > 40 KB [SUPUESTO] con WiFi, web y sensores activos.

---

## 6. Repositorio, upstream y licencia

- **Fork de GitHub** `Nautiluscl/k3ng_rotator_esp32_web` (nombre por confirmar) desde este repositorio. Antes: etiqueta `v1.0-esp32-port` en `esp32-port`; este repo queda congelado con correcciones críticas.
- Trabajo en la rama `runtime-config`; merge a `main` al cerrar cada etapa.
- Remoto `upstream` → `k3ng/k3ng_rotator_controller`; commit base en `docs/UPSTREAM.md`. Sin merges: revisión trimestral de `git log <base>..upstream/master -- k3ng_rotator_controller/` y portado manual de cambios de protocolo, seguimiento y sensores, con `Portado de upstream <hash>` en el commit.
- **GPL-3:** se mantiene `LICENSE`; `CREDITS.md` con K3NG (Anthony Good), X9X0 y los autores de cada librería (revisar compatibilidad de licencias). Cada binario publicado enlaza a la etiqueta y al commit exactos; la web «Acerca de» y el flasheador muestran versión, licencia y enlace al código.
- Carpetas ajenas al firmware (`k3ng_config_tool/`, `tle/`, `additions/`; `pstrotator/` ya ignorada): se mueven a `legacy/` o se eliminan del fork tras confirmarlo con el usuario.

---

## 7. Etapas

Esfuerzo relativo: S, M, L, XL. **Regla de subagentes:** encargos pequeños y cerrados (un módulo o una opción convertida + pruebas + un commit), con Opus 5.5 en esfuerzo bajo. Un subagente implementa y otro revisa ejecutando `pio test -e native` y `pio run`. La prueba en hardware la hace el usuario (ESP32-WROOM por USB) y se registra en `docs/MATRIZ_HW.md`. Cada etapa cierra con entrada en `BITACORA.md` (incluida la sección «Próximo paso» con una acción concreta) y ADR en `docs/DECISIONES.md` si hubo decisión estructural.

### E0. Fork, base y medición (S-M)
- **Objetivo:** fork operativo idéntico al port, con CI y medición real.
- **Tareas:** crear el fork y la rama; etiqueta `v1.0-esp32-port`; remoto `upstream`; `docs/UPSTREAM.md`, `CREDITS.md`, `docs/DECISIONES.md`, `docs/MATRIZ_HW.md`; CI `.github/workflows/build.yml` (`pio run`, control de tamaño); en un directorio temporal, compilar un perfil «todo activado» con las FEATURE compatibles con ESP32 para medir flash/RAM y listar choques de símbolos (`DateTime` RTClib/P13, `lcd`, `compass`, TimeLib).
- **Archivos:** `platformio.ini`, `.github/workflows/build.yml`, `docs/*`.
- **Entregables:** informe de tamaño y de choques en `docs/DECISIONES.md` (ADR-001).
- **Aceptación:** CI en verde; la placa arranca igual que v1.0 y pasan `tools/test_serie.py`, `test_red.py`, `test_potenciometros.py`, `test_horizonte.py`, `test_simulacion.py`, `test_config.py`.
- **Subagentes:** uno para CI y docs, uno para el perfil de medición.

### E1. Reorganización mínima sin cambio funcional (M)
- **Objetivo:** base compilable y mantenible antes de introducir configuración.
- **Tareas:** `.ino` → `src/k3ng_core.cpp`; `tools/gen_prototypes.py` (ctags o regex) que genera `k3ng_prototypes.h` y sustituye `rotator_prototypes_platformio.h` (84 líneas a mano); eliminar el grupo (d) y los perfiles de otras placas; `lib_ignore` de TimerOne, TimerFive, digitalWriteFast; fijar con versión exacta todas las librerías en `lib_deps`; pruebas nativas (Unity, entorno `native`) con casos sacados del K3NG: parser GS-232, overlap 360/450, cálculo de horizonte.
- **No se parte** el núcleo en módulos todavía.
- **Aceptación:** mismas pruebas de `tools/` en verde en placa; binario de tamaño ±2 % respecto a E0; `pio test -e native` en verde.

### E2. Configuración en tiempo de ejecución y pines (L)
- **Tareas:** `config/schema.cpp`, `store.cpp`, `validate.cpp`, `migrate.cpp`; `hal/pin_manager.cpp`; `rotator_pins_esp32.h` → macros a `cfg.pins.*` con `PIN_NONE`/`PIN_OK`; corregir `.ino:2722`; `initialize_pins()` lee la configuración; settings numéricos → macros `cfg.*`; `config_t` con layout fijo; migración de EEPROM y NVS; API `GET /api/schema`, `GET/POST /api/config`, `GET /config.json`; tabla de particiones definitiva (sección 5); script `tools/convert_ifdef.py` para convertir opciones de un solo uso; `tools/check_pins.py` en CI.
- **Recuperación mínima adelantada desde E3** (antes de aplicar pines leídos de la configuración): salidas inactivas en la primera línea de `setup()`, `config.bak` y reset de fábrica con BOOT 10 s. El resto (contador, modo seguro, AP) sigue en E3.
- **Migración desde v1.0:** la OTA no puede cambiar de `min_spiffs` a la tabla nueva, y el flasheo con borrado completo pierde NVS y EEPROM. Camino soportado: flasheo por USB **sin borrar** (NVS está en 0x9000/0x5000 en ambas tablas, así que se conserva), o exportar la configuración desde v1.0 antes. Se documenta en `docs/MIGRACION.md`.
- **Aceptación:** cambiar `rotate_cw` de 25 a 27 por web y reiniciar → el relé responde en 27; importar un JSON con GPIO 6, GPIO 0 o GPIO 34 como salida se rechaza con mensaje; la calibración sobrevive al cambio de pines y a un corte de luz; flasheando por USB sin borrar sobre un v1.0, la calibración se conserva (prueba explícita); una configuración mala se revierte con BOOT 10 s sin clic de relés.
- **Pruebas nativas:** validador (todas las reglas de la tabla 3.3, incluido GPIO0 rechazado como entrada y como salida, y 37/38 rechazados), migración de esquema, relleno de defectos.

### E3. Red, recuperación y OTA (M) — adelantada para no perder acceso a la placa
- **Tareas:** `recovery/` (contador, modo seguro, BOOT 5 s, `\FACTORY` con confirmación; lo mínimo ya está desde E2); portal cautivo nuevo (softAP con clave, AP+STA); seguridad de la sección 3.4 bis; Serial a 115200; página de recuperación incrustada; `POST /update` con Basic Auth y parada de motores; versión `-DFW_VERSION` en la web; Improv por serie con demultiplexado; verificar si el core 3.x precompilado permite rollback (`esp_ota_mark_app_valid_cancel_rollback()`) [SUPUESTO].
- **Aceptación:** una configuración que provoca fallo forzado lleva al AP `K3NG-Setup` al tercer reinicio y luego restaura `config.bak`; BOOT 10 s vuelve a fábrica; ningún relé hace clic al arrancar (comprobado con LED o multímetro); con la flash borrada se configura el WiFi solo con el móvil; una OTA conserva la configuración; una OTA cortada no inutiliza la placa; el instalador de ESP Web Tools detecta Improv por USB; cambiar a un SSID inexistente vuelve al AP en 30 s; **seguridad:** sin contraseña definida, `POST /update`, `/api/factory` e importación devuelven 403, un POST sin token CSRF se rechaza y el AP no es abierto.

### E4. Drivers de posición fase 1 (L)
- **Tareas:** `IPositionDriver`, registro, `I2CBus` con mutex; drivers pot, hh12, bno055, incremental, pulse, sim; borrar los `#ifdef` correspondientes en `read_azimuth()`/`read_elevation()`; escáner I2C; asistentes de calibración (mín/máx pot, cero encoder, offsets BNO055).
- **Aceptación:** sin recompilar, la misma placa funciona con pot+pot, HH-12+BNO055, encoder+pot y simulado; lectura dentro de ±1° de la referencia; cada combinación anotada en la matriz; tiempo máximo de `loop()` medido (ms) y sin reinicios por watchdog, con web e I2C activos.

### E5. Salidas y comportamiento (L-XL)
- **Tareas:** `out_relay`, `out_pwm_ledc` (`ledcAttach` con frecuencia configurable, `LedcAllocator`), frenos, límites, detección de bloqueo, polaridad (`ROTATE_PIN_ACTIVE_VALUE` → configuración); convertir los ~55 `#ifdef` del grupo (c) a `if (cfg.feat.*)` opción por opción, con compilación y pruebas tras cada una; `cfg.has_elevation`.
- **Aceptación:** placa de relés activa a nivel bajo sin clic al arrancar; un límite detiene la rotación; PWM a la frecuencia configurada (osciloscopio o LED); park/autopark y desactivar elevación por web funcionan; `test_horizonte.py` y `test_simulacion.py` en verde; tiempo máximo de `loop()` no peor que en E4 en más de un 20 %.

### E6. Protocolos por puerto (M)
- **Tareas:** compilar Yaesu, Easycom y DCU-1 juntos; `protocol/port_router.cpp` con `cfg.protocol[port]` para USB, TCP 23, TCP 24; extendidos `\` en todos; reasignar `\W`.
- **Aceptación:** PstRotator por Yaesu en TCP 23 y, a la vez, Easycom por USB; `test_serie.py` y `test_red.py` adaptados en verde; pruebas nativas de los tres parsers.

### E7. Pantallas y entradas locales (L)
- **Tareas:** convertir en `if` los bloques `#if FEATURE_*_DISPLAY` que envuelven las 88 llamadas a `k3ngdisplay` del `.ino`; quitar de `rotator_k3ngdisplay.h:34-71` la inclusión de perfiles `HARDWARE_*` y el acoplamiento con `rotator_features.h:81` (esfuerzo estimado: la mitad de la etapa); `ITextDisplay`; reincluir `rotator_k3ngdisplay.cpp` (quitarlo de `build_src_filter`, `platformio.ini:27`); `disp_hd44780_i2c`, `disp_oled_u8x8`; `OPTION_DISPLAY_*` como máscara; botones, pots (ADC1), LEDs configurables.
- **Aceptación:** LCD 20x4 detectado solo y OLED, alternando desde la web; botones CW/CCW/STOP mueven el rotor.

### E8. Reloj y GPS (S)
- **Tareas:** GPS en `Serial2` con pines configurables (`UartAllocator`), RTC DS3231/DS1307 con detección en 0x68, prioridad NTP > GPS > RTC.
- **Aceptación:** sin WiFi, la hora de GPS o RTC permite seguir el Sol; `test_horizonte.py` en verde.

### E9. Fase 2 (L, puede ir en paralelo con E10)
- Sensores prioridad 2, LCD 4 bits, Nextion, paso a paso con `hw_timer`/RMT, satélites con TLE.
- **Aceptación:** cada función con su fila completa en la matriz.

### E10. Paridad y beta (M)
- **Tareas:** matriz con el alcance de **fase 1** (E9 no la bloquea), documentación de usuario (README con capturas), `docs/MIGRACION.md` desde el port v1.0 (con aviso de 115200 baudios), medición de heap.
- **Aceptación (verificable por el usuario):** al menos 6 combinaciones de fase 1 probadas por el usuario en la matriz, lista de issues de la beta cerrada o diferida con motivo, heap mínimo > 40 KB. Si hay radioaficionados externos, la beta dura como máximo 4 semanas; su ausencia no bloquea E11.

### E11. Flasheador web (M) — solo tras aprobar E10
- **Tareas:** `.github/workflows/release.yml` (tag `v*`): `pio run`, `pio run -t buildfs`, `esptool merge_bin` (bootloader 0x1000, particiones 0x8000, boot_app0 0xE000, app 0x10000, LittleFS en el offset de la tabla fijada en E2) → `factory.bin`; `firmware.bin` para OTA en el Release; `flasher/` con ESP Web Tools v10 en GitHub Pages; `manifest.json` con `new_install_prompt_erase: true` y `new_install_improv_wait_time: 10`; binarios copiados a Pages (los assets de Releases no envían CORS [SUPUESTO conocido]); página con requisitos (Chrome/Edge de escritorio, cable de datos, drivers CP210x/CH340), licencia y enlace al código; `manifest-beta.json` opcional.
- **Aceptación:** un tercero sin herramientas instaladas flashea con Chrome, configura WiFi por Improv, elige sus sensores en la web y mueve el rotor.

**Peso:** E1-E5 son la parte crítica (~60 % del esfuerzo).

---

## 8. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| El binario no cabe | Medición en E0; `-Os`, gc-sections; U8x8 en vez de U8g2; plan B `min_spiffs` con web incrustada |
| Choques de símbolos (`DateTime`, `lcd`, `compass`, TimeLib) | Un `.cpp` por driver, `hd44780` como única librería LCD, `TimeLib.h` explícito |
| Errores al convertir 1884 `#if` | Script por opción, empezando por las de un solo uso, CI y pruebas de `tools/` tras cada conversión |
| Ramas nunca compiladas en ESP32 con fallos ocultos | Solo se activan las del alcance; pruebas nativas y matriz |
| Configuración que inutiliza la placa o activa relés | Salidas inactivas primero, modo seguro, `config.bak`, BOOT, `\FACTORY`, «borrar todo» en el flasheador |
| Heap insuficiente | Drivers con `new` solo si activos, ArduinoJson acotado, meta > 40 KB |
| Bus I2C compartido (clock stretching del BNO055, direcciones repetidas) | `I2CBus` con mutex, escáner, rechazo de conflictos |
| Improv y GS-232 en el mismo puerto | Demultiplexado por cabecera y ventana limitada |
| Cambiar la tabla de particiones tras publicar | Se fija en E2 |
| Migrar desde v1.0 (OTA no cambia particiones; borrado completo pierde NVS) | Flasheo USB sin borrar, exportar antes; `docs/MIGRACION.md` |
| Improv no detectado a 9600 baudios | Serial a 115200 por defecto (sección 3.4) |
| Web/OTA/fábrica abiertas sin contraseña | Contraseña obligatoria, CSRF, AP con clave (3.4 bis) |
| Latencia de `loop()` y watchdog al compilar todas las ramas | Medir el máximo en E4/E5; drivers no bloqueantes; `yield()` en bucles largos |
| `WebServer` síncrono e I2C en el mismo `loop()` (el mutex no protege nada en un solo hilo) | Los manejadores web no tocan I2C directamente: leen la última muestra en caché y piden escaneos/calibraciones como trabajo diferido en `loop()`; el mutex solo hace falta si algún driver pasa a una tarea FreeRTOS |
| Pin sin convertir con `PIN_NONE = -1` | `tools/check_pins.py` en CI y funciones `*Enhanced` que ignoran pines negativos |
| Regresiones al reorganizar | E1 sin cambio funcional; pruebas nativas con casos del K3NG antes de tocar el núcleo |
| Corte por límite de tokens | Ver sección 9.3 |

---

## 9. Pruebas, CI y continuidad

### 9.1 Pruebas
1. **Nativas (`pio test -e native`, Unity):** validador de configuración y pines, migración, parsers GS-232/Easycom/DCU-1, overlap, horizonte.
2. **En placa con simulación:** sensor simulado elegible por web como configuración de prueba por defecto.
3. **`tools/` contra la placa:** parametrizadas por IP/puerto y perfil; cada prueba aplica su configuración por `POST /api/config` antes de ejecutarse.
4. **Matriz de hardware** (`docs/MATRIZ_HW.md`): filas = combinaciones AZ×EL y pantallas; columnas = arranque, lectura, movimiento, cambio por web, reinicio con configuración persistida, fecha, commit. Lo no probado se marca así.

### 9.2 CI
- En cada push: `pio test -e native`, `pio run`, `pio run -t buildfs`, control de tamaño de app y LittleFS frente a la tabla de particiones.
- En cada tag `v*`: release (E11).

### 9.3 Ejecución y continuidad
- Orquestador: lee este plan, `BITACORA.md` y `docs/DECISIONES.md`; lanza subagentes Opus 5.5 en esfuerzo bajo por encargo.
- Al cerrar cada sesión: actualizar «Próximo paso» en `BITACORA.md` y hacer commit.
- Si se acaban los tokens: **cron o `at` local** (no `/schedule`, cuyos agentes en la nube no ven el repositorio local ni la placa por USB) a la hora de reinicio de la sesión. Lanza `tools/continuar.sh`, que:
  1. Toma un bloqueo con `flock -n /tmp/k3ng_fork.lock` (si ya hay una sesión, sale).
  2. Hace `cd` al directorio del fork y comprueba `git status --porcelain`: si hay trabajo a medias sin commit, lo guarda en una rama `wip/<fecha>` antes de seguir.
  3. Ejecuta `claude -p "Lee BITACORA.md (Próximo paso) y PLAN_FORK_CONFIG_WEB.md y continúa" --permission-mode acceptEdits --allowedTools "Bash(pio *),Bash(git *),Read,Edit,Write"`, con salida a `logs/continuar_<fecha>.log`.
- Las pruebas en hardware no se automatizan: quedan anotadas como pendientes para el usuario en `docs/MATRIZ_HW.md`.

---

## 10. Preguntas abiertas para el usuario

1. Nombre del repositorio fork (propuesta: `k3ng_rotator_esp32_web`) y si debe ser público desde el principio.
2. ¿Qué hardware tienes disponible para la matriz (sensores, LCD I2C, OLED, placa de relés activa a nivel bajo, GPS, RTC)?
3. ¿Hay usuarios del port v1.0 cuya calibración haya que migrar, o basta con empezar de cero?
4. ¿Se acepta perder la compatibilidad de diff con upstream a cambio de modularizar, o se prefiere mantener el núcleo en un solo `.cpp`?
5. ¿Prioridad de la fase 2: satélites, magnetómetros, Nextion o paso a paso?
6. ¿Se necesita soporte de otras placas (ESP32-S3, C3, módulos de 8/16 MB) en el flasheador, o solo WROOM-32 de 4 MB?
7. ¿Qué hacer con `k3ng_config_tool/`, `tle/`, `additions/`?
8. ¿Hay radioaficionados externos para la beta? (opcional; no bloquea E11)
9. Idioma de la web: ¿solo español, o español e inglés?
10. ¿Se acepta el cambio del puerto USB a 115200 baudios por defecto (los usuarios de PstRotator tendrían que ajustarlo)?

<!-- PLAN COMPLETO -->
