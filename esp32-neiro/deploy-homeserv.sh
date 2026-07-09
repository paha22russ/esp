#!/bin/bash
# Обновление сервера + прошивка ESP32 на homeserv (одной командой)
set -euo pipefail

REPO_URL="https://github.com/paha22russ/esp.git"
BRANCH="${BRANCH:-cursor/blink-led-command-bd71}"
INSTALL_DIR="${ESP32_NEIRO_HOME:-$HOME/esp}"
SERVER_DIR="$INSTALL_DIR/esp32-neiro/server"
FIRMWARE_DIR="$INSTALL_DIR/esp32-neiro/firmware"

echo "=== ESP32 neiro — деплой на homeserv ==="

if [ -d "$INSTALL_DIR/.git" ]; then
  cd "$INSTALL_DIR"
  git fetch origin
  git checkout "$BRANCH" 2>/dev/null || git checkout -b "$BRANCH" "origin/$BRANCH"
  git pull origin "$BRANCH"
else
  git clone -b "$BRANCH" "$REPO_URL" "$INSTALL_DIR"
fi

# Сервер
cd "$SERVER_DIR"
[ -d .venv ] || python3 -m venv .venv
source .venv/bin/activate
pip install -q -r requirements.txt

if [ -f .env ]; then
  sed -i '/^GOOGLE_/d' .env
  sed -i '/^LLM_PROVIDER=/d' .env
  echo "LLM_PROVIDER=ollama" >> .env
else
  cp .env.example .env
fi

systemctl --user restart esp32-neiro 2>/dev/null || {
  echo "[!] systemd-сервис не найден — запустите install-kali.sh"
}

# Прошивка
cd "$FIRMWARE_DIR"
chmod +x flash-homeserv.sh
./flash-homeserv.sh

echo ""
echo "Готово: http://$(hostname -I | awk '{print $1}'):8000/"
