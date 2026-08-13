# Архитектура 4.5-beta (ESP32 · твердотопливный котёл)

Отдельная линейка от production **4.2.x**. Собирается env `esp32dev_v45`, исходники в `src_v45/`, UI в `data_v45/`.

Версия прошивки и UI: **4.5.0-beta**.

## Цели этапа beta

1. Надёжная базовая автоматика (Auto / Comfort) + независимый Safety.
2. Новая распиновка (PT1000 MAX31865 + DS18B20).
3. Достоверность DS18B20 и recovery через реле GPIO25.
4. Сбор телеметрии и пользовательских событий на VPS.
5. Neuro = только наблюдение и гипотезы (без обхода safety).
6. `fan_power` 0–100% уже сейчас (HW пока ON/OFF).

## Слои

```
┌─────────────────────────────────────────────┐
│ Web UI / OLED / API                         │
├─────────────────────────────────────────────┤
│ Events (journal, user_events)               │
│ Telemetry (schema → VPS queue)              │
│ ML stubs (hypothesis_engine)                │
├─────────────────────────────────────────────┤
│ Orchestrator (mode select)                  │
│   AutoMode / ComfortMode / Neuro(observe)   │
├─────────────── SafetyGuard (always on) ─────┤
├─────────────────────────────────────────────┤
│ SensorHub (PT1000 + DS18B20 + quality)      │
│ FanActuator / PumpActuator / SensorPower    │
└─────────────────────────────────────────────┘
```

**Инвариант:** SafetyGuard выполняется до применения выхода режима и может принудительно выключить вентилятор. AI/Neuro/VPS/Telegram не имеют API для отключения safety.

## Распиновка

| GPIO | Назначение |
|------|------------|
| 16 | Реле вентилятора 220 В |
| 17 | Реле насоса |
| 25 | Реле **полного снятия/подачи** питания DS18B20 (не питание от GPIO) |
| 4 | 1-Wire #1 — обратка |
| 5 | 1-Wire #2 — котельная + улица |
| 21/22 | OLED SDA/SCL (0x3C) |
| 18/19/23 | Энкодер CLK/DT/SW |
| 26 | MAX31865 #1 CS — PT1000 дымоход (2-wire) |
| 27 | MAX31865 #2 CS — PT1000 подача (3-wire) |
| 32/33/34 | SPI SCK/MOSI/MISO |
| 2 | Свободен |

## Режимы

- **Auto** — коридор MIN/MAX по PT1000 подачи; аварийные защиты активны.
- **Comfort** — цель = **температура дома (MQTT)**. Если дом offline/invalid → **fallback в Auto**.
- **Neuro (этап 1)** — гипотезы + сбор; железо ведёт теневой Auto. Прямых AI-команд актуаторам нет.

## Прошивка / OTA

- Канал: `v45-beta` (`version_v45.txt`, `firmware_v45.bin`, `spiffs_v45.bin`).
- **Миграции с 4.2 нет.** Только чистая прошивка / OTA внутри beta.
- GPIO2 не резервируется под диммер.

## Публичный веб

VPS `esp.pahavpn.cloud-ip.cc` (`151.247.208.17`, `vps_v45/`): nginx vhost + ingest API.  
Браузер ↔ VPS; ESP пушит телеметрию и забирает команды. HTTPS/:443 на этом хосте занят VPN — UI по HTTP.

## DS18B20 recovery

Единичный выброс игнорируется. После N подряд ошибок шины:

1. запись в журнал `ds_recovery_start`
2. реле GPIO25 снимает питание
3. пауза (`DS_RECOVERY_POWER_OFF_MS`)
4. питание снова
5. re-init 1-Wire, detect, продолжение работы  
Cooldowndown между recovery одной шины — `DS_RECOVERY_COOLDOWN_MS`.

## Fan / Pump

- `FanActuator::setPowerPercent(0..100)` — сейчас порог → реле ON/OFF; позже диммер без смены API.
- Насос имеет afterheat после выключения вентилятора.

## Сборка

```bash
pio run -e esp32dev_v45
pio run -e esp32dev_v45 -t buildfs
pio run -e esp32dev_v45 -t upload
pio run -e esp32dev_v45 -t uploadfs
```

Production 4.2.x: `pio run -e esp32dev` (без изменений поведения).
