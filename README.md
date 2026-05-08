# ESP32 Boiler Control

Проект управления котлом на ESP32 с веб-интерфейсом, MQTT, OTA-обновлением через GitHub и доступом через VPS-туннель.

## Что важно помнить

- Удаленный доступ "из любой сети" работает через VPS, но туннель поднимает **ПК в той же локальной сети, что и ESP**.
- ESP хранит настройки туннеля и отдает готовый конфиг клиента по API.
- Без запущенного `frpc` на локальном ПК доступ через VPS работать не будет.

## Версия и OTA

- Версия прошивки хранится в `src/main.cpp` (`FIRMWARE_VERSION`) и в `version.txt`.
- OTA берет обновления из этого репозитория:
  - `version.txt`
  - `firmware.bin`
  - `spiffs.bin`

## Сборка (PlatformIO)

```bash
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

После сборки публикуются бинарники:

- `.pio/build/esp32dev/firmware.bin` -> `firmware.bin`
- `.pio/build/esp32dev/spiffs.bin` -> `spiffs.bin`

## VPS туннель (доступ с телефона)

В веб-интерфейсе ESP есть раздел `VPS Туннель`:

- `enabled`
- `vpsHost`
- `vpsPort`
- `authToken`
- `remotePort`
- `localTargetPort`
- `publicUrl`
- `tunnelName`

API:

- `GET /api/tunnel/settings`
- `POST /api/tunnel/settings`
- `GET /api/tunnel/frpc-config` (возвращает `frpc.toml`)

### Что запускать на ПК в той же сети с ESP

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Tools\frp\sync-and-run-frpc.ps1" -EspBaseUrl "http://<IP_ESP>"
```

Проверка:

```powershell
Get-Process frpc
```

Если процесс есть, туннель работает, и веб ESP доступен по `publicUrl` (например `http://72.56.101.71:18080`).

## Почему без этого нельзя

Если телефон не в домашней Wi-Fi сети, нужен внешний туннель/VPN.  
ESP сам по себе не держит стабильный VPS-туннель для этой схемы — это делает локальный ПК через `frpc`.
