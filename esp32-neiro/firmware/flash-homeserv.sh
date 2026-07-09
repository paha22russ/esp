# Прошивка ESP32 neiro на homeserv (Linux) — ESP32 в USB homeserv
# chmod +x flash-homeserv.sh && ./flash-homeserv.sh

set -euo pipefail
cd "$(dirname "$0")"
BIN="bin"
PORT="${1:-}"

if [ ! -f "$BIN/firmware.bin" ]; then
  echo "Сначала соберите: pip install platformio && pio run"
  exit 1
fi

if [ -z "$PORT" ]; then
  echo "Доступные порты:"
  ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  (нет — подключите ESP32 по USB)"
  read -rp "Порт (например /dev/ttyUSB0): " PORT
fi

# Права на порт
if groups | grep -q dialout; then
  :
else
  echo "Добавьте себя в dialout: sudo usermod -aG dialout $USER && newgrp dialout"
fi

python3 -m pip install -q esptool 2>/dev/null || pip install -q esptool

echo "[*] Прошивка на $PORT ..."
python3 -m esptool --chip esp32 --port "$PORT" --baud 921600 write_flash -z \
  0x1000  "$BIN/bootloader.bin" \
  0x8000  "$BIN/partitions.bin" \
  0x10000 "$BIN/firmware.bin"

echo "[OK] Готово!"
