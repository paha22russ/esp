# Публичный UI котла 4.5-beta (Германия)

**URL:** https://esp.pahavpn.cloud-ip.cc  
**IP:** 151.247.208.17 (`de-vmpico`)  
**DNS:** CloudDNS A → OK

## Важно про HTTPS

На этом VPS **порт 443 занят VPN/REALITY** (handshake reset).  
Поэтому публичный интерфейс котла работает по **HTTP :80** через отдельный nginx `server_name`.  
VPS Monitor на том же IP не ломаем.

## Архитектура

```
Browser  →  nginx :80  (server_name esp.pahavpn.cloud-ip.cc)
              ├─ /        → /var/www/esp-boiler
              └─ /api/    → 127.0.0.1:8088 (Docker ingest)
ESP      →  https://esp.pahavpn.cloud-ip.cc/api/v1/ingest
ESP      ←  .../commands/next
```

## Деплой

На сервере:

```bash
git clone https://github.com/paha22russ/esp.git
cd esp && git checkout cursor/architecture-4-5-beta-44fa
sudo bash vps_v45/deploy.sh
```

Или одной командой с вашей машины (если есть SSH):

```bash
ssh root@151.247.208.17 'bash -s' <<'EOS'
set -e
apt-get update -y && apt-get install -y git
cd /opt
rm -rf esp-tmp && git clone --branch cursor/architecture-4-5-beta-44fa --depth 1 https://github.com/paha22russ/esp.git esp-tmp
bash /opt/esp-tmp/vps_v45/deploy.sh
EOS
```

После деплоя откройте: **https://esp.pahavpn.cloud-ip.cc/**  
Токен: `grep INGEST_TOKEN /opt/boiler-v45/.env` → в настройки ESP.
