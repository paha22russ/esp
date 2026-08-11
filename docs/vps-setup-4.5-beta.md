# VPS: что поставить для 4.5-beta

VPS — центральный сервер хранения телеметрии, событий, ASR/классификации и будущего ML.  
ESP32 шлёт JSON на `POST {baseUrl}/api/v1/ingest`.

## Минимальный стек (рекомендация)

| Компонент | Зачем | Пример |
|-----------|--------|--------|
| Linux (Debian 12 / Ubuntu 24.04) | хост | VPS NL/FI |
| Docker + Compose | изоляция сервисов | docker.com |
| Reverse proxy | TLS, домены | Caddy или Nginx |
| PostgreSQL 16 | телеметрия, события, гипотезы | postgres:16 |
| Object storage (опц.) | голос/аудио | MinIO или локальный диск |
| Ingest API | приём с ESP / Telegram | Python FastAPI или Node |
| Telegram Bot | текст/голос пользователя | aiogram / telegraf |
| ASR | голос → текст | faster-whisper (CPU/GPU) или cloud API |
| Worker ML (позже) | обучение / гипотезы | отдельный контейнер |

## Compose-скелет (ориентир)

```yaml
services:
  db:
    image: postgres:16
    environment:
      POSTGRES_DB: boiler
      POSTGRES_USER: boiler
      POSTGRES_PASSWORD: change_me
    volumes: [ "pgdata:/var/lib/postgresql/data" ]

  ingest:
    build: ./ingest
    environment:
      DATABASE_URL: postgres://boiler:change_me@db:5432/boiler
      INGEST_TOKEN: change_me
    ports: [ "8080:8080" ]
    depends_on: [ db ]

  bot:
    build: ./bot
    environment:
      TELEGRAM_TOKEN: ...
      INGEST_URL: http://ingest:8080
      ASR_URL: http://asr:9000
    depends_on: [ ingest ]

  asr:
    image: fedirz/faster-whisper-server  # пример; выбрать актуальный образ
    # либо свой сервис на faster-whisper
    profiles: [ "asr" ]

  caddy:
    image: caddy:2
    ports: [ "80:80", "443:443" ]
    volumes: [ "./Caddyfile:/etc/caddy/Caddyfile", "caddy_data:/data" ]

volumes:
  pgdata:
  caddy_data:
```

## API, которые ждут от VPS (контракт beta)

### `POST /api/v1/ingest`
Authorization: `Bearer <token>`

Тело — один из типов:

1. **telemetry** — см. `schemas/v45/telemetry.sample.json`
2. **user_event** — см. `schemas/v45/user_event.json`

Ответ: `202/200 {"ok":true}`

### `POST /api/v1/events/telegram` (бот → ingest)
Принимает текст или путь к аудио.  
ASR на VPS → текст → классификация (`fuel_added`, …) → сохранение **оригинала** + snapshot телеметрии по timestamp (не спрашивать у пользователя температуры).

### (позже) `GET /api/v1/hypotheses`, `POST .../confirm`

## Таблицы (минимум)

- `devices(id, name, token_hash, created_at)`
- `telemetry(ts, device_id, payload jsonb)` — или timescaledb hypertable
- `user_events(ts, device_id, event, text, audio_uri, snapshot jsonb)`
- `sensor_recovery(ts, device_id, bus, detail)`
- `safety_events(ts, device_id, reason)`
- `hypotheses(id, device_id, text, basis, confidence, confirmed, created_at)`

## Что поставить на сервере прямо сейчас

```bash
sudo apt update
sudo apt install -y docker.io docker-compose-v2 git curl
sudo usermod -aG docker $USER
# перелогин
```

Далее: репозиторий ingest+bot (отдельный, ещё не в этом PR) + DNS + TLS.

## Сеть ESP → VPS

- HTTPS предпочтительно.
- Токен устройства в UI `/api/vps/settings` (`baseUrl`, `token`, `deviceId`).
- На ESP очередь ring-buffer (32) при офлайне.
- Safety **не** зависит от доступности VPS.

## Ресурсы

| Нагрузка | CPU/RAM |
|----------|---------|
| Только ingest + Postgres + bot (текст) | 1 vCPU / 1–2 GB |
| + faster-whisper CPU | 2–4 vCPU / 4–8 GB |
| + обучение моделей позже | отдельный worker / GPU по необходимости |

## Безопасность

- Отдельный token на устройство.
- Не открывать Postgres наружу.
- Rate-limit ingest.
- Аудио хранить с ACL; в ML отдавать только нужное.
