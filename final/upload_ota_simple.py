#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Простой скрипт для OTA загрузки прошивки на ESP32
"""
import sys
import os
import time

def upload_ota_simple(ip_address, firmware_path):
    """Загрузка прошивки через OTA используя requests"""
    try:
        import requests
    except ImportError:
        print("Установите requests: pip install requests")
        return False
    
    url = f"http://{ip_address}/update"
    
    if not os.path.exists(firmware_path):
        print(f"Ошибка: Файл {firmware_path} не найден!")
        return False
    
    file_size = os.path.getsize(firmware_path)
    print(f"Загрузка прошивки {firmware_path} на {ip_address}...")
    print(f"Размер файла: {file_size} байт ({file_size/1024:.1f} KB)")
    
    try:
        with open(firmware_path, 'rb') as f:
            files = {'update': (os.path.basename(firmware_path), f, 'application/octet-stream')}
            
            print("Отправка запроса...")
            response = requests.post(url, files=files, timeout=300)
            
            print(f"Статус ответа: {response.status_code}")
            print(f"Ответ сервера: {response.text}")
            
            if response.status_code == 200 and response.text.strip() == "OK":
                print("[OK] Прошивка успешно загружена! Устройство перезагрузится...")
                return True
            else:
                print(f"[ERROR] Ошибка загрузки: статус {response.status_code}, ответ: {response.text}")
                return False
                
    except requests.exceptions.Timeout:
        print("[ERROR] Таймаут при загрузке. Возможно файл слишком большой или устройство не отвечает.")
        return False
    except requests.exceptions.ConnectionError as e:
        print(f"[ERROR] Ошибка подключения: {e}")
        print("Проверьте, что устройство доступно по IP адресу")
        return False
    except Exception as e:
        print(f"[ERROR] Ошибка: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    ip = "192.168.0.198"
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    if len(sys.argv) > 2:
        firmware = sys.argv[2]
    
    print(f"IP адрес: {ip}")
    print(f"Файл прошивки: {firmware}")
    print("-" * 50)
    
    success = upload_ota_simple(ip, firmware)
    sys.exit(0 if success else 1)
