#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>
#include "config.h"

// Параметры режима АВТО
struct ExtendedAutoParams {
  float setpoint = DEFAULT_SETPOINT;
  float minTemp = DEFAULT_MIN_TEMP;
  float maxTemp = DEFAULT_MAX_TEMP;
  float hysteresis = DEFAULT_HYSTERESIS;
  float inertiaTemp = DEFAULT_INERTIA_TEMP;
  uint16_t inertiaTimeMinutes = DEFAULT_INERTIA_TIME_MINUTES;
  float overheatTemp = DEFAULT_OVERHEAT_TEMP;
  uint16_t heatingTimeoutMinutes = DEFAULT_HEATING_TIMEOUT_MINUTES;
  float tempTolerance = DEFAULT_TEMP_TOLERANCE;
};

// Настройки WiFi
struct WiFiSettings {
  char ssid[32] = "";
  char password[64] = "";
  bool useStaticIP = false;
  char staticIP[16] = "";
  char staticGateway[16] = "";
  char staticSubnet[16] = "";
  char hostname[32] = "kotel-esp32";
  uint16_t connectTimeout = WIFI_CONNECT_TIMEOUT;
};

// Настройки MQTT
struct MQTTSettings {
  bool enabled = true;
  char server[64] = DEFAULT_MQTT_SERVER;
  uint16_t port = DEFAULT_MQTT_PORT;
  char user[32] = DEFAULT_MQTT_USER;
  char password[64] = DEFAULT_MQTT_PASSWORD;
  char prefix[64] = DEFAULT_MQTT_PREFIX;
  bool useTLS = false;
  uint16_t publishInterval = MQTT_PUBLISH_INTERVAL;
};

// Настройки NTP
struct NTPSettings {
  char server[64] = DEFAULT_NTP_SERVER;
  int8_t timezoneOffset = DEFAULT_TIMEZONE_OFFSET;
  uint32_t updateInterval = NTP_UPDATE_INTERVAL;
};

// Привязка датчиков (адреса OneWire)
struct SensorMapping {
  uint8_t sensorAddresses[4][8];  // 4 датчика по 8 байт адреса
  bool addressesValid[4] = {false, false, false, false};
  // Индексы для совместимости со старым кодом
  uint8_t supplyIndex = 0;
  uint8_t returnIndex = 1;
  uint8_t boilerIndex = 2;
  uint8_t outsideIndex = 3;
};

// Последнее состояние для восстановления
struct LastState {
  BoilerState state = STATE_OFF;
  bool fanOn = false;
  bool pumpOn = false;
  bool systemOn = false;
};

// Структура сохраненных параметров
struct SavedParams {
  uint8_t magic = 0;
  ExtendedAutoParams autoParams;
  WorkMode mode = MODE_AUTO;
  WiFiSettings wifiSettings;
  MQTTSettings mqttSettings;
  NTPSettings ntpSettings;
  SensorMapping sensorMapping;
  LastState lastState;
  bool manualOff = false;
  uint16_t checksum = 0;
};

class EEPROMStorage {
public:
  void begin();
  bool loadParams(SavedParams& params);
  bool saveParams(const SavedParams& params);
  void resetToDefaults(SavedParams& params);
  uint16_t calculateChecksum(const SavedParams& params);
};

#endif // EEPROM_STORAGE_H

