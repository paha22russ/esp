#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Быстрая OTA загрузка с прогрессом
"""
import sys
import os
import http.client
import urllib.parse

def upload_ota_fast(ip_address, firmware_path):
    """Загрузка прошивки через OTA"""
    
    if not os.path.exists(firmware_path):
        print(f"Ошибка: Файл {firmware_path} не найден!")
        return False
    
    file_size = os.path.getsize(firmware_path)
    print(f"Загрузка: {os.path.basename(firmware_path)}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"Устройство: {ip_address}")
    print("-" * 40)
    
    try:
        with open(firmware_path, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        print(f"Ошибка чтения: {e}")
        return False
    
    # Формируем multipart
    boundary = '----WebKitFormBoundary' + str(os.getpid())
    filename = os.path.basename(firmware_path)
    
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
        # Проверка доступности устройства
        print("Проверка доступности устройства...")
        test_conn = http.client.HTTPConnection(ip_address, timeout=5)
        test_conn.request('GET', '/')
        test_conn.getresponse()
        test_conn.close()
        print("Устройство доступно")
        
        print("Подключение для загрузки...")
        conn = http.client.HTTPConnection(ip_address, timeout=600)  # 10 минут
        
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
            print("\n[OK] Прошивка загружена! Устройство перезагрузится...")
            return True
        else:
            print(f"\n[ERROR] Ошибка: статус {status}, ответ: {response_text}")
            return False
            
    except Exception as e:
        print(f"\n[ERROR] {e}")
        return False

if __name__ == "__main__":
    ip = "192.168.0.198"
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    if len(sys.argv) > 2:
        firmware = sys.argv[2]
    
    success = upload_ota_fast(ip, firmware)
    sys.exit(0 if success else 1)
