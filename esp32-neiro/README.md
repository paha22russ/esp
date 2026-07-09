# ESP32 neiro — экспериментальный ИИ-агент

Подпроект для реализации **Dynamic Agent**: ESP32 отправляет слепок своего железа (GPIO, I2C), а сервер на Kali Linux через **Function Calling** нейросети решает, что делать с подключённым оборудованием.

## Архитектура

```
ESP32 (прошивка)  ──POST /api/telemetry──►  Kali Linux (FastAPI)
       ▲                                        │
       │                                        ▼
       └── JSON commands ◄──  LLM (Gemini → Ollama fallback / OpenAI / Anthropic)
                                    ▲
                              Web Dashboard (браузер)
```

## Структура

```
esp32-neiro/
├── firmware/esp32_firmware/esp32_firmware.ino   # Прошивка Arduino IDE
├── server/
│   ├── main.py          # FastAPI + веб-дашборд
│   ├── llm_agent.py     # Function Calling
│   ├── state.py         # Глобальное состояние
│   ├── requirements.txt
│   └── .env.example
└── README.md
```

---

## Часть 1: Сервер (Kali Linux)

### Требования

- Python 3.10+
- API-ключ Google Gemini **или** Ollama на homeserv в локальной сети

### Установка

```bash
cd esp32-neiro/server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
# Отредактируйте .env — впишите GOOGLE_API_KEY (Gemini)
nano .env
```

**Рекомендуемый режим (`LLM_PROVIDER=auto`):** сначала Gemini, при исчерпании квоты — автоматический переход на Ollama (`http://192.168.1.112:11434`).

### Запуск

```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

Откройте в браузере: `http://<IP_KALI>:8000`

### Переменные окружения (.env)

| Переменная | Описание |
|------------|----------|
| `LLM_PROVIDER` | `auto` (Gemini→Ollama), `google`, `ollama`, `openai`, `anthropic` |
| `GOOGLE_API_KEY` | Ключ Google Gemini |
| `GOOGLE_MODEL` | `gemini-2.0-flash` |
| `OLLAMA_BASE_URL` | `http://192.168.1.112:11434/v1` |
| `OLLAMA_API_KEY` | `ollama` (не проверяется) |
| `OLLAMA_MODEL` | `qwen2.5-coder:7b` / `14b` / `3b` |
| `HOST` | `0.0.0.0` |
| `PORT` | `8000` |

### Ollama на homeserv

Проверка доступности с Kali или другого ПК в сети `192.168.1.x`:

```bash
curl http://192.168.1.112:11434/api/tags
```

OpenAI-совместимый чат:

```bash
curl http://192.168.1.112:11434/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer ollama" \
  -d '{"model":"qwen2.5-coder:7b","messages":[{"role":"user","content":"Привет"}]}'
```

### Автоматический fallback Gemini → Ollama

При `LLM_PROVIDER=auto`:

1. Каждый запрос сначала идёт в **Gemini** (облако)
2. При ошибке квоты / rate limit / 429 — сервер **автоматически** переключается на **Ollama** на homeserv
3. После fallback все запросы идут в Ollama до нажатия **«Перезапустить сервер ИИ»** в дашборде
4. В дашборде отображается активный провайдер и статус fallback

### API

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/api/telemetry` | Телеметрия от ESP32 |
| POST | `/api/reboot_esp` | Перезагрузка ESP32 |
| POST | `/api/direct_command` | Прямой приказ нейросети |
| POST | `/api/ai/reset` | Сброс контекста ИИ |
| GET/POST | `/api/settings/prompt` | Системный промпт |
| GET | `/api/state` | Снимок состояния |
| GET | `/api/events` | SSE-поток для дашборда |
| GET | `/` | Веб-дашборд |

---

## Часть 2: Прошивка ESP32 (Arduino IDE)

### Требования

- **Плата:** ESP32 Dev Module (arduino-esp32 ≥ 2.0)
- **Arduino IDE** 2.x

### Библиотеки (Менеджер библиотек)

| Библиотека | Автор |
|------------|-------|
| ArduinoJson | Benoit Blanchon |
| U8g2 | olikraus |
| LiquidCrystal I2C | Frank de Brabander |

### Установка платы ESP32

1. Файл → Настройки → «Дополнительные ссылки для Менеджера плат»:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. Инструменты → Плата → ESP32 Arduino → **ESP32 Dev Module**

### Прошивка

1. Откройте `firmware/esp32_firmware/esp32_firmware.ino`
2. Подключите ESP32 по USB
3. Выберите COM-порт
4. Нажмите «Загрузить»

### Wi-Fi и Captive Portal

| Этап | Поведение |
|------|-----------|
| Первый запуск | Подключение к `OpenWrt` / `00000001` |
| Таймаут 15 с | AP `ESP32_AI_Setup` (без пароля) |
| Captive Portal | `http://192.168.4.1` — форма SSID, пароль, URL сервера |
| После сохранения | Перезагрузка и подключение к новой сети |

### Телеметрия

Каждые 3–5 секунд ESP32 отправляет POST на `{server_url}/api/telemetry`:

```json
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "uptime_ms": 123456,
  "gpio": [{"pin": 2, "mode": "OUTPUT", "value": 1}],
  "i2c_devices": ["0x3C"]
}
```

### Команды от сервера

| Команда | Параметры | Действие |
|---------|-----------|----------|
| `pin_mode` | pin, mode | Настройка GPIO |
| `digital_write` | pin, value | HIGH/LOW |
| `init_display` | address | OLED 0x3C / LCD 0x27 |
| `print_text` | text, line | Текст на дисплей |
| `reboot` | — | Перезагрузка |

---

## Часть 3: Веб-дашборд

Доступен на `http://<IP_KALI>:8000/`. Содержит:

- **Что происходит сейчас** — живой лог событий
- **Текущий срез железа** — карта GPIO и I2C-устройства
- **Мысли нейросети** — рассуждения LLM
- **Панель управления** — перезагрузка, прямые приказы, редактор промпта

---

## Быстрый старт

1. Запустите сервер на Kali (`uvicorn main:app --host 0.0.0.0 --port 8000`)
2. Прошейте ESP32
3. Если Wi-Fi не подключился — подключитесь к `ESP32_AI_Setup`, откройте `192.168.4.1`, введите SSID/пароль и URL сервера (`http://<IP_KALI>:8000`)
4. Откройте дашборд в браузере
5. Подключите I2C OLED (0x3C) — нейросеть обнаружит его и предложит инициализацию

## Железо

- ESP32 DevKit (любая плата на ESP32)
- Опционально: I2C OLED SSD1306 (адрес 0x3C) или LCD 1602 (адрес 0x27)
- Светодиод на GPIO 2 (встроенный на большинстве плат)

## Ограничения

- Связь ESP32 ↔ сервер по HTTP (локальная сеть)
- GPIO 6–11 зарезервированы под flash — не используются
- Один активный дисплей одновременно (OLED или LCD)
- LLM-ответ может занимать несколько секунд

## Лицензия

Часть репозитория [paha22russ/esp](https://github.com/paha22russ/esp).
