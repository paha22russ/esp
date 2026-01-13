# Промпт для обучения ML модели управления твердотопливным котлом

## Описание задачи

Необходимо обучить модель машинного обучения для оптимизации управления твердотопливным котлом. Модель должна предсказывать оптимальные действия (включение/выключение вентилятора и насоса) на основе текущего состояния системы и предсказания температуры.

## Структура входных данных (Features)

### 1. Температуры (Temperature Features)

```json
{
  "supplyTemp": 65.5,        // float, Температура подачи (°C), диапазон: 40-80°C
  "returnTemp": 45.2,        // float, Температура обратки (°C), диапазон: 30-70°C
  "boilerTemp": 70.0,        // float, Температура котла (°C), диапазон: 50-90°C
  "outdoorTemp": -5.0,       // float, Температура на улице (°C), диапазон: -30 до +30°C
  "homeTemp": 22.5,          // float | null, Температура в доме (°C), диапазон: 15-30°C, может быть null если датчик offline
  "homeTempStatus": "online" // string, Статус датчика температуры дома: "online" | "offline"
}
```

**Производные признаки (можно вычислить):**
- `tempDiff`: `supplyTemp - returnTemp` (разница температур подачи и обратки)
- `supplyReturnRatio`: `supplyTemp / returnTemp` (соотношение температур)
- `boilerSupplyDiff`: `boilerTemp - supplyTemp` (разница между котлом и подачей)
- `homeOutdoorDiff`: `homeTemp - outdoorTemp` (разница между домом и улицей, если homeTemp доступен)

### 2. Состояния устройств (Device States)

```json
{
  "fan": true,               // boolean, Состояние вентилятора (включен/выключен)
  "pump": true,              // boolean, Состояние насоса (включен/выключен)
  "systemEnabled": true,     // boolean, Система включена/выключена
  "state": "IDLE"            // string, Текущее состояние системы
}
```

**Возможные значения `state`:**
- `"IDLE"` - Простой
- `"HEATING"` - Нагрев
- `"WORKING"` - Работа
- `"OVERHEAT"` - Перегрев
- `"STOPPED"` - Остановлена
- `"COAL_FEEDING"` - Подброс угля
- `"COAL_BURNED"` - Уголь прогорел
- `"FAN_TEST"` - Тест вентилятора
- `"HEATING_TIMEOUT"` - Таймаут нагрева
- `"HIGH_TEMP"` - Высокая температура

**Производные признаки:**
- `fanPumpCombined`: `fan * 2 + pump` (0=оба выключены, 1=только насос, 2=только вентилятор, 3=оба включены)
- `isActive`: `fan || pump` (boolean, активна ли система)

### 3. Настройки Auto режима (Auto Mode Settings)

```json
{
  "setpoint": 60.0,          // float, Уставка температуры (°C), диапазон: 40-80°C
  "minTemp": 40.0,           // float, Минимальная температура (°C), диапазон: 30-50°C
  "maxTemp": 80.0,           // float, Максимальная температура (°C), диапазон: 70-90°C
  "hysteresis": 2.0,        // float, Гистерезис (°C), диапазон: 1-5°C
  "inertiaTemp": 5.0,        // float, Температура инерции (°C), диапазон: 2-10°C
  "inertiaTime": 300,       // int, Время инерции (секунды), диапазон: 60-600 сек
  "overheatTemp": 82.0,      // float, Температура перегрева (°C), диапазон: 70-90°C
  "heatingTimeout": 1800    // int, Таймаут нагрева (секунды), диапазон: 600-3600 сек
}
```

**Производные признаки:**
- `setpointDiff`: `supplyTemp - setpoint` (отклонение от уставки)
- `setpointRatio`: `supplyTemp / setpoint` (соотношение к уставке)
- `hysteresisRange`: `[setpoint - hysteresis, setpoint + hysteresis]` (диапазон гистерезиса)
- `isInHysteresisRange`: `(setpoint - hysteresis) <= supplyTemp <= (setpoint + hysteresis)`
- `isOverheating`: `supplyTemp >= overheatTemp`
- `isBelowMinTemp`: `supplyTemp < minTemp`
- `isAboveMaxTemp`: `supplyTemp > maxTemp`

### 4. Подброс угля (Coal Feeding)

