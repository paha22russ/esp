#!/bin/bash
# =============================================================================
# Установка и запуск ESP32 neiro на домашнем Kali / homeserv
#
# Запуск на Kali (в терминале на 192.168.1.112):
#   bash install-kali.sh
#
# Или одной строкой с GitHub:
#   bash <(curl -fsSL "https://raw.githubusercontent.com/paha22russ/esp/cursor/esp32-neiro-bd71/esp32-neiro/server/install-kali.sh")
# =============================================================================
set -euo pipefail

REPO_URL="https://github.com/paha22russ/esp.git"
BRANCH="cursor/esp32-neiro-bd71"
INSTALL_DIR="${ESP32_NEIRO_HOME:-$HOME/esp}"
SERVER_DIR="$INSTALL_DIR/esp32-neiro/server"
SERVICE_NAME="esp32-neiro"
PORT="${PORT:-8000}"

echo "=== ESP32 neiro — установка на Kali/homeserv ==="

# --- Зависимости системы ---
if ! python3 -c "import venv" 2>/dev/null; then
  echo "[*] Установка python3-venv..."
  sudo apt-get update -qq
  sudo apt-get install -y python3-venv python3-pip git curl
fi

# --- Клонирование / обновление репозитория ---
if [ -d "$INSTALL_DIR/.git" ]; then
  echo "[*] Обновление репозитория в $INSTALL_DIR"
  cd "$INSTALL_DIR"
  git fetch origin
  git checkout "$BRANCH" 2>/dev/null || git checkout -b "$BRANCH" "origin/$BRANCH"
  git pull origin "$BRANCH"
else
  echo "[*] Клонирование репозитория..."
  git clone -b "$BRANCH" "$REPO_URL" "$INSTALL_DIR"
fi

cd "$SERVER_DIR"

# --- Виртуальное окружение Python ---
if [ ! -d .venv ]; then
  echo "[*] Создание venv..."
  python3 -m venv .venv
fi
source .venv/bin/activate
pip install -q --upgrade pip
pip install -q -r requirements.txt

# --- Файл .env ---
if [ ! -f .env ]; then
  echo "[*] Создание .env из шаблона..."
  cp .env.example .env
  echo ""
  echo "!!! ВАЖНО: отредактируйте $SERVER_DIR/.env"
  echo "    Впишите GOOGLE_API_KEY (Gemini) и проверьте OLLAMA_BASE_URL"
  echo "    nano $SERVER_DIR/.env"
  echo ""
fi

# --- Проверка Ollama (опционально) ---
OLLAMA_URL="${OLLAMA_BASE_URL:-http://192.168.1.112:11434}"
if curl -sf --connect-timeout 3 "${OLLAMA_URL%/v1}/api/tags" >/dev/null 2>&1; then
  echo "[OK] Ollama доступна: $OLLAMA_URL"
else
  echo "[!] Ollama не отвечает на $OLLAMA_URL (fallback может не работать)"
fi

# --- Systemd user-сервис (автозапуск) ---
UNIT_DIR="$HOME/.config/systemd/user"
mkdir -p "$UNIT_DIR"

cat > "$UNIT_DIR/${SERVICE_NAME}.service" << EOF
[Unit]
Description=ESP32 neiro Dynamic Agent (FastAPI)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=$SERVER_DIR
EnvironmentFile=-$SERVER_DIR/.env
ExecStart=$SERVER_DIR/.venv/bin/python3 -m uvicorn main:app --host 0.0.0.0 --port $PORT
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable "${SERVICE_NAME}.service"
systemctl --user restart "${SERVICE_NAME}.service"

# Разрешить сервису работать без активной сессии пользователя
sudo loginctl enable-linger "$(whoami)" 2>/dev/null || true

sleep 2

# --- Проверка ---
if curl -sf "http://127.0.0.1:${PORT}/api/state" >/dev/null; then
  LOCAL_IP=$(hostname -I | awk '{print $1}')
  echo ""
  echo "============================================"
  echo "  Сервер ЗАПУЩЕН"
  echo "  Дашборд:  http://${LOCAL_IP}:${PORT}/"
  echo "  API:      http://${LOCAL_IP}:${PORT}/api/state"
  echo "  Статус:   systemctl --user status ${SERVICE_NAME}"
  echo "  Логи:     journalctl --user -u ${SERVICE_NAME} -f"
  echo "============================================"
else
  echo "[ОШИБКА] Сервер не отвечает. Смотрите логи:"
  echo "  journalctl --user -u ${SERVICE_NAME} -n 50"
  exit 1
fi
