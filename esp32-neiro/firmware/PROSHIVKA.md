# Прошивка ESP32 без Arduino IDE

Arduino IDE **не нужен**. Есть 3 способа.

---

## Способ 1 — Готовый .bin + PowerShell (самый простой на Windows)

1. Подключите ESP32 по USB (кабель с передачей данных, не только зарядка)
2. Установите [Python 3](https://www.python.org/downloads/) — галочка **Add to PATH**
3. В PowerShell:

```powershell
cd "C:\Users\Yarag N2\Yandex.Disk\Программы\Проекты Сервер\ESP neiro\firmware"
powershell -ExecutionPolicy Bypass -File flash-windows.ps1
```

Скрипт сам поставит `esptool` и прошьёт `bin/firmware.bin`.

Если не видит порт — укажите вручную:
```powershell
.\flash-windows.ps1 -Port COM5
```

**Драйверы USB** (если COM-порт не появляется):
- CH340: https://www.wch.cn/downloads/CH341SER_EXE.html
- CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

---

## Способ 2 — PlatformIO из командной строки (без GUI)

Один раз:
```powershell
pip install platformio
```

Сборка + прошивка:
```powershell
cd firmware
pio run -t upload
pio device monitor -b 115200
```

Порт вручную: `pio run -t upload --upload-port COM5`

---

## Способ 3 — Прошить с homeserv (ESP32 в USB Kali)

Если удобнее воткнуть ESP32 в homeserv, а не в ПК:

```bash
ssh pavel@192.168.1.112
cd ~/esp/esp32-neiro/firmware
chmod +x flash-homeserv.sh
./flash-homeserv.sh
```

---

## Я не могу прошить удалённо

У агента **нет доступа к USB** на вашем ПК. Могу:
- собрать `firmware.bin` (уже в `firmware/bin/`)
- прошить **с homeserv**, если ESP32 подключён туда по USB

---

## После прошивки

1. Wi-Fi: сеть `OpenWrt` / `00000001` или Captive Portal `ESP32_AI_Setup`
2. URL сервера: `http://192.168.1.112:8000`
3. API-токен: `esp32-neiro-change-me`
4. Дашборд: http://192.168.1.112:8000

---

## Если Arduino IDE не запускается

Частые причины на Windows:
- Не хватает Java / повреждён кэш — **не чините**, используйте способ 1 или 2
- Антивирус блокирует — добавьте исключение или используйте `esptool`
- Portable-версия ломается — ставьте PlatformIO через `pip`
