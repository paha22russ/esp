#pragma once

// OTA 4.5-beta — ОТДЕЛЬНЫЕ артефакты. Миграции с 4.2 нет.
// Обновление возможно только чистой прошивкой / OTA внутри канала beta.
#define OTA_CHANNEL "v45-beta"
#define OTA_VERSION_URL "https://raw.githubusercontent.com/paha22russ/esp/main/version_v45.txt"
#define OTA_FIRMWARE_URL "https://raw.githubusercontent.com/paha22russ/esp/main/firmware_v45.bin"
#define OTA_SPIFFS_URL "https://raw.githubusercontent.com/paha22russ/esp/main/spiffs_v45.bin"

// Политика: 4.2.x OTA никогда не ставит 4.5; 4.5 не читает version.txt production.
