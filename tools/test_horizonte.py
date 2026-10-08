#!/usr/bin/env python3
"""Prueba del seguimiento con el astro bajo el horizonte, contra el ESP32 real en simulación.

Activa el seguimiento del Sol o de la Luna (el que esté bajo el horizonte) y comprueba que el
rotor va al azimut del astro con 0° de elevación. Comprueba también los créditos de las dos
páginas. Al final para el seguimiento y deja la simulación como estaba.

Uso: tools/test_horizonte.py [host]   (por defecto rotor.local)
"""
import json
import socket
import sys
import time
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
        return r.read()


def status():
    return json.loads(get("/api/status"))


def post(path, data=None):
    req = urllib.request.Request(BASE + path, data=urllib.parse.urlencode(data or {}).encode(), method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read())


def az_diff(a, b):
    return abs((a - b + 180) % 360 - 180)


for path in ("/", "/config"):
    page = get(path).decode("utf-8")
    check("Créditos en %s" % path, "K3NG" in page and "github.com/X9X0/k3ng_rotator_controller" in page)

s0 = status()
check("Hora válida (necesaria para el seguimiento)", s0["time_ok"] == 1, s0["utc"])
below = [t for t in ("sun", "moon") if s0[t]["el"] < 0]
if not below:
    print("El Sol y la Luna están sobre el horizonte: no se puede probar ahora")
    sys.exit(1)
target = below[0]
sim0 = s0["sim"]

post("/api/sim", {"on": 1})
post("/api/track", {"target": target, "on": 1})
# el K3NG no corrige el azimut por debajo de AZIMUTH_TOLERANCE (3° por defecto)
t0, s = time.time(), s0
while time.time() - t0 < 120:
    time.sleep(2)
    s = status()
    if az_diff(s["az"], s[target]["az"]) <= 3.5 and abs(s["el"]) < 1:
        break
check("Seguimiento de %s activo" % target, s[target]["trk"] == 1)
check("Azimut del astro bajo el horizonte", az_diff(s["az"], s[target]["az"]) <= 3.5,
      "rotor %.1f°, %s %.1f°" % (s["az"], target, s[target]["az"]))
check("Elevación a 0° (astro a %.1f°)" % s[target]["el"], abs(s["el"]) < 1, "rotor %.1f°" % s["el"])

post("/api/stop")
post("/api/sim", {"on": sim0})
check("Seguimiento parado y simulación restaurada", status()[target]["trk"] == 0 and status()["sim"] == sim0)

print("\n%d/%d pruebas correctas" % (sum(results), len(results)))
