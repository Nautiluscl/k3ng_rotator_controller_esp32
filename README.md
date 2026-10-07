# K3NG Rotator Controller para ESP32 con WiFi

Port del [K3NG Rotator Controller](https://github.com/k3ng/k3ng_rotator_controller) a un
**ESP32-WROOM-32** para controlar un rotor de azimut y elevación por red, sin cable USB hasta
el PC:

- **Puerto serie virtual por WiFi:** un servidor TCP en el puerto 23 acepta los mismos
  comandos que el puerto USB (Yaesu GS-232A/B y comandos `\` del K3NG). Funciona con
  PstRotator, Gpredict o hamlib, o con cualquier programa que abra un puerto COM a través de
  un puerto COM virtual.
- **Servidor web para el móvil** en `http://rotor.local/`, con posición, botones de
  movimiento, seguimiento del Sol y de la Luna, y STOP.
- **Azimut** con encoder absoluto **HH-12** (AS5045, SSI).
- **Elevación (inclinación)** con un **Bosch BNO055** por I2C.
- **Hora por NTP**, necesaria para seguir el Sol y la Luna, sin GPS ni RTC.

Este repositorio es un fork. La documentación original del fork, en inglés, con la herramienta
de configuración en Python, está en [README.en.md](README.en.md). El registro de desarrollo del
port está en [BITACORA.md](BITACORA.md) y el plan técnico en
[PLAN_PORT_ESP32.md](PLAN_PORT_ESP32.md).

> **Estado:** compila sin errores ni avisos para `esp32dev`, pero **todavía no se ha probado
> sobre el hardware real**. La interfaz web se probó contra un rotor simulado
> (`tools/web_preview.py`). Antes de conectar motores hay que hacer las pruebas de banco del
> apartado [Puesta en marcha](#puesta-en-marcha).

---

## Índice

1. [Hardware](#hardware)
2. [Compilar y cargar](#compilar-y-cargar)
3. [Configuración](#configuración)
4. [Puesta en marcha](#puesta-en-marcha)
5. [Interfaz web](#interfaz-web)
6. [Puerto serie virtual (TCP)](#puerto-serie-virtual-tcp)
7. [Comandos nuevos](#comandos-nuevos)
8. [Seguridad y robustez](#seguridad-y-robustez)
9. [Limitaciones](#limitaciones)
10. [Estructura del código](#estructura-del-código)

---

## Hardware

### Placa

Una **ESP32-WROOM-32** estándar (entorno `esp32dev` de PlatformIO). No se contemplan otras
variantes de ESP32.

### Conexiones

| Función | GPIO | Notas |
|---|---|---|
| Rotar CW (derecha) | 25 | Salida a relé o driver |
| Rotar CCW (izquierda) | 26 | Salida a relé o driver |
| Elevar (arriba) | 32 | Salida a relé o driver |
| Bajar (abajo) | 33 | Salida a relé o driver |
| HH-12 CLK | 18 | Salida |
| HH-12 CS | 17 | Salida |
| HH-12 DO (datos) | 19 | Entrada. **Necesita adaptador de nivel si el HH-12 va a 5 V** |
| BNO055 SDA | 21 | I2C |
| BNO055 SCL | 22 | I2C |
| BNO055 RST | 23 | Opcional; se activa con `BNO055_RESET_PIN 23` |

El mapa completo y los pines que se evitan a propósito (flash, pines de arranque, ADC2 con
WiFi) están en `k3ng_rotator_controller/rotator_pins_esp32.h`.

### Recomendaciones eléctricas

- **3,3 V:** las salidas del ESP32 son de 3,3 V. Hay que comprobar que los relés o los
  drivers de motor conmutan con ese nivel.
- **Pull-down en las salidas:** durante el arranque y en un reinicio por watchdog, los GPIO
  quedan como entradas. Una resistencia de pull-down (10 kΩ) en cada salida de relé garantiza
  que el rotor no se mueva en ese momento.
- **Alimentación separada:** el WiFi consume picos de unos 300 mA. Si el ESP32 comparte
  fuente con los motores, el arranque de estos puede reiniciarlo por caída de tensión
  (*brownout*). Conviene una fuente aparte o un buen desacoplo. La web y el puerto de
  control muestran el motivo del último reinicio, y `BROWNOUT` delata este problema.
- **Cable I2C hasta el BNO055:** el bus I2C no está pensado para cables largos. Por encima de
  aproximadamente 1 m conviene un extensor I2C diferencial (P82B715, PCA9615) o cable corto y
  apantallado.

### Montaje del BNO055

La elevación se calcula a partir del vector de gravedad:
`elevación = atan2(numerador, denominador)`. Con la configuración por defecto, el sensor va
**plano** (eje Z vertical) con el **eje Y a lo largo del boom**:

- antena horizontal → 0°
- antena apuntando al cénit → 90°

Si el sensor se monta de otra forma, se cambian los ejes y el signo en
`rotator_settings_esp32.h`:

```c
#define BNO055_ELEVATION_NUMERATOR(g) (g.y())
#define BNO055_ELEVATION_DENOMINATOR(g) (g.z())
#define BNO055_ELEVATION_INVERT 0
```

El BNO055 trabaja en modo **IMUPLUS**, que usa acelerómetro y giróscopo pero no el
magnetómetro. Así el campo magnético de los motores no altera la lectura.

---

## Compilar y cargar

Requisitos: [PlatformIO](https://platformio.org/), desde VS Code o desde la línea de comandos.

```bash
pio run -e esp32                 # compilar
pio run -e esp32 -t upload       # cargar en la placa
pio device monitor               # puerto de control USB (9600 baudios)
```

`tools/build_esp32.sh` compila y muestra solo los errores y el uso de memoria. El registro
completo queda en `.pio/build_esp32.log`.

La plataforma `espressif32` instala sola el core Arduino-ESP32 3.x. Las librerías de Adafruit
del BNO055 se descargan solas (`lib_deps`). Las propias del K3NG (HH-12, Sol y Luna, TimeLib)
están en `libraries/`.

---

## Configuración

Todo el perfil ESP32 está en tres archivos de `k3ng_rotator_controller/`:

| Archivo | Contenido |
|---|---|
| `rotator_features_esp32.h` | Funciones activas (`FEATURE_*`, `OPTION_*`) |
| `rotator_pins_esp32.h` | Pines |
| `rotator_settings_esp32.h` | Ajustes: WiFi, web, BNO055, rango de giro, etc. |

El perfil se activa con `-DHARDWARE_ESP32_WIFI` en `platformio.ini`, de modo que los archivos
genéricos (`rotator_features.h`, etc.) no se usan.

### Red WiFi

Se pueden fijar valores por defecto antes de compilar:

```c
#define WIFI_DEFAULT_SSID "mi_red"
#define WIFI_DEFAULT_PASSWORD "mi_clave"
#define WIFI_HOSTNAME "rotor"          // → rotor.local
```

También se pueden cambiar **sin recompilar**, por el puerto USB:

```
\WSmi_red
\WPmi_clave
\WR
```

Los datos se guardan en flash y tienen prioridad sobre los de compilación. `\WD` vuelve a los
valores de compilación. La IP se obtiene por DHCP; para fijarla se pone `WIFI_USE_DHCP 0` y
se rellenan `WIFI_STATIC_*`.

### Ubicación de la estación

Hace falta para calcular la posición del Sol y de la Luna. Se fija con el locator Maidenhead
de 6 caracteres, de dos maneras:

- desde la web, en el apartado *Locator*
- por el puerto de control: `\GFF46pn`

Se guarda en flash. En el firmware original se perdía en cada reinicio.

### Rango de giro

El HH-12 es un encoder absoluto de una vuelta. Por eso el rango por defecto es de 0° a 360°
(`AZIMUTH_STARTING_POINT_EEPROM_INITIALIZE 0`, `AZIMUTH_ROTATION_CAPABILITY_EEPROM_INITIALIZE 360`).
Se ajusta en `rotator_settings_esp32.h` o con los comandos `\I` y `\J` del K3NG.

### Contraseña de la web

Por defecto la web no pide contraseña. Para activarla:

```c
#define WEB_SERVER_USER "admin"
#define WEB_SERVER_PASSWORD "una_clave"
```

---

## Puesta en marcha

Hay que seguir este orden, **con los motores desconectados** hasta el punto 6:

1. **Arranque:** cargar el firmware y abrir el monitor serie. Deben aparecer
   `Reset reason: POWER_ON`, `BNO055: OK`, `WiFi: connecting to …` y, en unos segundos,
   `WiFi: connected, IP …` y `NTP: clock set …`.
2. **Azimut:** girar el HH-12 a mano y comprobar con el comando `C` que el azimut sigue al
   eje en todo el giro, sin saltos y en el sentido correcto. Si va al revés, activar
   `OPTION_REVERSE_AZ_HH12_AS5045`. Para alinear con el norte se usa el offset del K3NG.
3. **Elevación:** inclinar el BNO055 y comprobar con `C2` que 0° es la horizontal y 90° el
   cénit. Si no coincide, ajustar los ejes como se explica en
   [Montaje del BNO055](#montaje-del-bno055).
4. **Calibración del BNO055:**
   - `\XB` muestra el estado (`SYS`, `G` y `A` de 0 a 3).
   - Dejar el sensor quieto unos segundos para calibrar el giróscopo (`G:3`).
   - Colocarlo en seis posiciones distintas (cada cara hacia arriba), quieto unos segundos
     en cada una, hasta llegar a `A:3`.
   - `\XBS` guarda la calibración, que se restaura en cada arranque. `\XBC` la borra.
5. **Red:** abrir `http://rotor.local/` desde el móvil y comprobar que los datos se
   actualizan. Probar `nc rotor.local 23` y enviar `C2`.
6. **Motores:** conectar los relés y probar cada botón de la web con pulsaciones cortas.
   Comprobar que **STOP** detiene ambos ejes y que el eje se para al soltar el botón.

---

## Interfaz web

Se abre en `http://rotor.local/`. Si el móvil no resuelve `.local` (algunos Android), se usa
la IP que muestra `\WI`.

```
┌───────────────────────────────┐
│ AZIMUT          ELEVACIÓN     │
│ 123.4°          45.0°         │
│ girando CW                    │
├───────────────────────────────┤
│           [ ▲ ARRIBA ]        │
│  [ ◀ CCW ]  [ STOP ]  [ CW ▶ ]│
│           [ ▼ ABAJO ]         │
├───────────────────────────────┤
│ [☀ Seguir Sol] [☾ Seguir Luna]│
├───────────────────────────────┤
│ Red      ● Casa_2.4G          │
│ Señal    -61 dBm (Buena)      │
│ IP · Uptime · Último reinicio │
│ Hora UTC · Sensor EL · Locator│
│ [ Locator ______ ] [Guardar]  │
└───────────────────────────────┘
```

| Zona | Función |
|---|---|
| Azimut / Elevación | Posición actual y si el eje se está moviendo |
| Cruz de botones | **Mantener pulsado** para mover: ▲ arriba, ▼ abajo, ◀ CCW, ▶ CW. Al soltar, el eje se para |
| STOP | Parada inmediata de los dos ejes y fin de cualquier seguimiento |
| ☀ Seguir Sol / ☾ Seguir Luna | Activan o desactivan el seguimiento; muestran la posición del astro. Si está bajo el horizonte, el rotor espera a que salga |
| Información | Red y señal (RSSI), IP, uptime, último reinicio, hora UTC, estado del BNO055 y locator |

Mover un eje a mano desactiva el seguimiento activo.

### Seguridad del movimiento manual

Mientras se mantiene pulsado un botón, la página repite la orden cada 250 ms. Si el
firmware pasa **1 s sin recibirla** (móvil bloqueado, WiFi caído, pestaña cerrada), detiene
el eje. Un corte de red nunca deja el rotor girando.

### Vista previa sin hardware

```bash
tools/web_preview.py          # http://localhost:8080/
```

Extrae la página del firmware y la sirve con un rotor simulado. Sirve para revisar el diseño
desde el navegador o el móvil, en la misma red que el PC.

### API

La usa la página, pero sirve también para scripts:

| Método y ruta | Parámetros | Acción |
|---|---|---|
| `GET /api/status` | | Estado en JSON |
| `POST /api/move` | `dir=cw\|ccw\|up\|down\|release`; con `release`, `axis=az\|el` (opcional) | Movimiento mantenido: hay que repetir la orden antes de 1 s o el eje se para. `sid`, `seq` y `first` los usa la página; un script puede omitirlos |
| `POST /api/stop` | | Parada de todo |
| `POST /api/track` | `target=sun\|moon`, `on=1\|0` | Seguimiento |
| `POST /api/locator` | `grid=FF46pn` | Ubicación de la estación |

---

## Puerto serie virtual (TCP)

El ESP32 escucha en `rotor.local:23`. Cada línea terminada en CR se procesa igual que en el
puerto USB, con los comandos Yaesu GS-232 y los comandos `\`. Admite un cliente; si se
conecta otro, sustituye al anterior.

### Programas con TCP directo

- **hamlib / Gpredict:**
  ```bash
  rotctld -m 603 -r rotor.local:23     # 603 = Yaesu GS-232B
  ```
  Después, en Gpredict, se configura un rotor `localhost:4533`.
- **PstRotator:** configurar el controlador como *Yaesu GS-232B* con conexión TCP/IP a
  `rotor.local`, puerto 23.

### Programas que solo abren puertos COM

- **Windows:** HW VSP3 (HW group) o com0com con hub4com crean un COM virtual que reenvía a
  `rotor.local:23`.
- **Linux:**
  ```bash
  socat pty,link=/tmp/ttyROT,raw,echo=0 tcp:rotor.local:23
  ```
  El programa abre `/tmp/ttyROT`.

### Prueba rápida

```bash
nc rotor.local 23
C2            # → AZ=123 EL=045
M180          # girar a 180°
S             # parar
```

---

## Comandos nuevos

Se suman a los del K3NG (`H` muestra la ayuda de los comandos Yaesu):

| Comando | Función |
|---|---|
| `\WI` (o `\W`) | Estado WiFi: red, IP, RSSI, nombre mDNS y NTP |
| `\WS<ssid>` | Guardar el SSID (respeta mayúsculas) |
| `\WP<clave>` | Guardar la contraseña (respeta mayúsculas) |
| `\WR` | Reconectar con los datos guardados |
| `\WD` | Volver a la red de `rotator_settings_esp32.h` |
| `\XB` | Estado y calibración del BNO055 |
| `\XBS` | Guardar la calibración del BNO055 |
| `\XBC` | Borrar la calibración guardada |
| `\Gxxxxxx` | Locator de la estación; ahora se guarda en flash |

---

## Seguridad y robustez

- **Sensor de elevación:**
  - Si el BNO055 deja de responder (10 lecturas inválidas seguidas), la elevación se detiene,
    se desactiva el seguimiento y no se acepta ninguna orden de elevación hasta que vuelva.
  - Cada 10 s se reintenta la inicialización, solo con el rotor parado.
- **Control de motores aislado de la web:** el servidor web corre en su propia tarea (núcleo
  0). Un cliente lento no puede frenar el `loop()` que controla los motores (núcleo 1).
- **Watchdog:** si el `loop()` se cuelga más de 5 s, el ESP32 se reinicia y los relés se
  sueltan.
- **WiFi no bloqueante:** el rotor funciona por USB aunque no haya red, y se reconecta solo.
- **Red local:** el puerto 23 no tiene autenticación, como en el firmware original, y la web
  la tiene solo si se activa. **No hay que exponer el rotor a internet**; para acceder desde
  fuera conviene una VPN.

---

## Limitaciones

- Solo se ha probado la compilación y la interfaz web con simulación; falta la prueba sobre
  el hardware real.
- No están adaptadas al ESP32, y dan error de compilación si se activan:
  - pantallas LCD
  - encoders incrementales y entradas de pulsos
  - motores paso a paso
  - seguimiento de satélites
- Sin NTP, el seguimiento del Sol y de la Luna no se puede activar. La hora también se puede
  fijar a mano con `\O`, pero el NTP la sobrescribe cuando vuelve.
- El puerto TCP admite un solo cliente a la vez.

---

## Estructura del código

```
platformio.ini                      entorno esp32 (esp32dev)
k3ng_rotator_controller/
  k3ng_rotator_controller.ino       firmware K3NG (con los cambios del port)
  rotator_features_esp32.h          perfil ESP32: funciones
  rotator_pins_esp32.h              perfil ESP32: pines
  rotator_settings_esp32.h          perfil ESP32: ajustes
  rotator_esp32_wifi.h              WiFi, puerto TCP, NTP, comandos \W, ubicación
  rotator_esp32_web.h               servidor web y página
  rotator_prototypes_platformio.h   prototipos que PlatformIO no genera solo
libraries/                          librerías del K3NG (hh12, moon2, sunpos, TimeLib…)
tools/
  build_esp32.sh                    compilación con resumen
  web_preview.py                    vista previa de la web con rotor simulado
BITACORA.md                         registro de desarrollo del port
```

## Licencia

GPL-3.0, como el proyecto original de Anthony Good, K3NG. Ver [LICENSE](LICENSE).
