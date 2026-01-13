# Веб-интерфейс для управления котлом

## Описание

Полнофункциональный веб-интерфейс для управления твердотопливным котлом с возможностями:
- Управление уставкой температуры
- Отображение температур (Подача, Обратка, Котельная, Улица)
- Отображение состояний (Вентилятор, Насос)
- Настройки режима АВТО
- Настройки MQTT
- Настройки WiFi
- Привязка датчиков температуры
- Системная информация

## Загрузка HTML в SPIFFS

### Способ 1: Через PlatformIO

1. Убедитесь, что файл `data/index.html` существует
2. Выполните команду для загрузки файловой системы:
   ```bash
   pio run -e esp32dev -t uploadfs
   ```

### Способ 2: Через ESP32 Sketch Data Upload (Arduino IDE)

Если используете Arduino IDE:
1. Установите плагин "ESP32 Sketch Data Upload"
2. Поместите файл `index.html` в папку `data` проекта
3. Загрузите данные через Tools → ESP32 Sketch Data Upload

### Способ 3: Через веб-интерфейс (если доступен)

Если устройство уже работает и имеет веб-интерфейс для загрузки файлов, используйте его.

## Структура API

### GET /api/status
Получение текущего статуса системы
```json
{
  "supplyTemp": 65.5,
  "returnTemp": 58.2,
  "boilerTemp": 22.0,
  "outdoorTemp": -5.0,
  "setpoint": 60.0,
  "fan": true,
  "pump": true,
  "state": "HEATING",
  "wifiStatus": "Подключен"
}
```

### POST /api/setpoint?value=65
Установка уставки температуры (40-80°C)

### POST /api/control?device=fan&state=1
Управление устройствами (fan/pump, 0/1)

### GET /api/settings/auto
Получение настроек режима АВТО

### POST /api/settings/auto
Сохранение настроек режима АВТО
```json
{
  "setpoint": 60.0,
  "minTemp": 45.0,
  "maxTemp": 75.0,
  "hysteresis": 2.0,
  "inertiaTemp": 55.0,
  "inertiaTime": 10,
  "overheatTemp": 77.0,
  "heatingTimeout": 30
}
```

### GET /api/settings/mqtt
Получение настроек MQTT

### POST /api/settings/mqtt
Сохранение настроек MQTT
```json
{
  "enabled": true,
  "server": "m5.wqtt.ru",
  "port": 5374,
  "useTLS": false,
  "user": "u_OLTTB0",
  "password": "XDzCIXr0",
  "prefix": "kotel/device1",
  "tempInterval": 10,
  "stateInterval": 30
}
```

### POST /api/mqtt/test
Тест подключения к MQTT брокеру

### GET /api/wifi/info
Получение информации о WiFi подключении

### POST /api/wifi/reset
Сброс настроек WiFi (перезагрузка устройства)

### POST /api/sensors/scan
Сканирование датчиков DS18B20

### GET /api/sensors/mapping
Получение привязки датчиков

### POST /api/sensors/mapping
Сохранение привязки датчиков
```json
{
  "supply": "28FF1234567890AB",
  "return": "28FF1234567890CD",
  "boiler": "28FF1234567890EF",
  "outside": "28FF123456789012"
}
```

### GET /api/system/info
Получение системной информации

### POST /api/system/reboot
Перезагрузка устройства

## Использование

1. Подключитесь к WiFi сети устройства
2. Откройте в браузере IP адрес устройства (будет показан в Serial Monitor)
3. Используйте веб-интерфейс для управления и настройки

## Примечания

- Если HTML файл не загружен в SPIFFS, будет показана простая заглушка
- Все настройки сохраняются в EEPROM (реализация в TODO)
- Реальное управление реле и чтение датчиков нужно интегрировать из соответствующих модулей

