# Деплой VPS Germany (4.5-beta)

**Хост:** `151.247.208.17`  
**Цель:** веб с данными и управлением открывается из любой сети.

## Как это работает

```
Браузер (телефон/ПК) ──HTTP──► VPS :80 (Caddy + web UI)
                                   │
ESP32 ──telemetry──POST /api/v1/ingest──┘
ESP32 ◄──commands──GET  /api/v1/devices/{id}/commands/next
```

Прямой доступ к домашнему ESP из интернета **не обязателен**.  
Публичный UI живёт на VPS; ESP пушит телеметрию и забирает команды.

## Установка на сервере

```bash
ssh root@151.247.208.17   # или ваш пользователь
sudo apt update && sudo apt install -y docker.io docker-compose-v2 git
git clone <repo> && cd <repo>/vps_v45
cp .env.example .env   # задать пароли/токен
sudo docker compose up -d --build
curl http://127.0.0.1/api/v1/health
```

Открыть в браузере: **http://151.247.208.17/**

## Переменные (.env)

```
POSTGRES_PASSWORD=...
INGEST_TOKEN=...
```

На ESP: `POST /api/vps/settings` с `baseUrl=http://151.247.208.17`, тот же `token`.

## TLS

Когда будет домен — прописать его в `Caddyfile` (пример в файле). По IP Caddy отдаёт HTTP :80.

## Firewall

Открыть `80/tcp` (и `443/tcp` после TLS). Postgres наружу не открывать.

## Безопасность

- Сменить `INGEST_TOKEN` сразу.
- Команды управления требуют Bearer token.
- Safety на ESP не зависит от VPS.
