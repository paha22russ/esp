#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Скрипт для компиляции и загрузки прошивки через PlatformIO
"""
import subprocess
import sys
import os

def run_command(cmd, description):
    """Выполнение команды"""
    print(f"\n{'='*50}")
    print(f"{description}")
    print(f"{'='*50}")
    print(f"Команда: {cmd}")
    
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='utf-8', errors='ignore')
        print(result.stdout)
        if result.stderr:
            print("STDERR:", result.stderr)
        return result.returncode == 0
    except Exception as e:
        print(f"Ошибка: {e}")
        return False

def upload_ota(ip, filepath):
    """Загрузка файла через OTA"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    import urllib.request
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    
    print(f"\nЗагрузка: {filename} ({file_size/1024:.1f} KB)")
    
    try:
        with open(filepath, 'rb') as f:
            file_data = f.read()
        
        boundary = '----WebKitFormBoundary' + str(os.getpid())
        body = f'--{boundary}\r\n'.encode()
        body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
        body += b'Content-Type: application/octet-stream\r\n\r\n'
        body += file_data
        body += f'\r\n--{boundary}--\r\n'.encode()
        
        url = f"http://{ip}/update"
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        with urllib.request.urlopen(req, timeout=300) as response:
            status = response.getcode()
            text = response.read().decode('utf-8').strip()
            if status == 200 and text == "OK":
                print(f"[OK] {filename} загружен!")
                return True
            else:
                print(f"[ERROR] Статус: {status}, Ответ: {text}")
                return False
    except Exception as e:
        print(f"[ERROR] {e}")
        return False

if __name__ == "__main__":
    ip = "192.168.0.198"
    
    # Попытка найти PlatformIO
    pio_cmd = None
    for cmd in ["pio", "platformio", "python -m platformio"]:
        try:
            result = subprocess.run(f"{cmd} --version", shell=True, capture_output=True, timeout=5)
            if result.returncode == 0:
                pio_cmd = cmd
                break
        except:
            continue
    
    if not pio_cmd:
        print("="*50)
        print("PlatformIO не найден в PATH")
        print("="*50)
        print("\nПожалуйста, выполните в VS Code терминале PlatformIO:")
        print("1. pio run -e esp32dev_ota")
        print("2. python upload_ota_final.py 192.168.0.198")
        sys.exit(1)
    
    print(f"Найден PlatformIO: {pio_cmd}")
    
    # Компиляция
    if not run_command(f"{pio_cmd} run -e esp32dev_ota", "Компиляция проекта"):
        print("\n[ERROR] Ошибка компиляции!")
        sys.exit(1)
    
    # Загрузка SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    if os.path.exists(spiffs):
        if upload_ota(ip, spiffs):
            import time
            print("\nОжидание 3 секунды...")
            time.sleep(3)
    else:
        print(f"[WARNING] SPIFFS не найден: {spiffs}")
    
    # Загрузка прошивки
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    if os.path.exists(firmware):
        upload_ota(ip, firmware)
    else:
        print(f"[ERROR] Прошивка не найдена: {firmware}")
        sys.exit(1)
    
    print("\n" + "="*50)
    print("[OK] Готово!")
    print("="*50)
