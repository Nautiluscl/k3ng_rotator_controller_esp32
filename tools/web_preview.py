#!/usr/bin/env python3
"""Vista previa de la interfaz web sin ESP32.

Extrae la página embebida en k3ng_rotator_controller/rotator_esp32_web.h y la sirve con una
API simulada (rotor virtual que se mueve, seguimiento, parada, hombre muerto), para revisar
el diseño desde el navegador o el móvil.

Uso:
    tools/web_preview.py [puerto]          -> http://localhost:8080/
    tools/web_preview.py --extract salida.html   (solo extrae la página)
"""
import json
import math
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
WEB_H = ROOT / "k3ng_rotator_controller" / "rotator_esp32_web.h"
SETTINGS_H = ROOT / "k3ng_rotator_controller" / "rotator_settings_esp32.h"


def setting(name, default):
    m = re.search(r"#define\s+%s\s+(\d+)" % name, SETTINGS_H.read_text(encoding="utf-8"))
    return int(m.group(1)) if m else default


KEEPALIVE_MS = setting("WEB_JOG_KEEPALIVE_MS", 250)
JOG_TIMEOUT_MS = setting("WEB_JOG_TIMEOUT_MS", 1000)


def extract_page(name="web_page_html"):
    src = WEB_H.read_text(encoding="utf-8")
    m = re.search(name + r'\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral"', src, re.S)
    if not m:
        sys.exit("No se encontró %s en %s" % (name, WEB_H))
    return m.group(1).replace("%KEEPALIVE%", str(KEEPALIVE_MS))


class Rotor:
    """Rotor simulado: 6 °/s en azimut, 3 °/s en elevación."""

    def __init__(self):
        self.lock = threading.Lock()
        self.az, self.el = 123.4, 15.0
        self.jog = {"az": None, "el": None}
        self.keepalive = {"az": 0.0, "el": 0.0}
        self.released = {"az": (0, 0), "el": (0, 0)}   # (sid, seq) de la última pulsación soltada
        self.track = None
        self.grid = "FF46pn"
        self.sim = 0
        self.cfg = {"az_start": 0, "az_cap": 360, "el_offset": 0, "tz": -3, "sun_check": 5000, "sun_min": 5000,
                    "sun_thr": 0.5, "moon_check": 5000, "moon_min": 5000, "moon_thr": 0.5}
        self.ssid = "Casa_2.4G"
        self.start = time.time()
        self.t = time.time()

    def targets(self):
        h = (time.time() / 60) % 360
        return {"sun": (h, 35 + 10 * math.sin(h / 57)), "moon": ((h + 140) % 360, -12.0)}

    def step(self):
        now = time.time()
        dt, self.t = now - self.t, now
        for axis in ("az", "el"):
            if self.jog[axis] and (now - self.keepalive[axis]) * 1000 > JOG_TIMEOUT_MS:
                print("hombre muerto: parada de", axis)
                self.jog[axis] = None
        d = {"cw": 6, "ccw": -6}.get(self.jog["az"], 0)
        self.az = (self.az + d * dt) % 360
        e = {"up": 3, "down": -3}.get(self.jog["el"], 0)
        self.el = min(90, max(0, self.el + e * dt))
        if self.track:
            taz, tel = self.targets()[self.track]
            tel = max(0.0, tel)   # bajo el horizonte espera a 0° de elevación
            self.az += max(-6 * dt, min(6 * dt, taz - self.az))
            self.el += max(-3 * dt, min(3 * dt, tel - self.el))

    def status(self):
        with self.lock:
            self.step()
            t = self.targets()
            tracking_az = self.track and abs(t[self.track][0] - self.az) > 0.5
            return {
                "az": round(self.az, 2), "el": round(self.el, 2), "el_ok": 1,
                "az_mv": self.jog["az"] or ("cw" if tracking_az else ""), "el_mv": self.jog["el"] or "",
                "sim": self.sim, "sun": {"az": round(t["sun"][0], 2), "el": round(t["sun"][1], 2), "trk": int(self.track == "sun")},
                "moon": {"az": round(t["moon"][0], 2), "el": round(t["moon"][1], 2), "trk": int(self.track == "moon")},
                "wifi": {"ok": 1, "ssid": "Casa_2.4G", "rssi": -61, "ip": "192.168.1.172"},
                "uptime": int(time.time() - self.start) + 93784, "rst": "POWER_ON", "time_ok": 1,
                "utc": time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime()), "grid": self.grid, "bno": "BNO055 OK",
            }


