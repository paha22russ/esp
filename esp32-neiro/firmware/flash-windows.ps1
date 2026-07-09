# Прошивка ESP32 neiro на Windows БЕЗ Arduino IDE
# Запуск в PowerShell (от имени администратора, если COM-порт не виден):
#   powershell -NoProfile -ExecutionPolicy Bypass -File flash-windows.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File flash-windows.ps1 -Port COM5

param(
    [string]$Port = ""   # COM-порт, например COM3. Пусто = автоопределение
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BinDir = Join-Path $ScriptDir "bin"

Write-Host "=== ESP32 neiro — прошивка ===" -ForegroundColor Cyan

# 1. Python + esptool
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Установите Python 3: https://www.python.org/downloads/" -ForegroundColor Red
    Write-Host "При установке отметьте 'Add Python to PATH'"
    exit 1
}

Write-Host "[*] Установка esptool..."
python -m pip install -q esptool

# 2. Проверка bin-файлов
$fw = Join-Path $BinDir "firmware.bin"
$boot = Join-Path $BinDir "bootloader.bin"
$part = Join-Path $BinDir "partitions.bin"

foreach ($f in @($fw, $boot, $part)) {
    if (-not (Test-Path $f)) {
        Write-Host "Не найден: $f" -ForegroundColor Red
        Write-Host "Соберите прошивку: pip install platformio && cd firmware && pio run"
        exit 1
    }
}

# 3. COM-порт
if ($Port -eq "") {
    Write-Host ""
    Write-Host "Доступные COM-порты:"
    [System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
    $Port = Read-Host "Введите COM-порт ESP32 (например COM3)"
}

Write-Host "[*] Прошивка на $Port ..."
Write-Host "    Если не стартует — зажмите BOOT на ESP32 и нажмите EN/RESET"

python -m esptool --chip esp32 --port $Port --baud 921600 write_flash -z `
    0x1000  $boot `
    0x8000  $part `
    0x10000 $fw

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Готово! ESP32 перезагружается." -ForegroundColor Green
    Write-Host "Монитор (опционально): python -m esptool --port $Port chip_id"
    Write-Host "Сервер: http://192.168.1.112:8000"
    Write-Host "API-токен: esp32-neiro-change-me"
} else {
    Write-Host "Ошибка прошивки. Проверьте USB-кабель (должен передавать данные) и драйвер CH340/CP2102." -ForegroundColor Red
}
