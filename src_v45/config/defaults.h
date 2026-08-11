#pragma once

#include <Arduino.h>

namespace cfg {

// Периоды
constexpr uint32_t TELEMETRY_INTERVAL_MS = 5000;
constexpr uint32_t SENSOR_POLL_MS = 1000;
constexpr uint32_t CONTROL_TICK_MS = 500;
constexpr uint32_t SAFETY_TICK_MS = 200;

// DS18B20 recovery
constexpr uint8_t  DS_ERROR_STREAK_BEFORE_RECOVERY = 3;   // не из-за единичного выброса
constexpr uint32_t DS_RECOVERY_POWER_OFF_MS = 3000;
constexpr uint32_t DS_RECOVERY_COOLDOWN_MS = 60000;       // пауза между recovery одной шины
constexpr float    DS_IMPLAUSIBLE_HIGH_C = 84.5f;         // типичный «85°C» отвал
constexpr float    DS_MAX_JUMP_C = 15.0f;                 // разовый скачок → отбросить

// Аварийные пределы (аппаратно-независимый safety layer)
constexpr float SAFETY_SUPPLY_OVERHEAT_C = 90.0f;
constexpr float SAFETY_SUPPLY_CRITICAL_C = 95.0f;
constexpr float SAFETY_FLUE_CRITICAL_C = 350.0f;
constexpr float SAFETY_RETURN_MIN_WHILE_PUMP_C = 35.0f;

// Fan abstraction
constexpr uint8_t FAN_ON_THRESHOLD_PCT = 1;  // >=1% → реле ON в текущей HW-реализации

// Pump afterheat
constexpr uint32_t PUMP_AFTERHEAT_MS = 10UL * 60UL * 1000UL; // 10 мин после выкл вентилятора

// VPS
constexpr const char* DEFAULT_VPS_BASE_URL = "http://esp.pahavpn.cloud-ip.cc";
constexpr uint16_t VPS_HTTP_TIMEOUT_MS = 4000;

// Дом по MQTT
constexpr uint32_t HOME_TEMP_STALE_MS = 5UL * 60UL * 1000UL;  // 5 мин

}  // namespace cfg
