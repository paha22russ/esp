# Следующие шаги: от сбора данных к ML управлению

## Текущий статус ✅

- ✅ Приложение для сбора данных работает
- ✅ Данные сохраняются в `data/ml_data_*.jsonl`
- ✅ Визуализация в реальном времени

## Этап 1: Сбор данных (ТЕКУЩИЙ)

### Что делать сейчас:

1. **Оставьте приложение работать** как можно дольше
   - Идеально: 1-2 недели непрерывной работы
   - Минимум: несколько дней с разными условиями

2. **Собирайте данные в разных условиях:**
   - Разные температуры на улице
   - Разные уставки температуры
   - Разные режимы работы (день/ночь)
   - Экстремальные ситуации (перегрев, охлаждение)

3. **Проверяйте качество данных:**
   - Периодически открывайте файлы `.jsonl`
   - Убедитесь, что данные записываются
   - Проверьте на наличие аномалий

## Этап 2: Анализ данных (СЛЕДУЮЩИЙ)

### Создать скрипт для анализа:

```python
# src/analysis/data_explorer.py
- Загрузить все .jsonl файлы из data/
- Показать статистику по всем параметрам
- Построить графики зависимостей
- Найти корреляции
- Обнаружить аномалии
```

### Вопросы для анализа:

1. Какие температуры типичны для вашего котла?
2. Как быстро меняется температура?
3. Какие комбинации параметров приводят к перегреву?
4. Как влияет температура на улице на работу котла?
5. Какие паттерны в работе вентилятора и насоса?

## Этап 3: Разработка ML модели

### Варианты моделей:

#### Вариант 1: Предсказание температуры (РЕКОМЕНДУЕТСЯ)
**Цель:** Предсказать температуру подачи через 10-30 минут

**Преимущества:**
- Проще всего реализовать
- Легко проверить точность
- Можно использовать для проактивного управления

**Пример использования:**
```
Текущая температура: 60°C
Предсказанная через 10 мин: 65°C
Уставка: 62°C
→ Решение: выключить вентилятор заранее
```

#### Вариант 2: Рекомендация действий
**Цель:** Рекомендовать, что делать (включить/выключить вентилятор)

**Преимущества:**
- Прямое управление
- Учитывает все факторы

**Сложности:**
- Нужно разметить данные (какие действия были правильными)
- Сложнее валидировать

#### Вариант 3: Гибридный подход (ЛУЧШИЙ)
**Комбинация:**
1. ML предсказывает температуру
2. Правила принимают решение на основе предсказания
3. ML корректирует правила

## Этап 4: Обучение модели

### Простой пример (для начала):

```python
# src/models/simple_predictor.py
import pandas as pd
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import train_test_split

# Загрузка данных
data = []
for file in Path('data').glob('ml_data_*.jsonl'):
    with open(file) as f:
        for line in f:
            data.append(json.loads(line))

df = pd.DataFrame(data)

# Подготовка признаков
features = ['supplyTemp', 'returnTemp', 'boilerTemp', 
            'outdoorTemp', 'fan', 'pump', 'setpoint']
X = df[features]
y = df['supplyTemp'].shift(-6)  # Температура через 1 минуту (6 записей * 10 сек)

# Обучение
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2)
model = RandomForestRegressor()
model.fit(X_train, y_train)

# Сохранение
import joblib
joblib.dump(model, 'models/temperature_predictor.pkl')
```

### Продвинутый пример (LSTM):

```python
# src/models/lstm_predictor.py
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense

# Подготовка последовательностей
# X: окна по 10 записей (100 секунд истории)
# y: температура через 10 минут

model = Sequential([
    LSTM(50, return_sequences=True, input_shape=(10, 7)),
    LSTM(50),
    Dense(1)
])
model.compile(optimizer='adam', loss='mse')
model.fit(X_train, y_train, epochs=50)
model.save('models/temperature_lstm.h5')
```

## Этап 5: Интеграция в ESP32

### Архитектура:

```
┌─────────────┐
│   ESP32     │───MQTT───┐
│   (Котел)   │          │
└─────────────┘          │
                         ▼
                  ┌──────────────┐
                  │ MQTT Брокер │
                  └──────────────┘
                         │
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ GUI Приложение│  │ ML Сервис    │  │ ESP32        │
│ (Сбор данных)│  │ (Управление) │  │ (Получает    │
│              │  │              │  │  команды)     │
└──────────────┘  └──────────────┘  └──────────────┘
```

### ML Сервис (новый компонент):

```python
# src/ml_service.py
import paho.mqtt.client as mqtt
import joblib
import json

# Загрузка модели
model = joblib.load('models/temperature_predictor.pkl')

def on_message(client, userdata, msg):
    data = json.loads(msg.payload)
    
    # Предсказание
    features = prepare_features(data)
    prediction = model.predict([features])[0]
    
    # Принятие решения
    if prediction > data['setpoint'] + 2:
        command = {'fan': False, 'pump': True}
    elif prediction < data['setpoint'] - 2:
        command = {'fan': True, 'pump': True}
    else:
        command = {'fan': data['fan'], 'pump': data['pump']}
    
    # Отправка команды
    client.publish('kotel/device1/ml/command', json.dumps(command))

# Подключение
client = mqtt.Client()
client.on_message = on_message
client.connect('m5.wqtt.ru', 5374)
client.subscribe('kotel/device1/ml/data')
client.loop_forever()
```

### Модификация ESP32:

Добавить обработку команд из топика `kotel/device1/ml/command`:

```cpp
// В коде ESP32
void onMlCommand(char* topic, byte* payload, unsigned int length) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    if (doc.containsKey("fan")) {
        setFan(doc["fan"]);
    }
    if (doc.containsKey("pump")) {
        setPump(doc["pump"]);
    }
}

// Подписка
mqttClient.subscribe("kotel/device1/ml/command");
mqttClient.setCallback(onMlCommand);
```

## План действий (пошагово)

### Неделя 1-2: Сбор данных
- [ ] Оставить приложение работать
- [ ] Собрать минимум 10,000 записей
- [ ] Проверить качество данных

### Неделя 3: Анализ
- [ ] Создать скрипт анализа данных
- [ ] Изучить паттерны в данных
- [ ] Определить целевую переменную для ML

### Неделя 4: Простая модель
- [ ] Создать простую модель (Random Forest)
- [ ] Обучить на собранных данных
- [ ] Оценить точность

### Неделя 5-6: Продвинутая модель
- [ ] Реализовать LSTM модель
- [ ] Сравнить с простой моделью
- [ ] Выбрать лучшую

### Неделя 7: Интеграция
- [ ] Создать ML сервис
- [ ] Протестировать на реальных данных
- [ ] Добавить в ESP32 поддержку ML команд

### Неделя 8+: Оптимизация
- [ ] Улучшить модель на основе обратной связи
- [ ] Добавить мониторинг работы ML
- [ ] Настроить параметры

## Полезные ресурсы

- **Документация scikit-learn:** https://scikit-learn.org/
- **Документация TensorFlow:** https://www.tensorflow.org/
- **Курсы по временным рядам:** 
  - Time Series Forecasting with Python
  - LSTM для временных рядов

## Вопросы для обсуждения

1. Какую задачу решать в первую очередь? (предсказание температуры или управление)
2. Какой объем данных достаточен для начала?
3. Нужна ли онлайн-обучение модели (обучение на новых данных)?
4. Как обрабатывать экстремальные ситуации?

