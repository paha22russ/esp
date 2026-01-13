"""
Класс для обработки и парсинга данных
"""

import logging
from typing import Dict, Optional

logger = logging.getLogger(__name__)


class DataProcessor:
    """Класс для обработки и парсинга данных"""
    
    @staticmethod
    def _safe_float(value, default=0.0) -> float:
        """Безопасное преобразование в float"""
        if value is None:
            return default
        try:
            return float(value)
        except (ValueError, TypeError):
            logger.warning(f"Не удалось преобразовать в float: {value}, используется значение по умолчанию {default}")
            return default
    
    @staticmethod
    def _safe_int(value, default=0) -> int:
        """Безопасное преобразование в int"""
        if value is None:
            return default
        try:
            return int(value)
        except (ValueError, TypeError):
            logger.warning(f"Не удалось преобразовать в int: {value}, используется значение по умолчанию {default}")
            return default
    
    @staticmethod
    def _safe_bool(value, default=False) -> bool:
        """Безопасное преобразование в bool"""
        if value is None:
            return default
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            return bool(value)
        if isinstance(value, str):
            return value.lower() in ('true', '1', 'yes', 'on')
        return default
    
    @staticmethod
    def parse_message(data: Dict) -> Optional[Dict]:
        """Парсинг JSON сообщения с полной структурой"""
        try:
            return {
                # Температуры
                'timestamp': DataProcessor._safe_int(data.get('timestamp'), 0),
                'time': str(data.get('time', '')),
                'date': str(data.get('date', '')),
                'supplyTemp': DataProcessor._safe_float(data.get('supplyTemp'), 0.0),
                'returnTemp': DataProcessor._safe_float(data.get('returnTemp'), 0.0),
                'boilerTemp': DataProcessor._safe_float(data.get('boilerTemp'), 0.0),
                'outdoorTemp': DataProcessor._safe_float(data.get('outdoorTemp'), 0.0),
                'homeTemp': DataProcessor._safe_float(data.get('homeTemp'), None) if data.get('homeTemp') is not None and data.get('homeTempStatus') == 'online' else None,
                'homeTempStatus': str(data.get('homeTempStatus', 'offline')),
                'tempDiff': DataProcessor._safe_float(data.get('tempDiff'), 0.0),
                
                # Состояния устройств
                'fan': DataProcessor._safe_bool(data.get('fan'), False),
                'pump': DataProcessor._safe_bool(data.get('pump'), False),
                'systemEnabled': DataProcessor._safe_bool(data.get('systemEnabled'), False),
                'state': str(data.get('state', '')),
                
                # Настройки Auto режима
                'setpoint': DataProcessor._safe_float(data.get('setpoint'), 0.0),
                'minTemp': DataProcessor._safe_float(data.get('minTemp'), 0.0),
                'maxTemp': DataProcessor._safe_float(data.get('maxTemp'), 0.0),
                'hysteresis': DataProcessor._safe_float(data.get('hysteresis'), 0.0),
                'inertiaTemp': DataProcessor._safe_float(data.get('inertiaTemp'), 0.0),
                'inertiaTime': DataProcessor._safe_int(data.get('inertiaTime'), 0),
                'overheatTemp': DataProcessor._safe_float(data.get('overheatTemp'), 0.0),
                'heatingTimeout': DataProcessor._safe_int(data.get('heatingTimeout'), 0),
                
                # Подброс угля
                'coalFeedingActive': DataProcessor._safe_bool(data.get('coalFeedingActive'), False),
                'coalFeedingElapsed': DataProcessor._safe_int(data.get('coalFeedingElapsed'), 0),
                
                # WiFi и сеть
                'wifiRSSI': DataProcessor._safe_int(data.get('wifiRSSI'), 0),
                'wifiSSID': str(data.get('wifiSSID', '')),
                'wifiIP': str(data.get('wifiIP', '')),
                
                # Системные параметры
                'uptime': DataProcessor._safe_int(data.get('uptime'), 0),
                'freeMem': DataProcessor._safe_int(data.get('freeMem'), 0),
                'heapSize': DataProcessor._safe_int(data.get('heapSize'), 0),
                
                # ML настройки
                'mlPublishInterval': DataProcessor._safe_int(data.get('mlPublishInterval'), 10),
                
                # Дополнительная информация
                'sensorCount': DataProcessor._safe_int(data.get('sensorCount'), 0),
                'mqttConnected': DataProcessor._safe_bool(data.get('mqttConnected'), False),
            }
        except Exception as e:
            logger.error(f"Ошибка парсинга: {e}", exc_info=True)
            return None
    
    @staticmethod
    def get_temperature_color(temp: float, min_temp: float = 40, max_temp: float = 80) -> str:
        """Получить цвет для температуры"""
        if temp < min_temp:
            return "#4A90E2"  # Синий (холодно)
        elif temp < min_temp + (max_temp - min_temp) * 0.4:
            return "#50C878"  # Зеленый (норма)
        elif temp < min_temp + (max_temp - min_temp) * 0.7:
            return "#FFD700"  # Желтый (тепло)
        else:
            return "#FF4444"  # Красный (горячо)
    
    @staticmethod
    def format_uptime(seconds: int) -> str:
        """Форматирование времени работы"""
        days = seconds // 86400
        hours = (seconds % 86400) // 3600
        minutes = (seconds % 3600) // 60
        secs = seconds % 60
        
        if days > 0:
            return f"{days}д {hours}ч {minutes}м"
        elif hours > 0:
            return f"{hours}ч {minutes}м {secs}с"
        elif minutes > 0:
            return f"{minutes}м {secs}с"
        else:
            return f"{secs}с"
    
    @staticmethod
    def format_memory(bytes_value: int) -> str:
        """Форматирование размера памяти"""
        if bytes_value >= 1024 * 1024:
            return f"{bytes_value / (1024 * 1024):.1f} МБ"
        elif bytes_value >= 1024:
            return f"{bytes_value / 1024:.1f} КБ"
        else:
            return f"{bytes_value} Б"
    
    @staticmethod
    def rssi_to_percent(rssi: int) -> int:
        """Преобразование RSSI в проценты"""
        # RSSI обычно от -100 до 0 dBm
        if rssi >= -50:
            return 100
        elif rssi <= -100:
            return 0
        else:
            return int(2 * (rssi + 100))
