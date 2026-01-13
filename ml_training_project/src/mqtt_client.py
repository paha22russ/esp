"""
Класс для работы с MQTT брокером
"""

import json
import logging
import threading
import time
from typing import Callable, Optional
import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class MQTTClient:
    """Класс для работы с MQTT брокером с автоматическим переподключением"""
    
    def __init__(self, broker: str, port: int, username: str, password: str):
        self.broker = broker
        self.port = port
        self.username = username
        self.password = password
        self.client: Optional[mqtt.Client] = None
        self.connected = False
        self.connecting = False
        self.on_message_callback: Optional[Callable] = None
        self.on_connect_callback: Optional[Callable] = None
        self.on_disconnect_callback: Optional[Callable] = None
        self.reconnect_delay = 5
        self.reconnect_thread: Optional[threading.Thread] = None
        self.stop_reconnect = False
        self.pending_topic: Optional[str] = None
        
    def connect(self) -> bool:
        """Подключение к MQTT брокеру"""
        if self.connecting:
            return False
            
        self.connecting = True
        self.stop_reconnect = False
        
        try:
            self.client = mqtt.Client()
            self.client.username_pw_set(self.username, self.password)
            self.client.on_connect = self._on_connect
            self.client.on_message = self._on_message
            self.client.on_disconnect = self._on_disconnect
            
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            return True
        except Exception as e:
            logger.error(f"Ошибка подключения: {e}")
            self.connecting = False
            self._start_reconnect_thread()
            return False
    
    def disconnect(self):
        """Отключение от MQTT брокера"""
        self.stop_reconnect = True
        if self.client:
            try:
                self.client.loop_stop()
                self.client.disconnect()
            except:
                pass
        self.connected = False
        self.connecting = False
    
    def subscribe(self, topic: str):
        """Подписка на топик"""
        if self.client and self.connected:
            result = self.client.subscribe(topic, qos=0)
            if result[0] == mqtt.MQTT_ERR_SUCCESS:
                logger.info(f"Подписан на топик: {topic} (QoS: 0)")
            else:
                logger.error(f"Ошибка подписки на топик: {topic}, код: {result[0]}")
        else:
            logger.warning("Клиент не подключен, подписка будет выполнена после подключения")
            # Сохраняем топик для подписки после подключения
            self.pending_topic = topic
    
    def _on_connect(self, client, userdata, flags, rc):
        """Обработчик подключения"""
        self.connecting = False
        if rc == 0:
            self.connected = True
            self.stop_reconnect = True
            logger.info(f"Подключено к MQTT брокеру {self.broker}:{self.port}")
            
            # Подписываемся на топик, если он был указан до подключения
            if self.pending_topic:
                self.subscribe(self.pending_topic)
                self.pending_topic = None
            
            if self.on_connect_callback:
                self.on_connect_callback(True)
        else:
            self.connected = False
            error_messages = {
                1: "Неправильная версия протокола",
                2: "Неправильный идентификатор клиента",
                3: "Сервер недоступен",
                4: "Неправильное имя пользователя или пароль",
                5: "Не авторизован"
            }
            error_msg = error_messages.get(rc, f"Неизвестная ошибка: {rc}")
            logger.error(f"Ошибка подключения: {error_msg} (код: {rc})")
            if self.on_connect_callback:
                self.on_connect_callback(False)
            self._start_reconnect_thread()
    
    def _on_message(self, client, userdata, msg):
        """Обработчик получения сообщения"""
        try:
            logger.debug(f"Получено сообщение из топика: {msg.topic}, размер: {len(msg.payload)} байт")
            data = json.loads(msg.payload.decode('utf-8'))
            logger.debug(f"Данные распарсены успешно, ключи: {list(data.keys())[:5]}...")
            if self.on_message_callback:
                self.on_message_callback(data)
            else:
                logger.warning("Callback для сообщений не установлен")
        except json.JSONDecodeError as e:
            logger.error(f"Ошибка декодирования JSON: {e}, payload: {msg.payload[:100]}")
        except Exception as e:
            logger.error(f"Ошибка обработки сообщения: {e}", exc_info=True)
    
    def _on_disconnect(self, client, userdata, rc):
        """Обработчик отключения"""
        was_connected = self.connected
        self.connected = False
        self.connecting = False
        
        if rc != 0:
            logger.warning(f"Неожиданное отключение (код: {rc})")
            if was_connected:
                self._start_reconnect_thread()
        else:
            logger.info("Отключено от MQTT брокера")
        
        if self.on_disconnect_callback:
            self.on_disconnect_callback(rc)
    
    def _start_reconnect_thread(self):
        """Запуск потока для автоматического переподключения"""
        if self.reconnect_thread and self.reconnect_thread.is_alive():
            return
        
        self.reconnect_thread = threading.Thread(target=self._reconnect_loop, daemon=True)
        self.reconnect_thread.start()
    
    def _reconnect_loop(self):
        """Цикл автоматического переподключения"""
        while not self.stop_reconnect and not self.connected:
            time.sleep(self.reconnect_delay)
            if not self.stop_reconnect and not self.connected:
                logger.info("Попытка переподключения...")
                try:
                    if self.client:
                        self.client.loop_stop()
                    self.connect()
                except Exception as e:
                    logger.error(f"Ошибка переподключения: {e}")
    
    def set_message_callback(self, callback: Callable):
        """Установка callback для обработки сообщений"""
        self.on_message_callback = callback
    
    def set_connect_callback(self, callback: Callable):
        """Установка callback для обработки подключения"""
        self.on_connect_callback = callback
    
    def set_disconnect_callback(self, callback: Callable):
        """Установка callback для обработки отключения"""
        self.on_disconnect_callback = callback