```json
{
  "coalFeedingActive": false,      // boolean, Активен ли подброс угля
  "coalFeedingElapsed": 0,         // int, Время работы подброса (секунды), 0 если не активен
  "coalFeedingRemaining": 0        // int, Оставшееся время подброса (секунды), 0 если не активен
}
```

**Производные признаки:**
- `coalFeedingProgress`: `coalFeedingElapsed / (coalFeedingElapsed + coalFeedingRemaining)` (прогресс подброса, 0-1)
- `isCoalFeeding`: `coalFeedingActive` (boolean)

### 5. Временные метки и временные признаки (Time Features)

```json
{
  "timestamp": 1704312000,   // int, Unix timestamp (секунды)
  "uptime": 86400,           // int, Время работы ESP32 (секунды)
  "time": "12:00:00",        // string, Время в формате HH:MM:SS (если NTP включен)
  "date": "03.01.2026"       // string, Дата в формате DD.MM.YYYY (если NTP включен)
}
```

**Производные временные признаки (из timestamp):**
- `hour`: час дня (0-23)
- `minute`: минута (0-59)
- `dayOfWeek`: день недели (0=понедельник, 6=воскресенье)
- `dayOfMonth`: день месяца (1-31)
- `month`: месяц (1-12)
- `isWeekend`: boolean (суббота или воскресенье)
- `isNight`: boolean (22:00 - 06:00)
- `isDay`: boolean (06:00 - 22:00)
- `isMorning`: boolean (06:00 - 12:00)
- `isAfternoon`: boolean (12:00 - 18:00)
- `isEvening`: boolean (18:00 - 22:00)

**Временные признаки из истории (rolling features):**
- `supplyTempTrend`: тренд температуры подачи за последние N минут (1=рост, 0=стабильно, -1=падение)
- `returnTempTrend`: тренд температуры обратки
- `boilerTempTrend`: тренд температуры котла
- `outdoorTempTrend`: тренд температуры улицы
- `homeTempTrend`: тренд температуры дома (если доступен)

### 6. WiFi и сеть (Network Features)

```json
{
  "wifiRSSI": -65,           // int, Уровень сигнала WiFi (dBm), диапазон: -100 до 0
  "wifiSSID": "MyWiFi",      // string, Имя WiFi сети
  "wifiIP": "192.168.1.37",  // string, IP адрес ESP32
  "mqttConnected": true      // boolean, Статус подключения MQTT
}
```

**Производные признаки:**
- `wifiSignalQuality`: нормализованное значение RSSI (0-1, где 1 = отличный сигнал)
- `isNetworkStable`: `wifiRSSI > -70 && mqttConnected` (boolean)

### 7. Системные параметры (System Parameters)

```json
{
  "freeMem": 274548,         // int, Свободная память (байты)
  "heapSize": 327680,        // int, Размер heap (байты)
  "sensorCount": 4,          // int, Количество найденных датчиков DS18B20
  "sensorCountBus1": 2,      // int, Количество датчиков на шине 1
  "sensorCountBus2": 2       // int, Количество датчиков на шине 2
}
```

**Производные признаки:**
- `memoryUsage`: `1 - (freeMem / heapSize)` (использование памяти, 0-1)
- `isLowMemory`: `memoryUsage > 0.8` (boolean, низкая память)

### 8. ML настройки (ML Settings)

```json
{
  "mlPublishInterval": 10   // int, Интервал публикации ML данных (секунды)
}
```

## Целевые переменные (Target Variables)

### Основные цели (Primary Targets)

```json
{
  "fan_next": true,          // boolean, Должен ли быть включен вентилятор в следующий момент
  "pump_next": true          // boolean, Должен ли быть включен насос в следующий момент
}
```

### Альтернативные цели (Alternative Targets)

```json
{
  "fan_action": "ON",        // string, Действие для вентилятора: "ON" | "OFF" | "KEEP"
  "pump_action": "ON",       // string, Действие для насоса: "ON" | "OFF" | "KEEP"
  "supplyTemp_predicted": 66.2  // float, Предсказанная температура подачи через N минут
}
```

## Временные окна (Time Windows)

### Для предсказания температуры:
- **Краткосрочное**: 5-10 минут вперед
- **Среднесрочное**: 15-30 минут вперед
- **Долгосрочное**: 1-2 часа вперед

