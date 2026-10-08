#!/usr/bin/env python3
"""Prueba de la calibración de potenciómetros desde la web, contra el ESP32 real.

Requiere el firmware compilado con ESP32_SENSORS_POTENTIOMETERS. Sin potenciómetros conectados
las entradas quedan al aire y la lectura es arbitraria: la prueba solo comprueba que la API
responde de forma coherente con esa lectura (guarda el extremo o explica por qué no).
Con potenciómetros reales, la calibración se hace a mano desde /config, con el rotor en cada tope.

Uso: tools/test_potenciometros.py [host]   (por defecto rotor.local)
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
        return r.read()


def cfg():
    return json.loads(get("/api/config"))


def post(path, data=None):
    req = urllib.request.Request(BASE + path, data=urllib.parse.urlencode(data or {}).encode(), method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


page = get("/config").decode("utf-8")
check("Tarjeta de potenciómetros en /config", 'id="cpot"' in page and "/api/potcal" in page)

c = cfg()
p = c["pot"]
check("Firmware con potenciómetros en AZ y EL", p["az"] == 1 and p["el"] == 1)
check("Lecturas del ADC en rango (0 a 4095)", 0 <= p["adc_az"] <= 4095 and 0 <= p["adc_el"] <= 4095,
      "az=%d el=%d" % (p["adc_az"], p["adc_el"]))
check("Sensor EL informado como potenciómetro", json.loads(get("/api/status"))["bno"] == "Potenciómetro")
check("Tarjeta del BNO055 oculta (sin BNO055)", c["bno"]["present"] == 0)

code, j = post("/api/potcal", {"action": "nada"})
check("Rechaza una acción no válida", code == 400, j.get("msg", ""))

# cada extremo: o se guarda con la lectura actual, o se rechaza por saturación o cercanía
pairs = [("az_ccw", "adc_az", "az_ccw", "az_cw"), ("az_cw", "adc_az", "az_cw", "az_ccw"),
         ("el_down", "adc_el", "el_0", "el_max"), ("el_up", "adc_el", "el_max", "el_0")]
for action, adc_key, cal_key, other_key in pairs:
    before = cfg()["pot"]
    code, j = post("/api/potcal", {"action": action})
    time.sleep(0.8)
    after = cfg()["pot"]
    if code == 200:
        ok = abs(after[cal_key] - before[adc_key]) < 150
        detail = "guardado %d (lectura %d)" % (after[cal_key], before[adc_key])
    else:
        close = abs(before[adc_key] - before[other_key]) < 200
        ok = (before[adc_key] >= 4095 or close) and after[cal_key] == before[cal_key]
        detail = "rechazado: %s" % j.get("msg", "")
    check("Calibración %s coherente" % action, ok, detail)

final = cfg()["pot"]
check("Extremos nunca iguales (evita división por cero)",
      final["az_ccw"] != final["az_cw"] and final["el_0"] != final["el_max"],
      "az %d/%d el %d/%d" % (final["az_ccw"], final["az_cw"], final["el_0"], final["el_max"]))

print("\n%d/%d pruebas correctas" % (sum(results), len(results)))
