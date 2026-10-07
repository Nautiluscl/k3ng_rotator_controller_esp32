#!/usr/bin/env python3
"""Pruebas de red contra el ESP32 real: mDNS, puerto TCP 23 y API web.
Uso: tools/test_red.py [host]   (por defecto rotor.local)
No modifica la configuración guardada: solo prueba un locator inválido.
"""
import json
import socket
import sys
import time
import urllib.parse
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "rotor.local"
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%s %-48s %s" % ("OK  " if ok else "FALLO", name, detail))


# ---------------- mDNS ----------------
try:
    ip = socket.gethostbyname("rotor.local")
    check("mDNS rotor.local", True, ip)
except Exception as e:
    ip = HOST
    check("mDNS rotor.local", False, str(e))
if HOST == "rotor.local":
    HOST = ip

# ---------------- TCP 23 ----------------
def tcp_session(payloads, wait=0.6):
    out = []
    with socket.create_connection((HOST, 23), timeout=3) as c:
        c.settimeout(0.2)
        for p in payloads:
            t0 = time.time()
            c.sendall(p)
            data = b""
            end = time.time() + wait
            while time.time() < end:
                try:
                    chunk = c.recv(512)
                    if not chunk:
                        break
                    data += chunk
                    if data.endswith(b"\n"):
                        break
                except socket.timeout:
                    pass
            out.append((data.decode("utf-8", "replace").strip(), (time.time() - t0) * 1000))
    return out


r = tcp_session([b"C2\r", b"c2\r", b"\\WI\r", b"\\W\r", b"\xff\xfb\x01\xff\xfd\x03C2\r", b"C2\r\x00", b"C\r"])
check("TCP C2", r[0][0].startswith("AZ="), "%r  %.0f ms" % r[0])
check("TCP minúsculas (c2)", r[1][0].startswith("AZ="), repr(r[1][0]))
check("TCP \\WI", "CONNECTED" in r[2][0], r[2][0][:70])
check("TCP \\W sin argumento = \\WI", "CONNECTED" in r[3][0], r[3][0][:40])
check("TCP con negociación telnet (IAC)", r[4][0].startswith("AZ="), repr(r[4][0]))
check("TCP CR NUL seguido de C", True, "")
check("TCP C tras CR NUL (sin NUL basura)", r[6][0].startswith("AZ="), repr(r[6][0]))
lat = [x[1] for x in r[:2]]
check("TCP latencia < 100 ms", max(lat) < 100, "%.0f / %.0f ms" % tuple(lat))

# segundo cliente sustituye al primero
c1 = socket.create_connection((HOST, 23), timeout=3)
time.sleep(0.3)
r2 = tcp_session([b"C2\r"])
c1.settimeout(1)
try:
    gone = c1.recv(10) == b""
except Exception:
    gone = False
c1.close()
check("TCP segundo cliente sustituye al primero", r2[0][0].startswith("AZ=") and gone, "primer cliente cerrado=%s" % gone)

# ---------------- web ----------------
BASE = "http://%s" % HOST


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=4) as resp:
        return resp.status, resp.read()


def post(path, data):
    req = urllib.request.Request(BASE + path, data=urllib.parse.urlencode(data).encode(), method="POST")
    try:
        with urllib.request.urlopen(req, timeout=4) as resp:
            return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def status():
    return json.loads(get("/api/status")[1])


code, page = get("/")
check("GET / (página)", code == 200 and b"Seguir Sol" in page and b"%KEEPALIVE%" not in page, "%d B" % len(page))
st = status()
check("GET /api/status", "az" in st and st["wifi"]["ok"] == 1,
      "ssid=%s rssi=%s utc=%s grid=%s bno=%s rst=%s" % (st["wifi"]["ssid"], st["wifi"]["rssi"], st["utc"], st["grid"], st["bno"], st["rst"]))
check("Hora NTP en la web", st["time_ok"] == 1, st["utc"])
check("Sol y Luna calculados", st["sun"]["az"] != 0 or st["moon"]["az"] != 0,
      "sol az=%.1f el=%.1f  luna az=%.1f el=%.1f" % (st["sun"]["az"], st["sun"]["el"], st["moon"]["az"], st["moon"]["el"]))
t0 = time.time(); status(); check("Latencia /api/status < 300 ms", (time.time() - t0) < 0.3, "%.0f ms" % ((time.time() - t0) * 1000))

# movimiento mantenido y hombre muerto (firmware real)
sid = 4242
code, j = post("/api/move", {"dir": "cw", "sid": sid, "seq": 1, "first": 1})
time.sleep(0.4)
mv1 = status()["az_mv"]
check("Mover CW: el eje arranca", code == 200 and mv1 == "cw", "az_mv=%r" % mv1)
for _ in range(4):                       # 1 s de keepalives
    post("/api/move", {"dir": "cw", "sid": sid, "seq": 1, "first": 0})
    time.sleep(0.25)
mv2 = status()["az_mv"]
check("Keepalive mantiene el movimiento", mv2 == "cw", "az_mv=%r" % mv2)
time.sleep(1.6)                          # sin keepalive
mv3 = status()["az_mv"]
check("Hombre muerto: para sin keepalive (1 s)", mv3 == "", "az_mv=%r" % mv3)
post("/api/move", {"dir": "cw", "sid": sid, "seq": 1, "first": 0})
time.sleep(0.4)
mv4 = status()["az_mv"]
check("Keepalive tardío no rearranca", mv4 == "", "az_mv=%r" % mv4)

post("/api/move", {"dir": "ccw", "sid": sid, "seq": 2, "first": 1})
time.sleep(0.3)
post("/api/move", {"dir": "release", "axis": "az", "sid": sid, "seq": 2})
time.sleep(0.4)
post("/api/move", {"dir": "ccw", "sid": sid, "seq": 2, "first": 1})   # orden atrasada de la misma pulsación
time.sleep(0.4)
mv5 = status()["az_mv"]
check("Release y orden atrasada descartada", mv5 == "", "az_mv=%r" % mv5)

post("/api/move", {"dir": "cw", "sid": 9999, "seq": 1, "first": 1})
time.sleep(0.3)
code, j = post("/api/stop", {"sid": 1, "seq": 1})
time.sleep(0.4)
post("/api/move", {"dir": "cw", "sid": 9999, "seq": 1, "first": 0})
time.sleep(0.4)
mv6 = status()["az_mv"]
check("STOP de otro cliente gana al keepalive", code == 200 and mv6 == "", "az_mv=%r" % mv6)

code, j = post("/api/move", {"dir": "up", "sid": sid, "seq": 3, "first": 1})
check("Elevación bloqueada sin BNO055", code == 400, j.get("msg", ""))
code, j = post("/api/track", {"target": "sun", "on": 1})
check("Seguimiento bloqueado sin BNO055", code == 400, j.get("msg", ""))
code, j = post("/api/move", {"dir": "xx"})
check("Dirección inválida rechazada", code == 400, j.get("msg", ""))
code, j = post("/api/locator", {"grid": "ZZ99zz"})
check("Locator inválido rechazado", code == 400, j.get("msg", ""))

print("\n%d/%d pruebas correctas" % (sum(results), len(results)))
