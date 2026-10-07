#!/usr/bin/env python3
"""Prueba por puerto serie: reinicia la placa, captura el arranque y envía comandos.

Uso: serial_test.py [segundos_arranque] [comando ...]
Cada comando se envía terminado en CR y se muestran las líneas que llegan en 1,5 s.

Ejemplo: tools/test_serie.py 15 C2 '\\WI' '\\XB'   (requiere pyserial; viene con PlatformIO:
~/.platformio/penv/bin/python tools/test_serie.py ...)
"""
import sys
import time
import serial

PORT, BAUD = "/dev/ttyUSB0", 9600
boot_seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 15
commands = sys.argv[2:]

s = serial.Serial(PORT, BAUD, timeout=0.1)
# reset por RTS/DTR, como hace esptool, sin entrar al bootloader
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
start = time.time()


def read_for(seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(512)
    return buf.decode("utf-8", "replace")


def show(prefix, text):
    for line in text.replace("\r", "\n").split("\n"):
        line = line.strip()
        if line:
            print("%s %s" % (prefix, line))


show("[arranque]", read_for(boot_seconds))
for cmd in commands:
    if cmd.startswith("sleep:"):
        time.sleep(float(cmd[6:]))
        show("[espera]", read_for(0.1))
        continue
    s.write(cmd.encode() + b"\r")
    show("[%s]" % cmd, read_for(1.5))
s.close()
