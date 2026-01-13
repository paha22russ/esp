"""
Скрипт для сборки исполняемого файла (.exe) приложения
"""

import PyInstaller.__main__
import sys
from pathlib import Path

# Параметры сборки
app_name = "ML_Training_Kotel"
main_script = "run.py"
icon_path = None  # Можно добавить иконку .ico если есть

# Скрытые импорты (модули, которые PyInstaller может не найти автоматически)
hidden_imports = [
    'customtkinter',
    'paho.mqtt.client',
    'matplotlib.backends.backend_tkagg',
    'matplotlib.figure',
    'numpy',
    'pandas',
    'json',
    'logging',
    'threading',
    'datetime',
    'pathlib',
    'collections',
    'typing',
]

# Данные для включения (если нужны дополнительные файлы)
datas = []

# Параметры PyInstaller
args = [
    main_script,
    '--name', app_name,
    '--onefile',  # Один exe файл
    '--windowed',  # Без консоли (GUI приложение)
    '--clean',  # Очистить кэш перед сборкой
    '--noconfirm',  # Не спрашивать подтверждение
    
    # Включить скрытые импорты
    *[f'--hidden-import={imp}' for imp in hidden_imports],
    
    # Добавить данные
    *[f'--add-data={data}' for data in datas],
    
    # Исключить ненужные модули (для уменьшения размера)
    '--exclude-module', 'tkinter.test',
    '--exclude-module', 'matplotlib.tests',
    '--exclude-module', 'numpy.tests',
    '--exclude-module', 'pandas.tests',
    
    # Иконка (если есть)
    # f'--icon={icon_path}' if icon_path else '',
]

print("Начинаю сборку исполняемого файла...")
print(f"Имя приложения: {app_name}")
print(f"Главный скрипт: {main_script}")
print("\nЭто может занять несколько минут...\n")

try:
    PyInstaller.__main__.run(args)
    print("\n" + "="*50)
    print("[OK] Сборка завершена успешно!")
    print("="*50)
    print(f"\nИсполняемый файл находится в: dist/{app_name}.exe")
    print("\nДля распространения:")
    print("1. Скопируйте dist/ML_Training_Kotel.exe на целевой компьютер")
    print("2. Создайте папку 'data' рядом с exe файлом (для сохранения данных)")
    print("3. Запустите exe файл")
except Exception as e:
    print(f"\n[ERROR] Ошибка при сборке: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

