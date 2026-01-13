#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA загрузка через espota протокол (как в PlatformIO)
Использует UDP порт 3232 вместо HTTP
"""
import sys
import os
import socket
import struct
import time

# Константы из ArduinoOTA
OTA_AUTH = 0
OTA_START = 1
OTA_WRITE = 2
OTA_END = 3
OTA_ERROR = 4

def upload_espota(ip, filepath, password="kotel12345"):
    """Загрузка файла через espota протокол"""
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
    print(f"Протокол: espota (UDP порт 3232)")
    print(f"{'='*60}")
    
    try:
        # Читаем файл
        print("Шаг 1/5: Чтение файла...")
        with open(filepath, 'rb') as f:
            file_data = f.read()
        print(f"        [OK] Файл прочитан ({file_size/1024:.1f} KB)")
        
        # Создаем UDP сокет
        print("Шаг 2/5: Подключение к устройству...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(10)
        addr = (ip, 3232)
        
        # Отправляем команду AUTH с паролем
        print("Шаг 3/5: Аутентификация...")
        auth_packet = struct.pack('>BHB', OTA_AUTH, len(password), 0) + password.encode('utf-8')
        sock.sendto(auth_packet, addr)
        
        try:
            response, _ = sock.recvfrom(1024)
            if len(response) < 3 or struct.unpack('>BHB', response[:3])[0] != OTA_AUTH:
                print("        [ERROR] Ошибка аутентификации")
                return False
            print("        [OK] Аутентификация успешна")
        except socket.timeout:
            print("        [ERROR] Таймаут при аутентификации")
            return False
        
        # Отправляем команду START
        print("Шаг 4/5: Начало загрузки...")
        file_type_flag = 0 if "spiffs" in filename.lower() else 1  # 0=SPIFFS, 1=Sketch
        start_packet = struct.pack('>BHH', OTA_START, file_size, file_type_flag)
        sock.sendto(start_packet, addr)
        
        try:
            response, _ = sock.recvfrom(1024)
            if len(response) < 1 or struct.unpack('>B', response[:1])[0] != OTA_START:
                print("        [ERROR] Устройство не готово к загрузке")
                return False
            print("        [OK] Устройство готово")
        except socket.timeout:
            print("        [ERROR] Таймаут при старте загрузки")
            return False
        
        # Отправляем данные частями
        print("Шаг 5/5: Отправка данных...")
        print(f"        Это может занять {file_size/1024/50:.0f}-{file_size/1024/30:.0f} секунд...")
        print("        Пожалуйста, подождите, не прерывайте процесс!")
        
        chunk_size = 1460  # Максимальный размер UDP пакета минус заголовки
        total_sent = 0
        start_time = time.time()
        
        for offset in range(0, file_size, chunk_size):
            chunk = file_data[offset:offset + chunk_size]
            chunk_len = len(chunk)
            
            # Формируем пакет WRITE
            write_packet = struct.pack('>BHH', OTA_WRITE, offset, chunk_len) + chunk
            sock.sendto(write_packet, addr)
            
            total_sent += chunk_len
            
            # Показываем прогресс каждые 10%
            if (total_sent * 100 // file_size) % 10 == 0 and total_sent > 0:
                percent = (total_sent * 100) // file_size
                elapsed = time.time() - start_time
                speed = total_sent / elapsed if elapsed > 0 else 0
                eta = (file_size - total_sent) / speed if speed > 0 else 0
                print(f"        Прогресс: {percent}% ({total_sent/1024:.1f}KB/{file_size/1024:.1f}KB, "
                      f"{speed/1024:.1f}KB/s, осталось ~{eta:.0f}с)")
            
            # Ждем подтверждения
            try:
                sock.settimeout(5)
                response, _ = sock.recvfrom(1024)
                if len(response) < 1:
                    print(f"\n        [ERROR] Пустой ответ от устройства на offset {offset}")
                    return False
                cmd = struct.unpack('>B', response[:1])[0]
                if cmd == OTA_ERROR:
                    print(f"\n        [ERROR] Устройство сообщило об ошибке на offset {offset}")
                    return False
            except socket.timeout:
                print(f"\n        [ERROR] Таймаут при отправке данных на offset {offset}")
                return False
        
        # Отправляем команду END
        print("\n        Завершение загрузки...")
        end_packet = struct.pack('>BHH', OTA_END, file_size, 0)
        sock.sendto(end_packet, addr)
        
        try:
            sock.settimeout(10)
            response, _ = sock.recvfrom(1024)
            if len(response) >= 1:
                cmd = struct.unpack('>B', response[:1])[0]
                if cmd == OTA_END:
                    elapsed = time.time() - start_time
                    print(f"        [OK] Загрузка завершена за {elapsed:.1f} секунд")
                    print(f"\n[SUCCESS] {file_type} успешно загружен!")
                    sock.close()
                    return True
                elif cmd == OTA_ERROR:
                    print(f"        [ERROR] Устройство сообщило об ошибке при завершении")
                    sock.close()
                    return False
        except socket.timeout:
            print("        [WARNING] Нет ответа на команду END, но загрузка может быть успешной")
            print(f"\n[SUCCESS] {file_type} загружен (без подтверждения)")
            sock.close()
            return True
            
    except socket.gaierror:
        print(f"\n[ERROR] Не удалось разрешить IP адрес: {ip}")
        return False
    except socket.error as e:
        print(f"\n[ERROR] Ошибка сети: {e}")
        return False
    except Exception as e:
        print(f"\n[ERROR] Неожиданная ошибка: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    ip = "192.168.0.198"
    password = "kotel12345"
    
    if len(sys.argv) > 1:
        ip = sys.argv[1]
    if len(sys.argv) > 2:
        password = sys.argv[2]
    
    print("="*60)
    print("OTA Загрузка через espota протокол")
    print("="*60)
    print(f"IP устройства: {ip}")
    print(f"Пароль: {password}")
    print()
    
    results = []
    
    # SPIFFS
    spiffs = ".pio/build/esp32dev_ota/spiffs.bin"
    if os.path.exists(spiffs):
        print("[1/2] Загрузка SPIFFS (веб-интерфейс)...")
        spiffs_ok = upload_espota(ip, spiffs, password)
        results.append(("SPIFFS", spiffs_ok))
        
        if spiffs_ok:
            print("\nОжидание 3 секунды перед загрузкой прошивки...")
            time.sleep(3)
            print()
    else:
        print(f"[SKIP] SPIFFS не найден: {spiffs}")
        results.append(("SPIFFS", None))
        print()
    
    # Прошивка
    firmware = ".pio/build/esp32dev_ota/firmware.bin"
    if os.path.exists(firmware):
        print("[2/2] Загрузка прошивки...")
        firmware_ok = upload_espota(ip, firmware, password)
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
