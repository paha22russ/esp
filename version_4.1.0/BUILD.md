# Инструкции по сборке и развертыванию

## Требования

### Программное обеспечение

1. **PlatformIO** (рекомендуется) или Arduino IDE
   - PlatformIO: https://platformio.org/
   - Arduino IDE: https://www.arduino.cc/en/software

2. **Python 3.x** (для esptool)
   - Устанавливается автоматически с PlatformIO

3. **Драйверы USB для ESP32**
   - CH340/CH341 для китайских модулей
   - CP2102 для других модулей

### Оборудование

- ESP32 DevKit (любая версия)
- USB кабель для подключения
- Компоненты согласно `ПИНЫ_ПОДКЛЮЧЕНИЯ.md`

---

## Установка зависимостей

### PlatformIO

Зависимости указаны в `platformio.ini` и устанавливаются автоматически:

```ini
lib_deps = 
    OneWire @ 2.3.8
    DallasTemperature @ 3.11.0
    ArduinoJson @ 6.21.5
    PubSubClient @ 2.8.0
    U8g2 @ 2.36.15
    NTPClient @ 3.2.1
    WiFiManager @ 2.0.17
    ArduinoOTA @ 2.0.0
```

**Установка**:
```bash
pio lib install
```

### Python зависимости

Для работы esptool требуется:
```bash
pip install intelhex
```

---

## Сборка прошивки

### PlatformIO

**Команда сборки**:
```bash
pio run -e esp32dev
```

**Результат**:
- Прошивка: `.pio/build/esp32dev/firmware.bin`
- Размер: ~1MB

### Arduino IDE

1. Выберите плату: **ESP32 Dev Module**
2. Настройки:
   - Upload Speed: 921600
   - CPU Frequency: 240MHz
   - Flash Frequency: 80MHz
   - Flash Size: 4MB
   - Partition Scheme: Default 4MB with spiffs
3. Установите библиотеки через Library Manager
4. Скомпилируйте: Sketch → Verify/Compile

---

## Загрузка прошивки

### Через USB (первая загрузка)

**PlatformIO**:
```bash
pio run -e esp32dev -t upload --upload-port COM3
```

**Arduino IDE**:
- Выберите порт: Tools → Port → COM3
- Загрузите: Sketch → Upload

**Примечание**: Замените COM3 на ваш порт.

### Загрузка файловой системы (SPIFFS)

**PlatformIO**:
```bash
pio run -e esp32dev -t uploadfs --upload-port COM3
```

**Arduino IDE**:
- Используйте плагин ESP32 Sketch Data Upload
- Или загрузите через веб-интерфейс после первой прошивки

---

## Первый запуск

### Подключение к WiFi

1. **Режим точки доступа** (если нет сохраненной сети):
   - SSID: `KotelAP`
   - Пароль: `kotel12345`
   - IP адрес: `192.168.4.1` (по умолчанию)

2. **Режим станции** (если есть сохраненная сеть):
   - Подключение к сохраненной сети
   - IP адрес получается от DHCP

### Определение IP адреса

**Способы**:
1. Через Serial Monitor (115200 baud):
   ```
   WiFi подключен!
   IP адрес: 192.168.1.100
   ```

2. Через OLED дисплей (отображается внизу)

3. Через роутер (список подключенных устройств)

4. Через точку доступа (всегда 192.168.4.1)

---

## Настройка проекта

### Изменение порта COM

**PlatformIO**:
```bash
pio run -e esp32dev -t upload --upload-port COM5
```

Или добавьте в `platformio.ini`:
```ini
[env:esp32dev]
upload_port = COM3
```

### Изменение скорости загрузки

В `platformio.ini`:
```ini
[env:esp32dev]
upload_speed = 460800
```

---

## OTA Обновление

### Через веб-интерфейс

1. Скомпилируйте прошивку: `pio run -e esp32dev`
2. Откройте веб-интерфейс: `http://{IP_ESP32}`
3. Перейдите в "Система" → "OTA Обновление"
4. Выберите файл: `.pio/build/esp32dev/firmware.bin`
5. Нажмите "Загрузить прошивку"
6. Дождитесь завершения (устройство перезагрузится)

### Через PlatformIO (OTA)

**Настройка в `platformio.ini`**:
```ini
[env:esp32dev_ota]
platform = espressif32
board = esp32dev
framework = arduino
upload_protocol = espota
upload_port = 192.168.1.100
upload_flags = --port=3232 --auth=kotel12345
```

**Загрузка**:
```bash
pio run -e esp32dev_ota -t upload
```

**Примечание**: Замените IP адрес на актуальный.

---

## Отладка

### Serial Monitor

**PlatformIO**:
```bash
pio device monitor --port COM3 --baud 115200
```

**Arduino IDE**: Tools → Serial Monitor (115200 baud)

### Типичные проблемы

1. **Порт занят**:
   - Закройте Serial Monitor
   - Отключите другие программы, использующие порт
   - Переподключите ESP32

2. **Ошибка загрузки**:
   - Нажмите кнопку BOOT на ESP32 во время загрузки
   - Уменьшите скорость загрузки
   - Проверьте кабель USB

3. **WiFi не подключается**:
   - Проверьте пароль
   - Убедитесь, что сеть в диапазоне 2.4GHz
   - Сбросьте настройки через веб-интерфейс

4. **Веб-интерфейс не загружается**:
   - Проверьте загрузку SPIFFS
   - Перезагрузите ESP32
   - Очистите кеш браузера (Ctrl+F5)

---

## Структура файлов проекта

```
stable_version/
├── src/
│   └── main.cpp          # Основной код (1483 строки)
├── data/
│   └── index.html        # Веб-интерфейс (1622 строки)
├── platformio.ini        # Конфигурация PlatformIO
├── ПИНЫ_ПОДКЛЮЧЕНИЯ.md   # Схема подключения
├── README.md             # Общее описание
├── FUNCTIONS.md          # Описание функций
├── LOGIC.md              # Логика работы
├── WEB_MENU.md          # Структура веб-меню
├── API.md               # API endpoints
└── BUILD.md              # Этот файл
```

---

## Версионирование

**Текущая версия**: 1.0.3 (стабильная)

**Изменения в версии 1.0.3**:
- Исправлена отзывчивость веб-интерфейса
- Добавлена OTA загрузка через веб
- Исправлено WiFi сканирование
- Оптимизированы задержки и блокирующие операции
- Добавлено отложенное сохранение в EEPROM

---

## Лицензия

Открытый исходный код. Используйте свободно.

---

## Поддержка

При возникновении проблем:
1. Проверьте Serial Monitor для диагностики
2. Убедитесь в правильности подключения (см. `ПИНЫ_ПОДКЛЮЧЕНИЯ.md`)
3. Проверьте версии библиотек
4. Очистите кеш PlatformIO: `pio run -t clean`

