"""
Виджет для отображения графиков
"""

import customtkinter as ctk
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.animation import FuncAnimation
import numpy as np
from collections import deque
from typing import Dict, Optional


class GraphPanel(ctk.CTkFrame):
    """Панель с графиками температур"""
    
    def __init__(self, parent, max_points: int = 100, **kwargs):
        super().__init__(parent, **kwargs)
        
        self.max_points = max_points
        self.time_data = deque(maxlen=max_points)
        self.supply_data = deque(maxlen=max_points)
        self.return_data = deque(maxlen=max_points)
        self.boiler_data = deque(maxlen=max_points)
        self.outdoor_data = deque(maxlen=max_points)
        self.home_data = deque(maxlen=max_points)
        
        # Создание фигуры matplotlib
        self.fig = Figure(figsize=(10, 6), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_xlabel('Время')
        self.ax.set_ylabel('Температура (°C)')
        self.ax.set_title('График температур в реальном времени')
        self.ax.grid(True, alpha=0.3)
        
        # Инициализация линий
        self.supply_line, = self.ax.plot([], [], label='Подача', color='#FF4444', linewidth=2)
        self.return_line, = self.ax.plot([], [], label='Обратка', color='#4A90E2', linewidth=2)
        self.boiler_line, = self.ax.plot([], [], label='Котел', color='#FFA500', linewidth=2)
        self.outdoor_line, = self.ax.plot([], [], label='Улица', color='#50C878', linewidth=2, linestyle='--')
        self.home_line, = self.ax.plot([], [], label='Дом', color='#9B59B6', linewidth=2, linestyle=':')
        
        self.ax.legend(loc='upper left')
        
        # Встраивание графика в tkinter
        self.canvas = FigureCanvasTkAgg(self.fig, self)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill='both', expand=True)
        
        # Анимация
        self.animation: Optional[FuncAnimation] = None
        self.start_time = 0
        self.point_counter = 0
    
    def start_animation(self):
        """Запуск анимации графика"""
        if self.animation is None:
            self.animation = FuncAnimation(
                self.fig, self._update_graph, interval=1000, blit=False,
                cache_frame_data=False, save_count=self.max_points
            )
    
    def stop_animation(self):
        """Остановка анимации графика"""
        if self.animation:
            self.animation.event_source.stop()
            self.animation = None
    
    def add_data_point(self, data: Dict):
        """Добавление точки данных"""
        if self.start_time == 0:
            self.start_time = data.get('timestamp', 0)
        
        timestamp = data.get('timestamp', 0)
        relative_time = timestamp - self.start_time if self.start_time > 0 else self.point_counter
        
        self.time_data.append(relative_time)
        self.supply_data.append(data.get('supplyTemp', 0.0))
        self.return_data.append(data.get('returnTemp', 0.0))
        self.boiler_data.append(data.get('boilerTemp', 0.0))
        self.outdoor_data.append(data.get('outdoorTemp', 0.0))
        # Температура дома (только если доступна)
        home_temp = data.get('homeTemp')
        if home_temp is not None and data.get('homeTempStatus') == 'online':
            self.home_data.append(home_temp)
        else:
            self.home_data.append(None)  # None для пропуска на графике
        
        self.point_counter += 1
    
    def _update_graph(self, frame):
        """Обновление графика"""
        if len(self.time_data) == 0:
            return
        
        time_array = np.array(self.time_data)
        
        self.supply_line.set_data(time_array, np.array(self.supply_data))
        self.return_line.set_data(time_array, np.array(self.return_data))
        self.boiler_line.set_data(time_array, np.array(self.boiler_data))
        self.outdoor_line.set_data(time_array, np.array(self.outdoor_data))
        # Температура дома (фильтруем None значения)
        home_array = np.array([t if t is not None else np.nan for t in self.home_data])
        self.home_line.set_data(time_array, home_array)
        
        # Автоматическое масштабирование
        if len(time_array) > 0:
            self.ax.set_xlim(max(0, time_array[-1] - 600), max(600, time_array[-1] + 60))
            
            all_temps = (list(self.supply_data) + list(self.return_data) + 
                        list(self.boiler_data) + list(self.outdoor_data) +
                        [t for t in self.home_data if t is not None])
            if all_temps:
                min_temp = min(all_temps) - 5
                max_temp = max(all_temps) + 5
                self.ax.set_ylim(min_temp, max_temp)
        
        self.canvas.draw()
    
    def clear_data(self):
        """Очистка данных графика"""
        self.time_data.clear()
        self.supply_data.clear()
        self.return_data.clear()
        self.boiler_data.clear()
        self.outdoor_data.clear()
        self.home_data.clear()
        self.start_time = 0
        self.point_counter = 0

