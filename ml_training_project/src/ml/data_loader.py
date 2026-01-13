"""
Загрузка данных для обучения ML модели
"""

import json
import logging
from pathlib import Path
from typing import List, Dict, Optional
import pandas as pd

logger = logging.getLogger(__name__)


class DataLoader:
    """Класс для загрузки данных из JSON Lines файлов"""
    
    def __init__(self, data_dir: str = "data"):
        self.data_dir = Path(data_dir)
    
    def load_all_files(self) -> pd.DataFrame:
        """Загрузка всех JSON Lines файлов из директории"""
        jsonl_files = list(self.data_dir.glob("ml_data_*.jsonl"))
        
        if not jsonl_files:
            logger.warning(f"Не найдено файлов данных в {self.data_dir}")
            return pd.DataFrame()
        
        logger.info(f"Найдено {len(jsonl_files)} файлов данных")
        
        all_data = []
        for file_path in jsonl_files:
            try:
                data = self.load_file(file_path)
                if data:
                    all_data.extend(data)
                    logger.info(f"Загружено {len(data)} записей из {file_path.name}")
            except Exception as e:
                logger.error(f"Ошибка загрузки файла {file_path}: {e}")
        
        if not all_data:
            return pd.DataFrame()
        
        df = pd.DataFrame(all_data)
        logger.info(f"Всего загружено {len(df)} записей")
        return df
    
    def load_file(self, file_path: Path) -> List[Dict]:
        """Загрузка одного JSON Lines файла"""
        data = []
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        record = json.loads(line)
                        data.append(record)
                    except json.JSONDecodeError as e:
                        logger.warning(f"Ошибка парсинга строки {line_num} в {file_path.name}: {e}")
        except Exception as e:
            logger.error(f"Ошибка чтения файла {file_path}: {e}")
            raise
        
        return data
    
    def get_statistics(self, df: pd.DataFrame) -> Dict:
        """Получение статистики по данным"""
        if df.empty:
            return {}
        
        stats = {
            'total_records': len(df),
            'date_range': {
                'start': None,
                'end': None
            },
            'columns': list(df.columns),
            'numeric_stats': {}
        }
        
        # Статистика по временным меткам
        if 'timestamp' in df.columns:
            timestamps = pd.to_datetime(df['timestamp'], unit='s', errors='coerce')
            stats['date_range']['start'] = timestamps.min()
            stats['date_range']['end'] = timestamps.max()
        
        # Статистика по числовым полям
        numeric_cols = df.select_dtypes(include=['float64', 'int64']).columns
        for col in numeric_cols:
            stats['numeric_stats'][col] = {
                'mean': float(df[col].mean()),
                'std': float(df[col].std()),
                'min': float(df[col].min()),
                'max': float(df[col].max()),
                'null_count': int(df[col].isnull().sum())
            }
        
        return stats
