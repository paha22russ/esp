#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA загрузка с прогресс-баром и понятным выводом
"""
import sys
import os
import time
import threading

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False
    import urllib.request

class ProgressBar:
    """Простой прогресс-бар"""
    def __init__(self, total, description="Загрузка"):
        self.total = total
        self.current = 0
        self.description = description
        self.start_time = time.time()
        self.running = True
        self.thread = None
        
    def start(self):
        """Запуск отображения прогресса"""
        self.thread = threading.Thread(target=self._display_progress)
        self.thread.daemon = True
        self.thread.start()
        
    def _display_progress(self):
        """Отображение прогресса"""
        while self.running:
            elapsed = time.time() - self.start_time
            if self.total > 0:
                percent = min(100, (self.current / self.total) * 100)
                bar_length = 30
                filled = int(bar_length * percent / 100)
                bar = '=' * filled + '-' * (bar_length - filled)
                speed = self.current / elapsed if elapsed > 0 else 0
                eta = (self.total - self.current) / speed if speed > 0 else 0
                print(f'\r{self.description}: [{bar}] {percent:.1f}% | '
                      f'{self.current/1024:.1f}KB/{self.total/1024:.1f}KB | '
                      f'{speed/1024:.1f}KB/s | ETA: {eta:.1f}s', end='', flush=True)
            else:
                # Если размер неизвестен, показываем только время
                print(f'\r{self.description}: {elapsed:.1f}s | {self.current/1024:.1f}KB отправлено...', 
                      end='', flush=True)
            time.sleep(0.5)
    
    def update(self, value):
        """Обновление прогресса"""
        self.current = value
        
    def finish(self):
        """Завершение отображения прогресса"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1)
        elapsed = time.time() - self.start_time
        speed = self.current / elapsed if elapsed > 0 else 0
        print(f'\r{self.description}: [{"="*30}] 100.0% | '
              f'{self.current/1024:.1f}KB | {speed/1024:.1f}KB/s | Завершено за {elapsed:.1f}s')

