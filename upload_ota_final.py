#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Финальная версия OTA загрузки
"""
import sys
import os
import urllib.request
import urllib.parse

def upload_ota(ip, filepath):
    """Загрузка файла через OTA"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    file_type = "SPIFFS" if "spiffs" in filename else "Прошивка"
    
    print(f"\n{'='*50}")
    print(f"{file_type}: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"{'='*50}")
    
    try:
        with open(filepath, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        print(f"[ERROR] Ошибка чтения: {e}")
        return False
    
    boundary = '----WebKitFormBoundary' + str(os.getpid())
    
    body = f'--{boundary}\r\n'.encode()
    body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
    body += b'Content-Type: application/octet-stream\r\n\r\n'
    body += file_data
    body += f'\r\n--{boundary}--\r\n'.encode()
    
    url = f"http://{ip}/update"
    
    try:
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        print("Отправка...")
        print(f"Это может занять {file_size/1024/50:.0f}-{file_size/1024/30:.0f} секунд...")
        with urllib.request.urlopen(req, timeout=600) as response:
            status = response.getcode()
            text = response.read().decode('utf-8').strip()
            print(f"Статус: {status}")
            print(f"Ответ: {text}")
            
            if status == 200 and text == "OK":
                print(f"[OK] {file_type} загружен!")
                return True
            else:
                print(f"[ERROR] Ошибка: {text}")
                return False
    except Exception as e:
        print(f"[ERROR] {e}")
        return False

if __name__ == "__main__":
    ip = "192.168.0.197"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    print(f"IP: {ip}")
    
    # Сначала SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    spiffs_ok = upload_ota(ip, spiffs)
    
    if spiffs_ok:
        import time
        print("\nОжидание 3 секунды...")
        time.sleep(3)
    
    # Потом прошивка
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    firmware_ok = upload_ota(ip, firmware)
    
    if spiffs_ok and firmware_ok:
        print("\n" + "="*50)
        print("[OK] Все загружено успешно!")
        print("="*50)
    else:
        print("\n" + "="*50)
        print("[ERROR] Ошибка загрузки")
        print("="*50)
