#!/usr/bin/env python3
"""Prueba del modo simulación por TCP (puerto 23), como lo usaría PstRotator.

Activa la simulación con \\XV1, ordena posiciones con comandos GS-232B (M, W, S) y
comprueba con C2 que el rotor virtual llega al destino. Al terminar deja la simulación
como estaba.

Uso: tools/test_simulacion.py [host]   (por defecto rotor.local)
"""
import re
import socket
import sys
import time

HOST = socket.gethostbyname(sys.argv[1] if len(sys.argv) > 1 else "rotor.local")
results = []


class Rotor:
    def __init__(self):
        self.c = socket.create_connection((HOST, 23), timeout=3)
        self.c.settimeout(0.3)

    def cmd(self, text, wait=0.6):
        self.c.sendall(text.encode() + b"\r")
        data, end = b"", time.time() + wait
        while time.time() < end:
            try:
                chunk = self.c.recv(256)
                data += chunk
                if data.endswith(b"\n"):
                    break
            except socket.timeout:
                pass
        return data.decode("utf-8", "replace").strip()

    def pos(self):
        m = re.search(r"AZ=(\d+)\s*EL=(\d+)", self.cmd("C2"))
        return (int(m.group(1)), int(m.group(2))) if m else None

    def wait_for(self, az=None, el=None, tol=4, timeout=90):
        t0, last = time.time(), None
        while time.time() - t0 < timeout:
            last = self.pos()
            if last and (az is None or abs(last[0] - az) <= tol) and (el is None or abs(last[1] - el) <= tol):
                return last, time.time() - t0
            time.sleep(0.5)
        return last, None


def check(name, ok, detail=""):
    results.append(ok)
    print("%s %-46s %s" % ("OK  " if ok else "FALLO", name, detail))


r = Rotor()
was_on = "ON" in r.cmd("\\XV")
check("Activar simulación (\\XV1)", "ON" in r.cmd("\\XV1"))
r.cmd("S")
time.sleep(0.5)
start = r.pos()
check("Posición inicial (C2)", start is not None, str(start))

# azimut a 90 y luego a 150 (relativo al punto de partida, dentro de 0-360)
for target in (90, 150):
    r.cmd("M%03d" % target)
    p, t = r.wait_for(az=target)
    check("M%03d: azimut llega a %d°" % (target, target), t is not None, "pos=%s en %.1f s" % (p, t or -1))

# azimut y elevación a la vez (GS-232B Waaa eee)
r.cmd("W200 030")
p, t = r.wait_for(az=200, el=30)
check("W200 030: llega a az 200 / el 30", t is not None, "pos=%s en %.1f s" % (p, t or -1))

# parada en mitad del recorrido
r.cmd("M020")
time.sleep(3)
r.cmd("S")
time.sleep(0.6)
p1 = r.pos()
time.sleep(2)
p2 = r.pos()
check("S detiene el movimiento", p1 == p2 and p1[0] not in (20, 200), "%s -> %s" % (p1, p2))

# manuales R/L y U/D
r.cmd("R")
time.sleep(2)
r.cmd("A")
pr = r.pos()
check("R (manual CW) mueve el azimut", pr[0] > p2[0], "%s -> %s" % (p2, pr))
r.cmd("U")
time.sleep(2)
r.cmd("E")
pu = r.pos()
check("U (manual arriba) mueve la elevación", pu[1] > pr[1], "%s -> %s" % (pr, pu))

if not was_on:
    check("Desactivar simulación (\\XV0)", "OFF" in r.cmd("\\XV0"))
print("\n%d/%d pruebas correctas" % (sum(results), len(results)))
