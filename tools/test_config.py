#!/usr/bin/env python3
"""Prueba de la página de configuración contra el ESP32 real.

Cambia los ajustes, comprueba que se aplican y se conservan tras reiniciar, prueba la
validación y el modo simulación, y al final deja todo como estaba. No toca la red WiFi.

Uso: tools/test_config.py [host]   (por defecto rotor.local)
"""
import json
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HOST = socket.gethostbyname(sys.argv[1] if len(sys.argv) > 1 else "rotor.local")
BASE = "http://%s" % HOST
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%s %-50s %s" % ("OK  " if ok else "FALLO", name, detail))


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as r:
        return r.status, r.read()


def cfg():
    return json.loads(get("/api/config")[1])


def post(path, data=None):
    req = urllib.request.Request(BASE + path, data=urllib.parse.urlencode(data or {}).encode(), method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def wait_up(timeout=40):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            return cfg()
        except Exception:
            time.sleep(1)
    return None


code, page = get("/config")
check("GET /config", code == 200 and b"Configuraci" in page and b"Modo simulaci" in page, "%d B" % len(page))
code, main = get("/")
check("Enlace a /config en la página principal", b'href="/config"' in main)

c0 = cfg()
check("GET /api/config", "cfg" in c0 and "az_start" in c0["cfg"],
      "ver=%s heap=%d KB sim=%d" % (c0["ver"], c0["heap"] // 1024, c0["sim"]))
check("La contraseña WiFi no se expone", "pass" not in json.dumps(c0["wifi"]).lower())
original = dict(c0["cfg"])

# cambio de ajustes
new = dict(original, az_start=10, az_cap=450, el_offset=1.5, sun_thr=1.2, moon_check=6000)
code, j = post("/api/config", new)
time.sleep(0.8)
c1 = cfg()["cfg"]
check("POST /api/config aplica los valores", code == 200 and all(abs(c1[k] - new[k]) < 0.01 for k in new),
      "az_start=%s az_cap=%s el_offset=%s sun_thr=%s" % (c1["az_start"], c1["az_cap"], c1["el_offset"], c1["sun_thr"]))

# validación
code, j = post("/api/config", dict(new, az_cap=5000))
check("Rechaza un valor fuera de rango", code == 400, j.get("msg", ""))
code, j = post("/api/config", dict(new, tz="abc"))
check("Rechaza un valor no numérico", code == 400, j.get("msg", ""))
code, j = post("/api/wifi", {"ssid": "", "pass": ""})
check("Rechaza una red WiFi vacía", code == 400, j.get("msg", ""))
code, j = post("/api/wifi", {"ssid": "x", "pass": "corta"})
check("Rechaza una contraseña WPA corta", code == 400, j.get("msg", ""))

# simulación desde la web
code, j = post("/api/sim", {"on": 1})
time.sleep(0.6)
check("Activar simulación desde /config", code == 200 and cfg()["sim"] == 1, j.get("msg", ""))

# persistencia tras reinicio
code, j = post("/api/restart")
check("POST /api/restart", code == 200, j.get("msg", ""))
time.sleep(4)
c2 = wait_up()
check("Vuelve tras el reinicio", c2 is not None, "uptime=%s s rst=%s" % (c2 and c2["uptime"], c2 and c2["rst"]))
if c2:
    check("Los ajustes se conservan tras reiniciar", all(abs(c2["cfg"][k] - new[k]) < 0.01 for k in new))
    check("La simulación se conserva tras reiniciar", c2["sim"] == 1)

# restaurar
post("/api/sim", {"on": 1 if c0["sim"] else 0})
code, j = post("/api/config", original)
time.sleep(0.8)
c3 = cfg()
check("Ajustes y simulación restaurados", code == 200 and c3["sim"] == c0["sim"] and
      all(abs(c3["cfg"][k] - original[k]) < 0.01 for k in original))

print("\n%d/%d pruebas correctas" % (sum(results), len(results)))
