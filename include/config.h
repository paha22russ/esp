#ifndef CONFIG_H
#define CONFIG_H

// Версия прошивки
#define FIRMWARE_VERSION "1.0.3"

// ПИНЫ ДЛЯ ESP32
#define PIN_RELAY_FAN 16      // Реле вентилятора
#define PIN_RELAY_PUMP 17     // Реле насоса
#define PIN_I2C_SDA 21        // I2C SDA (OLED дисплей)
#define PIN_I2C_SCL 22        // I2C SCL (OLED дисплей)
#define PIN_ONEWIRE_1 4       // OneWire шина 1 (Подача, Обратка)
#define PIN_ONEWIRE_2 5       // OneWire шина 2 (Котельная, Улица)
#define PIN_ENCODER_CLK 18    // Энкодер CLK
#define PIN_ENCODER_DT 19     // Энкодер DT
#define PIN_ENCODER_SW 23     // Энкодер SW (кнопка)

// АДРЕСА ДАТЧИКОВ
#define SENSOR_SUPPLY 0       // Подача
#define SENSOR_RETURN 1       // Обратка
#define SENSOR_BOILER 2       // Котельная
#define SENSOR_OUTSIDE 3      // Улица

// OLED ДИСПЛЕЙ
#define OLED_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// ПАРАМЕТРЫ ПО УМОЛЧАНИЮ
#define DEFAULT_SETPOINT 60.0f
#define DEFAULT_MIN_TEMP 45.0f
#define DEFAULT_MAX_TEMP 75.0f
#define DEFAULT_HYSTERESIS 2.0f
#define DEFAULT_INERTIA_TEMP 55.0f
#define DEFAULT_INERTIA_TIME_MINUTES 10
#define DEFAULT_OVERHEAT_TEMP 77.0f
#define DEFAULT_HEATING_TIMEOUT_MINUTES 30
#define DEFAULT_TEMP_TOLERANCE 5.0f

// Константы для валидации (совместимость со старым кодом)
#define AUTO_SETPOINT_MIN 40.0f
#define AUTO_SETPOINT_MAX 80.0f
#define AUTO_TEMP_MIN 40.0f
#define AUTO_TEMP_MAX 80.0f
#define AUTO_HYSTERESIS_MIN 0.5f
#define AUTO_HYSTERESIS_MAX 5.0f
#define AUTO_INERTIA_TEMP_DEFAULT DEFAULT_INERTIA_TEMP
#define AUTO_INERTIA_TIME_MINUTES_DEFAULT DEFAULT_INERTIA_TIME_MINUTES
#define AUTO_OVERHEAT_TEMP_DEFAULT DEFAULT_OVERHEAT_TEMP
#define AUTO_HEATING_TIMEOUT_MINUTES_DEFAULT DEFAULT_HEATING_TIMEOUT_MINUTES
#define AUTO_TEMP_TOLERANCE_DEFAULT DEFAULT_TEMP_TOLERANCE

// ТАЙМАУТЫ И ИНТЕРВАЛЫ
#define TEMP_UPDATE_INTERVAL 1500        // Обновление температуры (мс)
#define TEMP_CONVERSION_DELAY 750        // Задержка конвертации DS18B20 (мс)
#define ENCODER_SHORT_PRESS_TIME 500     // Короткое нажатие (мс)
#define ENCODER_MENU_PRESS_TIME 3000     // Вход в меню (мс)
#define ENCODER_LONG_PRESS_TIME 10000    // Долгое нажатие (мс)
#define SETPOINT_TIMEOUT 10000           // Таймаут установки уставки (мс)
#define MENU_TIMEOUT 30000               // Таймаут меню (мс)
#define FUEL_FEED_DURATION 600000        // Длительность подброса угля (10 мин)
#define PUMP_ANTI_STICK_INTERVAL 3600000 // Антизалипание насоса (1 час)
#define PUMP_ANTI_STICK_DURATION 60000   // Длительность антизалипания (1 мин)
#define MANUAL_CONTROL_TIMEOUT 300000    // Таймаут принудительного управления (5 мин)

// ПЕРЕСКАНИРОВАНИЕ ДАТЧИКОВ
#define SOFT_RESCAN_INTERVAL_NORMAL 300000   // Нормальный интервал (5 мин)
#define SOFT_RESCAN_INTERVAL_ERROR 30000     // При ошибках (30 сек)
#define FULL_RESCAN_ERROR_COUNT 5            // Количество ошибок для полного пересканирования

// MQTT НАСТРОЙКИ ПО УМОЛЧАНИЮ
#define DEFAULT_MQTT_SERVER "m5.wqtt.ru"
#define DEFAULT_MQTT_PORT 5374
#define DEFAULT_MQTT_USER "u_OLTTB0"
#define DEFAULT_MQTT_PASSWORD "XDzCIXr0"
#define DEFAULT_MQTT_PREFIX "kotel/device1"
#define MQTT_PUBLISH_INTERVAL 10000          // Интервал публикации (мс)
#define MQTT_UPTIME_INTERVAL 3600000         // Интервал публикации uptime (1 час)

// NTP НАСТРОЙКИ ПО УМОЛЧАНИЮ
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_OFFSET 3            // UTC+3 (Москва)
#define NTP_UPDATE_INTERVAL 3600000          // Интервал обновления (1 час)

// WiFi НАСТРОЙКИ
#define AP_SSID "KotelAP"
#define AP_PASSWORD "kotel12345"
#define AP_IP "192.168.4.1"
#define WIFI_CONNECT_TIMEOUT 30               // Таймаут подключения (сек)

// OTA НАСТРОЙКИ
#define OTA_HOSTNAME "kotel-esp32"
#define OTA_PASSWORD "kotel12345"

// EEPROM
#define EEPROM_SIZE 1024
#define EEPROM_MAGIC 0xAA

// СОСТОЯНИЯ СИСТЕМЫ
enum BoilerState {
  STATE_OFF = 0,
  STATE_IDLE,
  STATE_HEATING,
  STATE_INERTIA_WAIT,
  STATE_CATCH_UP,
  STATE_FUEL_FEED,
  STATE_EMERGENCY
};

// СОБЫТИЯ ЭНКОДЕРА
enum EncoderEvent {
  ENCODER_NONE = 0,
  ENCODER_ROTATE_CW,
  ENCODER_ROTATE_CCW,
  ENCODER_SHORT_PRESS,
  ENCODER_MENU_PRESS,
  ENCODER_LONG_PRESS
};

// РЕЖИМЫ РАБОТЫ
enum WorkMode {
  MODE_AUTO = 0
};

#endif // CONFIG_H