### Для истории (lookback):
- **Краткосрочная история**: последние 5-10 минут (30-60 записей при интервале 10 сек)
- **Среднесрочная история**: последние 30-60 минут (180-360 записей)
- **Долгосрочная история**: последние 2-4 часа (720-1440 записей)

## Рекомендуемые модели

### 1. Для предсказания температуры:
- **LSTM (Long Short-Term Memory)** - для временных рядов
- **GRU (Gated Recurrent Unit)** - более легкая альтернатива LSTM
- **Transformer** - для сложных зависимостей

### 2. Для классификации действий:
- **Random Forest** - для интерпретируемости
- **XGBoost / LightGBM** - для производительности
- **Neural Network** - для сложных паттернов

### 3. Гибридная модель:
- **LSTM + Classifier** - предсказание температуры + классификация действий
- **Ensemble** - комбинация нескольких моделей

## Особенности обработки данных

### Обработка пропусков (Missing Values):
- `homeTemp`: если `homeTempStatus == "offline"`, использовать среднее значение за последние N записей или удалить признак
- `outdoorTemp`: если отсутствует, использовать среднее за день или сезонное значение

### Нормализация:
- **Min-Max Scaling**: для температур (0-1)
- **Standard Scaling**: для временных признаков
- **One-Hot Encoding**: для категориальных признаков (state, dayOfWeek)

### Обработка выбросов:
- Температуры вне диапазона: удалить или заменить на граничные значения
- Аномальные скачки: использовать скользящее среднее

## Метрики оценки

### Для предсказания температуры:
- **MAE (Mean Absolute Error)**: средняя абсолютная ошибка
- **RMSE (Root Mean Squared Error)**: корень из средней квадратичной ошибки
- **MAPE (Mean Absolute Percentage Error)**: средняя абсолютная процентная ошибка

### Для классификации действий:
- **Accuracy**: точность классификации
- **Precision**: точность для каждого класса
- **Recall**: полнота для каждого класса
- **F1-Score**: гармоническое среднее precision и recall
- **Confusion Matrix**: матрица ошибок

## Дополнительные рекомендации

1. **Feature Engineering**: Создайте производные признаки из базовых (тренды, разницы, соотношения)
2. **Временные признаки**: Используйте циклическое кодирование для времени (sin/cos)
3. **Валидация**: Используйте временную валидацию (train на старых данных, test на новых)
4. **Балансировка**: Учитывайте дисбаланс классов (вентилятор/насос чаще включены или выключены)
5. **Интерпретируемость**: Используйте SHAP values для понимания важности признаков

## Пример структуры данных для обучения

```json
{
  "timestamp": 1704312000,
  "supplyTemp": 65.5,
  "returnTemp": 45.2,
  "boilerTemp": 70.0,
  "outdoorTemp": -5.0,
  "homeTemp": 22.5,
  "homeTempStatus": "online",
  "tempDiff": 20.3,
  "fan": true,
  "pump": true,
  "systemEnabled": true,
  "state": "WORKING",
  "setpoint": 60.0,
  "minTemp": 40.0,
  "maxTemp": 80.0,
  "hysteresis": 2.0,
  "inertiaTemp": 5.0,
  "inertiaTime": 300,
  "overheatTemp": 82.0,
  "heatingTimeout": 1800,
  "coalFeedingActive": false,
  "coalFeedingElapsed": 0,
  "coalFeedingRemaining": 0,
  "uptime": 86400,
  "time": "12:00:00",
  "date": "03.01.2026",
  "wifiRSSI": -65,
  "wifiSSID": "MyWiFi",
  "wifiIP": "192.168.1.37",
  "freeMem": 274548,
  "heapSize": 327680,
  "mlPublishInterval": 10,
  "sensorCount": 4,
  "sensorCountBus1": 2,
  "sensorCountBus2": 2,
  "mqttConnected": true,
  "supplyTrend": 1,
  "returnTrend": 1,
  "boilerTrend": 0,
  "outdoorTrend": -1,
  "homeTrend": 0
}
```

## Следующие шаги

1. Собрать достаточное количество данных (минимум 20,000 записей)
2. Провести EDA (Exploratory Data Analysis)
3. Создать pipeline для предобработки данных
4. Обучить базовую модель
5. Оценить результаты и итеративно улучшать
