#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys
import os
import urllib.request
import urllib.parse

def upload(ip, filepath):
    url = f"http://{ip}/update"
    
    if not os.path.exists(filepath):
        print(f"Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    print(f"Загрузка: {os.path.basename(filepath)}")
    print(f"Размер: {file_size/1024:.1f} KB")
    
    try:
        with open(filepath, 'rb') as f:
            file_data = f.read()
        
        boundary = '----WebKitFormBoundary' + str(os.getpid())
        filename = os.path.basename(filepath)
        
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
            if i == 4:
                body += part
            else:
                body += part.encode('utf-8')
            if i < len(body_parts) - 1:
                body += b'\r\n'
        
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        print("Отправка...")
        with urllib.request.urlopen(req, timeout=300) as response:
            status = response.getcode()
            text = response.read().decode('utf-8').strip()
            print(f"Статус: {status}")
            print(f"Ответ: {text}")
            if status == 200 and text == "OK":
                print("OK!")
                return True
            return False
    except Exception as e:
        print(f"Ошибка: {e}")
        return False

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.198"
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    upload(ip, firmware)
