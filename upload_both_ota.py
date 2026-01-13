#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Загрузка SPIFFS и прошивки по OTA
"""
import sys
import os
import http.client
import time

def upload_file(ip, filepath, file_type="firmware"):
    """Загрузка файла через OTA"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    print(f"\n{'='*50}")
    print(f"Загрузка {file_type}: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"{'='*50}")
    
    try:
        with open(filepath, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        print(f"[ERROR] Ошибка чтения: {e}")
        return False
    
    # Multipart form data
    boundary = '----WebKitFormBoundary' + str(int(time.time()))
    body_parts = [
        f'--{boundary}',
        f'Content-Disposition: form-data; name="update"; filename="{filename}"',
        'Content-Type: application/octet-stream',
        '',
        file_data,
        f'--{boundary}--'
    ]
    
    body = b''
    for i, part in enumerate(body_parts):
        if i == 4:  # Бинарные данные
            body += part
        else:
            body += part.encode('utf-8')
        if i < len(body_parts) - 1:
            body += b'\r\n'
    
    try:
        print("Подключение...")
        conn = http.client.HTTPConnection(ip, timeout=600)
        
        headers = {
            'Content-Type': f'multipart/form-data; boundary={boundary}',
            'Content-Length': str(len(body))
        }
        
        print("Отправка данных...")
        conn.request('POST', '/update', body, headers)
        
        print("Ожидание ответа...")
        response = conn.getresponse()
        status = response.status
        response_text = response.read().decode('utf-8').strip()
        
        conn.close()
        
        print(f"Статус: {status}")
        print(f"Ответ: {response_text}")
        
        if status == 200 and response_text == "OK":
            print(f"[OK] {file_type} загружен успешно!")
            if file_type == "firmware":
                print("Устройство перезагрузится...")
            return True
        else:
            print(f"[ERROR] Ошибка: статус {status}, ответ: {response_text}")
            return False
            
    except Exception as e:
        print(f"[ERROR] {e}")
        return False

if __name__ == "__main__":
    ip = "192.168.0.197"
    spiffs_path = ".pio/build/esp32dev_ota/spiffs.bin"
    firmware_path = ".pio/build/esp32dev_ota/firmware.bin"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    print(f"IP адрес устройства: {ip}")
    print(f"SPIFFS: {spiffs_path}")
    print(f"Прошивка: {firmware_path}")
    
    # Сначала SPIFFS
    spiffs_ok = upload_file(ip, spiffs_path, "SPIFFS")
    
    if spiffs_ok:
        print("\nОжидание 3 секунды перед загрузкой прошивки...")
        time.sleep(3)
    
    # Потом прошивка
    firmware_ok = upload_file(ip, firmware_path, "прошивка")
    
    if spiffs_ok and firmware_ok:
        print("\n" + "="*50)
        print("[OK] Все файлы загружены успешно!")
        print("="*50)
        sys.exit(0)
    else:
        print("\n" + "="*50)
        print("[ERROR] Ошибка при загрузке")
        print("="*50)
        sys.exit(1)
