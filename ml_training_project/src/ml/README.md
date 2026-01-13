# Модуль машинного обучения

Этот модуль содержит инструменты для обучения ML модели управления котлом.

## Структура

- `data_loader.py` - Загрузка данных из JSON Lines файлов
- `feature_engineering.py` - Создание признаков для обучения
- `model_trainer.py` - Обучение моделей (будущее)
- `model_predictor.py` - Использование обученной модели (будущее)

## Использование

### Загрузка данных

```python
from src.ml.data_loader import DataLoader

loader = DataLoader("data")
df = loader.load_all_files()
stats = loader.get_statistics(df)
```

### Создание признаков

```python
from src.ml.feature_engineering import FeatureEngineer

engineer = FeatureEngineer()
df_features = engineer.create_features(df)
features = engineer.get_feature_list(df_features)
```

## Следующие шаги

1. Создать `model_trainer.py` для обучения моделей
2. Создать `model_predictor.py` для предсказаний
3. Добавить валидацию и метрики
