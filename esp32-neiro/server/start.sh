#!/bin/bash
# Запуск ESP32 neiro на Kali / homeserv
set -e
cd "$(dirname "$0")"

if [ ! -f .env ]; then
  echo "Создайте .env из .env.example и проверьте OLLAMA_BASE_URL"
  cp -n .env.example .env
  echo "Отредактируйте: nano .env"
  exit 1
fi

if [ ! -d .venv ]; then
  python3 -m venv .venv
fi
source .venv/bin/activate
pip install -q -r requirements.txt

echo "Запуск на http://0.0.0.0:8000"
echo "Дашборд: http://$(hostname -I | awk '{print $1}'):8000"
exec uvicorn main:app --host 0.0.0.0 --port 8000
