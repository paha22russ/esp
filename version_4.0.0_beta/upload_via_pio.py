#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Загрузка через PlatformIO команду (использует espota протокол)
"""
import subprocess
import sys
import os

def main():
    print("="*60)
    print("OTA Загрузка через PlatformIO")
    print("="*60)
    print("Используется команда: pio run -e esp32dev_ota -t upload")
    print()
    
    # Проверяем наличие PlatformIO
    pio_cmd = None
    for cmd in ["pio", "platformio"]:
        try:
            result = subprocess.run(f"{cmd} --version", shell=True, capture_output=True, timeout=5)
            if result.returncode == 0:
                pio_cmd = cmd
                break
        except:
            continue
    
    if not pio_cmd:
        print("[ERROR] PlatformIO не найден в PATH")
        print("\nПожалуйста, выполните в VS Code терминале PlatformIO:")
        print("  pio run -e esp32dev_ota -t upload")
        print("\nИли используйте кнопку Upload в VS Code")
        sys.exit(1)
    
    print(f"[OK] Найден PlatformIO: {pio_cmd}")
    print("\nЗагрузка прошивки...")
    print("(Это может занять 1-2 минуты)")
    print()
    
    try:
        # Загружаем прошивку
        cmd = f"{pio_cmd} run -e esp32dev_ota -t upload"
        result = subprocess.run(cmd, shell=True)
        
        if result.returncode == 0:
            print("\n" + "="*60)
            print("[SUCCESS] Прошивка загружена успешно!")
            print("="*60)
        else:
            print("\n" + "="*60)
            print("[ERROR] Ошибка при загрузке")
            print("="*60)
            sys.exit(1)
            
    except KeyboardInterrupt:
        print("\n\n[INTERRUPTED] Загрузка прервана пользователем")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERROR] Ошибка: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
