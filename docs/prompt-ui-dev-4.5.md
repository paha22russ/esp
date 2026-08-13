# Промпт для ИИ: разработка веб-интерфейса котла (ESP32 · 4.5-beta)

Скопируй блок ниже целиком и отдай агенту/ИИ как ТЗ на интерфейс.

---

## РОЛЬ

Ты разрабатываешь **веб-интерфейс управления твердотопливным котлом** на ESP32.  
Интерфейс должен работать:

1. **Локально на ESP** — SPIFFS, один файл `index.html` (или компактный набор статики), без сборщика на устройстве.
2. **На публичном VPS-зеркале** — тот же UI, данные из `/api/status` (телеметрия с ESP).

Репозиторий: `paha22russ/esp`, ветка архитектуры 4.5: `cursor/architecture-4-5-beta-44fa`.  
Текущий референс UI: `data_v45/index.html` (полная копия production 4.2 + пометки 4.5).  
Прошивка API: `src_v45/ui/legacy_http.cpp`.  
Архитектура: `docs/architecture-4.5-beta.md`.

**Не меняй контракты API без явной необходимости.** Сначала UI поверх существующих endpoints.

---

## ПРОДУКТ В ОДНОМ АБЗАЦЕ

Контроллер котла: датчики температур → режимы Auto/Comfort/Neuro → вентилятор и насос.  
Safety всегда независим и выше режимов. Neuro на этапе 1 только наблюдает (теневой Auto), не командует железом напрямую.  
Подача и дымоход — PT1000 через MAX31865; обратка/котельная/улица — DS18B20; дом — MQTT с ESP01.

---

## ЧТО УЖЕ ЕСТЬ (использовать как baseline)

- Полноценный одностраничный UI 4.2 (~4000 строк HTML+CSS+JS) уже лежит в `data_v45/index.html`.
- Главный экран + выпадающее меню настроек + страницы настроек.
- Опрос `GET /api/status` каждые ~3 с.
- На VPS: https://esp.pahavpn.cloud-ip.cc/ — тот же UI; `/api/status` мапится из последней телеметрии.

Твоя задача: **улучшить/пересобрать интерфейс**, сохранив смысл экранов и API, либо сделать новый UI с тем же API-контрактом.

---

## РЕЖИМЫ РАБОТЫ (обязательная логика UI)

| Код | Имя | Смысл для UI |
|-----|-----|--------------|
| `0` / `auto` | Авто | Уставка по подаче (коридор min/max или setpoint+hysteresis). Переключатель «Авто↔Комфорт» в положении Авто. |
| `1` / `comfort` | Комфорт | Цель = температура **дома**. Показывать уставку дома. Если дом offline — предупреждение; Comfort без дома runtime уходит в Auto. |
| `2` / `neuro` | Нейро | Наблюдение/гипотезы. Подпись «Нейро». Не обещать ручное AI-управление актуаторами. |

Смена режима:

```http
POST /api/system/mode
Content-Type: application/json

{"mode": 0}   // или 1, или 2
// также допустимо: "auto" | "comfort" | "neuro"
```

Ответ: `{"success":true,"mode":0,"modeName":"Авто"}`.

Включение системы:

```http
POST /api/system/enable?enabled=1
```

Ручное реле (сервис):

```http
POST /api/control?device=fan&state=1
POST /api/control?device=pump&state=1
```

`device`: `fan` | `pump` | `sensor_power`.

---

## ГЛАВНЫЙ ЭКРАН — ЧТО ДОЛЖНО БЫТЬ

Один экран оператора (не дашборд из десяти виджетов). Приоритет:

1. **Статус системы** — Работа / Ожидание / Остановлена / Перегрев / Подброс угля / Safety trip.
2. **Подача** (°C) — главный датчик (PT1000). Уставка подачи видна в Авто.
3. **Дом** (°C) — MQTT. Если `homeTempSensorLWTOnline=false` → показать `offline`, без °C. В Комфорте — уставка дома ±.
4. **Обратка / Котельная / Улица** (°C).
5. **Дымоход** — есть поле `flueTemp`; можно совместить с карточкой улицы («Улица / дым NN°») или отдельным блоком.
6. **Режим** — Авто / Комфорт / Нейро.
7. **Вентилятор / Насос** — ВКЛ/ВЫКЛ (и желательно `fanPowerPct` 0–100, даже если HW пока ON/OFF).
8. **Система ВКЛ/ВЫКЛ**.
9. Кнопка **«Подброс угля»** (`POST /api/coalFeeding`) + таймер остатка.
10. Предупреждение **низкая обратка** при `lowReturnTemp=true`.

Поллинг: `GET /api/status` → обновление DOM. Null температуры → `--`.

### Поля `/api/status` (контракт UI)

