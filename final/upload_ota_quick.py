#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys
import os
import http.client
import time

ip = "192.168.0.198"
firmware = ".pio/build/esp32dev_ota/firmware.bin"

if len(sys.argv) > 1:
    ip = sys.argv[1]

if not os.path.exists(firmware):
    print(f"Файл не найден: {firmware}")
    sys.exit(1)

file_size = os.path.getsize(firmware)
print(f"Загрузка прошивки на {ip}")
print(f"Размер: {file_size/1024:.1f} KB")

with open(firmware, 'rb') as f:
    data = f.read()

boundary = '----WebKitFormBoundary' + str(int(time.time()))
filename = os.path.basename(firmware)

body = f'--{boundary}\r\n'.encode()
body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
body += b'Content-Type: application/octet-stream\r\n\r\n'
body += data
body += f'\r\n--{boundary}--\r\n'.encode()

try:
    conn = http.client.HTTPConnection(ip, timeout=60)
    conn.putrequest('POST', '/update')
    conn.putheader('Content-Type', f'multipart/form-data; boundary={boundary}')
    conn.putheader('Content-Length', str(len(body)))
    conn.endheaders()
    conn.send(body)
    
    resp = conn.getresponse()
    result = resp.read().decode('utf-8').strip()
    print(f"Статус: {resp.status}")
    print(f"Ответ: {result}")
    
    if resp.status == 200 and result == "OK":
        print("Успешно!")
    else:
        print("Ошибка!")
except Exception as e:
    print(f"Ошибка: {e}")
