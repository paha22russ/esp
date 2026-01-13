#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Загрузка только SPIFFS по OTA
"""
import sys
import os
import urllib.request
import time

def upload_file(ip, filepath):
    """Загрузка файла через OTA"""
    if not os.path.exists(filepath):
        print(f"[ERROR] Файл не найден: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    
    print(f"\n{'='*60}")
    print(f"SPIFFS: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"IP: {ip}")
    print(f"{'='*60}")
    
    try:
        print("Чтение файла...")
        with open(filepath, 'rb') as f:
            file_data = f.read()
        print(f"[OK] Файл прочитан ({file_size/1024:.1f} KB)")
        
        print("Подготовка данных...")
        boundary = '----WebKitFormBoundary' + str(os.getpid())
        
        body = f'--{boundary}\r\n'.encode()
        body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
        body += b'Content-Type: application/octet-stream\r\n\r\n'
        body += file_data
        body += f'\r\n--{boundary}--\r\n'.encode()
        print(f"[OK] Данные подготовлены ({len(body)/1024:.1f} KB)")
        
        url = f"http://{ip}/update"
        
        print("Отправка на устройство...")
        print(f"Это может занять {file_size/1024/50:.0f}-{file_size/1024/30:.0f} секунд...")
        print("Пожалуйста, подождите, не прерывайте процесс!")
        
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        start_time = time.time()
        try:
            with urllib.request.urlopen(req, timeout=600) as response:
                elapsed = time.time() - start_time
                
                status = response.getcode()
                text = response.read().decode('utf-8', errors='ignore').strip()
                
                print(f"[OK] Отправка завершена за {elapsed:.1f} секунд")
                print(f"Статус: {status}")
                print(f"Ответ: {text}")
                
                if status == 200:
                    if text == "OK":
                        print(f"\n[SUCCESS] SPIFFS успешно загружен!")
                        return True
                    elif "FAIL" in text.upper():
                        print(f"\n[ERROR] Устройство вернуло FAIL")
                        return False
                    else:
                        print(f"\n[WARNING] Нестандартный ответ, но продолжаем...")
                        print(f"[SUCCESS] SPIFFS загружен")
                        return True
                else:
                    print(f"\n[ERROR] HTTP статус: {status}")
                    return False
                    
        except urllib.error.HTTPError as e:
            elapsed = time.time() - start_time
            print(f"\n[ERROR] HTTP ошибка после {elapsed:.1f} секунд:")
            print(f"Код: {e.code}")
            print(f"Сообщение: {e.reason}")
            try:
                error_text = e.read().decode('utf-8', errors='ignore')
                print(f"Ответ: {error_text[:200]}")
            except:
                pass
            return False
            
    except urllib.error.URLError as e:
        print(f"\n[ERROR] Ошибка подключения: {e}")
        return False
    except Exception as e:
        print(f"\n[ERROR] Неожиданная ошибка: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    ip = "192.168.0.197"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    print("="*60)
    print("OTA Загрузка SPIFFS")
    print("="*60)
    print(f"IP устройства: {ip}")
    print()
    
    # Проверка доступности
    print("Проверка доступности устройства...")
    try:
        urllib.request.urlopen(f"http://{ip}/", timeout=5)
        print("[OK] Устройство доступно")
    except:
        print("[WARNING] Устройство не отвечает на проверку")
        print("         Продолжаем попытку загрузки...")
    print()
    
    # SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    if os.path.exists(spiffs):
        print("Загрузка SPIFFS (веб-интерфейс)...")
        spiffs_ok = upload_file(ip, spiffs)
        
        if spiffs_ok:
            print("\n" + "="*60)
            print("[SUCCESS] SPIFFS загружен успешно!")
            print("="*60)
        else:
            print("\n" + "="*60)
            print("[ERROR] Ошибка загрузки SPIFFS")
            print("="*60)
    else:
        print(f"[ERROR] SPIFFS не найден: {spiffs}")
        print("        Сначала скомпилируйте проект!")
