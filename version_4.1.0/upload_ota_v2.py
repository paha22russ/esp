#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA загрузка прошивки на ESP32 (без внешних зависимостей)
"""
import sys
import os
import socket
import time

def upload_ota_v2(ip_address, firmware_path):
    """Загрузка прошивки через OTA используя socket"""
    
    if not os.path.exists(firmware_path):
        print(f"Ошибка: Файл {firmware_path} не найден!")
        return False
    
    file_size = os.path.getsize(firmware_path)
    print(f"Загрузка прошивки {firmware_path} на {ip_address}...")
    print(f"Размер файла: {file_size} байт ({file_size/1024:.1f} KB)")
    
    # Читаем файл
    try:
        with open(firmware_path, 'rb') as f:
            file_data = f.read()
    except Exception as e:
        print(f"Ошибка чтения файла: {e}")
        return False
    
    # Формируем multipart/form-data
    boundary = '----WebKitFormBoundary' + str(int(time.time()))
    filename = os.path.basename(firmware_path)
    
    # Заголовки multipart
    part1 = f'--{boundary}\r\n'
    part1 += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'
    part1 += 'Content-Type: application/octet-stream\r\n'
    part1 += '\r\n'
    
    part2 = f'\r\n--{boundary}--\r\n'
    
    body = part1.encode('utf-8') + file_data + part2.encode('utf-8')
    
    # HTTP запрос
    request = f'POST /update HTTP/1.1\r\n'
    request += f'Host: {ip_address}\r\n'
    request += f'Content-Type: multipart/form-data; boundary={boundary}\r\n'
    request += f'Content-Length: {len(body)}\r\n'
    request += 'Connection: close\r\n'
    request += '\r\n'
    
    try:
        print("Подключение к устройству...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(300)  # 5 минут таймаут
        sock.connect((ip_address, 80))
        
        print("Отправка запроса...")
        sock.sendall(request.encode('utf-8'))
        
        # Отправляем данные по частям для больших файлов
        chunk_size = 4096
        sent = 0
        while sent < len(body):
            chunk = body[sent:sent + chunk_size]
            sock.sendall(chunk)
            sent += len(chunk)
            if sent % (chunk_size * 10) == 0:
                print(f"Отправлено: {sent}/{len(body)} байт ({sent*100//len(body)}%)")
        
        print("Ожидание ответа...")
        response = b''
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            if b'\r\n\r\n' in response:
                # Получили заголовки, читаем тело если есть
                header_end = response.find(b'\r\n\r\n')
                headers = response[:header_end].decode('utf-8', errors='ignore')
                body_start = header_end + 4
                if b'Content-Length:' in response[:header_end]:
                    # Есть Content-Length, читаем тело
                    for line in headers.split('\r\n'):
                        if line.startswith('Content-Length:'):
                            content_len = int(line.split(':')[1].strip())
                            while len(response) < header_end + 4 + content_len:
                                chunk = sock.recv(4096)
                                if not chunk:
                                    break
                                response += chunk
                            break
                break
        
        sock.close()
        
        # Парсим ответ
        if b'\r\n\r\n' in response:
            header_end = response.find(b'\r\n\r\n')
            headers = response[:header_end].decode('utf-8', errors='ignore')
            body_text = response[header_end + 4:].decode('utf-8', errors='ignore').strip()
            
            # Ищем статус код
            status_line = headers.split('\r\n')[0]
            if '200' in status_line:
                print(f"Статус: 200 OK")
                print(f"Ответ: {body_text}")
                if body_text == "OK":
                    print("[OK] Прошивка успешно загружена! Устройство перезагрузится...")
                    return True
                else:
                    print(f"[ERROR] Ответ не OK: {body_text}")
                    return False
            else:
                print(f"[ERROR] HTTP ошибка: {status_line}")
                print(f"Ответ: {body_text}")
                return False
        else:
            print("[ERROR] Неполный ответ от сервера")
            return False
            
    except socket.timeout:
        print("[ERROR] Таймаут при загрузке")
        return False
    except socket.error as e:
        print(f"[ERROR] Ошибка подключения: {e}")
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
    
    success = upload_ota_v2(ip, firmware)
    sys.exit(0 if success else 1)
