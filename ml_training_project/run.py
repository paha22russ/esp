"""
Скрипт для запуска приложения ML Training
"""

import sys
from pathlib import Path

# Добавление пути к src в PYTHONPATH
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

from src.gui_main import main

if __name__ == "__main__":
    main()

