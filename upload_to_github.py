#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Скрипт для автоматической загрузки прошивки на GitHub и обновления версии
"""
import subprocess
import sys
import os
import re
import shutil
from pathlib import Path

# Устанавливаем кодировку для Windows
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

# Конфигурация
GITHUB_REPO_OWNER = "paha22russ"
GITHUB_REPO_NAME = "esp"
GITHUB_REPO_URL = f"https://github.com/{GITHUB_REPO_OWNER}/{GITHUB_REPO_NAME}.git"
FIRMWARE_SOURCE = "src/main.cpp"
VERSION_FILE = "version.txt"
FIRMWARE_BIN = "firmware.bin"
SPIFFS_BIN = "spiffs.bin"
PLATFORMIO_ENV = "esp32dev_ota"
BUILD_DIR = f".pio/build/{PLATFORMIO_ENV}"
FIRMWARE_BUILD_PATH = f"{BUILD_DIR}/firmware.bin"
SPIFFS_BUILD_PATH = f"{BUILD_DIR}/spiffs.bin"

def get_platformio_cmd():
    """Определяет команду для запуска PlatformIO"""
    if sys.platform == "win32":
        platformio_cmd = f"{os.environ.get('USERPROFILE', '')}\\.platformio\\penv\\Scripts\\platformio.exe"
        if not os.path.exists(platformio_cmd):
            platformio_cmd = "platformio"
    else:
        platformio_cmd = "platformio"
    return platformio_cmd

def run_command(cmd, description, check=True):
    """Выполнение команды"""
    print(f"\n{'='*60}")
    print(f"{description}")
    print(f"{'='*60}")
    print(f"Команда: {cmd}")
    
    try:
        # Используем cp1251 для Windows консоли, но с обработкой ошибок
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, 
                              encoding='utf-8', errors='replace')
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print("STDERR:", result.stderr)
        
        if check and result.returncode != 0:
            print(f"[ERROR] Команда завершилась с ошибкой (код: {result.returncode})")
            return False
        return True
    except Exception as e:
        print(f"[ERROR] Ошибка выполнения команды: {e}")
        return False

def get_current_version():
    """Получает текущую версию из main.cpp"""
    try:
        with open(FIRMWARE_SOURCE, 'r', encoding='utf-8') as f:
            content = f.read()
            match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
            if match:
                return match.group(1)
    except Exception as e:
        print(f"[ERROR] Ошибка чтения версии: {e}")
    return None

def update_version_in_code(new_version):
    """Обновляет версию в main.cpp"""
    try:
        with open(FIRMWARE_SOURCE, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Заменяем версию - используем более безопасный способ
        pattern = r'#define\s+FIRMWARE_VERSION\s+"[^"]+"'
        replacement = f'#define FIRMWARE_VERSION "{new_version}"'
        new_content = re.sub(pattern, replacement, content)
        
        if new_content == content:
            print("[WARNING] Версия не найдена в коде или уже установлена")
            return False
        
        with open(FIRMWARE_SOURCE, 'w', encoding='utf-8') as f:
            f.write(new_content)
        
        print(f"[OK] Версия обновлена в {FIRMWARE_SOURCE}: {new_version}")
        return True
    except Exception as e:
        print(f"[ERROR] Ошибка обновления версии в коде: {e}")
        return False

def build_firmware():
    """Компилирует прошивку и SPIFFS"""
    platformio = get_platformio_cmd()
    # Компилируем прошивку
    if not run_command(f'{platformio} run -e {PLATFORMIO_ENV}', "Компиляция прошивки"):
        return False
    # Компилируем SPIFFS
    if not run_command(f'{platformio} run -e {PLATFORMIO_ENV} -t buildfs', "Компиляция SPIFFS"):
        print("[WARNING] Не удалось скомпилировать SPIFFS, продолжаем без него...")
    return True

def copy_firmware_to_repo(repo_path):
    """Копирует скомпилированную прошивку в репозиторий"""
    source = Path(FIRMWARE_BUILD_PATH)
    if not source.exists():
        print(f"[ERROR] Файл прошивки не найден: {FIRMWARE_BUILD_PATH}")
        return False
    
    dest = Path(repo_path) / FIRMWARE_BIN
    try:
        shutil.copy2(source, dest)
        print(f"[OK] Прошивка скопирована: {dest}")
        return True
    except Exception as e:
        print(f"[ERROR] Ошибка копирования прошивки: {e}")
        return False

def copy_spiffs_to_repo(repo_path):
    """Копирует скомпилированный SPIFFS в репозиторий"""
    source = Path(SPIFFS_BUILD_PATH)
    if not source.exists():
        print(f"[WARNING] Файл SPIFFS не найден: {SPIFFS_BUILD_PATH}")
        print(f"[INFO] SPIFFS будет пропущен (возможно, не скомпилирован)")
        return True  # Не критично, продолжаем
    
    dest = Path(repo_path) / SPIFFS_BIN
    try:
        shutil.copy2(source, dest)
        print(f"[OK] SPIFFS скопирован: {dest}")
        return True
    except Exception as e:
        print(f"[ERROR] Ошибка копирования SPIFFS: {e}")
        return False

def update_version_file(repo_path, version):
    """Обновляет version.txt в репозитории"""
    version_file = Path(repo_path) / VERSION_FILE
    try:
        with open(version_file, 'w', encoding='utf-8') as f:
            f.write(version + '\n')
        print(f"[OK] {VERSION_FILE} обновлен: {version}")
        return True
    except Exception as e:
        print(f"[ERROR] Ошибка обновления {VERSION_FILE}: {e}")
        return False

def clone_or_update_repo(repo_path):
    """Клонирует или обновляет репозиторий"""
    repo_path = Path(repo_path)
    
    if repo_path.exists() and (repo_path / ".git").exists():
        print(f"[INFO] Репозиторий уже существует, обновляем...")
        original_dir = os.getcwd()
        try:
            os.chdir(str(repo_path))
            result = subprocess.run("git pull", shell=True, capture_output=True, 
                                  text=True, encoding='utf-8', errors='replace')
            if result.returncode != 0:
                print("[WARNING] Не удалось обновить репозиторий, продолжаем...")
        finally:
            os.chdir(original_dir)
        return True
    else:
        print(f"[INFO] Клонируем репозиторий...")
        if repo_path.exists():
            shutil.rmtree(repo_path)
        result = subprocess.run(f"git clone {GITHUB_REPO_URL} {repo_path}", 
                              shell=True, capture_output=True, 
                              text=True, encoding='utf-8', errors='replace')
        if result.stdout:
            print(result.stdout)
        if result.returncode == 0:
            return True
        else:
            print(f"[ERROR] Ошибка клонирования: {result.stderr}")
            return False

def commit_and_push(repo_path, version):
    """Делает commit и push изменений"""
    repo_path = Path(repo_path)
    os.chdir(repo_path)
    
    # Проверяем статус
    result = subprocess.run("git status --porcelain", shell=True, capture_output=True, text=True)
    if not result.stdout.strip():
        print("[INFO] Нет изменений для коммита")
        os.chdir("..")
        return True
    
    # Проверяем, есть ли SPIFFS файл
    spiffs_exists = (repo_path / SPIFFS_BIN).exists()
    
    # Добавляем файлы
    files_to_add = "firmware.bin version.txt"
    if spiffs_exists:
        files_to_add += " spiffs.bin"
        print(f"[INFO] SPIFFS будет включен в коммит")
    if not run_command(f"git add {files_to_add}", "Добавление файлов в git"):
        os.chdir("..")
        return False
    
    # Коммит
    commit_message = f"Update firmware to version {version}"
    if not run_command(f'git commit -m "{commit_message}"', "Создание коммита"):
        os.chdir("..")
        return False
    
    # Push
    if not run_command("git push", "Отправка изменений на GitHub"):
        os.chdir("..")
        return False
    
    os.chdir("..")
    print(f"[OK] Изменения отправлены на GitHub!")
    return True

def main():
    """Основная функция"""
    import sys
    
    print("\n" + "="*60)
    print("Скрипт автоматической загрузки прошивки на GitHub")
    print("="*60)
    
    # Получаем текущую версию
    current_version = get_current_version()
    if not current_version:
        print("[ERROR] Не удалось определить текущую версию")
        return 1
    
    print(f"\n[INFO] Текущая версия: {current_version}")
    
    # Проверяем аргументы командной строки
    new_version_input = ""
    auto_confirm = False
    if len(sys.argv) > 1:
        if sys.argv[1] in ["-h", "--help"]:
            print("\nИспользование:")
            print("  python upload_to_github.py [версия] [--yes]")
            print("\nПримеры:")
            print("  python upload_to_github.py                    # Интерактивный режим")
            print("  python upload_to_github.py 4.2.1              # Указать версию")
            print("  python upload_to_github.py 4.2.1 --yes         # Автоматическое подтверждение")
            return 0
        else:
            for arg in sys.argv[1:]:
                if arg in ["--yes", "-y", "--auto"]:
                    auto_confirm = True
                else:
                    new_version_input = arg.strip()
    
    # Запрашиваем новую версию, если не указана
    if not new_version_input:
        print("\nВведите новую версию (или нажмите Enter для автоинкремента):")
        try:
            new_version_input = input().strip()
        except (EOFError, KeyboardInterrupt):
            print("\n[INFO] Отменено пользователем")
            return 0
    
    if not new_version_input:
        # Автоинкремент патч-версии
        parts = current_version.split('.')
        if len(parts) >= 3:
            try:
                patch = int(parts[2])
                parts[2] = str(patch + 1)
                new_version = '.'.join(parts)
            except:
                print("[ERROR] Не удалось автоматически увеличить версию")
                return 1
        else:
            print("[ERROR] Неверный формат версии для автоинкремента")
            return 1
    else:
        new_version = new_version_input
    
    print(f"\n[INFO] Новая версия: {new_version}")
    
    # Подтверждение
    if not auto_confirm:
        try:
            confirm = input(f"\nПродолжить с версией {new_version}? (y/n): ").strip().lower()
            if confirm != 'y':
                print("[INFO] Отменено пользователем")
                return 0
        except (EOFError, KeyboardInterrupt):
            print("\n[INFO] Отменено пользователем")
            return 0
    else:
        print(f"\n[INFO] Автоматическое подтверждение: да")
    
    # Обновляем версию в коде
    if not update_version_in_code(new_version):
        print("[WARNING] Не удалось обновить версию в коде, продолжаем...")
    
    # Компилируем прошивку
    if not build_firmware():
        print("[ERROR] Ошибка компиляции прошивки")
        return 1
    
    # Определяем путь к репозиторию (в родительской директории или рядом)
    repo_path = Path("..") / GITHUB_REPO_NAME
    if not repo_path.exists():
        repo_path = Path(".") / GITHUB_REPO_NAME
    
    # Клонируем или обновляем репозиторий
    if not clone_or_update_repo(repo_path):
        print("[ERROR] Ошибка работы с репозиторием")
        return 1
    
    # Копируем прошивку
    if not copy_firmware_to_repo(repo_path):
        return 1
    
    # Копируем SPIFFS (если есть)
    copy_spiffs_to_repo(repo_path)
    
    # Обновляем version.txt
    if not update_version_file(repo_path, new_version):
        return 1
    
    # Коммитим и пушим
    if not commit_and_push(repo_path, new_version):
        print("[WARNING] Не удалось отправить изменения на GitHub")
        print("[INFO] Файлы готовы в репозитории, можно отправить вручную")
        return 1
    
    print("\n" + "="*60)
    print(f"[SUCCESS] Прошивка версии {new_version} успешно загружена на GitHub!")
    print("="*60)
    print(f"\nРепозиторий: {GITHUB_REPO_URL}")
    print(f"Версия: {new_version}")
    files_list = "firmware.bin, version.txt"
    if (Path(repo_path) / SPIFFS_BIN).exists():
        files_list += ", spiffs.bin"
    print(f"Файлы: {files_list}")
    
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\n[INFO] Прервано пользователем")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERROR] Критическая ошибка: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