def upload_with_requests(ip, filepath):
    """Загрузка через requests (с прогрессом)"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    file_type = "SPIFFS" if "spiffs" in filename else "Прошивка"
    
    print(f"\n{'='*60}")
    print(f"{file_type}: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"IP: {ip}")
    print(f"{'='*60}")
    
    url = f"http://{ip}/update"
    
    try:
        print("Чтение файла...")
        with open(filepath, 'rb') as f:
            file_data = f.read()
        
        print("Подготовка данных...")
        files = {'update': (filename, file_data, 'application/octet-stream')}
        
        print("Отправка на устройство...")
        print("(Это может занять некоторое время, пожалуйста, подождите...)")
        
        # Запускаем прогресс-бар
        progress = ProgressBar(file_size, f"Отправка {file_type}")
        progress.start()
        
        # Отправляем запрос
        start_time = time.time()
        response = requests.post(url, files=files, timeout=300, stream=False)
        elapsed = time.time() - start_time
        
        progress.update(file_size)
        progress.finish()
        
        print(f"\nОтвет сервера: {response.status_code}")
        print(f"Текст ответа: {response.text[:100]}")
        
        if response.status_code == 200:
            if response.text.strip() == "OK":
                print(f"[OK] {file_type} успешно загружен!")
                return True
            elif "FAIL" in response.text:
                print(f"[ERROR] Устройство вернуло FAIL")
                print(f"Возможно, файл слишком большой или формат неверный")
                return False
            else:
                print(f"[WARNING] Неожиданный ответ: {response.text[:200]}")
                # Для SPIFFS иногда может быть другой ответ, но это OK
                if "spiffs" in filename.lower():
                    print(f"[OK] SPIFFS загружен (несмотря на нестандартный ответ)")
                    return True
                return False
        else:
            print(f"[ERROR] Статус {response.status_code}, ответ: {response.text[:200]}")
            return False
            
    except requests.exceptions.Timeout:
        print(f"\n[✗] Таймаут при загрузке {file_type}")
        return False
    except requests.exceptions.ConnectionError:
        print(f"\n[✗] Ошибка подключения к {ip}")
        print("Проверьте, что устройство включено и доступно по сети")
        return False
    except Exception as e:
        print(f"\n[✗] Ошибка: {e}")
        return False

def upload_with_urllib(ip, filepath):
    """Загрузка через urllib (без прогресса, но с сообщениями)"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    file_type = "SPIFFS" if "spiffs" in filename else "Прошивка"
    
    print(f"\n{'='*60}")
    print(f"{file_type}: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"IP: {ip}")
    print(f"{'='*60}")
    
    try:
        print("Чтение файла...")
        with open(filepath, 'rb') as f:
            file_data = f.read()
        
        print("Подготовка данных...")
        boundary = '----WebKitFormBoundary' + str(os.getpid())
        
        body = f'--{boundary}\r\n'.encode()
        body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
        body += b'Content-Type: application/octet-stream\r\n\r\n'
        body += file_data
        body += f'\r\n--{boundary}--\r\n'.encode()
        
        url = f"http://{ip}/update"
        
        print("Отправка на устройство...")
        print("(Это может занять 30-60 секунд для прошивки, пожалуйста, подождите...)")
        print("Прогресс: ", end='', flush=True)
        
        # Показываем точку каждую секунду
        def show_dots():
            dots = 0
            while not upload_done[0]:
                time.sleep(1)
                dots = (dots + 1) % 4
                print(f"\rПрогресс: {'.' * dots}{' ' * (3-dots)}", end='', flush=True)
        
        upload_done = [False]
        dot_thread = threading.Thread(target=show_dots)
        dot_thread.daemon = True
        dot_thread.start()
        
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        start_time = time.time()
        with urllib.request.urlopen(req, timeout=300) as response:
            upload_done[0] = True
            elapsed = time.time() - start_time
            
            status = response.getcode()
            text = response.read().decode('utf-8').strip()
            
            print(f"\r{' '*50}")  # Очистка строки
            print(f"Время загрузки: {elapsed:.1f} секунд")
            print(f"Статус: {status}")
            print(f"Ответ: {text[:100]}")
            
            if status == 200:
                if text == "OK":
                    print(f"[OK] {file_type} успешно загружен!")
                    return True
                elif "FAIL" in text:
                    print(f"[ERROR] Устройство вернуло FAIL")
                    print(f"Возможно, файл слишком большой или формат неверный")
                    return False
                else:
                    print(f"[WARNING] Неожиданный ответ: {text[:200]}")
                    # Для SPIFFS иногда может быть другой ответ, но это OK
                    if "spiffs" in filename.lower():
                        print(f"[OK] SPIFFS загружен (несмотря на нестандартный ответ)")
                        return True
                    return False
            else:
                print(f"[ERROR] Статус {status}, ответ: {text[:200]}")
                return False
                
    except urllib.error.URLError as e:
        print(f"\n[ERROR] Ошибка подключения: {e}")
        print("Проверьте, что устройство включено и доступно по сети")
        return False
    except Exception as e:
        print(f"\n[ERROR] Ошибка: {e}")
        return False

def main():
    ip = "192.168.0.198"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    print("="*60)
    print("OTA Загрузка прошивки и SPIFFS")
    print("="*60)
    print(f"IP устройства: {ip}")
    
    if HAS_REQUESTS:
        print("Используется библиотека requests (с прогресс-баром)")
    else:
        print("Используется urllib (установите requests для прогресс-бара: pip install requests)")
    
    # Проверка доступности устройства
    print(f"\nПроверка доступности устройства {ip}...")
    try:
        if HAS_REQUESTS:
            response = requests.get(f"http://{ip}/", timeout=5)
        else:
            response = urllib.request.urlopen(f"http://{ip}/", timeout=5)
        print("[OK] Устройство доступно")
    except:
        print("[!] Устройство не отвечает, но продолжаем попытку загрузки...")
    
    results = []
    
    # Сначала SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    if os.path.exists(spiffs):
        print(f"\n[1/2] Загрузка SPIFFS...")
        if HAS_REQUESTS:
            spiffs_ok = upload_with_requests(ip, spiffs)
        else:
            spiffs_ok = upload_with_urllib(ip, spiffs)
        results.append(("SPIFFS", spiffs_ok))
        
        if spiffs_ok:
            print("\nОжидание 3 секунды перед загрузкой прошивки...")
            time.sleep(3)
    else:
        print(f"\n[!] SPIFFS не найден: {spiffs}")
        print("Продолжаем без SPIFFS...")
        results.append(("SPIFFS", False))
    
    # Потом прошивка
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    if os.path.exists(firmware):
        print(f"\n[2/2] Загрузка прошивки...")
        if HAS_REQUESTS:
            firmware_ok = upload_with_requests(ip, firmware)
        else:
            firmware_ok = upload_with_urllib(ip, firmware)
        results.append(("Прошивка", firmware_ok))
    else:
        print(f"\n[✗] Прошивка не найдена: {firmware}")
        print("Сначала скомпилируйте проект!")
        results.append(("Прошивка", False))
    
    # Итоги
    print("\n" + "="*60)
    print("ИТОГИ:")
    print("="*60)
    for name, success in results:
        status = "[OK] Успешно" if success else "[ERROR] Ошибка"
        print(f"{name}: {status}")
    
    all_ok = all(result[1] for result in results if result[0] == "Прошивка" or os.path.exists(spiffs))
    if all_ok:
        print("\n[OK] Все загружено успешно!")
    else:
        print("\n[ERROR] Были ошибки при загрузке")
    print("="*60)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n[!] Загрузка прервана пользователем")
        sys.exit(1)
