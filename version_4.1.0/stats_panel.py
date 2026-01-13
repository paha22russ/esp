"""
Виджет для отображения статистики и системной информации
"""

import customtkinter as ctk
from typing import Dict, Optional
from datetime import datetime
import sys
from pathlib import Path

# Добавление пути к src для импорта
_project_root = Path(__file__).parent.parent.parent
if str(_project_root) not in sys.path:
    sys.path.insert(0, str(_project_root))

try:
    from src.data_processor import DataProcessor
except ImportError:
    # Альтернативный импорт
    import importlib.util
    _spec = importlib.util.spec_from_file_location(
        "data_processor",
        Path(__file__).parent.parent / "data_processor.py"
    )
    _module = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_module)
    DataProcessor = _module.DataProcessor


class StatsPanel(ctk.CTkFrame):
    """Панель статистики и системной информации"""
    
    def __init__(self, parent, **kwargs):
        super().__init__(parent, **kwargs)
        self.grid_columnconfigure((0, 1), weight=1)
        self.grid_rowconfigure((0, 1, 2, 3), weight=1)
        
        processor = DataProcessor()
        
        # WiFi информация
        self.wifi_frame = ctk.CTkFrame(self)
        self.wifi_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        self.wifi_label = ctk.CTkLabel(self.wifi_frame, text="WiFi", font=ctk.CTkFont(size=14, weight="bold"))
        self.wifi_label.pack(pady=(10, 5))
        self.wifi_ssid = ctk.CTkLabel(self.wifi_frame, text="SSID: --", font=ctk.CTkFont(size=12))
        self.wifi_ssid.pack(pady=2)
        self.wifi_ip = ctk.CTkLabel(self.wifi_frame, text="IP: --", font=ctk.CTkFont(size=12))
        self.wifi_ip.pack(pady=2)
        self.wifi_rssi = ctk.CTkLabel(self.wifi_frame, text="RSSI: --", font=ctk.CTkFont(size=12))
        self.wifi_rssi.pack(pady=2)
        self.wifi_percent = ctk.CTkLabel(self.wifi_frame, text="Сигнал: --%", font=ctk.CTkFont(size=12))
        self.wifi_percent.pack(pady=2)
        
        # Системная информация
        self.sys_frame = ctk.CTkFrame(self)
        self.sys_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")
        self.sys_label = ctk.CTkLabel(self.sys_frame, text="Система", font=ctk.CTkFont(size=14, weight="bold"))
        self.sys_label.pack(pady=(10, 5))
        self.sys_uptime = ctk.CTkLabel(self.sys_frame, text="Время работы: --", font=ctk.CTkFont(size=12))
        self.sys_uptime.pack(pady=2)
        self.sys_memory = ctk.CTkLabel(self.sys_frame, text="Память: --", font=ctk.CTkFont(size=12))
        self.sys_memory.pack(pady=2)
        self.sys_sensors = ctk.CTkLabel(self.sys_frame, text="Датчики: --", font=ctk.CTkFont(size=12))
        self.sys_sensors.pack(pady=2)
        
        # Статистика приложения
        self.app_stats_frame = ctk.CTkFrame(self)
        self.app_stats_frame.grid(row=1, column=0, columnspan=2, padx=10, pady=10, sticky="nsew")
        self.app_stats_label = ctk.CTkLabel(self.app_stats_frame, text="Статистика приложения", font=ctk.CTkFont(size=14, weight="bold"))
        self.app_stats_label.pack(pady=(10, 5))
        
        stats_inner = ctk.CTkFrame(self.app_stats_frame)
        stats_inner.pack(pady=5)
        
        self.messages_count = ctk.CTkLabel(stats_inner, text="Получено сообщений: 0", font=ctk.CTkFont(size=12))
        self.messages_count.pack(pady=2)
        self.last_message_time = ctk.CTkLabel(stats_inner, text="Последнее сообщение: --", font=ctk.CTkFont(size=12))
        self.last_message_time.pack(pady=2)
        self.saved_records = ctk.CTkLabel(stats_inner, text="Сохранено записей: 0", font=ctk.CTkFont(size=12))
        self.saved_records.pack(pady=2)
        
        # Настройки
        self.settings_frame = ctk.CTkFrame(self)
        self.settings_frame.grid(row=2, column=0, columnspan=2, padx=10, pady=10, sticky="nsew")
        self.settings_label = ctk.CTkLabel(self.settings_frame, text="Настройки Auto режима", font=ctk.CTkFont(size=14, weight="bold"))
        self.settings_label.pack(pady=(10, 5))
        
        settings_inner = ctk.CTkFrame(self.settings_frame)
        settings_inner.pack(pady=5)
        
        self.min_temp_label = ctk.CTkLabel(settings_inner, text="Мин. темп.: --°C", font=ctk.CTkFont(size=11))
        self.min_temp_label.pack(side='left', padx=10)
        self.max_temp_label = ctk.CTkLabel(settings_inner, text="Макс. темп.: --°C", font=ctk.CTkFont(size=11))
        self.max_temp_label.pack(side='left', padx=10)
        self.hysteresis_label = ctk.CTkLabel(settings_inner, text="Гистерезис: --°C", font=ctk.CTkFont(size=11))
        self.hysteresis_label.pack(side='left', padx=10)
        self.overheat_label = ctk.CTkLabel(settings_inner, text="Перегрев: --°C", font=ctk.CTkFont(size=11))
        self.overheat_label.pack(side='left', padx=10)
        
        # Время и дата
        self.time_frame = ctk.CTkFrame(self)
        self.time_frame.grid(row=3, column=0, columnspan=2, padx=10, pady=10, sticky="nsew")
        self.time_label = ctk.CTkLabel(self.time_frame, text="--:--:--", font=ctk.CTkFont(size=20, weight="bold"))
        self.time_label.pack(pady=5)
        self.date_label = ctk.CTkLabel(self.time_frame, text="--.--.----", font=ctk.CTkFont(size=14))
        self.date_label.pack(pady=2)
    
    def update_data(self, data: Dict):
        """Обновление статистики"""
        processor = DataProcessor()
        
        # WiFi
        wifi_ssid = data.get('wifiSSID', '')
        wifi_ip = data.get('wifiIP', '')
        wifi_rssi = data.get('wifiRSSI', 0)
        wifi_percent = processor.rssi_to_percent(wifi_rssi)
        
        self.wifi_ssid.configure(text=f"SSID: {wifi_ssid}")
        self.wifi_ip.configure(text=f"IP: {wifi_ip}")
        self.wifi_rssi.configure(text=f"RSSI: {wifi_rssi} dBm")
        self.wifi_percent.configure(text=f"Сигнал: {wifi_percent}%")
        
        # Цвет сигнала
        if wifi_percent >= 70:
            color = "#50C878"
        elif wifi_percent >= 40:
            color = "#FFD700"
        else:
            color = "#FF4444"
        self.wifi_percent.configure(text_color=color)
        
        # Система
        uptime = data.get('uptime', 0)
        free_mem = data.get('freeMem', 0)
        sensor_count = data.get('sensorCount', 0)
        
        self.sys_uptime.configure(text=f"Время работы: {processor.format_uptime(uptime)}")
        self.sys_memory.configure(text=f"Память: {processor.format_memory(free_mem)}")
        self.sys_sensors.configure(text=f"Датчики: {sensor_count}")
        
        # Настройки
        min_temp = data.get('minTemp', 0.0)
        max_temp = data.get('maxTemp', 0.0)
        hysteresis = data.get('hysteresis', 0.0)
        overheat_temp = data.get('overheatTemp', 0.0)
        
        self.min_temp_label.configure(text=f"Мин. темп.: {min_temp:.1f}°C")
        self.max_temp_label.configure(text=f"Макс. темп.: {max_temp:.1f}°C")
        self.hysteresis_label.configure(text=f"Гистерезис: {hysteresis:.1f}°C")
        self.overheat_label.configure(text=f"Перегрев: {overheat_temp:.1f}°C")
        
        # Время и дата
        time_str = data.get('time', '')
        date_str = data.get('date', '')
        if time_str:
            self.time_label.configure(text=time_str)
        if date_str:
            self.date_label.configure(text=date_str)
    
    def update_app_stats(self, messages_count: int, last_message_time: Optional[datetime], saved_records: int):
        """Обновление статистики приложения"""
        self.messages_count.configure(text=f"Получено сообщений: {messages_count}")
        
        if last_message_time:
            time_str = last_message_time.strftime("%H:%M:%S")
            self.last_message_time.configure(text=f"Последнее сообщение: {time_str}")
        else:
            self.last_message_time.configure(text="Последнее сообщение: --")
        
        self.saved_records.configure(text=f"Сохранено записей: {saved_records}")

