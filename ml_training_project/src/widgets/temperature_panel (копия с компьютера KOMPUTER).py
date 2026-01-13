"""
Виджет для отображения температур
"""

import customtkinter as ctk
from typing import Dict, Optional
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


class TemperaturePanel(ctk.CTkFrame):
    """Панель отображения температур"""
    
    def __init__(self, parent, **kwargs):
        super().__init__(parent, **kwargs)
        self.grid_columnconfigure((0, 1, 2, 3), weight=1)
        self.grid_rowconfigure((0, 1, 2), weight=1)
        
        # Температура подачи (главный индикатор)
        self.supply_frame = ctk.CTkFrame(self)
        self.supply_frame.grid(row=0, column=0, columnspan=2, padx=10, pady=10, sticky="nsew")
        self.supply_label = ctk.CTkLabel(self.supply_frame, text="Температура подачи", font=ctk.CTkFont(size=14))
        self.supply_label.pack(pady=(10, 5))
        self.supply_value = ctk.CTkLabel(self.supply_frame, text="--°C", font=ctk.CTkFont(size=48, weight="bold"))
        self.supply_value.pack(pady=5)
        
        # Температура обратки
        self.return_frame = ctk.CTkFrame(self)
        self.return_frame.grid(row=0, column=2, padx=10, pady=10, sticky="nsew")
        self.return_label = ctk.CTkLabel(self.return_frame, text="Обратка", font=ctk.CTkFont(size=12))
        self.return_label.pack(pady=(10, 5))
        self.return_value = ctk.CTkLabel(self.return_frame, text="--°C", font=ctk.CTkFont(size=32, weight="bold"))
        self.return_value.pack(pady=5)
        
        # Температура котла
        self.boiler_frame = ctk.CTkFrame(self)
        self.boiler_frame.grid(row=0, column=3, padx=10, pady=10, sticky="nsew")
        self.boiler_label = ctk.CTkLabel(self.boiler_frame, text="Котел", font=ctk.CTkFont(size=12))
        self.boiler_label.pack(pady=(10, 5))
        self.boiler_value = ctk.CTkLabel(self.boiler_frame, text="--°C", font=ctk.CTkFont(size=32, weight="bold"))
        self.boiler_value.pack(pady=5)
        
        # Температура на улице
        self.outdoor_frame = ctk.CTkFrame(self)
        self.outdoor_frame.grid(row=1, column=0, padx=10, pady=10, sticky="nsew")
        self.outdoor_label = ctk.CTkLabel(self.outdoor_frame, text="Улица", font=ctk.CTkFont(size=12))
        self.outdoor_label.pack(pady=(10, 5))
        self.outdoor_value = ctk.CTkLabel(self.outdoor_frame, text="--°C", font=ctk.CTkFont(size=28, weight="bold"))
        self.outdoor_value.pack(pady=5)
        
        # Разница температур
        self.diff_frame = ctk.CTkFrame(self)
        self.diff_frame.grid(row=1, column=1, padx=10, pady=10, sticky="nsew")
        self.diff_label = ctk.CTkLabel(self.diff_frame, text="Разница", font=ctk.CTkFont(size=12))
        self.diff_label.pack(pady=(10, 5))
        self.diff_value = ctk.CTkLabel(self.diff_frame, text="--°C", font=ctk.CTkFont(size=28, weight="bold"))
        self.diff_value.pack(pady=5)
        
        # Уставка
        self.setpoint_frame = ctk.CTkFrame(self)
        self.setpoint_frame.grid(row=1, column=2, columnspan=2, padx=10, pady=10, sticky="nsew")
        self.setpoint_label = ctk.CTkLabel(self.setpoint_frame, text="Уставка температуры", font=ctk.CTkFont(size=12))
        self.setpoint_label.pack(pady=(10, 5))
        self.setpoint_value = ctk.CTkLabel(self.setpoint_frame, text="--°C", font=ctk.CTkFont(size=28, weight="bold"))
        self.setpoint_value.pack(pady=5)
    
    def update_data(self, data: Dict):
        """Обновление данных о температурах"""
        processor = DataProcessor()
        
        # Температура подачи
        supply_temp = data.get('supplyTemp', 0.0)
        self.supply_value.configure(text=f"{supply_temp:.1f}°C")
        color = processor.get_temperature_color(supply_temp)
        self.supply_value.configure(text_color=color)
        
        # Температура обратки
        return_temp = data.get('returnTemp', 0.0)
        self.return_value.configure(text=f"{return_temp:.1f}°C")
        color = processor.get_temperature_color(return_temp)
        self.return_value.configure(text_color=color)
        
        # Температура котла
        boiler_temp = data.get('boilerTemp', 0.0)
        self.boiler_value.configure(text=f"{boiler_temp:.1f}°C")
        color = processor.get_temperature_color(boiler_temp, min_temp=50, max_temp=90)
        self.boiler_value.configure(text_color=color)
        
        # Температура на улице
        outdoor_temp = data.get('outdoorTemp', 0.0)
        self.outdoor_value.configure(text=f"{outdoor_temp:.1f}°C")
        if outdoor_temp < 0:
            self.outdoor_value.configure(text_color="#4A90E2")
        else:
            self.outdoor_value.configure(text_color="#50C878")
        
        # Разница температур
        temp_diff = data.get('tempDiff', 0.0)
        self.diff_value.configure(text=f"{temp_diff:.1f}°C")
        
        # Уставка
        setpoint = data.get('setpoint', 0.0)
        self.setpoint_value.configure(text=f"{setpoint:.1f}°C")

