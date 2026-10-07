#!/usr/bin/env bash
# Compila el entorno esp32 y muestra solo el resumen: errores (sin repetir) y uso de memoria.
# Uso: tools/build_esp32.sh [archivo_log]
cd "$(dirname "$0")/.." || exit 1
LOG="${1:-.pio/build_esp32.log}"
mkdir -p "$(dirname "$LOG")"
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
"$PIO" run -e esp32 > "$LOG" 2>&1
RC=$?
grep -E 'error:|Error [0-9]' "$LOG" | sed 's|.*k3ng_rotator_controller/||' | sort -u | head -40
grep -E '^(RAM|Flash):' "$LOG"
echo "rc=$RC  (log completo: $LOG)"
exit $RC
