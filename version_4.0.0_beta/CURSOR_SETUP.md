# Настройка Cursor для проекта ESP32

## Установленные расширения в VS Code:
- `ms-vscode.cpptools` - C/C++ поддержка
- `platformio.platformio-ide` - PlatformIO IDE

## Способы установки расширений в Cursor:

### Способ 1: Через интерфейс Cursor (рекомендуется)
1. Откройте Cursor
2. Нажмите `Ctrl+Shift+X` (или `Cmd+Shift+X` на Mac)
3. Найдите и установите:
   - **C/C++** от Microsoft (ms-vscode.cpptools)
   - **PlatformIO IDE** от PlatformIO (platformio.platformio-ide)

### Способ 2: Через командную строку
```powershell
# Попробуйте с полными путями:
cursor --install-extension ms-vscode.cpptools
cursor --install-extension platformio.platformio-ide
```

### Способ 3: Импорт настроек VS Code
1. В Cursor откройте настройки (`Ctrl+,`)
2. Найдите опцию "Import VS Code Settings"
3. Выберите папку с настройками VS Code

## Открытие проекта в Cursor:

```powershell
# Из текущей директории:
cursor .

# Или укажите полный путь:
cursor "c:\Users\Pavel\Yandex.Disk\Arduino\Projects\kotel_esp32"
```

## Проверка установки PlatformIO:

После установки PlatformIO IDE в Cursor:
1. Откройте проект
2. В нижней панели должна появиться иконка PlatformIO
3. Компиляция: `pio run -e esp32dev_ota`
4. Загрузка: `pio run -e esp32dev_ota -t upload`

## Примечание:

Cursor использует свой marketplace расширений, поэтому некоторые расширения могут иметь другие ID или быть недоступны. 
Если расширения не устанавливаются через командную строку, используйте интерфейс Cursor.
