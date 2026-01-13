"""
Класс для сохранения данных в файл
"""

import json
import logging
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional

logger = logging.getLogger(__name__)


class DataSaver:
    """Класс для сохранения данных в файл"""
    
    def __init__(self, data_dir: str = "data"):
        self.data_dir = Path(data_dir)
        self.data_dir.mkdir(exist_ok=True)
        self.file = None
        self.count = 0
        self.filename: Optional[str] = None
        
    def start_new_file(self) -> str:
        """Создание нового файла для сохранения данных"""
        if self.file:
            self.close()
            
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.filename = str(self.data_dir / f"ml_data_{timestamp}.jsonl")
        
        try:
            self.file = open(self.filename, 'w', encoding='utf-8')
            self.count = 0
            logger.info(f"Создан файл: {self.filename}")
            return self.filename
        except Exception as e:
            logger.error(f"Ошибка создания файла: {e}")
            raise
    
    def save(self, data: Dict) -> bool:
        """Сохранение данных в файл"""
        if not self.file:
            self.start_new_file()
            
        try:
            json_line = json.dumps(data, ensure_ascii=False)
            self.file.write(json_line + '\n')
            self.file.flush()
            self.count += 1
            return True
        except Exception as e:
            logger.error(f"Ошибка сохранения данных: {e}")
            return False
    
    def close(self):
        """Закрытие файла"""
        if self.file:
            try:
                self.file.close()
                logger.info(f"Сохранено записей: {self.count} в файл {self.filename}")
            except Exception as e:
                logger.error(f"Ошибка закрытия файла: {e}")
            finally:
                self.file = None
    
    def get_count(self) -> int:
        """Получить количество сохраненных записей"""
        return self.count
    
    def get_filename(self) -> Optional[str]:
        """Получить имя текущего файла"""
        return self.filename

