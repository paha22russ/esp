"""
Виджет для отображения состояний устройств
"""

import customtkinter as ctk
from typing import Dict


class StatusPanel(ctk.CTkFrame):
    """Панель отображения состояний устройств"""
    
    def __init__(self, parent, **kwargs):
        super().__init__(parent, **kwargs)
        self.grid_columnconfigure((0, 1, 2), weight=1)
        self.grid_rowconfigure((0, 1, 2), weight=1)
        
        # Вентилятор
        self.fan_frame = ctk.CTkFrame(self)
        self.fan_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        self.fan_label = ctk.CTkLabel(self.fan_frame, text="Вентилятор", font=ctk.CTkFont(size=14, weight="bold"))
        self.fan_label.pack(pady=(10, 5))
        self.fan_indicator = ctk.CTkLabel(self.fan_frame, text="●", font=ctk.CTkFont(size=40))
        self.fan_indicator.pack(pady=5)
        self.fan_status = ctk.CTkLabel(self.fan_frame, text="Выкл", font=ctk.CTkFont(size=16))
        self.fan_status.pack(pady=5)
        
        # Насос
        self.pump_frame = ctk.CTkFrame(self)
        self.pump_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")
        self.pump_label = ctk.CTkLabel(self.pump_frame, text="Насос", font=ctk.CTkFont(size=14, weight="bold"))
        self.pump_label.pack(pady=(10, 5))
        self.pump_indicator = ctk.CTkLabel(self.pump_frame, text="●", font=ctk.CTkFont(size=40))
        self.pump_indicator.pack(pady=5)
        self.pump_status = ctk.CTkLabel(self.pump_frame, text="Выкл", font=ctk.CTkFont(size=16))
        self.pump_status.pack(pady=5)
        
        # Система
        self.system_frame = ctk.CTkFrame(self)
        self.system_frame.grid(row=0, column=2, padx=10, pady=10, sticky="nsew")
        self.system_label = ctk.CTkLabel(self.system_frame, text="Система", font=ctk.CTkFont(size=14, weight="bold"))
        self.system_label.pack(pady=(10, 5))
        self.system_indicator = ctk.CTkLabel(self.system_frame, text="●", font=ctk.CTkFont(size=40))
        self.system_indicator.pack(pady=5)
        self.system_status = ctk.CTkLabel(self.system_frame, text="Выкл", font=ctk.CTkFont(size=16))
        self.system_status.pack(pady=5)
        
        # Режим работы
        self.workmode_frame = ctk.CTkFrame(self)
        self.workmode_frame.grid(row=1, column=0, padx=10, pady=10, sticky="nsew")
        self.workmode_label = ctk.CTkLabel(self.workmode_frame, text="Режим работы", font=ctk.CTkFont(size=12))
        self.workmode_label.pack(pady=(10, 5))
        self.workmode_value = ctk.CTkLabel(self.workmode_frame, text="--", font=ctk.CTkFont(size=18, weight="bold"))
        self.workmode_value.pack(pady=5)
        
        # Состояние системы
        self.state_frame = ctk.CTkFrame(self)
        self.state_frame.grid(row=1, column=1, padx=10, pady=10, sticky="nsew")
        self.state_label = ctk.CTkLabel(self.state_frame, text="Состояние системы", font=ctk.CTkFont(size=12))
        self.state_label.pack(pady=(10, 5))
        self.state_value = ctk.CTkLabel(self.state_frame, text="--", font=ctk.CTkFont(size=18, weight="bold"))
        self.state_value.pack(pady=5)
        
        # Состояние комфорта (если режим Комфорт)
        self.comfort_frame = ctk.CTkFrame(self)
        self.comfort_frame.grid(row=1, column=2, padx=10, pady=10, sticky="nsew")
        self.comfort_label = ctk.CTkLabel(self.comfort_frame, text="Состояние комфорта", font=ctk.CTkFont(size=12))
        self.comfort_label.pack(pady=(10, 5))
        self.comfort_value = ctk.CTkLabel(self.comfort_frame, text="--", font=ctk.CTkFont(size=16))
        self.comfort_value.pack(pady=5)
        
        # Подброс угля
        self.coal_frame = ctk.CTkFrame(self)
        self.coal_frame.grid(row=2, column=0, columnspan=3, padx=10, pady=10, sticky="nsew")
        self.coal_label = ctk.CTkLabel(self.coal_frame, text="Подброс угля", font=ctk.CTkFont(size=12))
        self.coal_label.pack(pady=(10, 5))
        self.coal_status = ctk.CTkLabel(self.coal_frame, text="Неактивен", font=ctk.CTkFont(size=16))
        self.coal_status.pack(pady=5)
        
        # Погасание котла и розжиг
        self.boiler_frame = ctk.CTkFrame(self)
        self.boiler_frame.grid(row=3, column=0, columnspan=3, padx=10, pady=10, sticky="nsew")
        self.boiler_label = ctk.CTkLabel(self.boiler_frame, text="Состояние котла", font=ctk.CTkFont(size=12))
        self.boiler_label.pack(pady=(10, 5))
        self.boiler_status = ctk.CTkLabel(self.boiler_frame, text="Норма", font=ctk.CTkFont(size=16))
        self.boiler_status.pack(pady=5)
        self.ignition_info = ctk.CTkLabel(self.boiler_frame, text="", font=ctk.CTkFont(size=12))
        self.ignition_info.pack(pady=2)
    
    def update_data(self, data: Dict):
        """Обновление данных о состояниях"""
        # Вентилятор
        fan_state = data.get('fan', False)
        if fan_state:
            self.fan_indicator.configure(text_color="#50C878")
            self.fan_status.configure(text="Вкл", text_color="#50C878")
        else:
            self.fan_indicator.configure(text_color="#808080")
            self.fan_status.configure(text="Выкл", text_color="#808080")
        
        # Насос
        pump_state = data.get('pump', False)
        if pump_state:
            self.pump_indicator.configure(text_color="#50C878")
            self.pump_status.configure(text="Вкл", text_color="#50C878")
        else:
            self.pump_indicator.configure(text_color="#808080")
            self.pump_status.configure(text="Выкл", text_color="#808080")
        
        # Система
        system_enabled = data.get('systemEnabled', False)
        if system_enabled:
            self.system_indicator.configure(text_color="#50C878")
            self.system_status.configure(text="Вкл", text_color="#50C878")
        else:
            self.system_indicator.configure(text_color="#808080")
            self.system_status.configure(text="Выкл", text_color="#808080")
        
        # Режим работы
        work_mode = data.get('workMode', 0)
        work_mode_name = data.get('workModeName', 'Авто')
        self.workmode_value.configure(text=work_mode_name)
        if work_mode == 1:
            self.workmode_value.configure(text_color="#4A90E2")  # Синий для Комфорт
        else:
            self.workmode_value.configure(text_color="#50C878")  # Зеленый для Авто
        
        # Состояние системы
        state = data.get('state', '')
        self.state_value.configure(text=state)
        
        # Цвет состояния
        state_colors = {
            'IDLE': '#808080',
            'Работа': '#50C878',
            'Перегрев!': '#FF4444',
            'Остановлена': '#FFD700',
            'Подброс угля': '#FFA500',
            'Ожидание': '#4A90E2'
        }
        color = state_colors.get(state, '#808080')
        self.state_value.configure(text_color=color)
        
        # Состояние комфорта (только в режиме Комфорт)
        if work_mode == 1:
            comfort_state = data.get('comfortState', '')
            comfort_state_names = {
                'WAIT': 'Ожидание',
                'HEATING_1': 'Нагрев 1',
                'WAIT_COOLING': 'Ожидание охлаждения',
                'WAIT_HEATING': 'Ожидание нагрева',
                'HEATING_2': 'Нагрев 2',
                'COMFORT': 'Комфорт',
                'MAINTAIN': 'Поддержание',
                'OVERHEAT': 'Перегрев'
            }
            comfort_display = comfort_state_names.get(comfort_state, comfort_state)
            self.comfort_value.configure(text=comfort_display)
            if comfort_state in ['COMFORT', 'MAINTAIN']:
                self.comfort_value.configure(text_color="#50C878")
            elif comfort_state in ['HEATING_1', 'HEATING_2']:
                self.comfort_value.configure(text_color="#FFA500")
            elif comfort_state == 'OVERHEAT':
                self.comfort_value.configure(text_color="#FF4444")
            else:
                self.comfort_value.configure(text_color="#808080")
        else:
            self.comfort_value.configure(text="--", text_color="#808080")
        
        # Подброс угля
        coal_active = data.get('coalFeedingActive', False)
        coal_elapsed = data.get('coalFeedingElapsed', 0)
        if coal_active:
            minutes = coal_elapsed // 60
            seconds = coal_elapsed % 60
            self.coal_status.configure(
                text=f"Активен ({minutes}м {seconds}с)",
                text_color="#FFA500"
            )
        else:
            self.coal_status.configure(text="Неактивен", text_color="#808080")
        
        # Погасание котла и розжиг
        boiler_extinguished = data.get('boilerExtinguished', False)
        ignition_in_progress = data.get('ignitionInProgress', False)
        ignition_elapsed = data.get('ignitionElapsed', 0)
        ignition_temp_increase = data.get('ignitionTempIncrease', 0.0)
        
        if boiler_extinguished:
            self.boiler_status.configure(text="КОТЕЛ ПОГАС", text_color="#FF4444")
            self.ignition_info.configure(text="Требуется розжиг", text_color="#FF4444")
        elif ignition_in_progress:
            minutes = ignition_elapsed // 60
            self.boiler_status.configure(text="РОЗЖИГ", text_color="#FFD700")
            self.ignition_info.configure(
                text=f"{minutes} мин, +{ignition_temp_increase:.1f}°C", 
                text_color="#FFD700"
            )
        else:
            self.boiler_status.configure(text="Норма", text_color="#50C878")
            self.ignition_info.configure(text="")

