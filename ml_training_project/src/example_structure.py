"""
Пример структуры приложения для первого этапа проекта ML обучения.
Этот файл демонстрирует базовую архитектуру приложения.
"""

import json
import time
from datetime import datetime
from typing import Dict, Optional
import paho.mqtt.client as mqtt
from pathlib import Path


class MQTTClient:
    """Класс для работы с MQTT брокером"""
    
    def __init__(self, broker: str, port: int, username: str, password: str):
        self.broker = broker
        self.port = port
        self.username = username
        self.password = password
        self.client = None
        self.connected = False
        self.on_message_callback = None
        
    def connect(self):
        """Подключение к MQTT брокеру"""
        self.client = mqtt.Client()
        self.client.username_pw_set(self.username, self.password)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            return True
        except Exception as e:
            print(f"Ошибка подключения: {e}")
            return False
    
    def subscribe(self, topic: str):
        """Подписка на топик"""
        if self.client:
            self.client.subscribe(topic)
            print(f"Подписан на топик: {topic}")
    
    def _on_connect(self, client, userdata, flags, rc):
        """Обработчик подключения"""
        if rc == 0:
            self.connected = True
            print("Подключено к MQTT брокеру")
        else:
            print(f"Ошибка подключения: {rc}")
    
    def _on_message(self, client, userdata, msg):
        """Обработчик получения сообщения"""
        try:
            data = json.loads(msg.payload.decode())
            if self.on_message_callback:
                self.on_message_callback(data)
        except Exception as e:
            print(f"Ошибка обработки сообщения: {e}")
    
    def _on_disconnect(self, client, userdata, rc):
        """Обработчик отключения"""
        self.connected = False
        print("Отключено от MQTT брокера")
    
    def set_message_callback(self, callback):
        """Установка callback для обработки сообщений"""
        self.on_message_callback = callback


class DataSaver:
    """Класс для сохранения данных в файл"""
    
    def __init__(self, data_dir: str = "data"):
        self.data_dir = Path(data_dir)
        self.data_dir.mkdir(exist_ok=True)
        self.file = None
        self.count = 0
        
    def start_new_file(self):
        """Создание нового файла для сохранения данных"""
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = self.data_dir / f"ml_data_{timestamp}.jsonl"
        self.file = open(filename, 'w', encoding='utf-8')
        self.count = 0
        print(f"Создан файл: {filename}")
    
    def save(self, data: Dict):
        """Сохранение данных в файл"""
        if self.file:
            json_line = json.dumps(data, ensure_ascii=False)
            self.file.write(json_line + '\n')
            self.file.flush()
            self.count += 1
    
    def close(self):
        """Закрытие файла"""
        if self.file:
            self.file.close()
            print(f"Сохранено записей: {self.count}")


class DataProcessor:
    """Класс для обработки и парсинга данных"""
    
    @staticmethod
    def parse_message(data: Dict) -> Optional[Dict]:
        """Парсинг JSON сообщения"""
        try:
            return {
                'timestamp': data.get('timestamp', 0),
                'time': data.get('time', ''),
                'date': data.get('date', ''),
                'supplyTemp': data.get('supplyTemp', 0.0),
                'returnTemp': data.get('returnTemp', 0.0),
                'boilerTemp': data.get('boilerTemp', 0.0),
                'outdoorTemp': data.get('outdoorTemp', 0.0),
                'tempDiff': data.get('tempDiff', 0.0),
                'fan': data.get('fan', False),
                'pump': data.get('pump', False),
                'systemEnabled': data.get('systemEnabled', False),
                'state': data.get('state', ''),
                'setpoint': data.get('setpoint', 0.0),
                'wifiRSSI': data.get('wifiRSSI', 0),
                'wifiIP': data.get('wifiIP', ''),
                'uptime': data.get('uptime', 0),
                'freeMem': data.get('freeMem', 0),
            }
        except Exception as e:
            print(f"Ошибка парсинга: {e}")
            return None


# Пример использования
if __name__ == "__main__":
    # Настройки MQTT
    MQTT_BROKER = "m5.wqtt.ru"
    MQTT_PORT = 5374
    MQTT_USER = "u_OLTTB0"
    MQTT_PASSWORD = "XDzCIXr0"
    MQTT_TOPIC = "kotel/device1/ml/data"
    
    # Инициализация компонентов
    mqtt_client = MQTTClient(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD)
    data_saver = DataSaver()
    data_processor = DataProcessor()
    
    # Callback для обработки сообщений
    def on_data_received(data):
        """Обработка полученных данных"""
        processed = data_processor.parse_message(data)
        if processed:
            print(f"Получены данные: Температура подачи = {processed['supplyTemp']}°C")
            data_saver.save(data)  # Сохраняем оригинальные данные
    
    # Установка callback
    mqtt_client.set_message_callback(on_data_received)
    
    # Подключение и подписка
    if mqtt_client.connect():
        data_saver.start_new_file()
        mqtt_client.subscribe(MQTT_TOPIC)
        
        # Ожидание сообщений
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nОстановка...")
            mqtt_client.client.loop_stop()
            data_saver.close()

