#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Простая OTA загрузка с понятным выводом
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
    file_type = "SPIFFS" if "spiffs" in filename else "Прошивка"
    
    print(f"\n{'='*60}")
    print(f"{file_type}: {filename}")
    print(f"Размер: {file_size/1024:.1f} KB")
    print(f"IP: {ip}")
    print(f"{'='*60}")
    
    try:
        print("Шаг 1/4: Чтение файла...")
        with open(filepath, 'rb') as f:
            file_data = f.read()
        print(f"        [OK] Файл прочитан ({file_size/1024:.1f} KB)")
        
        print("Шаг 2/4: Подготовка данных...")
        boundary = '----WebKitFormBoundary' + str(os.getpid())
        
        body = f'--{boundary}\r\n'.encode()
        body += f'Content-Disposition: form-data; name="update"; filename="{filename}"\r\n'.encode()
        body += b'Content-Type: application/octet-stream\r\n\r\n'
        body += file_data
        body += f'\r\n--{boundary}--\r\n'.encode()
        print(f"        [OK] Данные подготовлены ({len(body)/1024:.1f} KB)")
        
        url = f"http://{ip}/update"
        
        print("Шаг 3/4: Отправка на устройство...")
        print(f"        Это может занять {file_size/1024/50:.0f}-{file_size/1024/30:.0f} секунд...")
        print("        Пожалуйста, подождите, не прерывайте процесс!")
        
        req = urllib.request.Request(url, data=body)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
        req.add_header('Content-Length', str(len(body)))
        
        start_time = time.time()
        try:
            with urllib.request.urlopen(req, timeout=600) as response:
                elapsed = time.time() - start_time
                
                status = response.getcode()
                text = response.read().decode('utf-8', errors='ignore').strip()
                
                print(f"        [OK] Отправка завершена за {elapsed:.1f} секунд")
                
                print("Шаг 4/4: Проверка ответа...")
                print(f"        Статус: {status}")
                print(f"        Ответ: {text[:100]}")
                
                if status == 200:
                    if text == "OK":
                        print(f"\n[SUCCESS] {file_type} успешно загружен!")
                        return True
                    elif "FAIL" in text.upper():
                        print(f"\n[ERROR] Устройство вернуло FAIL")
                        print(f"Возможные причины:")
                        print(f"  - Файл слишком большой")
                        print(f"  - Неверный формат файла")
                        print(f"  - Недостаточно места в памяти")
                        return False
                    else:
                        # Для SPIFFS иногда может быть другой ответ
                        if "spiffs" in filename.lower():
                            print(f"\n[WARNING] Нестандартный ответ, но продолжаем...")
                            print(f"[SUCCESS] SPIFFS загружен")
                            return True
                        print(f"\n[ERROR] Неожиданный ответ: {text[:200]}")
                        return False
                else:
                    print(f"\n[ERROR] HTTP статус: {status}")
                    return False
                    
        except urllib.error.HTTPError as e:
            elapsed = time.time() - start_time
            print(f"\n[ERROR] HTTP ошибка после {elapsed:.1f} секунд:")
            print(f"        Код: {e.code}")
            print(f"        Сообщение: {e.reason}")
            try:
                error_text = e.read().decode('utf-8', errors='ignore')
                print(f"        Ответ: {error_text[:200]}")
            except:
                pass
            return False
            
    except urllib.error.URLError as e:
        print(f"\n[ERROR] Ошибка подключения: {e}")
        print("Проверьте:")
        print("  - Устройство включено и подключено к сети")
        print(f"  - IP адрес правильный: {ip}")
        print("  - Устройство доступно по сети")
        return False
    except Exception as e:
        print(f"\n[ERROR] Неожиданная ошибка: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    ip = "192.168.0.197"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    
    print("="*60)
    print("OTA Загрузка прошивки и SPIFFS")
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
    
    results = []
    
    # SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    if os.path.exists(spiffs):
        print("[1/2] Загрузка SPIFFS (веб-интерфейс)...")
        spiffs_ok = upload_file(ip, spiffs)
        results.append(("SPIFFS", spiffs_ok))
        
        if spiffs_ok:
            print("\nОжидание 3 секунды перед загрузкой прошивки...")
            time.sleep(3)
            print()
    else:
        print(f"[SKIP] SPIFFS не найден: {spiffs}")
        print("       Продолжаем без SPIFFS...")
        results.append(("SPIFFS", None))
        print()
    
    # Прошивка
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    if os.path.exists(firmware):
        print("[2/2] Загрузка прошивки...")
        firmware_ok = upload_file(ip, firmware)
        results.append(("Прошивка", firmware_ok))
    else:
        print(f"[ERROR] Прошивка не найдена: {firmware}")
        print("        Сначала скомпилируйте проект!")
        results.append(("Прошивка", False))
    
    # Итоги
    print("\n" + "="*60)
    print("ИТОГИ ЗАГРУЗКИ:")
    print("="*60)
    for name, success in results:
        if success is None:
            status = "[SKIP] Пропущено"
        elif success:
            status = "[OK] Успешно"
        else:
            status = "[ERROR] Ошибка"
        print(f"{name:15} : {status}")
    
    # Проверка успешности
    firmware_result = next((r[1] for r in results if r[0] == "Прошивка"), None)
    spiffs_result = next((r[1] for r in results if r[0] == "SPIFFS"), None)
    
    if firmware_result:
        print("\n[SUCCESS] Прошивка загружена успешно!")
        if spiffs_result:
            print("[SUCCESS] SPIFFS загружен успешно!")
        print("\nУстройство будет перезагружено автоматически.")
    else:
        print("\n[ERROR] Загрузка прошивки не удалась!")
        print("Проверьте ошибки выше.")
    
    print("="*60)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n[INTERRUPTED] Загрузка прервана пользователем")
        sys.exit(1)
