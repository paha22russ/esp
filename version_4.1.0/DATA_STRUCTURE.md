# Структура данных ML

## Описание

Документ описывает структуру JSON данных, публикуемых ESP32 в топик `kotel/device1/ml/data`.

## Полная структура JSON

```json
{
  // === ТЕМПЕРАТУРЫ ===
  "supplyTemp": 65.5,        // float, Температура подачи (°C)
  "returnTemp": 45.2,        // float, Температура обратки (°C)
  "boilerTemp": 70.0,        // float, Температура котла (°C)
  "outdoorTemp": -5.0,       // float, Температура на улице (°C)
  "homeTemp": 22.5,          // float | null, Температура в доме (°C), может быть null если датчик offline
  "homeTempStatus": "online", // string, Статус датчика температуры дома: "online" | "offline"
  "tempDiff": 20.3,          // float, Разница температур подачи и обратки (°C)

  // === СОСТОЯНИЯ УСТРОЙСТВ ===
  "fan": true,               // boolean, Состояние вентилятора
  "pump": true,              // boolean, Состояние насоса
  "systemEnabled": true,     // boolean, Система включена/выключена
  "state": "IDLE",           // string, Текущее состояние системы
                             // Возможные значения: "IDLE", "Работа", "Перегрев!", 
                             // "Остановлена", "Подброс угля", "Ожидание"

  // === НАСТРОЙКИ AUTO РЕЖИМА ===
  "setpoint": 60.0,          // float, Уставка температуры (°C)
  "minTemp": 40.0,           // float, Минимальная температура (°C)
  "maxTemp": 80.0,           // float, Максимальная температура (°C)
  "hysteresis": 2.0,         // float, Гистерезис (°C)
  "inertiaTemp": 5.0,        // float, Температура инерции (°C)
  "inertiaTime": 300,        // int, Время инерции (секунды)
  "overheatTemp": 77.0,      // float, Температура перегрева (°C)
  "heatingTimeout": 1800,    // int, Таймаут нагрева (секунды)

  // === ПОДБРОС УГЛЯ ===
  "coalFeedingActive": false,      // boolean, Активен ли подброс угля
  "coalFeedingElapsed": 0,         // int, Время работы подброса (секунды)

  // === ВРЕМЕННЫЕ МЕТКИ ===
  "timestamp": 1704312000,   // int, Unix timestamp (секунды)
  "uptime": 86400,           // int, Время работы ESP32 (секунды)
  "time": "12:00:00",        // string, Время в формате HH:MM:SS (если NTP включен)
  "date": "03.01.2026",      // string, Дата в формате DD.MM.YYYY (если NTP включен)

  // === WIFI И СЕТЬ ===
  "wifiRSSI": -65,           // int, Уровень сигнала WiFi (dBm)
  "wifiSSID": "MyWiFi",      // string, Имя WiFi сети
  "wifiIP": "192.168.1.37",  // string, IP адрес ESP32

  // === СИСТЕМНЫЕ ПАРАМЕТРЫ ===
  "freeMem": 274548,         // int, Свободная память (байты)
  "heapSize": 327680,        // int, Размер heap (байты)

  // === ML НАСТРОЙКИ ===
  "mlPublishInterval": 10,   // int, Интервал публикации ML данных (секунды)

  // === ДОПОЛНИТЕЛЬНАЯ ИНФОРМАЦИЯ ===
  "sensorCount": 4,          // int, Количество найденных датчиков DS18B20
  "mqttConnected": true       // boolean, Статус подключения MQTT
}
```

## Диапазоны значений

### Температуры
- `supplyTemp`: обычно 40-80°C
- `returnTemp`: обычно 30-70°C
- `boilerTemp`: обычно 50-90°C
- `outdoorTemp`: обычно -30 до +30°C (зависит от региона)
- `homeTemp`: обычно 15-30°C, может быть null если датчик offline
- `homeTempStatus`: "online" когда датчик подключен и данные актуальны (обновление < 5 минут), "offline" когда датчик не в сети
- `tempDiff`: обычно 0-30°C

### Состояния
- `fan`: `true` или `false`
- `pump`: `true` или `false`
- `systemEnabled`: `true` или `false`
- `state`: строка с текущим состоянием

### WiFi RSSI
- Обычно от -100 до 0 dBm
- -50 dBm и выше: отличный сигнал
- -70 до -50 dBm: хороший сигнал
- -90 до -70 dBm: слабый сигнал
- Ниже -90 dBm: очень слабый сигнал

## Частота публикации

Данные публикуются с интервалом, настроенным в веб-интерфейсе ESP32:
- По умолчанию: 10 секунд
- Минимум: 5 секунд
- Максимум: 300 секунд

## Размер сообщения

- Размер JSON: ~540-600 байт (зависит от наличия homeTemp)
- Размер после сжатия: зависит от MQTT брокера

## Примечания

- Поле `homeTemp` может быть `null` если датчик температуры в доме не подключен или offline
- Поле `homeTempStatus` показывает статус датчика: "online" когда данные актуальны (обновление < 5 минут), "offline" когда датчик не в сети
- Все числовые поля имеют значения по умолчанию при отсутствии данных
- Булевы поля всегда имеют значение `true` или `false`

## Примеры использования

### Python
```python
import json

data = json.loads(mqtt_message)
supply_temp = data['supplyTemp']
fan_state = data['fan']
```

### C#
```csharp
var data = JsonSerializer.Deserialize<Dictionary<string, object>>(mqttMessage);
double supplyTemp = Convert.ToDouble(data["supplyTemp"]);
bool fanState = Convert.ToBoolean(data["fan"]);
```