```json
{
  "supplyTemp": 62.3,
  "returnTemp": 48.1,
  "boilerTemp": 35.0,
  "outdoorTemp": -5.2,
  "homeTemp": 23.4,
  "flueTemp": 180.0,
  "setpoint": 60,
  "hysteresis": 2.0,
  "fan": true,
  "fanPowerPct": 100,
  "pump": true,
  "systemEnabled": true,
  "state": "HEATING",
  "workMode": 0,
  "workModeName": "Авто",
  "mode": "auto",
  "modeRu": "Авто",
  "homeTempSensorValid": true,
  "homeTempSensorLWTOnline": true,
  "targetHomeTemp": 22.0,
  "firmwareVersion": "4.5.0-beta",
  "version": "4.5.0-beta",
  "wifiStatus": "Подключен",
  "wifiRSSI": -55,
  "mqttStatus": "Подключен",
  "coalFeeding": false,
  "coalFeedingRemaining": 0,
  "lowReturnTemp": false,
  "coalBurned": false,
  "boilerExtinguished": false,
  "ignitionInProgress": false,
  "safetyTrip": false,
  "safetyReason": "",
  "sensorPowerOn": true,
  "supplyTrend": 1,
  "returnTrend": -1,
  "boilerTrend": 0,
  "outdoorTrend": null,
  "homeTrend": 1,
  "autoMin": 55,
  "autoMax": 70,
  "comfortRoom": 22,
  "uptime": 3600,
  "freeHeap": 120000,
  "fanStats": {
    "totalWorkTime": 0,
    "dailyWorkTime": 0,
    "cycleCount": 0,
    "dailyCycleCount": 0,
    "currentWorkTime": 0
  }
}
```

Тренды: `1` рост, `-1` падение, иначе скрыть стрелку.  
Температуры могут быть `null`.

---

## СТРАНИЦЫ НАСТРОЕК (меню)

Сохрани разделы (можно перекомпоновать UX, но не выкидывай смысл):

| Ключ | Название | API |
|------|----------|-----|
| auto | Режим Авто | `GET/POST /api/settings/auto` — `setpoint`, `hysteresis`, опц. `autoMin`/`autoMax` |
| comfort | Режим Комфорт | `GET/POST /api/settings/comfort` — `targetHomeTemp`, `minBoilerTemp`, `maxBoilerTemp`, `waitTemp`, `hysteresisOn/Off/Boiler` |
| neuro | Режим Нейро | `POST /api/system/mode` `{mode:2}` + позже гипотезы `GET /api/hypotheses` |
| mqtt | MQTT | `GET/POST /api/settings/mqtt`, тест `POST /api/mqtt/test` |
| wifi | WiFi | `/api/wifi/info`, `scan`, `scan/results`, `settings`, `signal`, `reset` |
| sensors | Датчики | `/api/sensors/scan`, `/api/sensors/mapping` |
| pins | Подключение | справочная распиновка (см. ниже) + опц. `GET /api/pins` |
| ntp | NTP | `/api/ntp/settings`, `/api/ntp/time` |
| ml | ML / гипотезы | `/api/ml/settings`, `/api/hypotheses` |
| relay | Реле | `/api/settings/relay` + ручной `/api/control` |
| update | Обновления | `/api/update/check`, `install`, `settings` (канал **v45**, не 4.2) |
| tunnel | VPS | `/api/tunnel/settings`, `/api/vps/settings` |
| system | Система | `/api/system/info`, reboot, boot log |
| log | Журнал | `/api/system/log`, `/api/journal` |

Пользовательские события (подброс топлива и т.п.):

```http
POST /api/events/user
{"text":"загрузил уголь", "audio_url": null}
```

---

## ДАТЧИКИ И РАСПИНОВКА (для экрана «Подключение»)

| GPIO | Назначение |
|------|------------|
| 16 | Реле вентилятора 220 В |
| 17 | Реле насоса |
| 25 | Реле **снятия питания** шин DS18B20 (не питание с GPIO) |
| 4 | 1-Wire #1 — обратка |
| 5 | 1-Wire #2 — котельная + улица |
| 21/22 | OLED SDA/SCL |
| 18/19/23 | Энкодер CLK/DT/SW |
| 26 | MAX31865 CS — PT1000 дымоход (2-wire) |
| 27 | MAX31865 CS — PT1000 подача (3-wire) |
| 32/33/34 | SPI SCK/MOSI/MISO |
| 2 | Свободен |

Критический датчик: **подача PT1000**. Без неё автоматика в безопасном состоянии.

---

## ДВА КОНТЕКСТА ЗАПУСКА UI

### A) ESP (основной)
- Origin = IP ESP (`http://192.168.x.x/`).
- Все `/api/*` идут на устройство.
- Полный CRUD настроек.

### B) VPS-зеркало
- Origin = `https://esp.pahavpn.cloud-ip.cc/` (или HTTP).
- `GET /api/status` — зеркало телеметрии.
- `POST /api/system/mode` и `enable` — **очередь команд** на ESP (`queued: true`).
- Остальные settings на VPS могут быть stub (`success: true, vps: true`) — не ломай UI на пустых stub-ответах; показывай «доступно на устройстве» где уместно.

