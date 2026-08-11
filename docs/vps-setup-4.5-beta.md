# VPS: 4.5-beta на Германии

**Хост:** `151.247.208.17`  
Готовый стек: каталог [`vps_v45/`](../vps_v45/README.md) (Caddy + FastAPI ingest + Postgres + публичный web UI).

ESP по умолчанию шлёт на `http://151.247.208.17` (`cfg::DEFAULT_VPS_BASE_URL`).

## Зачем так

Веб с данными и управлением должен открываться **отовсюду**.  
Надёжный вариант без проброса домашнего роутера: UI и API на VPS; ESP только исходящие HTTP (телеметрия + poll команд).

## Минимальная установка

См. `vps_v45/README.md`. Кратко:

```bash
ssh user@151.247.208.17
cd vps_v45 && cp .env.example .env   # сменить пароли/токен
sudo docker compose up -d --build
```

Открыть: http://151.247.208.17/

## Контракт

- `POST /api/v1/ingest` — телеметрия / user_event (Bearer)
- `GET  /api/v1/devices/{id}/latest` — для публичной страницы
- `POST /api/v1/devices/{id}/commands` — команды с веб-UI
- `GET  /api/v1/devices/{id}/commands/next` — ESP забирает команду

Схемы: `schemas/v45/`.
