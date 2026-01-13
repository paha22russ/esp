"""
Главное окно приложения для обучения ML модели управления котлом
"""

import customtkinter as ctk
import logging
import threading
from datetime import datetime
from typing import Optional, Dict
from pathlib import Path

from src.mqtt_client import MQTTClient
from src.data_saver import DataSaver
from src.data_processor import DataProcessor
from src.widgets import TemperaturePanel, StatusPanel, GraphPanel, StatsPanel

# Настройка логирования
logging.basicConfig(
    level=logging.DEBUG,  # Изменено на DEBUG для диагностики
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler('app.log', encoding='utf-8')
    ]
)
logger = logging.getLogger(__name__)

# Настройка темы CustomTkinter
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


class MainWindow(ctk.CTk):
    """Главное окно приложения"""
    
    def __init__(self):
        super().__init__()
        
        self.title("ML Training - Управление котлом")
        self.geometry("1400x900")
        self.minsize(1200, 700)
        
        # Переменные состояния
        self.mqtt_client: Optional[MQTTClient] = None
        self.data_saver: Optional[DataSaver] = None
        self.data_processor = DataProcessor()
        self.messages_count = 0
        self.last_message_time: Optional[datetime] = None
        self.current_data: Optional[Dict] = None
        
        # Настройки MQTT по умолчанию
        self.mqtt_broker = "m5.wqtt.ru"
        self.mqtt_port = 5374
        self.mqtt_username = "u_OLTTB0"
        self.mqtt_password = "XDzCIXr0"
        self.mqtt_topic = "kotel/device1/ml/data"
        
        # Создание интерфейса
        self._create_menu()
        self._create_main_interface()
        
        # Запуск обновления статистики
        self._update_stats_periodically()
    
    def _create_menu(self):
        """Создание меню"""
        # Меню подключения
        self.menu_frame = ctk.CTkFrame(self)
        self.menu_frame.pack(fill='x', padx=10, pady=5)
        
        # Кнопка подключения
        self.connect_button = ctk.CTkButton(
            self.menu_frame,
            text="Подключиться",
            command=self._toggle_connection,
            width=150,
            height=35
        )
        self.connect_button.pack(side='left', padx=5)
        
        # Индикатор подключения
        self.connection_status = ctk.CTkLabel(
            self.menu_frame,
            text="● Отключено",
            font=ctk.CTkFont(size=12),
            text_color="#808080"
        )
        self.connection_status.pack(side='left', padx=10)
        
        # Кнопка настроек
        self.settings_button = ctk.CTkButton(
            self.menu_frame,
            text="Настройки",
            command=self._show_settings,
            width=120,
            height=35
        )
        self.settings_button.pack(side='left', padx=5)
        
        # Кнопка сохранения данных
        self.save_button = ctk.CTkButton(
            self.menu_frame,
            text="Начать сохранение",
            command=self._toggle_saving,
            width=150,
            height=35
        )
        self.save_button.pack(side='left', padx=5)
        
        self.saving_status = ctk.CTkLabel(
            self.menu_frame,
            text="Сохранение: выкл",
            font=ctk.CTkFont(size=12),
            text_color="#808080"
        )
        self.saving_status.pack(side='left', padx=10)
    
    def _create_main_interface(self):
        """Создание основного интерфейса"""
        # Создание вкладок
        self.tabview = ctk.CTkTabview(self)
        self.tabview.pack(fill='both', expand=True, padx=10, pady=5)
        
        # Вкладка "Мониторинг"
        self.monitor_tab = self.tabview.add("Мониторинг")
        self.monitor_tab.grid_columnconfigure(0, weight=1)
        self.monitor_tab.grid_rowconfigure(0, weight=1)
        
        # Панель температур
        self.temperature_panel = TemperaturePanel(self.monitor_tab)
        self.temperature_panel.pack(fill='x', padx=10, pady=5)
        
        # Панель состояний
        self.status_panel = StatusPanel(self.monitor_tab)
        self.status_panel.pack(fill='x', padx=10, pady=5)
        
        # Вкладка "Графики"
        self.graph_tab = self.tabview.add("Графики")
        self.graph_tab.grid_columnconfigure(0, weight=1)
        self.graph_tab.grid_rowconfigure(0, weight=1)
        
        self.graph_panel = GraphPanel(self.graph_tab)
        self.graph_panel.pack(fill='both', expand=True, padx=10, pady=10)
        self.graph_panel.start_animation()
        
        # Вкладка "Статистика"
        self.stats_tab = self.tabview.add("Статистика")
        self.stats_tab.grid_columnconfigure(0, weight=1)
        self.stats_tab.grid_rowconfigure(0, weight=1)
        
        self.stats_panel = StatsPanel(self.stats_tab)
        self.stats_panel.pack(fill='both', expand=True, padx=10, pady=10)
    
    def _toggle_connection(self):
        """Переключение состояния подключения"""
        if self.mqtt_client and self.mqtt_client.connected:
            self._disconnect()
        else:
            self._connect()
    
    def _connect(self):
        """Подключение к MQTT брокеру"""
        if self.mqtt_client and self.mqtt_client.connecting:
            return
        
        self.connect_button.configure(text="Подключение...", state="disabled")
        self.connection_status.configure(text="● Подключение...", text_color="#FFD700")
        
        # Создание MQTT клиента
        self.mqtt_client = MQTTClient(
            self.mqtt_broker,
            self.mqtt_port,
            self.mqtt_username,
            self.mqtt_password
        )
        
        # Установка callback'ов
        self.mqtt_client.set_message_callback(self._on_message_received)
        self.mqtt_client.set_connect_callback(self._on_connect_status)
        self.mqtt_client.set_disconnect_callback(self._on_disconnect_status)
        
        # Подключение в отдельном потоке
        def connect_thread():
            logger.info(f"Попытка подключения к {self.mqtt_broker}:{self.mqtt_port}")
            logger.info(f"Топик для подписки: {self.mqtt_topic}")
            success = self.mqtt_client.connect()
            if success:
                # Подписка будет выполнена автоматически в _on_connect
                self.mqtt_client.subscribe(self.mqtt_topic)
            else:
                logger.error("Не удалось инициировать подключение")
        
        threading.Thread(target=connect_thread, daemon=True).start()
    
    def _disconnect(self):
        """Отключение от MQTT брокера"""
        if self.mqtt_client:
            self.mqtt_client.disconnect()
            self.mqtt_client = None
        
        self.connect_button.configure(text="Подключиться", state="normal")
        self.connection_status.configure(text="● Отключено", text_color="#808080")
    
    def _on_connect_status(self, connected: bool):
        """Обработчик изменения статуса подключения"""
        if connected:
            self.connect_button.configure(text="Отключиться", state="normal")
            self.connection_status.configure(text="● Подключено", text_color="#50C878")
        else:
            self.connect_button.configure(text="Подключиться", state="normal")
            self.connection_status.configure(text="● Ошибка подключения", text_color="#FF4444")
    
    def _on_disconnect_status(self, rc: int):
        """Обработчик отключения"""
        self.connect_button.configure(text="Подключиться", state="normal")
        if rc != 0:
            self.connection_status.configure(text="● Переподключение...", text_color="#FFD700")
        else:
            self.connection_status.configure(text="● Отключено", text_color="#808080")
    
    def _on_message_received(self, data: Dict):
        """Обработчик получения сообщения от MQTT"""
        logger.info(f"Получено сообщение #{self.messages_count + 1}")
        self.messages_count += 1
        self.last_message_time = datetime.now()
        self.current_data = data
        
        # Обработка данных
        processed = self.data_processor.parse_message(data)
        if not processed:
            logger.warning("Не удалось обработать данные")
            return
        
        logger.debug(f"Данные обработаны: температура подачи = {processed.get('supplyTemp', 'N/A')}°C")
        
        # Обновление интерфейса в главном потоке
        self.after(0, self._update_ui, processed)
        
        # Сохранение данных
        if self.data_saver and self.data_saver.file:
            self.data_saver.save(data)
    
    def _update_ui(self, data: Dict):
        """Обновление интерфейса"""
        # Обновление панелей
        self.temperature_panel.update_data(data)
        self.status_panel.update_data(data)
        self.stats_panel.update_data(data)
        
        # Добавление точки на график
        self.graph_panel.add_data_point(data)
        
        # Обновление статистики приложения
        saved_count = self.data_saver.get_count() if self.data_saver else 0
        self.stats_panel.update_app_stats(
            self.messages_count,
            self.last_message_time,
            saved_count
        )
    
    def _toggle_saving(self):
        """Переключение состояния сохранения данных"""
        if self.data_saver and self.data_saver.file:
            self._stop_saving()
        else:
            self._start_saving()
    
    def _start_saving(self):
        """Начало сохранения данных"""
        if not self.data_saver:
            self.data_saver = DataSaver()
        
        try:
            filename = self.data_saver.start_new_file()
            self.save_button.configure(text="Остановить сохранение")
            self.saving_status.configure(
                text=f"Сохранение: вкл ({Path(filename).name})",
                text_color="#50C878"
            )
            logger.info(f"Начато сохранение в файл: {filename}")
        except Exception as e:
            logger.error(f"Ошибка начала сохранения: {e}")
            self.saving_status.configure(
                text="Сохранение: ошибка",
                text_color="#FF4444"
            )
    
    def _stop_saving(self):
        """Остановка сохранения данных"""
        if self.data_saver:
            self.data_saver.close()
        
        self.save_button.configure(text="Начать сохранение")
        self.saving_status.configure(
            text="Сохранение: выкл",
            text_color="#808080"
            )
        logger.info("Сохранение остановлено")
    
    def _show_settings(self):
        """Показ окна настроек"""
        settings_window = SettingsWindow(self)
        settings_window.grab_set()
    
    def _update_stats_periodically(self):
        """Периодическое обновление статистики"""
        if self.stats_panel and self.data_saver:
            saved_count = self.data_saver.get_count() if self.data_saver else 0
            self.stats_panel.update_app_stats(
                self.messages_count,
                self.last_message_time,
                saved_count
            )
        
        # Обновление каждые 5 секунд
        self.after(5000, self._update_stats_periodically)
    
    def on_closing(self):
        """Обработчик закрытия окна"""
        if self.mqtt_client:
            self._disconnect()
        if self.data_saver:
            self._stop_saving()
        self.destroy()


