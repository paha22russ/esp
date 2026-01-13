"""
Создание признаков для ML модели
"""

import logging
import pandas as pd
import numpy as np
from typing import Dict

logger = logging.getLogger(__name__)


class FeatureEngineer:
    """Класс для создания признаков из сырых данных"""
    
    @staticmethod
    def create_features(df: pd.DataFrame) -> pd.DataFrame:
        """Создание признаков для обучения"""
        df = df.copy()
        
        # Сортировка по времени
        if 'timestamp' in df.columns:
            df = df.sort_values('timestamp').reset_index(drop=True)
            df['datetime'] = pd.to_datetime(df['timestamp'], unit='s', errors='coerce')
        
        # Временные признаки
        if 'datetime' in df.columns:
            df['hour'] = df['datetime'].dt.hour
            df['day_of_week'] = df['datetime'].dt.dayofweek
            df['is_weekend'] = (df['day_of_week'] >= 5).astype(int)
        
        # Производные признаки температур
        if 'supplyTemp' in df.columns:
            df['supplyTemp_change'] = df['supplyTemp'].diff()
            df['supplyTemp_change_rate'] = df['supplyTemp'].diff() / df['timestamp'].diff().replace(0, np.nan)
        
        if 'returnTemp' in df.columns:
            df['returnTemp_change'] = df['returnTemp'].diff()
        
        if 'boilerTemp' in df.columns:
            df['boilerTemp_change'] = df['boilerTemp'].diff()
        
        # Разница температур
        if 'supplyTemp' in df.columns and 'returnTemp' in df.columns:
            df['temp_diff_supply_return'] = df['supplyTemp'] - df['returnTemp']
        
        if 'supplyTemp' in df.columns and 'outdoorTemp' in df.columns:
            df['temp_diff_supply_outdoor'] = df['supplyTemp'] - df['outdoorTemp']
        
        # Скользящие средние
        if 'supplyTemp' in df.columns:
            df['supplyTemp_ma_5'] = df['supplyTemp'].rolling(window=5, min_periods=1).mean()
            df['supplyTemp_ma_10'] = df['supplyTemp'].rolling(window=10, min_periods=1).mean()
        
        # Состояния устройств (преобразование в числовые)
        if 'fan' in df.columns:
            df['fan_int'] = df['fan'].astype(int)
        if 'pump' in df.columns:
            df['pump_int'] = df['pump'].astype(int)
        if 'systemEnabled' in df.columns:
            df['systemEnabled_int'] = df['systemEnabled'].astype(int)
        
        # Взаимодействия
        if 'fan_int' in df.columns and 'pump_int' in df.columns:
            df['fan_pump_interaction'] = df['fan_int'] * df['pump_int']
        
        # Заполнение NaN значений
        numeric_cols = df.select_dtypes(include=[np.number]).columns
        df[numeric_cols] = df[numeric_cols].ffill().fillna(0)
        
        logger.info(f"Создано признаков: {len(df.columns)}")
        return df
    
    @staticmethod
    def get_feature_list(df: pd.DataFrame, exclude_cols: list = None) -> list:
        """Получение списка признаков для обучения"""
        if exclude_cols is None:
            exclude_cols = ['timestamp', 'datetime', 'time', 'date', 'state', 
                          'wifiSSID', 'wifiIP']
        
        # Исключаем целевую переменную и служебные поля
        feature_cols = [col for col in df.columns 
                       if col not in exclude_cols and df[col].dtype in ['float64', 'int64', 'bool']]
        
        return feature_cols