Сделай UI устойчивым: missing fields → дефолты, не падать.

---

## ТЕЛЕМЕТРИЯ VPS (если рисуешь отдельный remote UI)

Сэмпл с ESP (schema v1):

```json
{
  "schema": "1.0",
  "fw": "4.5.0-beta",
  "channel": "beta",
  "mode": "auto",
  "state": "HEATING",
  "system_enabled": true,
  "temps": {
    "supply": 62.0,
    "flue": 180.0,
    "return": 48.0,
    "boiler_room": 30.0,
    "outdoor": -5.0,
    "home": 22.5
  },
  "actuators": {
    "fan_power_pct": 100,
    "fan_relay": true,
    "pump": true,
    "sensor_power": true
  },
  "setpoints": {
    "auto_min": 55,
    "auto_max": 70,
    "comfort_room": 22,
    "comfort_boiler_min": 50,
    "comfort_boiler_max": 75
  },
  "safety_trip": false,
  "safety_reason": ""
}
```

Для операторского UI предпочтительнее контракт `/api/status` выше (уже нормализован).

---

## ДИЗАЙН-ТРЕБОВАНИЯ

- Мобильный-first: телефон у котла / удалённо. Крупные температуры, крупные тач-зоны.
- Тёмная тема ок (котельная, ночь), но **не** клише «фиолетовый AI-glow».
- Один главный экран = одна композиция оператора, не админ-дашборд.
- Бренд/название продукта — заметный якорь (например «Котел» / версия `4.5-beta`), не только мелкий текст в углу.
- Настройки — отдельные экраны/панели, не свалка на главном.
- Карточки только если они помогают сканировать температуры; без декоративного шума, pill-кластеров, emoji-спама.
- Русский язык UI.
- Минимум 2–3 осмысленных motion (смена статуса, появление предупреждений, переключение режима) — без параллакса ради параллакса.
- Статику для SPIFFS держать компактной: один `index.html` предпочтительно; если разбиваешь — объясни как собрать в SPIFFS.

Избегать: Inter/Roboto/Arial как единственный стек; плоский однотонный фон без атмосферы; перегруженный hero со статистикой.

---

## НЕЛОМАТЬ / ИНВАРИАНТЫ

1. Safety нельзя «выключить» из UI. Можно только показать `safetyTrip` + `safetyReason`.
2. Neuro ≠ ручное AI-управление вентилятором.
3. Comfort без дома → UI честно показывает проблему; не притворяйся что Comfort активен идеально.
4. Канал OTA в UI: **v45-beta**, не путать с 4.2.
5. Миграции настроек с 4.2 нет (clean flash).
6. Сохраняй совместимость полей `workMode` как **число** 0/1/2 (старый UI так работает).

---

## ОЖИДАЕМЫЙ РЕЗУЛЬТАТ РАБОТЫ

1. Новый или переработанный `data_v45/index.html` (или эквивалент + инструкция упаковки в SPIFFS).
2. Главный экран + все ключевые настройки работают против API выше.
3. Offline/demo-режим с фейковыми данными (чтобы кликать без ESP) — желательно отдельным флагом `?demo=1` или `demo/boiler-ui-demo.html`.
4. Краткий README: как открыть локально, какие API дергаются, отличия ESP vs VPS.
5. Скрин/описание состояний: Авто, Комфорт, Нейро, дом offline, safety trip, подброс угля.

---

## ПРИЁМОЧНЫЕ СЦЕНАРИИ

1. Поллинг статуса обновляет все температуры и реле.
2. Переключение Авто↔Комфорт шлёт `POST /api/system/mode` с `0`/`1`, UI не откатывается зря.
3. Нейро выставляет `mode:2`, подпись «Нейро».
4. Дом offline: значение `offline`, Комфорт нельзя включить «втихую».
5. Подброс угля запускает таймер.
6. Страницы Авто/Комфорт читают и сохраняют settings.
7. На пустых/частичных JSON UI не падает.
8. Мобильная вёрстка 360–430px ширины читаема одной рукой.

---

## СТЕК (рекомендация)

- Один HTML + CSS + JS без React/Vue на устройстве (лимит SPIFFS/простота).
- Если хочешь современный фреймворк — собери статику в `data_v45/` одним бандлом.
- Mock: перехват `fetch` или статичный JSON.

---

## КОНТЕКСТ ДЛЯ ОРИЕНТИРА ПО КОДУ

```
data_v45/index.html          # текущий UI
src_v45/ui/legacy_http.cpp   # API на ESP
src_v45/app/types.h          # WorkMode, PlantState
src_v45/telemetry/schema_writer.h
vps_v45/ingest/app.py        # /api/status на VPS
docs/architecture-4.5-beta.md
docs/clarifications-4.5-beta.md
```

Начни с аудита текущего `data_v45/index.html`, предложи план экранов на 5–8 пунктов, затем реализуй главный экран и API-связку, потом настройки.

---

*Конец промпта.*