class SettingsWindow(ctk.CTkToplevel):
    """Окно настроек"""
    
    def __init__(self, parent):
        super().__init__(parent)
        self.parent = parent
        
        self.title("Настройки MQTT")
        self.geometry("500x400")
        self.resizable(False, False)
        
        # Переменные
        self.broker_var = ctk.StringVar(value=parent.mqtt_broker)
        self.port_var = ctk.StringVar(value=str(parent.mqtt_port))
        self.username_var = ctk.StringVar(value=parent.mqtt_username)
        self.password_var = ctk.StringVar(value=parent.mqtt_password)
        self.topic_var = ctk.StringVar(value=parent.mqtt_topic)
        
        self._create_widgets()
    
    def _create_widgets(self):
        """Создание виджетов настроек"""
        # Брокер
        ctk.CTkLabel(self, text="MQTT Брокер:", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(20, 5))
        broker_entry = ctk.CTkEntry(self, textvariable=self.broker_var, width=400)
        broker_entry.pack(pady=5)
        
        # Порт
        ctk.CTkLabel(self, text="Порт:", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(10, 5))
        port_entry = ctk.CTkEntry(self, textvariable=self.port_var, width=400)
        port_entry.pack(pady=5)
        
        # Пользователь
        ctk.CTkLabel(self, text="Пользователь:", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(10, 5))
        username_entry = ctk.CTkEntry(self, textvariable=self.username_var, width=400)
        username_entry.pack(pady=5)
        
        # Пароль
        ctk.CTkLabel(self, text="Пароль:", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(10, 5))
        password_entry = ctk.CTkEntry(self, textvariable=self.password_var, width=400, show="*")
        password_entry.pack(pady=5)
        
        # Топик
        ctk.CTkLabel(self, text="Топик:", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(10, 5))
        topic_entry = ctk.CTkEntry(self, textvariable=self.topic_var, width=400)
        topic_entry.pack(pady=5)
        
        # Кнопки
        button_frame = ctk.CTkFrame(self)
        button_frame.pack(pady=20)
        
        save_button = ctk.CTkButton(
            button_frame,
            text="Сохранить",
            command=self._save_settings,
            width=120
        )
        save_button.pack(side='left', padx=10)
        
        cancel_button = ctk.CTkButton(
            button_frame,
            text="Отмена",
            command=self.destroy,
            width=120
        )
        cancel_button.pack(side='left', padx=10)
    
    def _save_settings(self):
        """Сохранение настроек"""
        try:
            self.parent.mqtt_broker = self.broker_var.get()
            self.parent.mqtt_port = int(self.port_var.get())
            self.parent.mqtt_username = self.username_var.get()
            self.parent.mqtt_password = self.password_var.get()
            self.parent.mqtt_topic = self.topic_var.get()
            
            logger.info("Настройки сохранены")
            self.destroy()
        except ValueError:
            logger.error("Ошибка: неверный формат порта")


def main():
    """Главная функция запуска приложения"""
    try:
        app = MainWindow()
        app.protocol("WM_DELETE_WINDOW", app.on_closing)
        logger.info("Приложение запущено, открывается окно...")
        app.mainloop()
    except Exception as e:
        logger.error(f"Ошибка при запуске приложения: {e}", exc_info=True)
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()

