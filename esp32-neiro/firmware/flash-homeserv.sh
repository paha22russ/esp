#!/bin/bash
# Прошивка ESP32 neiro на homeserv — автоопределение USB-порта
# chmod +x flash-homeserv.sh && ./flash-homeserv.sh [/dev/ttyUSB0]

set -euo pipefail
cd "$(dirname "$0")"
BIN="bin"
PORT="${1:-}"

if [ ! -f "$BIN/firmware.bin" ]; then
  echo "Сначала соберите: pio run && cp .pio/build/esp32dev/*.bin bin/"
  exit 1
fi

if [ -z "$PORT" ]; then
  PORT="$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1 || true)"
fi

if [ -z "$PORT" ]; then
  echo "[ОШИБКА] ESP32 не найден. Подключите по USB и повторите."
  ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
  exit 1
fi

if ! groups | grep -q dialout; then
  echo "[!] Нет группы dialout — sudo usermod -aG dialout $USER"
fi

python3 -m pip install -q esptool 2>/dev/null || pip install -q esptool

echo "[*] Прошивка на $PORT (без erase — сохраняет Wi-Fi) ..."
python3 -m esptool --chip esp32 --port "$PORT" --baud 115200 write_flash -z \
  0x1000  "$BIN/bootloader.bin" \
  0x8000  "$BIN/partitions.bin" \
  0x10000 "$BIN/firmware.bin"

echo "[OK] ESP32 прошит!"
