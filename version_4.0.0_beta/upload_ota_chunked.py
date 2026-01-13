#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA загрузка с отправкой по частям (chunked)
"""
import sys
import os
import socket
import time

def upload_chunked(ip, filepath):
    """Загрузка файла по частям"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    print(f"\nЗагрузка: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    
    boundary = '----WebKitFormBoundary' + str(int(time.time()))
    
    try:
        with open(filepath, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        print(f"[ERROR] Ошибка чтения: {e}")
        return False
    
    # Формируем multipart
    part1 = f'--{boundary}\r\n'
    part1 += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'
    part1 += 'Content-Type: application/octet-stream\r\n'
    part1 += '\r\n'
    
    part2 = f'\r\n--{boundary}--\r\n'
    
    # HTTP запрос
    request = f'POST /update HTTP/1.1\r\n'
    request += f'Host: {ip}\r\n'
    request += f'Content-Type: multipart/form-data; boundary={boundary}\r\n'
    request += f'Content-Length: {len(part1.encode()) + len(file_data) + len(part2.encode())}\r\n'
    request += 'Connection: close\r\n'
    request += '\r\n'
    
    try:
        print("Подключение...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30)  # Таймаут 30 секунд
        sock.connect((ip, 80))
        
        print("Отправка заголовков...")
        sock.sendall(request.encode('utf-8'))
        
        # Отправляем данные по частям
        print("Отправка данных...")
        sock.sendall(part1.encode('utf-8'))
        
        chunk_size = 8192
        sent = 0
        while sent < len(file_data):
            chunk = file_data[sent:sent + chunk_size]
            sock.sendall(chunk)
            sent += len(chunk)
            if sent % (chunk_size * 5) == 0:
                percent = (sent * 100) // len(file_data)
                print(f"Прогресс: {percent}% ({sent}/{len(file_data)} байт)")
        
        sock.sendall(part2.encode('utf-8'))
        
        print("Ожидание ответа...")
        sock.settimeout(10)
        response = b''
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if b'\r\n\r\n' in response:
                    break
            except socket.timeout:
                break
        
        sock.close()
        
        # Парсим ответ
        if response:
            header_end = response.find(b'\r\n\r\n')
            if header_end > 0:
                headers = response[:header_end].decode('utf-8', errors='ignore')
                body = response[header_end + 4:].decode('utf-8', errors='ignore').strip()
                
                if '200' in headers and body == "OK":
                    print(f"[OK] Загрузка успешна!")
                    return True
                else:
                    print(f"[ERROR] Ответ: {body}")
                    return False
        
        print("[ERROR] Нет ответа от сервера")
        return False
        
    except socket.timeout:
        print("[ERROR] Таймаут")
        return False
    except Exception as e:
        print(f"[ERROR] {e}")
        return False

if __name__ == "__main__":
    ip = "192.168.0.198"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    
    print(f"IP: {ip}")
    print("="*50)
    
    # SPIFFS
    print("\n[1/2] Загрузка SPIFFS...")
    spiffs_ok = upload_chunked(ip, spiffs)
    
    if spiffs_ok:
        print("\nОжидание 2 секунды...")
        time.sleep(2)
    
    # Прошивка
    print("\n[2/2] Загрузка прошивки...")
    firmware_ok = upload_chunked(ip, firmware)
    
    if spiffs_ok and firmware_ok:
        print("\n" + "="*50)
        print("[OK] Все загружено!")
        print("="*50)
    else:
        print("\n" + "="*50)
        print("[ERROR] Ошибка загрузки")
        print("="*50)
