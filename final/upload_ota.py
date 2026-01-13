#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import urllib.request
import urllib.parse
import sys
import os

def upload_firmware_ota(ip_address, firmware_path):
    """Загрузка прошивки по OTA через HTTP POST"""
    url = f"http://{ip_address}/update"
    
    if not os.path.exists(firmware_path):
        print(f"Ошибка: Файл {firmware_path} не найден!")
        return False
    
    file_size = os.path.getsize(firmware_path)
    print(f"Загрузка прошивки {firmware_path} на {ip_address}...")
    print(f"Размер файла: {file_size} байт")
    
    try:
        # Читаем файл
        with open(firmware_path, 'rb') as f:
            file_data = f.read()
        
        # Формируем multipart/form-data
        boundary = '----WebKitFormBoundary' + ''.join([str(i) for i in range(10)])
        filename = os.path.basename(firmware_path)
        
        body_parts = [
            f'--{boundary}',
            f'Content-Disposition: form-data; name="update"; filename="{filename}"',
            'Content-Type: application/octet-stream',
            '',
            file_data,
            f'--{boundary}--'
        ]
        
        # Объединяем части (кроме бинарных данных)
        body = b''
        for i, part in enumerate(body_parts):
            if i == 4:  # Бинарные данные
                body += part
            else:
                body += part.encode('utf-8')
            if i < len(body_parts) - 1:
                body += b'\r\n'
        
        # Создаем запрос
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        print("Отправка запроса...")
        with urllib.request.urlopen(req, timeout=300) as response:
            status_code = response.getcode()
            response_text = response.read().decode('utf-8')
            
            print(f"Статус ответа: {status_code}")
            print(f"Ответ сервера: {response_text}")
            
            if status_code == 200 and response_text.strip() == "OK":
                print("[OK] Прошивка успешно загружена!")
                return True
            else:
                print(f"[ERROR] Ошибка загрузки: статус {status_code}, ответ: {response_text}")
                return False
                
    except urllib.error.HTTPError as e:
        print(f"[ERROR] HTTP ошибка: {e.code} - {e.reason}")
        try:
            print(f"Ответ: {e.read().decode('utf-8')}")
        except:
            pass
        return False
    except urllib.error.URLError as e:
        print(f"[ERROR] Ошибка подключения: {e.reason}")
        return False
    except Exception as e:
        print(f"[ERROR] Ошибка: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    ip = "192.168.0.198"
    firmware = ".pio/build/esp32dev/firmware.bin"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    if len(sys.argv) > 2:
        firmware = sys.argv[2]
    
    success = upload_firmware_ota(ip, firmware)
    sys.exit(0 if success else 1)
