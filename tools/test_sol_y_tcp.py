#!/usr/bin/env python3
"""1) Posición del Sol del firmware frente a un cálculo independiente (NOAA simplificado).
   2) Cliente TCP que envía comandos y no lee nunca: el loop no debe bloquearse ni reiniciarse.
Uso: tools/test_sol_y_tcp.py
La referencia del Sol usa la ubicación FF46pi; si la estación está en otro sitio, hay que
cambiar lat/lon en el script.
"""
import json
import math
import socket
import time
import urllib.request
from datetime import datetime, timezone

HOST = socket.gethostbyname("rotor.local")


def status():
    with urllib.request.urlopen("http://%s/api/status" % HOST, timeout=4) as r:
        return json.loads(r.read())


def sun_position(lat, lon, when):
    # algoritmo NOAA (precisión ~0,1-0,5°)
    jd = when.timestamp() / 86400.0 + 2440587.5
    n = jd - 2451545.0
    L = (280.460 + 0.9856474 * n) % 360
    g = math.radians((357.528 + 0.9856003 * n) % 360)
    lam = math.radians(L + 1.915 * math.sin(g) + 0.020 * math.sin(2 * g))
    eps = math.radians(23.439 - 0.0000004 * n)
    ra = math.atan2(math.cos(eps) * math.sin(lam), math.cos(lam))
    dec = math.asin(math.sin(eps) * math.sin(lam))
    gmst = (18.697374558 + 24.06570982441908 * n) % 24
    ha = math.radians((gmst * 15 + lon) - math.degrees(ra))
    la = math.radians(lat)
    el = math.asin(math.sin(la) * math.sin(dec) + math.cos(la) * math.cos(dec) * math.cos(ha))
    az = math.atan2(-math.sin(ha), math.tan(dec) * math.cos(la) - math.sin(la) * math.cos(ha))
    return (math.degrees(az) % 360, math.degrees(el))


# FF46pi según grid2deg del firmware
lat, lon = -33.645833, -70.741667
st = status()
ref_az, ref_el = sun_position(lat, lon, datetime.now(timezone.utc))
d_az, d_el = abs(st["sun"]["az"] - ref_az), abs(st["sun"]["el"] - ref_el)
print("Sol firmware: az %.2f el %.2f | referencia: az %.2f el %.2f | diferencia %.2f° / %.2f°  %s"
      % (st["sun"]["az"], st["sun"]["el"], ref_az, ref_el, d_az, d_el, "OK" if d_az < 1 and d_el < 1 else "FALLO"))

# 2) cliente TCP que no lee
up0, rst0 = st["uptime"], st["rst"]
c = socket.create_connection((HOST, 23), timeout=3)
c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)   # ventana de recepción pequeña: se llena antes
sent = 0
worst = 0.0
t_end = time.time() + 20
dropped = False
while time.time() < t_end:
    try:
        c.sendall(b"\\WI\r" * 20)
        sent += 20
    except OSError:
        dropped = True
        break
    t0 = time.time()
    try:
        status()
        worst = max(worst, time.time() - t0)
    except Exception as e:
        worst = max(worst, 99)
    time.sleep(0.1)
c.close()
time.sleep(1)
st2 = status()
print("Cliente sin leer: %d comandos enviados en 20 s, conexión cortada por el ESP32=%s" % (sent, dropped))
print("Peor latencia web durante la prueba: %.0f ms" % (worst * 1000))
print("Uptime antes/después: %d/%d s  reinicio: %s -> %s  %s"
      % (up0, st2["uptime"], rst0, st2["rst"], "OK (sin reinicio)" if st2["uptime"] > up0 else "FALLO (se reinició)"))
