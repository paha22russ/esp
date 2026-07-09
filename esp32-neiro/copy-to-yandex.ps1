# Копирование ESP32 neiro в Yandex.Disk (Windows)
# Запуск на ПК Yarag N2 в PowerShell:
#   powershell -NoProfile -ExecutionPolicy Bypass -File copy-to-yandex.ps1

$ErrorActionPreference = "Stop"

$Dest = "C:\Users\Yarag N2\Yandex.Disk\Программы\Проекты Сервер\ESP neiro"
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
Get-ChildItem (Join-Path $Temp "esp32-neiro") | ForEach-Object {
    Copy-Item -Recurse -Force $_.FullName (Join-Path $Dest $_.Name)
}

Remove-Item -Recurse -Force $Temp

Write-Host ""
Write-Host "Готово!" -ForegroundColor Green
Write-Host ""
Write-Host "Прошивка Arduino IDE:"
Write-Host "  $Dest\firmware\esp32_firmware\esp32_firmware.ino"
Write-Host ""
Write-Host "Сервер Python:"
Write-Host "  $Dest\server\"
Write-Host ""
Write-Host "Сервер на homeserv уже запущен: http://192.168.1.112:8000"
Write-Host "API-токен: esp32-neiro-change-me"
