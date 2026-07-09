# Копирование ESP32 neiro в Yandex.Disk (Windows)
# Запуск в PowerShell:
#   powershell -NoProfile -ExecutionPolicy Bypass -File copy-to-yandex.ps1

$ErrorActionPreference = "Stop"

$Dest = "C:\Users\myzhe\Yandex.Disk\Программы\Проекты Сервер\ESP neiro"
$Branch = "cursor/esp32-neiro-bd71"
$Repo = "https://github.com/paha22russ/esp.git"
$Temp = Join-Path $env:TEMP "esp-neiro-clone"

Write-Host "=== Копирование ESP32 neiro ===" -ForegroundColor Cyan
Write-Host "Цель: $Dest"

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

if (Test-Path $Temp) { Remove-Item -Recurse -Force $Temp }

Write-Host "Клонирование репозитория (ветка $Branch)..."
git clone -b $Branch --depth 1 $Repo $Temp

Write-Host "Копирование файлов..."
Copy-Item -Recurse -Force (Join-Path $Temp "esp32-neiro\*") $Dest

Remove-Item -Recurse -Force $Temp

Write-Host ""
Write-Host "Готово!" -ForegroundColor Green
Write-Host "Прошивка:" (Join-Path $Dest "firmware\esp32_firmware\esp32_firmware.ino")
Write-Host "Сервер:" (Join-Path $Dest "server")
Write-Host ""
Write-Host "API-токен по умолчанию: esp32-neiro-change-me"
Write-Host "URL сервера: http://192.168.1.112:8000"