rotor = Rotor()
PAGE = None
CONFIG_PAGE = None
LIMITS = {"az_start": (0, 359), "az_cap": (90, 720), "el_offset": (-90, 90), "tz": (-12, 14), "sun_check": (100, 60000),
          "sun_min": (0, 600000), "sun_thr": (0.1, 20), "moon_check": (100, 60000), "moon_min": (0, 600000), "moon_thr": (0.1, 20)}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if "/api/status" not in (args[0] if args else ""):
            sys.stderr.write("%s\n" % (fmt % args))

    def reply(self, code, body, ctype="application/json"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def result(self, ok, msg=""):
        self.reply(200 if ok else 400, json.dumps({"ok": ok, "msg": msg}))

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif path == "/config":
            self.reply(200, CONFIG_PAGE, "text/html; charset=utf-8")
        elif path == "/api/config":
            st = rotor.status()
            self.reply(200, json.dumps({"cfg": rotor.cfg, "sim": rotor.sim, "grid": rotor.grid, "lat": -33.4378, "lon": -70.6505,
                "bno": {"present": 1, "text": "BNO055 OK", "sys": 3, "gyro": 3, "accel": 2, "saved": 0, "raw": st["el"]},
                "wifi": {"ok": 1, "ssid": rotor.ssid, "rssi": -61, "ip": "192.168.1.172"},
                "ver": "2023.10.06.2200 ESP32 (simulado)", "uptime": st["uptime"], "rst": "POWER_ON", "heap": 214000}))
        elif path == "/api/status":
            self.reply(200, json.dumps(rotor.status()))
        else:
            self.reply(404, "404", "text/plain")

    def do_POST(self):
        path = urlparse(self.path).path
        n = int(self.headers.get("Content-Length") or 0)
        args = {k: v[0] for k, v in parse_qs(self.rfile.read(n).decode()).items()}
        with rotor.lock:
            rotor.step()
            if path == "/api/move":
                d = args.get("dir", "")
                sid, seq = int(args.get("sid", 0)), int(args.get("seq", 0))
                if d == "release":
                    for axis in ([args["axis"]] if args.get("axis") in ("az", "el") else ["az", "el"]):
                        rotor.jog[axis] = None
                        rotor.released[axis] = (sid, seq)
                    return self.result(True)
                axis = {"cw": "az", "ccw": "az", "up": "el", "down": "el"}.get(d)
                if not axis:
                    return self.result(False, "Dirección no válida")
                rs, rq = rotor.released[axis]
                if sid and sid == rs and seq <= rq:
                    print("keepalive atrasado descartado:", d, seq)
                    return self.result(True)
                first = (not sid) or args.get("first") == "1"
                if not first and rotor.jog[axis] is None:
                    print("keepalive sin movimiento en curso: no arranca", d)
                    return self.result(True)
                rotor.track = None
                rotor.jog[axis] = d
                rotor.keepalive[axis] = time.time()
                return self.result(True)
            if path == "/api/stop":
                rotor.jog = {"az": None, "el": None}
                sid, seq = int(args.get("sid", 0)), int(args.get("seq", 0))
                rotor.released = {"az": (sid, seq), "el": (sid, seq)}
                rotor.track = None
                return self.result(True, "Movimiento detenido")
            if path == "/api/track":
                target, on = args.get("target"), args.get("on") == "1"
                if target not in ("sun", "moon"):
                    return self.result(False, "Objetivo no válido")
                name = "el Sol" if target == "sun" else "la Luna"
                if on:
                    rotor.track = target
                    visible = rotor.targets()[target][1] > 0
                    return self.result(True, ("Siguiendo " + name) if visible else
                                       "Seguimiento de %s activo (bajo el horizonte: el rotor espera en su azimut, a 0° de elevación)" % name)
                rotor.track = None
                return self.result(True, "Seguimiento de %s desactivado" % name)
            if path == "/api/config":
                for k, (lo, hi) in LIMITS.items():
                    try:
                        v = float(args.get(k, ""))
                    except ValueError:
                        return self.result(False, "Valor no válido en %s (%g a %g)" % (k, lo, hi))
                    if not lo <= v <= hi:
                        return self.result(False, "Valor no válido en %s (%g a %g)" % (k, lo, hi))
                    rotor.cfg[k] = v
                return self.result(True, "Ajustes guardados")
            if path == "/api/sim":
                rotor.sim = int(args.get("on") == "1")
                return self.result(True, "Simulación activada: los motores no se moverán" if rotor.sim else "Simulación desactivada")
            if path == "/api/bno055":
                return self.result(args.get("action") in ("save", "clear"),
                                   "Falta calibrar: Giro y Acel tienen que estar en 3" if args.get("action") == "save" else "Calibración borrada")
            if path == "/api/wifi":
                s = args.get("ssid", "")
                if not 1 <= len(s) <= 32:
                    return self.result(False, "El nombre de la red debe tener de 1 a 32 caracteres")
                rotor.ssid = s
                return self.result(True, "Red guardada. El rotor se reconecta en unos segundos: búscalo en la red nueva")
            if path == "/api/restart":
                return self.result(True, "Reiniciando… la página se recarga sola")
            if path == "/api/locator":
                g = args.get("grid", "")
                if not re.fullmatch(r"[A-Ra-r]{2}\d\d[A-Xa-x]{2}", g):
                    return self.result(False, "Locator no válido (formato XXnnxx, p. ej. FF46pn)")
                rotor.grid = g[:2].upper() + g[2:4] + g[4:].lower()
                return self.result(True, "Ubicación guardada: " + rotor.grid)
        self.reply(404, "404", "text/plain")


def main():
    global PAGE, CONFIG_PAGE
    PAGE = extract_page()
    CONFIG_PAGE = extract_page("web_config_html")
    if len(sys.argv) > 2 and sys.argv[1] == "--extract":
        Path(sys.argv[2]).write_text(PAGE, encoding="utf-8")
        print("Página extraída en", sys.argv[2])
        return
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print("Vista previa en http://localhost:%d/  (Ctrl+C para salir)" % port)
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
