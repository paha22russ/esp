#!/usr/bin/env bash
# One-shot deploy на Германии (de-vmpico / 151.247.208.17)
# Запуск НА СЕРВЕРЕ:
#   sudo bash vps_v45/deploy.sh
#
# ВАЖНО: :443 на этом VPS занят VPN/REALITY — HTTPS через certbot НЕ ставим.
# Публичный UI: http://esp.pahavpn.cloud-ip.cc/  (nginx :80 vhost)
set -euo pipefail

DOMAIN="${DOMAIN:-esp.pahavpn.cloud-ip.cc}"
APP_DIR="${APP_DIR:-/opt/boiler-v45}"
WEB_ROOT="${WEB_ROOT:-/var/www/esp-boiler}"
REPO_VPS_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "==> Deploy boiler 4.5-beta UI to http://${DOMAIN}"

if [[ $EUID -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y docker.io docker-compose-v2 nginx curl ca-certificates rsync openssl

systemctl enable --now docker nginx

mkdir -p "$APP_DIR" "$WEB_ROOT" /var/www/html
rsync -a --delete \
  --exclude '.env' \
  --exclude '__pycache__' \
  "$REPO_VPS_DIR/" "$APP_DIR/"

if [[ ! -f "$APP_DIR/.env" ]]; then
  cp "$APP_DIR/.env.example" "$APP_DIR/.env"
  PW=$(openssl rand -hex 16)
  TOK=$(openssl rand -hex 24)
  sed -i "s/^POSTGRES_PASSWORD=.*/POSTGRES_PASSWORD=${PW}/" "$APP_DIR/.env"
  sed -i "s/^INGEST_TOKEN=.*/INGEST_TOKEN=${TOK}/" "$APP_DIR/.env"
  echo "Generated $APP_DIR/.env"
fi

echo "---- INGEST_TOKEN (save for ESP) ----"
grep '^INGEST_TOKEN=' "$APP_DIR/.env"
echo "------------------------------------"

rsync -a --delete "$APP_DIR/web/" "$WEB_ROOT/"
sed -i "s#151\.247\.208\.17#${DOMAIN}#g" "$WEB_ROOT/index.html" || true
sed -i "s#esp\.pahavpn\.cloud-ip\.cc#${DOMAIN}#g" "$WEB_ROOT/index.html" || true

cp "$APP_DIR/nginx/esp-boiler.conf" /etc/nginx/sites-available/esp-boiler
ln -sfn /etc/nginx/sites-available/esp-boiler /etc/nginx/sites-enabled/esp-boiler
# ensure default monitor site (if present) stays enabled
nginx -t
systemctl reload nginx

cd "$APP_DIR"
docker compose up -d --build

echo -n "Waiting ingest"
for i in $(seq 1 40); do
  if curl -fsS "http://127.0.0.1:8088/api/v1/health" >/dev/null 2>&1; then
    echo " OK"
    break
  fi
  echo -n "."
  sleep 1
done

echo
curl -fsS "http://127.0.0.1:8088/api/v1/health" || true
echo
curl -fsS -H "Host: ${DOMAIN}" "http://127.0.0.1/api/v1/health" || true
echo
echo "==> Public URL: http://${DOMAIN}/"
echo "==> Note: HTTPS/:443 not used (occupied by VPN stack on this host)"
