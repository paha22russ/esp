#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>  // mDNS для доступа по kotel.local
#include <esp_task_wdt.h>  // Watchdog timer для диагностики
#include <esp_system.h>  // Для получения причины перезагрузки

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define EEPROM_SIZE 1024
#define EEPROM_MAGIC 0xAA
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_AUTO 1
#define EEPROM_ADDR_MQTT 200
#define EEPROM_ADDR_SENSORS 400
#define EEPROM_ADDR_SYSTEM 600
#define EEPROM_ADDR_WIFI 700
#define EEPROM_ADDR_NTP 800
#define EEPROM_ADDR_ML 1000
#define EEPROM_ADDR_RELAY 1100
#define EEPROM_ADDR_COMFORT 1200
#define EEPROM_ADDR_WORKMODE 1300
#define EEPROM_ADDR_UPDATE 1400
#define EEPROM_ADDR_BOOT_COUNT 1500
#define EEPROM_ADDR_BOOT_LOG 1600  // Журнал перезагрузок (50 записей по 32 байта = 1600 байт)
#define BOOT_LOG_MAX_ENTRIES 50
#define BOOT_LOG_ENTRY_SIZE 32  // Размер одной записи
#define EEPROM_ADDR_EVENT_LOG 2000  // Журнал событий (30 записей по 80 байт = 2400 байт)
#define EEPROM_ADDR_FAN_STATS 4400  // Статистика работы вентилятора (около 50 байт)

// Версия прошивки
#define FIRMWARE_VERSION "4.2.23"

// GitHub репозиторий для обновлений
#define GITHUB_REPO_OWNER "paha22russ"
#define GITHUB_REPO_NAME "esp"
#define GITHUB_VERSION_URL "https://raw.githubusercontent.com/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/main/version.txt"
#define GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/main/firmware.bin"
#define GITHUB_SPIFFS_URL "https://raw.githubusercontent.com/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/main/spiffs.bin"

// Пины подключения
#define PIN_RELAY_FAN 16
#define PIN_RELAY_PUMP 17
#define PIN_RELAY_SENSORS 25  // Реле питания датчиков DS18B20
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define PIN_ENCODER_CLK 18
#define PIN_ENCODER_DT 19
#define PIN_ENCODER_SW 23
#define PIN_DS18B20_1 4  // OneWire шина 1: Подача, Обратка
#define PIN_DS18B20_2 5  // OneWire шина 2: Котельная, Улица
#define PIN_LED_BUILTIN 2  // Встроенный светодиод ESP32

// OLED дисплей (SSD1306 128x64, I2C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Переменные для энкодера
volatile int encoderPosition = 0;
volatile int lastEncoderState = 0;
volatile int lastValidEncoderState = 0; // Последнее валидное состояние для фильтрации дребезга
int lastEncoderPosition = 0;
unsigned long lastEncoderChange = 0;
bool encoderButtonPressed = false;
unsigned long lastButtonPress = 0;
unsigned long lastEncoderRotation = 0; // Время последнего поворота энкодера
volatile unsigned long lastEncoderISRTime = 0; // Время последнего прерывания энкодера
volatile unsigned long lastEncoderISRCallTime = 0; // Время последнего вызова ISR (для защиты от дребезга)
volatile int encoderISRCount = 0; // Счетчик прерываний для отладки
const unsigned long ENCODER_ROTATION_DEBOUNCE_MS = 2000; // Игнорировать кнопку после поворота (увеличено до 2 сек)
const unsigned long ENCODER_ISR_DEBOUNCE_US = 2000; // Минимальный интервал между прерываниями (микросекунды) - увеличено для стабильности


WiFiManager wifiManager;
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// OneWire и DallasTemperature для датчиков DS18B20 (две шины)
OneWire oneWire1(PIN_DS18B20_1);  // Шина 1: Подача, Обратка
DallasTemperature sensors1(&oneWire1);
OneWire oneWire2(PIN_DS18B20_2);  // Шина 2: Котельная, Улица
DallasTemperature sensors2(&oneWire2);

// Заглушки данных (заменить на реальные функции управления котлом)
float supplyTemp = 0.0;
float returnTemp = 0.0;
float boilerTemp = 0.0;
float outdoorTemp = 0.0;
float homeTemp = 0.0;  // Температура в доме (получается с MQTT от ESP01)
unsigned long lastHomeTempUpdate = 0;  // Время последнего обновления температуры дома
const unsigned long HOME_TEMP_TIMEOUT = 300000;  // 5 минут - таймаут для определения offline датчика
bool homeTempSensorLWTOnline = false;  // LWT статус датчика температуры дома (true = online, false = offline)
float setpoint = 60.0;

// Структура для отслеживания истории температур (для определения тренда)
struct TemperatureHistory {
  float values[20];  // История последних 20 значений (увеличено)
  int index;         // Текущий индекс
  int count;         // Количество записанных значений
  unsigned long lastUpdate;  // Время последнего обновления
  bool isValid;      // Валидность данных (защита от помех)
};

TemperatureHistory supplyHistory = {{0}, 0, 0, 0, false};
TemperatureHistory returnHistory = {{0}, 0, 0, 0, false};
TemperatureHistory boilerHistory = {{0}, 0, 0, 0, false};
TemperatureHistory outdoorHistory = {{0}, 0, 0, 0, false};
TemperatureHistory homeHistory = {{0}, 0, 0, 0, false};

const float TEMP_CHANGE_THRESHOLD = 0.01;  // Минимальное изменение для определения тренда (°C)
const float TEMP_NOISE_THRESHOLD = 5.0;  // Максимальное изменение за один шаг (защита от помех)
const unsigned long TEMP_UPDATE_INTERVAL = 3000;  // Интервал обновления истории (3 сек для сбора 10 значений за 30 секунд)
const int TEMP_HISTORY_SIZE = 20;  // Размер истории (увеличено с 10 до 20 для более точного анализа)
const int TEMP_TREND_SAMPLES = 10;  // Количество значений для анализа тренда (увеличено с 5 до 10)

// Константы для защиты от застоя насоса
const unsigned long PUMP_ANTI_STAGNATION_INTERVAL = 30 * 60 * 1000;  // 30 минут в миллисекундах
const unsigned long PUMP_ANTI_STAGNATION_DURATION = 2 * 60 * 1000;   // 2 минуты в миллисекундах

// Константы для обнаружения прогорания угля
const unsigned long COAL_BURNED_CHECK_TIME = 10 * 60 * 1000;  // 10 минут в миллисекундах
bool fanState = false;
bool pumpState = false;
bool systemEnabled = true;  // Флаг включения/выключения системы
String systemState = "IDLE";
int workMode = 0;  // 0 = Авто, 1 = Комфорт

// Защита от частых переключений вентилятора
unsigned long lastFanToggleTime = 0;  // Время последнего переключения вентилятора
const unsigned long FAN_TOGGLE_MIN_INTERVAL_MS = 10000;  // Минимальный интервал между переключениями (10 секунд)
float lastFanToggleTemp = 0;  // Температура при последнем переключении
const float FAN_TOGGLE_MIN_TEMP_DELTA = 0.3;  // Минимальное изменение температуры для переключения (0.3°C)

// Инженерное управление реле
bool manualFanControl = false;  // Ручное управление вентилятором
bool manualPumpControl = false;  // Ручное управление насосом
bool sensorsRelayState = true;  // Состояние реле датчиков (true = включено, false = выключено)
bool sensorsResetPending = false;  // Флаг ожидания автоматического включения реле датчиков
unsigned long sensorsResetStartTime = 0;  // Время начала ожидания сброса
const unsigned long SENSORS_RESET_DELAY = 3000;  // Задержка перед автоматическим включением (3 секунды)

// Переменные для асинхронного сканирования WiFi
bool wifiScanInProgress = false;
unsigned long wifiScanStartTime = 0;
int wifiScanResult = 0;
unsigned long lastManualControlTime = 0;  // Время последнего ручного управления
const unsigned long MANUAL_CONTROL_TIMEOUT = 2 * 60 * 1000;  // 2 минуты в миллисекундах

// Автоматический сброс питания датчиков при отсутствии обнаружения
unsigned long lastSensorsDetectedTime = 0;  // Время последнего успешного обнаружения датчиков
const unsigned long SENSORS_AUTO_RESET_TIMEOUT = 60 * 1000;  // 60 секунд в миллисекундах
bool sensorsAutoResetInProgress = false;  // Флаг активного автоматического сброса
unsigned long sensorsAutoResetStartTime = 0;  // Время начала автоматического сброса

// Отслеживание зависания датчиков (показания 0 или 85 градусов)
unsigned long lastValidSupplyTempTime = 0;  // Время последнего валидного показания подачи
unsigned long lastValidReturnTempTime = 0;  // Время последнего валидного показания обратки
unsigned long lastValidBoilerTempTime = 0;  // Время последнего валидного показания котельной
unsigned long lastValidOutdoorTempTime = 0;  // Время последнего валидного показания улицы
const unsigned long SENSORS_FREEZE_TIMEOUT = 60 * 1000;  // 60 секунд для зависания датчика

// Переменные для асинхронного чтения температур DS18B20
bool tempRequestPending = false;  // Флаг ожидания конвертации температуры
unsigned long tempRequestTime = 0;  // Время запроса температуры
const unsigned long TEMP_CONVERSION_DELAY = 800;  // Задержка для конвертации DS18B20 (мс)

// Переменные для подброса угля
bool coalFeedingActive = false;  // Флаг активного подброса угля
unsigned long coalFeedingStartTime = 0;  // Время начала подброса угля
const unsigned long COAL_FEEDING_DURATION = 10 * 60 * 1000;  // 10 минут в миллисекундах
bool fanStateBeforeCoalFeeding = false;  // Состояние вентилятора до подброса угля

// Переменная для отслеживания времени начала разогрева
unsigned long heatingStartTime = 0;  // Время начала разогрева (для проверки таймаута)

// Переменные для защиты от застоя насоса
unsigned long lastPumpRunTime = 0;  // Время последнего запуска насоса

// Переменные для защиты от разгона без наддува
// Переменные для обнаружения прогорания угля
unsigned long coalBurnedCheckStart = 0;  // Время начала отслеживания падения температуры

// Переменные для обнаружения погасания котла
unsigned long fanStartTime = 0;  // Время начала работы вентилятора
float maxTempDuringFan = 0.0;   // Максимальная температура за время работы вентилятора
bool boilerExtinguished = false; // Флаг погасания котла
const unsigned long BOILER_EXTINGUISHED_CHECK_TIME = 60 * 60 * 1000;  // 60 минут в миллисекундах
const float BOILER_EXTINGUISHED_TEMP_DROP = 5.0;  // Падение температуры на 5°C для определения погасания

// Переменные для контроля розжига
bool ignitionInProgress = false;  // Флаг активного розжига
unsigned long ignitionStartTime = 0;  // Время начала розжига
float ignitionStartTemp = 0.0;  // Температура при начале розжига
const unsigned long IGNITION_TIMEOUT_MIN = 10 * 60 * 1000;  // 10 минут в миллисекундах (минимальный таймаут)
const unsigned long IGNITION_TIMEOUT_MAX = 20 * 60 * 1000;  // 20 минут в миллисекундах (максимальный таймаут)
const float IGNITION_TEMP_INCREASE = 2.0;  // Минимальное повышение температуры для успешного розжига (°C)

// Структура для логирования событий
struct EventLogEntry {
  uint32_t timestamp;  // Unix timestamp
  char eventType[20];  // Тип события: "BOILER_EXTINGUISHED", "IGNITION_STARTED", "IGNITION_FAILED", "IGNITION_SUCCESS"
  char details[50];     // Дополнительная информация
  bool valid;           // Флаг валидности записи
};

const int EVENT_LOG_MAX_ENTRIES = 30;  // Максимум 30 записей событий
EventLogEntry eventLog[EVENT_LOG_MAX_ENTRIES];
uint8_t eventLogWriteIndex = 0;  // Индекс для записи следующей записи
const int EVENT_LOG_ENTRY_SIZE = 80;  // Размер одной записи (timestamp + eventType + details + valid)

// Статистика работы
struct FanStatistics {
  unsigned long totalWorkTime = 0;  // Общее время работы вентилятора (миллисекунды)
  unsigned long dailyWorkTime = 0;  // Время работы за сегодня (миллисекунды)
  unsigned long lastDayReset = 0;   // Время последнего сброса дневной статистики
  int cycleCount = 0;               // Количество циклов включения/выключения
  int dailyCycleCount = 0;          // Количество циклов за сегодня
} fanStats;

// Флаг для отложенного сохранения в EEPROM
bool autoSettingsDirty = false;
unsigned long lastAutoSettingsChange = 0;

// Глобальная переменная для отложенной перезагрузки
unsigned long pendingRebootTime = 0;

// Счетчик перезагрузок
uint32_t bootCount = 0;
String lastResetReason = "";

// Структура записи журнала перезагрузок
struct BootLogEntry {
  uint32_t bootCount;
  uint32_t timestamp;  // Unix timestamp
  char reason[20];      // Причина перезагрузки
  bool valid;           // Флаг валидности записи
};

BootLogEntry bootLog[BOOT_LOG_MAX_ENTRIES];
uint8_t bootLogWriteIndex = 0;  // Индекс для записи следующей записи

// Переменные для вычисления загрузки CPU
unsigned long loopStartTime = 0;
unsigned long totalLoopTime = 0;  // Суммарное время выполнения loop() за период обновления
unsigned long loopCount = 0;  // Количество выполнений loop() за период обновления
float cpuLoad = 0.0;  // Загрузка CPU в процентах
unsigned long lastCpuUpdate = 0;
const unsigned long CPU_UPDATE_INTERVAL = 1000;  // Обновление загрузки CPU раз в секунду

// Настройки MQTT (по умолчанию включен)
struct MqttSettings {
  bool enabled = true;  // По умолчанию включен
  String server = "m5.wqtt.ru";
  int port = 5374;
  bool useTLS = false;
  String user = "u_OLTTB0";
  String password = "XDzCIXr0";
  String prefix = "kotel/device1";
  int tempInterval = 10;
  int stateInterval = 30;
} mqttSettings;

// Настройки Авто (заглушки)
struct AutoSettings {
  float setpoint = 60.0;
  float minTemp = 45.0;
  float maxTemp = 75.0;
  float hysteresis = 2.0;
  float inertiaTemp = 55.0;
  int inertiaTime = 10;
  float overheatTemp = 77.0;
  int heatingTimeout = 30;
} autoSettings;

// Настройки режима "Комфорт"
struct ComfortSettings {
  float targetHomeTemp = 24.0;  // Целевая температура в доме (°C)
  float minBoilerTemp = 45.0;  // Минимальная температура котла (°C)
  float maxBoilerTemp = 75.0;  // Максимальная температура котла (°C)
  float waitTemp = 65.0;  // Температура ожидания (°C)
  float catchUpTemp = 23.5;  // Догон до (°C) - начало снижения наддува
  int waitCoolingTime = 10;  // Время ожидания на промежуточной температуре (минуты)
  int waitAfterHeating1Time = 20;  // Время ожидания после первого разогрева (минуты)
  int waitAfterReductionTime = 25;  // Время ожидания после снижения наддува (минуты)
  int inertiaCheckInterval = 5;  // Интервал проверки инерции (минуты)
  float hysteresisOn = 0.5;  // Гистерезис включения (°C)
  float hysteresisOff = 0.3;  // Гистерезис выключения (°C)
  float hysteresisBoiler = 2.0;  // Гистерезис температуры котла (°C)
  float warningTemp = 85.0;  // Температура предупреждения (°C)
} comfortSettings;

// Переменные для режима "Комфорт"
String comfortState = "WAIT";  // Состояния: WAIT, HEATING_1, WAIT_COOLING, WAIT_HEATING, HEATING_2, COMFORT, MAINTAIN
unsigned long comfortStateStartTime = 0;  // Время входа в текущее состояние
float homeTempAtStateStart = 0.0;  // Температура дома при входе в состояние

// Настройки WiFi
struct WiFiSettings {
  String primarySSID = "";
  String primaryPassword = "";
  String backupSSID = "";
  String backupPassword = "";
  bool useBackup = false;  // Использовать резервный WiFi
  bool antennaTuning = false;  // Режим настройки антенны
} wifiSettings;

// Настройки NTP
struct NTPSettings {
  bool enabled = true;
  String server = "ru.pool.ntp.org";  // Российский NTP сервер
  int timezone = 3;  // Часовой пояс (UTC+3 для Москвы)
  int updateInterval = 3600;  // Интервал обновления в секундах (1 час)
} ntpSettings;

// Настройки ML (Machine Learning) для обучения модели
struct MLSettings {
  bool enabled = true;  // По умолчанию включено
  int publishInterval = 10;  // Интервал публикации в секундах (по умолчанию 10 сек)
} mlSettings;

// Настройки реле (инженерные)
struct RelaySettings {
  bool fanOffIsLow = true;   // Выключено = LOW (true) или HIGH (false)
  bool pumpOffIsLow = true;  // Выключено = LOW (true) или HIGH (false)
  bool sensorsOffIsLow = true;  // Выключено = LOW (true) или HIGH (false) - обратная логика вентилятора
} relaySettings;

// Настройки обновлений через GitHub
struct UpdateSettings {
  bool autoCheckEnabled = false;  // Автоматическая проверка обновлений
  unsigned long checkInterval = 86400000;  // Интервал проверки в миллисекундах (24 часа)
  unsigned long lastCheckTime = 0;  // Время последней проверки
} updateSettings;

// Переменные для отслеживания прогресса обновления
struct UpdateProgress {
  bool isUpdating = false;  // Флаг активного обновления
  String stage = "";  // Этап обновления: "firmware", "spiffs"
  int percent = 0;  // Процент выполнения (0-100)
  String message = "";  // Сообщение о статусе
  unsigned long startTime = 0;  // Время начала обновления
  unsigned long bytesDownloaded = 0;  // Загружено байт
  unsigned long totalBytes = 0;  // Всего байт
  float speedKBps = 0.0;  // Скорость загрузки в KB/s
} updateProgress;

// Структура для привязки датчиков
struct SensorMapping {
  String supply = "";
  String return_sensor = "";
  String boiler = "";
  String outside = "";
} sensorMapping;

// Forward declarations
void startCoalFeeding();
void stopCoalFeeding();
void checkCoalFeeding();
int getCoalFeedingRemainingSeconds();
void updateDisplay();
void setupOTA();
void updateTemperatures();
float getTemperatureByAddress(String address);
void saveWiFiSettingsToEEPROM();
void loadWiFiSettingsFromEEPROM();
void saveUpdateSettingsToEEPROM();
void loadUpdateSettingsFromEEPROM();
bool connectToWiFi();
void saveMLSettingsToEEPROM();
void loadMLSettingsFromEEPROM();
void publishMqttML();
void syncRelays();  // Синхронизация состояния реле с переменными
void saveRelaySettingsToEEPROM();
void loadRelaySettingsFromEEPROM();
void saveComfortSettingsToEEPROM();
void loadComfortSettingsFromEEPROM();
void saveWorkModeToEEPROM();
void loadWorkModeFromEEPROM();
void logEvent(const char* eventType, const char* details);
void saveEventLogToEEPROM();
void loadEventLogFromEEPROM();
void saveFanStatsToEEPROM();
void loadFanStatsFromEEPROM();
void startIgnition();
void checkBoilerExtinguished(unsigned long now);
void checkIgnitionProgress(unsigned long now);

// Функция обработки прерывания энкодера с улучшенной фильтрацией дребезга
void IRAM_ATTR encoderISR() {
  // Защита от слишком частых прерываний (дребезг)
  unsigned long nowMicros = micros();
  if (nowMicros - lastEncoderISRCallTime < ENCODER_ISR_DEBOUNCE_US) {
    return; // Игнорируем слишком частые прерывания
  }
  lastEncoderISRCallTime = nowMicros;
  
  encoderISRCount++; // Увеличиваем счетчик прерываний
  
  int clkState = digitalRead(PIN_ENCODER_CLK);
  int dtState = digitalRead(PIN_ENCODER_DT);
  int currentState = (clkState << 1) | dtState;
  
  // Обновляем lastEncoderState для отслеживания изменений
  lastEncoderState = currentState;
  
  // Фильтрация: обрабатываем только валидные переходы состояний
  // Валидные переходы для quadrature энкодера:
  // 00 -> 01 -> 11 -> 10 -> 00 (по часовой)
  // 00 -> 10 -> 11 -> 01 -> 00 (против часовой)
  
  int lastValid = lastValidEncoderState;
  int current = currentState;
  
  // Проверяем валидность перехода
  bool validTransition = false;
  int direction = 0; // 1 = вправо, -1 = влево
  
  // Валидные переходы для поворота вправо (по часовой) - поменяно местами
  if ((lastValid == 0b00 && current == 0b01) ||
      (lastValid == 0b01 && current == 0b11) ||
      (lastValid == 0b11 && current == 0b10) ||
      (lastValid == 0b10 && current == 0b00)) {
    validTransition = true;
    direction = -1; // Поменяно: было 1, стало -1
  }
  // Валидные переходы для поворота влево (против часовой) - поменяно местами
  else if ((lastValid == 0b00 && current == 0b10) ||
           (lastValid == 0b10 && current == 0b11) ||
           (lastValid == 0b11 && current == 0b01) ||
           (lastValid == 0b01 && current == 0b00)) {
    validTransition = true;
    direction = 1; // Поменяно: было -1, стало 1
  }
  
  // Если переход валидный, обновляем позицию
  if (validTransition) {
    encoderPosition += direction;
    lastValidEncoderState = current;
    unsigned long now = millis();
    lastEncoderChange = now;
    lastEncoderISRTime = now;
  }
  // Если переход невалидный, но состояние стабильное (не дребезг), обновляем lastValid
  else if (current == 0b00 || current == 0b11) {
    // Стабильные состояния - можно обновить lastValid для восстановления после пропусков
    lastValidEncoderState = current;
  }
}

// Обновление дисплея OLED
void updateDisplay() {
  u8g2.clearBuffer();
  
  // Температура подачи (крупным шрифтом)
  u8g2.setFont(u8g2_font_ncenB18_tr);
  char tempStr[16];
  if (supplyTemp > 0) {
    snprintf(tempStr, sizeof(tempStr), "%.1f°C", supplyTemp);
  } else {
    strcpy(tempStr, "--°C");
  }
  u8g2.drawStr(0, 25, tempStr);
  
  // Уставка (меньшим шрифтом)
  u8g2.setFont(u8g2_font_ncenB10_tr);
  char setpointStr[16];
  snprintf(setpointStr, sizeof(setpointStr), "Уст: %.1f°C", setpoint);
  u8g2.drawStr(0, 40, setpointStr);
  
  // Статус подброса угля (если активен) или текущее время или состояние погасания/розжига
  if (boilerExtinguished || systemState == "КОТЕЛ_ПОГАС") {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 55, "КОТЕЛ ПОГАС");
  } else if (ignitionInProgress || systemState == "РОЗЖИГ") {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    unsigned long elapsed = (millis() >= ignitionStartTime) ? (millis() - ignitionStartTime) : (ULONG_MAX - ignitionStartTime + millis());
    char ignitionStr[20];
    snprintf(ignitionStr, sizeof(ignitionStr), "РОЗЖИГ %luм", elapsed / 60000);
    u8g2.drawStr(0, 55, ignitionStr);
  } else if (systemState == "ОШИБКА_РОЗЖИГА") {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 55, "ОШИБКА РОЗЖИГА");
  } else if (coalFeedingActive) {
    int remaining = getCoalFeedingRemainingSeconds(); // Оставшееся время
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    char coalStr[20];
    snprintf(coalStr, sizeof(coalStr), "Уголь: %02d:%02d", minutes, seconds);
    u8g2.drawStr(0, 55, coalStr);
  } else {
    // Показываем текущее время с секундами, если нет подброса угля
    if (ntpSettings.enabled && timeClient.isTimeSet()) {
      timeClient.update();
      unsigned long epochTime = timeClient.getEpochTime();
      time_t rawTime = epochTime;
      struct tm *timeinfo = localtime(&rawTime);
      if (timeinfo != NULL) {
        char timeStr[20];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        u8g2.drawStr(0, 55, timeStr);
      }
    }
  }
  // Загрузка CPU убрана с дисплея
  
  // Нижняя строка: состояние системы и активный таймер
  u8g2.setFont(u8g2_font_6x10_tr);
  String bottomLine = "";
  unsigned long now = millis();
  
  // Определяем активный таймер для отображения
  if (ignitionInProgress && ignitionStartTime > 0) {
    unsigned long elapsed = (now >= ignitionStartTime) ? (now - ignitionStartTime) : (ULONG_MAX - ignitionStartTime + now);
    unsigned long minutes = elapsed / 60000;
    unsigned long seconds = (elapsed % 60000) / 1000;
    char timerStr[16];
    snprintf(timerStr, sizeof(timerStr), "Розж:%lu:%02lu", minutes, seconds);
    bottomLine = String(timerStr);
  } else if (coalFeedingActive && coalFeedingStartTime > 0) {
    unsigned long elapsed = (now >= coalFeedingStartTime) ? (now - coalFeedingStartTime) : (ULONG_MAX - coalFeedingStartTime + now);
    unsigned long remaining = (COAL_FEEDING_DURATION > elapsed) ? (COAL_FEEDING_DURATION - elapsed) : 0;
    unsigned long minutes = remaining / 60000;
    unsigned long seconds = (remaining % 60000) / 1000;
    char timerStr[16];
    snprintf(timerStr, sizeof(timerStr), "Уголь:%lu:%02lu", minutes, seconds);
    bottomLine = String(timerStr);
  } else if (fanStartTime > 0 && fanState) {
    unsigned long elapsed = (now >= fanStartTime) ? (now - fanStartTime) : (ULONG_MAX - fanStartTime + now);
    unsigned long minutes = elapsed / 60000;
    unsigned long seconds = (elapsed % 60000) / 1000;
    char timerStr[16];
    snprintf(timerStr, sizeof(timerStr), "Вент:%lu:%02lu", minutes, seconds);
    bottomLine = String(timerStr);
  } else {
    // Показываем состояние системы
    if (systemState.length() > 0) {
      // Сокращаем длинные состояния
      String stateDisplay = systemState;
      if (stateDisplay == "КОТЕЛ_ПОГАС") stateDisplay = "ПОГАС";
      else if (stateDisplay == "ОШИБКА_РОЗЖИГА") stateDisplay = "ОШИБКА";
      else if (stateDisplay == "HEATING_TIMEOUT") stateDisplay = "ТАЙМАУТ";
      else if (stateDisplay == "COAL_BURNED") stateDisplay = "ПРОГОРЕЛ";
      else if (stateDisplay == "HIGH_TEMP") stateDisplay = "ВЫСОКАЯ";
      bottomLine = stateDisplay;
    } else {
      bottomLine = systemEnabled ? "РАБОТА" : "СТОП";
    }
  }
  
  // Обрезаем строку если слишком длинная (максимум 16 символов для дисплея)
  if (bottomLine.length() > 16) {
    bottomLine = bottomLine.substring(0, 16);
  }
  
  u8g2.drawStr(0, 64, bottomLine.c_str());
  
  u8g2.sendBuffer();
}

// Функция получения температуры по адресу датчика (проверяет обе шины)
float getTemperatureByAddress(String address) {
  if (address.length() != 16) {
    return DEVICE_DISCONNECTED_C;
  }
  
  DeviceAddress deviceAddress;
  // Преобразуем строку адреса в массив байт
  for (uint8_t i = 0; i < 8; i++) {
    String byteStr = address.substring(i * 2, i * 2 + 2);
    deviceAddress[i] = strtol(byteStr.c_str(), NULL, 16);
  }
  
  // Проверяем первую шину
  if (sensors1.validAddress(deviceAddress)) {
    float temp = sensors1.getTempC(deviceAddress);
    if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
      return temp;
    }
  }
  
  // Проверяем вторую шину
  if (sensors2.validAddress(deviceAddress)) {
    float temp = sensors2.getTempC(deviceAddress);
    if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
      return temp;
    }
  }
  
  return DEVICE_DISCONNECTED_C;
}

// Функция добавления значения в историю с защитой от помех
void addToHistory(TemperatureHistory* history, float newValue) {
  unsigned long now = millis();
  
  // Проверка на валидность значения
  if (newValue < -50.0 || newValue > 150.0) {
    // Недопустимое значение - помеха
    history->isValid = false;
    return;
  }
  
  // Защита от резких скачков (помехи)
  if (history->count > 0) {
    float lastValue = history->values[(history->index - 1 + TEMP_HISTORY_SIZE) % TEMP_HISTORY_SIZE];
    float diff = abs(newValue - lastValue);
    
    if (diff > TEMP_NOISE_THRESHOLD) {
      // Слишком большой скачок - вероятно помеха
      // Для датчика обратки делаем более мягкую проверку (он часто отваливается)
      if (history != &returnHistory || diff > TEMP_NOISE_THRESHOLD * 2) {
        history->isValid = false;
        return;
      }
    }
  }
  
  // Добавляем значение в историю
  history->values[history->index] = newValue;
  history->index = (history->index + 1) % TEMP_HISTORY_SIZE;
  if (history->count < TEMP_HISTORY_SIZE) {
    history->count++;
  }
  history->lastUpdate = now;
  history->isValid = true;
}

// Функция определения тренда температуры
// Возвращает: 1 = рост, 0 = стабильно, -1 = падение
int getTemperatureTrend(TemperatureHistory* history) {
  if (history->count < TEMP_TREND_SAMPLES || !history->isValid) {
    return 0; // Недостаточно данных или невалидные данные
  }
  
  // Берем последние TEMP_TREND_SAMPLES значений для анализа
  int samples = min(TEMP_TREND_SAMPLES, history->count);
  float firstHalf = 0, secondHalf = 0;
  
  // Первая половина (старые значения) - первые samples/2 значений
  int firstCount = samples / 2;
  for (int i = 0; i < firstCount; i++) {
    int idx = (history->index - samples + i + TEMP_HISTORY_SIZE) % TEMP_HISTORY_SIZE;
    firstHalf += history->values[idx];
  }
  firstHalf /= firstCount;
  
  // Вторая половина (новые значения) - последние samples/2 значений
  int secondCount = samples - firstCount;
  for (int i = firstCount; i < samples; i++) {
    int idx = (history->index - samples + i + TEMP_HISTORY_SIZE) % TEMP_HISTORY_SIZE;
    secondHalf += history->values[idx];
  }
  secondHalf /= secondCount;
  
  float diff = secondHalf - firstHalf;
  
  if (diff > TEMP_CHANGE_THRESHOLD) {
    return 1; // Рост
  } else if (diff < -TEMP_CHANGE_THRESHOLD) {
    return -1; // Падение
  }
  
  return 0; // Стабильно
}

// Обновление температур с датчиков (работает с двумя шинами) - АСИНХРОННОЕ
void updateTemperatures() {
  unsigned long now = millis();
  
  // Если запрос еще не отправлен, отправляем его
  if (!tempRequestPending) {
    // Запрашиваем температуру с обеих шин
    if (sensors1.getDeviceCount() > 0) {
      sensors1.requestTemperatures();
    }
    if (sensors2.getDeviceCount() > 0) {
      sensors2.requestTemperatures();
    }
    
    tempRequestPending = true;
    tempRequestTime = now;
    return; // Выходим, ждем конвертации в следующем цикле
  }
  
  // Если запрос отправлен, проверяем, прошло ли достаточно времени для конвертации
  if (tempRequestPending) {
    // Защита от переполнения millis()
    unsigned long elapsed = (now >= tempRequestTime) ? (now - tempRequestTime) : (ULONG_MAX - tempRequestTime + now);
    
    if (elapsed >= TEMP_CONVERSION_DELAY) {
      // Время конвертации прошло, читаем температуры
      if (sensorMapping.supply.length() == 16) {
        float temp = getTemperatureByAddress(sensorMapping.supply);
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          // Проверка на зависание (0 или 85 градусов), но разрешаем отрицательные температуры
          if ((temp < -0.1 || temp > 0.1) && temp < 84.9) {
            // Валидное показание (включая отрицательные) - обновляем время
            lastValidSupplyTempTime = now;
            supplyTemp = temp;
            addToHistory(&supplyHistory, temp);
          } else {
            // Зависшее показание (0 или 85) - не обновляем температуру, но проверяем таймер
            if (lastValidSupplyTempTime == 0) {
              lastValidSupplyTempTime = now;  // Первое показание - устанавливаем время
            }
          }
        }
      }
      
      if (sensorMapping.return_sensor.length() == 16) {
        float temp = getTemperatureByAddress(sensorMapping.return_sensor);
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          // Проверка на зависание (0 или 85 градусов), но разрешаем отрицательные температуры
          if ((temp < -0.1 || temp > 0.1) && temp < 84.9) {
            // Валидное показание (включая отрицательные) - обновляем время
            lastValidReturnTempTime = now;
            // Для датчика обратки - базовая защита от помех (только очень большие скачки)
            // Принимаем значение если оно первое или изменение не слишком большое
            if (returnTemp == 0.0 || abs(temp - returnTemp) < TEMP_NOISE_THRESHOLD * 2 || abs(temp - returnTemp) < 10.0) {
              returnTemp = temp;
              addToHistory(&returnHistory, temp);
            }
          } else {
            // Зависшее показание (0 или 85) - не обновляем температуру, но проверяем таймер
            if (lastValidReturnTempTime == 0) {
              lastValidReturnTempTime = now;  // Первое показание - устанавливаем время
            }
          }
        }
      }
      
      if (sensorMapping.boiler.length() == 16) {
        float temp = getTemperatureByAddress(sensorMapping.boiler);
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          // Проверка на зависание (0 или 85 градусов), но разрешаем отрицательные температуры
          if ((temp < -0.1 || temp > 0.1) && temp < 84.9) {
            // Валидное показание (включая отрицательные) - обновляем время
            lastValidBoilerTempTime = now;
            boilerTemp = temp;
            addToHistory(&boilerHistory, temp);
          } else {
            // Зависшее показание (0 или 85) - не обновляем температуру, но проверяем таймер
            if (lastValidBoilerTempTime == 0) {
              lastValidBoilerTempTime = now;  // Первое показание - устанавливаем время
            }
          }
        }
      }
      
      if (sensorMapping.outside.length() == 16) {
        float temp = getTemperatureByAddress(sensorMapping.outside);
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          // Для датчика улицы: 0°C - валидное значение (зима), блокируем только 85°C
          if (temp < 84.9) {
            // Валидное показание (включая 0°C) - обновляем время
            lastValidOutdoorTempTime = now;
            outdoorTemp = temp;
            addToHistory(&outdoorHistory, temp);
          } else {
            // Зависшее показание (85°C) - не обновляем температуру, но проверяем таймер
            if (lastValidOutdoorTempTime == 0) {
              lastValidOutdoorTempTime = now;  // Первое показание - устанавливаем время
            }
          }
        }
      }
      
      // Сбрасываем флаг для следующего запроса
      tempRequestPending = false;
    }
  }
}

// Проверка зависания датчиков (0 или 85 градусов) и автоматический сброс питания
void checkSensorsFreeze() {
  unsigned long now = millis();
  
  // Если идет автоматический сброс, не проверяем зависание
  if (sensorsAutoResetInProgress) {
    return;
  }
  
  // Проверяем каждый датчик на зависание
  bool needReset = false;
  String frozenSensor = "";
  
  // Проверка датчика подачи
  if (sensorMapping.supply.length() == 16 && lastValidSupplyTempTime > 0) {
    unsigned long timeSinceValid = (now >= lastValidSupplyTempTime) ? (now - lastValidSupplyTempTime) : (ULONG_MAX - lastValidSupplyTempTime + now);
    if (timeSinceValid >= SENSORS_FREEZE_TIMEOUT) {
      needReset = true;
      frozenSensor = "supply";
      Serial.print("[Зависание датчиков] Датчик подачи завис (0 или 85°C) более 60 секунд, выполняю сброс питания...");
    }
  }
  
  // Проверка датчика обратки
  if (sensorMapping.return_sensor.length() == 16 && lastValidReturnTempTime > 0) {
    unsigned long timeSinceValid = (now >= lastValidReturnTempTime) ? (now - lastValidReturnTempTime) : (ULONG_MAX - lastValidReturnTempTime + now);
    if (timeSinceValid >= SENSORS_FREEZE_TIMEOUT) {
      needReset = true;
      frozenSensor = "return";
      Serial.print("[Зависание датчиков] Датчик обратки завис (0 или 85°C) более 60 секунд, выполняю сброс питания...");
    }
  }
  
  // Проверка датчика котельной
  if (sensorMapping.boiler.length() == 16 && lastValidBoilerTempTime > 0) {
    unsigned long timeSinceValid = (now >= lastValidBoilerTempTime) ? (now - lastValidBoilerTempTime) : (ULONG_MAX - lastValidBoilerTempTime + now);
    if (timeSinceValid >= SENSORS_FREEZE_TIMEOUT) {
      needReset = true;
      frozenSensor = "boiler";
      Serial.print("[Зависание датчиков] Датчик котельной завис (0 или 85°C) более 60 секунд, выполняю сброс питания...");
    }
  }
  
  // Проверка датчика улицы (только 85°C, так как 0°C - валидное значение зимой)
  if (sensorMapping.outside.length() == 16 && lastValidOutdoorTempTime > 0) {
    // Проверяем, что текущее показание действительно 85°C (зависание)
    if (outdoorTemp >= 84.9) {
      unsigned long timeSinceValid = (now >= lastValidOutdoorTempTime) ? (now - lastValidOutdoorTempTime) : (ULONG_MAX - lastValidOutdoorTempTime + now);
      if (timeSinceValid >= SENSORS_FREEZE_TIMEOUT) {
        needReset = true;
        frozenSensor = "outside";
        Serial.print("[Зависание датчиков] Датчик улицы завис (85°C) более 60 секунд, выполняю сброс питания...");
      }
    }
  }
  
  // Если нужно сбросить питание
  if (needReset) {
    Serial.println(frozenSensor);
    sensorsRelayState = false;
    int sensorsLevel = relaySettings.sensorsOffIsLow ? LOW : HIGH;
    digitalWrite(PIN_RELAY_SENSORS, sensorsLevel);
    sensorsAutoResetInProgress = true;
    sensorsAutoResetStartTime = now;
    // Сбрасываем таймеры валидных показаний
    lastValidSupplyTempTime = 0;
    lastValidReturnTempTime = 0;
    lastValidBoilerTempTime = 0;
    lastValidOutdoorTempTime = 0;
  }
}

// Проверка обнаружения датчиков и автоматический сброс при отсутствии
void checkSensorsDetection() {
  unsigned long now = millis();
  
  // Если идет автоматический сброс, обрабатываем его
  if (sensorsAutoResetInProgress) {
    unsigned long elapsed = (now >= sensorsAutoResetStartTime) ? (now - sensorsAutoResetStartTime) : (ULONG_MAX - sensorsAutoResetStartTime + now);
    
    if (elapsed >= SENSORS_RESET_DELAY) {
      // Время сброса прошло, включаем реле обратно
      sensorsRelayState = true;
      digitalWrite(PIN_RELAY_SENSORS, HIGH);
      sensorsAutoResetInProgress = false;
      lastSensorsDetectedTime = now;  // Сбрасываем таймер после сброса
      Serial.println("[Авто-сброс датчиков] Реле включено после автоматического сброса");
    }
    return;  // Во время сброса не проверяем обнаружение
  }
  
  // Проверяем обнаружение датчиков (периодически, не каждый цикл)
  static unsigned long lastDetectionCheck = 0;
  if (now - lastDetectionCheck > 5000 || now < lastDetectionCheck) {  // Проверяем каждые 5 секунд
    lastDetectionCheck = now;
    
    // Инициализируем шины для проверки
    sensors1.begin();
    sensors2.begin();
    
    int count1 = sensors1.getDeviceCount();
    int count2 = sensors2.getDeviceCount();
    int totalCount = count1 + count2;
    
    if (totalCount > 0) {
      // Датчики обнаружены - обновляем время последнего обнаружения
      if (lastSensorsDetectedTime == 0 || (now - lastSensorsDetectedTime > 1000 || now < lastSensorsDetectedTime)) {
        lastSensorsDetectedTime = now;
      }
    } else {
      // Датчики не обнаружены - проверяем таймаут
      if (lastSensorsDetectedTime > 0) {
        unsigned long timeSinceDetection = (now >= lastSensorsDetectedTime) ? (now - lastSensorsDetectedTime) : (ULONG_MAX - lastSensorsDetectedTime + now);
        
        if (timeSinceDetection >= SENSORS_AUTO_RESET_TIMEOUT) {
          // Прошло 60 секунд без обнаружения - выполняем автоматический сброс
          Serial.println("[Авто-сброс датчиков] Датчики не обнаружены 60 секунд, выполняю сброс питания...");
          sensorsRelayState = false;
          int sensorsLevel = relaySettings.sensorsOffIsLow ? LOW : HIGH;
          digitalWrite(PIN_RELAY_SENSORS, sensorsLevel);
          sensorsAutoResetInProgress = true;
          sensorsAutoResetStartTime = now;
        }
      } else {
        // Если lastSensorsDetectedTime == 0, значит это первая проверка - устанавливаем время
        lastSensorsDetectedTime = now;
      }
    }
  }
}

// Функции работы с EEPROM
void saveAutoSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_ADDR_MAGIC, (uint8_t)EEPROM_MAGIC);
  
  // Сохранение через JSON для надежности
  String json;
  DynamicJsonDocument doc(512);
  doc["setpoint"] = autoSettings.setpoint;
  doc["minTemp"] = autoSettings.minTemp;
  doc["maxTemp"] = autoSettings.maxTemp;
  doc["hysteresis"] = autoSettings.hysteresis;
  doc["inertiaTemp"] = autoSettings.inertiaTemp;
  doc["inertiaTime"] = autoSettings.inertiaTime;
  doc["overheatTemp"] = autoSettings.overheatTemp;
  doc["heatingTimeout"] = autoSettings.heatingTimeout;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_AUTO, len);
  for (int i = 0; i < len && i < 200; i++) {
    EEPROM.write(EEPROM_ADDR_AUTO + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadAutoSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_AUTO, len);
    if (len > 0 && len < 250) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_AUTO + 4 + i);
      }
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, json);
      
      if (error == DeserializationError::Ok) {
        // Загрузка значений с проверкой валидности
        if (doc.containsKey("setpoint")) {
          float val = doc["setpoint"];
          if (val >= 40 && val <= 80) autoSettings.setpoint = val;
        }
        if (doc.containsKey("minTemp")) {
          float val = doc["minTemp"];
          if (val >= 30 && val <= 60) autoSettings.minTemp = val;
        }
        if (doc.containsKey("maxTemp")) {
          float val = doc["maxTemp"];
          if (val >= 60 && val <= 90) autoSettings.maxTemp = val;
        }
        if (doc.containsKey("hysteresis")) {
          float val = doc["hysteresis"];
          if (val >= 0.5 && val <= 10) autoSettings.hysteresis = val;
        }
        if (doc.containsKey("inertiaTemp")) {
          float val = doc["inertiaTemp"];
          if (val >= 40 && val <= 70) autoSettings.inertiaTemp = val;
        }
        if (doc.containsKey("inertiaTime")) {
          int val = doc["inertiaTime"];
          if (val >= 1 && val <= 60) autoSettings.inertiaTime = val;
        }
        if (doc.containsKey("overheatTemp")) {
          float val = doc["overheatTemp"];
          if (val >= 70 && val <= 90) autoSettings.overheatTemp = val;
        }
        if (doc.containsKey("heatingTimeout")) {
          int val = doc["heatingTimeout"];
          if (val >= 10 && val <= 120) autoSettings.heatingTimeout = val;
        }
        
        setpoint = autoSettings.setpoint;
      } else {
        saveAutoSettingsToEEPROM();
      }
    } else {
      saveAutoSettingsToEEPROM();
    }
  } else {
    Serial.println("EEPROM empty, using defaults");
    saveAutoSettingsToEEPROM();
  }
  EEPROM.end();
}

void saveMqttSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String json;
  DynamicJsonDocument doc(512);
  doc["enabled"] = mqttSettings.enabled;
  doc["server"] = mqttSettings.server;
  doc["port"] = mqttSettings.port;
  doc["useTLS"] = mqttSettings.useTLS;
  doc["user"] = mqttSettings.user;
  doc["password"] = mqttSettings.password;
  doc["prefix"] = mqttSettings.prefix;
  doc["tempInterval"] = mqttSettings.tempInterval;
  doc["stateInterval"] = mqttSettings.stateInterval;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_MQTT, len);
  for (int i = 0; i < len && i < 180; i++) {
    EEPROM.write(EEPROM_ADDR_MQTT + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadMqttSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_MQTT, len);
    if (len > 0 && len < 200) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_MQTT + 4 + i);
      }
      DynamicJsonDocument doc(512);
      deserializeJson(doc, json);
      if (doc.containsKey("enabled")) mqttSettings.enabled = doc["enabled"];
      if (doc.containsKey("server")) mqttSettings.server = doc["server"].as<String>();
      if (doc.containsKey("port")) mqttSettings.port = doc["port"];
      if (doc.containsKey("useTLS")) mqttSettings.useTLS = doc["useTLS"];
      if (doc.containsKey("user")) mqttSettings.user = doc["user"].as<String>();
      if (doc.containsKey("password")) mqttSettings.password = doc["password"].as<String>();
      if (doc.containsKey("prefix")) mqttSettings.prefix = doc["prefix"].as<String>();
      if (doc.containsKey("tempInterval")) mqttSettings.tempInterval = doc["tempInterval"];
      if (doc.containsKey("stateInterval")) mqttSettings.stateInterval = doc["stateInterval"];
    }
  }
  EEPROM.end();
}

void saveSensorMappingToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String json;
  DynamicJsonDocument doc(256);
  doc["supply"] = sensorMapping.supply;
  doc["return"] = sensorMapping.return_sensor;
  doc["boiler"] = sensorMapping.boiler;
  doc["outside"] = sensorMapping.outside;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_SENSORS, len);
  for (int i = 0; i < len && i < 200; i++) {
    EEPROM.write(EEPROM_ADDR_SENSORS + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Sensor mapping saved to EEPROM");
}

void loadSensorMappingFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_SENSORS, len);
    if (len > 0 && len < 250) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_SENSORS + 4 + i);
      }
      DynamicJsonDocument doc(256);
      deserializeJson(doc, json);
      if (doc.containsKey("supply")) sensorMapping.supply = doc["supply"].as<String>();
      if (doc.containsKey("return")) sensorMapping.return_sensor = doc["return"].as<String>();
      if (doc.containsKey("boiler")) sensorMapping.boiler = doc["boiler"].as<String>();
      if (doc.containsKey("outside")) sensorMapping.outside = doc["outside"].as<String>();
      Serial.println("Sensor mapping loaded from EEPROM");
    }
  }
  EEPROM.end();
}

void saveSystemEnabledToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_ADDR_SYSTEM, systemEnabled);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("System enabled saved to EEPROM");
}

void loadSystemEnabledFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_ADDR_SYSTEM, systemEnabled);
    Serial.print("System enabled loaded from EEPROM: ");
    Serial.println(systemEnabled);
  } else {
    systemEnabled = true;  // По умолчанию включена
    saveSystemEnabledToEEPROM();
  }
  EEPROM.end();
}

// Функции работы с настройками WiFi
void saveWiFiSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String json;
  DynamicJsonDocument doc(512);
  doc["primarySSID"] = wifiSettings.primarySSID;
  doc["primaryPassword"] = wifiSettings.primaryPassword;
  doc["backupSSID"] = wifiSettings.backupSSID;
  doc["backupPassword"] = wifiSettings.backupPassword;
  doc["useBackup"] = wifiSettings.useBackup;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_WIFI, len);
  for (int i = 0; i < len && i < 200; i++) {
    EEPROM.write(EEPROM_ADDR_WIFI + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadWiFiSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_WIFI, len);
    if (len > 0 && len < 250) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_WIFI + 4 + i);
      }
      DynamicJsonDocument doc(512);
      deserializeJson(doc, json);
      if (doc.containsKey("primarySSID")) wifiSettings.primarySSID = doc["primarySSID"].as<String>();
      if (doc.containsKey("primaryPassword")) wifiSettings.primaryPassword = doc["primaryPassword"].as<String>();
      if (doc.containsKey("backupSSID")) wifiSettings.backupSSID = doc["backupSSID"].as<String>();
      if (doc.containsKey("backupPassword")) wifiSettings.backupPassword = doc["backupPassword"].as<String>();
      if (doc.containsKey("useBackup")) wifiSettings.useBackup = doc["useBackup"];
      if (doc.containsKey("antennaTuning")) wifiSettings.antennaTuning = doc["antennaTuning"];
    }
  }
  EEPROM.end();
}

// Функция подключения к WiFi с приоритетом
bool connectToWiFi() {
  // Сначала пытаемся подключиться к основному WiFi (даже при слабом сигнале)
  if (wifiSettings.primarySSID.length() > 0) {
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSettings.primarySSID.c_str(), wifiSettings.primaryPassword.c_str());
    
    // Увеличенное количество попыток и таймаут для надежности
    int attempts = 0;
    int maxAttempts = 30;  // 30 попыток по 1 секунде = 30 секунд
    unsigned long lastCheckTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
      unsigned long now = millis();
      // Неблокирующая задержка 1 секунда с yield()
      if (now - lastCheckTime >= 1000 || now < lastCheckTime) {
        lastCheckTime = now;
        Serial.print(".");
        attempts++;
        
        // Периодически проверяем статус
        if (attempts % 5 == 0) {
          Serial.print(" [");
          Serial.print(attempts);
          Serial.print("/");
          Serial.print(maxAttempts);
          Serial.println("]");
        }
      }
      yield(); // Позволяем другим задачам выполняться
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    } else {
    }
  }
  
  // Если основной не подключился, пробуем резервный
  if (wifiSettings.backupSSID.length() > 0) {
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSettings.backupSSID.c_str(), wifiSettings.backupPassword.c_str());
    
    int attempts = 0;
    int maxAttempts = 20;  // 20 секунд для резервного
    unsigned long lastCheckTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
      unsigned long now = millis();
      // Неблокирующая задержка 1 секунда с yield()
      if (now - lastCheckTime >= 1000 || now < lastCheckTime) {
        lastCheckTime = now;
        Serial.print(".");
        attempts++;
      }
      yield(); // Позволяем другим задачам выполняться
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nПодключено к резервному WiFi!");
      Serial.print("IP адрес: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
    }
  }
  
  return false;
}

// Функции работы с настройками NTP
void saveNTPSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String json;
  DynamicJsonDocument doc(256);
  doc["enabled"] = ntpSettings.enabled;
  doc["server"] = ntpSettings.server;
  doc["timezone"] = ntpSettings.timezone;
  doc["updateInterval"] = ntpSettings.updateInterval;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_NTP, len);
  for (int i = 0; i < len && i < 150; i++) {
    EEPROM.write(EEPROM_ADDR_NTP + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadNTPSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_NTP, len);
    if (len > 0 && len < 200) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_NTP + 4 + i);
      }
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, json);
      if (!error) {
        if (doc.containsKey("enabled")) ntpSettings.enabled = doc["enabled"];
        if (doc.containsKey("server")) ntpSettings.server = doc["server"].as<String>();
        if (doc.containsKey("timezone")) ntpSettings.timezone = doc["timezone"];
        if (doc.containsKey("updateInterval")) ntpSettings.updateInterval = doc["updateInterval"];
      } else {
        Serial.println("Error parsing NTP settings from EEPROM, using defaults");
        // Сохраняем настройки по умолчанию
        saveNTPSettingsToEEPROM();
      }
    } else {
      // Настройки не найдены, сохраняем по умолчанию
      Serial.println("NTP settings not found in EEPROM, saving defaults");
      saveNTPSettingsToEEPROM();
    }
  } else {
    // EEPROM не инициализирован, сохраняем настройки по умолчанию
    Serial.println("EEPROM not initialized, saving default NTP settings");
    saveNTPSettingsToEEPROM();
  }
  EEPROM.end();
}

// Сохранение настроек ML в EEPROM
void saveMLSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  DynamicJsonDocument doc(128);
  doc["enabled"] = mlSettings.enabled;
  doc["publishInterval"] = mlSettings.publishInterval;
  
  String json;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_ML, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(EEPROM_ADDR_ML + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Загрузка настроек ML из EEPROM
void loadMLSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_ML, len);
    if (len > 0 && len < 200) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_ML + 4 + i);
      }
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, json);
      if (!error) {
        if (doc.containsKey("enabled")) mlSettings.enabled = doc["enabled"];
        if (doc.containsKey("publishInterval")) {
          int interval = doc["publishInterval"];
          if (interval >= 5 && interval <= 300) {  // Валидация: от 5 до 300 секунд
            mlSettings.publishInterval = interval;
          }
        }
      } else {
        Serial.println("Error parsing ML settings from EEPROM, using defaults");
        // Сохраняем настройки по умолчанию если ошибка парсинга
        saveMLSettingsToEEPROM();
      }
    } else {
      // Настройки не найдены, сохраняем по умолчанию (включено)
      Serial.println("ML settings not found in EEPROM, saving defaults (enabled=true)");
      saveMLSettingsToEEPROM();
    }
  } else {
    // EEPROM не инициализирован, сохраняем настройки по умолчанию
    Serial.println("EEPROM not initialized, saving default ML settings (enabled=true)");
    saveMLSettingsToEEPROM();
  }
  EEPROM.end();
}

// Сохранение настроек реле в EEPROM
void saveRelaySettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  DynamicJsonDocument doc(128);
  doc["fanOffIsLow"] = relaySettings.fanOffIsLow;
  doc["pumpOffIsLow"] = relaySettings.pumpOffIsLow;
  
  String json;
  serializeJson(doc, json);
  
  Serial.print("[Реле] Сохранение в EEPROM: ");
  Serial.println(json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_RELAY, len);
  for (int i = 0; i < len && i < 200; i++) {
    EEPROM.write(EEPROM_ADDR_RELAY + 4 + i, json[i]);
  }
  
  if (EEPROM.commit()) {
    Serial.println("[Реле] Настройки успешно сохранены в EEPROM");
  } else {
    Serial.println("[Реле] ОШИБКА: Не удалось сохранить в EEPROM!");
  }
  EEPROM.end();
}

// Загрузка настроек реле из EEPROM
void loadRelaySettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_RELAY, len);
    if (len > 0 && len < 200) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_RELAY + 4 + i);
      }
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, json);
      if (!error) {
        Serial.print("[Реле] Загружено из EEPROM: ");
        Serial.println(json);
        if (doc.containsKey("fanOffIsLow")) {
          relaySettings.fanOffIsLow = doc["fanOffIsLow"].as<bool>();
          Serial.print("[Реле] fanOffIsLow загружено: ");
          Serial.println(relaySettings.fanOffIsLow ? "true" : "false");
        }
        if (doc.containsKey("pumpOffIsLow")) {
          relaySettings.pumpOffIsLow = doc["pumpOffIsLow"].as<bool>();
          Serial.print("[Реле] pumpOffIsLow загружено: ");
          Serial.println(relaySettings.pumpOffIsLow ? "true" : "false");
        }
        if (doc.containsKey("sensorsOffIsLow")) {
          relaySettings.sensorsOffIsLow = doc["sensorsOffIsLow"].as<bool>();
          Serial.print("[Реле] sensorsOffIsLow загружено: ");
          Serial.println(relaySettings.sensorsOffIsLow ? "true" : "false");
        }
        Serial.println("[Реле] Настройки успешно загружены из EEPROM");
      } else {
        Serial.print("[Реле] Ошибка парсинга из EEPROM: ");
        Serial.println(error.c_str());
        Serial.println("[Реле] Используются настройки по умолчанию");
        saveRelaySettingsToEEPROM();
      }
    } else {
      Serial.println("Relay settings not found in EEPROM, saving defaults");
      saveRelaySettingsToEEPROM();
    }
  } else {
    Serial.println("EEPROM not initialized, saving default relay settings");
    saveRelaySettingsToEEPROM();
  }
  EEPROM.end();
}

// Сохранение настроек комфорт в EEPROM
void saveComfortSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  DynamicJsonDocument doc(512);
  doc["targetHomeTemp"] = comfortSettings.targetHomeTemp;
  doc["minBoilerTemp"] = comfortSettings.minBoilerTemp;
  doc["maxBoilerTemp"] = comfortSettings.maxBoilerTemp;
  doc["waitTemp"] = comfortSettings.waitTemp;
  doc["catchUpTemp"] = comfortSettings.catchUpTemp;
  doc["waitCoolingTime"] = comfortSettings.waitCoolingTime;
  doc["waitAfterHeating1Time"] = comfortSettings.waitAfterHeating1Time;
  doc["waitAfterReductionTime"] = comfortSettings.waitAfterReductionTime;
  doc["inertiaCheckInterval"] = comfortSettings.inertiaCheckInterval;
  doc["hysteresisOn"] = comfortSettings.hysteresisOn;
  doc["hysteresisOff"] = comfortSettings.hysteresisOff;
  doc["hysteresisBoiler"] = comfortSettings.hysteresisBoiler;
  doc["warningTemp"] = comfortSettings.warningTemp;
  
  String json;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_COMFORT, len);
  for (int i = 0; i < len && i < 250; i++) {
    EEPROM.write(EEPROM_ADDR_COMFORT + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Загрузка настроек комфорт из EEPROM
void loadComfortSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_COMFORT, len);
    if (len > 0 && len < 250) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_COMFORT + 4 + i);
      }
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, json);
      if (!error) {
        if (doc.containsKey("targetHomeTemp")) {
          float val = doc["targetHomeTemp"];
          if (val >= 20 && val <= 28) comfortSettings.targetHomeTemp = val;
        }
        if (doc.containsKey("minBoilerTemp")) {
          float val = doc["minBoilerTemp"];
          if (val >= 40 && val <= 80) comfortSettings.minBoilerTemp = val;
        }
        if (doc.containsKey("maxBoilerTemp")) {
          float val = doc["maxBoilerTemp"];
          if (val >= 40 && val <= 80) comfortSettings.maxBoilerTemp = val;
        }
        if (doc.containsKey("waitTemp")) {
          float val = doc["waitTemp"];
          if (val >= 50 && val <= 80) comfortSettings.waitTemp = val;
        }
        if (doc.containsKey("catchUpTemp")) {
          float val = doc["catchUpTemp"];
          if (val >= 20 && val <= 28) comfortSettings.catchUpTemp = val;
        }
        if (doc.containsKey("waitCoolingTime")) {
          int val = doc["waitCoolingTime"];
          if (val >= 5 && val <= 30) comfortSettings.waitCoolingTime = val;
        }
        if (doc.containsKey("waitAfterHeating1Time")) {
          int val = doc["waitAfterHeating1Time"];
          if (val >= 10 && val <= 60) comfortSettings.waitAfterHeating1Time = val;
        }
        if (doc.containsKey("waitAfterReductionTime")) {
          int val = doc["waitAfterReductionTime"];
          if (val >= 10 && val <= 60) comfortSettings.waitAfterReductionTime = val;
        }
        if (doc.containsKey("inertiaCheckInterval")) {
          int val = doc["inertiaCheckInterval"];
          if (val >= 1 && val <= 15) comfortSettings.inertiaCheckInterval = val;
        }
        if (doc.containsKey("hysteresisOn")) {
          float val = doc["hysteresisOn"];
          if (val >= 0.1 && val <= 2.0) comfortSettings.hysteresisOn = val;
        }
        if (doc.containsKey("hysteresisOff")) {
          float val = doc["hysteresisOff"];
          if (val >= 0.1 && val <= 2.0) comfortSettings.hysteresisOff = val;
        }
        if (doc.containsKey("hysteresisBoiler")) {
          float val = doc["hysteresisBoiler"];
          if (val >= 0.5 && val <= 5.0) comfortSettings.hysteresisBoiler = val;
        }
        if (doc.containsKey("warningTemp")) {
          float val = doc["warningTemp"];
          if (val >= 80 && val <= 90) comfortSettings.warningTemp = val;
        }
      }
    }
  }
  EEPROM.end();
}

// Сохранение режима работы в EEPROM
void saveWorkModeToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_ADDR_WORKMODE, workMode);
  EEPROM.commit();
  EEPROM.end();
}

// Загрузка режима работы из EEPROM
void loadWorkModeFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int mode = 0;
    EEPROM.get(EEPROM_ADDR_WORKMODE, mode);
    if (mode == 0 || mode == 1) {
      workMode = mode;
      Serial.print("[Boot] Work mode loaded from EEPROM: ");
      Serial.println(workMode == 0 ? "Авто" : "Комфорт");
    } else {
      workMode = 0;  // По умолчанию Авто
      Serial.println("[Boot] Invalid work mode in EEPROM, using default: Авто");
    }
  } else {
    workMode = 0;  // По умолчанию Авто
    Serial.println("[Boot] EEPROM not initialized, using default work mode: Авто");
  }
  EEPROM.end();
}

// Сохранение счетчика перезагрузок в EEPROM
void saveBootCountToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_ADDR_BOOT_COUNT, bootCount);
  EEPROM.commit();
  EEPROM.end();
  Serial.print("[Boot] Boot count saved to EEPROM: ");
  Serial.println(bootCount);
}

// Загрузка счетчика перезагрузок из EEPROM
void loadBootCountFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_ADDR_BOOT_COUNT, bootCount);
    Serial.print("[Boot] Boot count loaded from EEPROM: ");
    Serial.println(bootCount);
  } else {
    bootCount = 0;
    Serial.println("[Boot] EEPROM not initialized, starting boot count from 0");
  }
  EEPROM.end();
}

// Преобразование причины перезагрузки в строку
String getResetReasonString() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON: return "Включение питания";
    case ESP_RST_EXT: return "Внешний сброс";
    case ESP_RST_SW: return "Программный сброс";
    case ESP_RST_PANIC: return "Паника/Сбой";
    case ESP_RST_INT_WDT: return "WDT (внутренний)";
    case ESP_RST_TASK_WDT: return "WDT (задача)";
    case ESP_RST_WDT: return "WDT (общий)";
    case ESP_RST_DEEPSLEEP: return "Глубокий сон";
    case ESP_RST_BROWNOUT: return "Просадка питания";
    case ESP_RST_SDIO: return "SDIO сброс";
    default: return "Неизвестно";
  }
}

// Загрузка журнала перезагрузок из EEPROM
void loadBootLogFromEEPROM() {
  EEPROM.get(EEPROM_ADDR_BOOT_LOG, bootLog);
  
  // Находим индекс последней записи
  bootLogWriteIndex = 0;
  for (int i = 0; i < BOOT_LOG_MAX_ENTRIES; i++) {
    if (bootLog[i].valid) {
      bootLogWriteIndex = (i + 1) % BOOT_LOG_MAX_ENTRIES;
    }
  }
}

// Сохранение записи в журнал перезагрузок
void saveBootLogEntry() {
  BootLogEntry entry;
  entry.bootCount = bootCount;
  entry.timestamp = (ntpSettings.enabled && timeClient.isTimeSet()) ? timeClient.getEpochTime() : 0;
  lastResetReason.toCharArray(entry.reason, sizeof(entry.reason));
  entry.valid = true;
  
  // Записываем в текущую позицию
  bootLog[bootLogWriteIndex] = entry;
  EEPROM.put(EEPROM_ADDR_BOOT_LOG + bootLogWriteIndex * BOOT_LOG_ENTRY_SIZE, entry);
  EEPROM.commit();
  
  // Увеличиваем индекс (циклический буфер)
  bootLogWriteIndex = (bootLogWriteIndex + 1) % BOOT_LOG_MAX_ENTRIES;
  
  Serial.print("[Boot] Log entry saved: bootCount=");
  Serial.print(entry.bootCount);
  Serial.print(", reason=");
  Serial.println(entry.reason);
}

// API: Получение журнала перезагрузок
void handleBootLog() {
  DynamicJsonDocument doc(4096);
  JsonArray entries = doc.createNestedArray("entries");
  
  // Собираем все валидные записи
  for (int i = 0; i < BOOT_LOG_MAX_ENTRIES; i++) {
    if (bootLog[i].valid) {
      JsonObject entry = entries.createNestedObject();
      entry["bootCount"] = bootLog[i].bootCount;
      entry["timestamp"] = bootLog[i].timestamp;
      entry["reason"] = bootLog[i].reason;
      
      // Форматируем дату/время если доступно
      if (bootLog[i].timestamp > 0) {
        time_t t = bootLog[i].timestamp;
        struct tm *timeInfo = localtime(&t);
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeInfo);
        entry["datetime"] = timeStr;
      } else {
        entry["datetime"] = "N/A";
      }
    }
  }
  
  doc["total"] = entries.size();
  doc["currentBootCount"] = bootCount;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сброс счетчика перезагрузок
void handleBootCountReset() {
  bootCount = 0;
  saveBootCountToEEPROM();
  
  // Очищаем журнал
  for (int i = 0; i < BOOT_LOG_MAX_ENTRIES; i++) {
    bootLog[i].valid = false;
    EEPROM.put(EEPROM_ADDR_BOOT_LOG + i * BOOT_LOG_ENTRY_SIZE, bootLog[i]);
  }
  EEPROM.commit();
  bootLogWriteIndex = 0;
  
  DynamicJsonDocument doc(128);
  doc["success"] = true;
  doc["bootCount"] = bootCount;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  
  Serial.println("[Boot] Boot count and log reset to 0");
}

// Функция логирования событий
void logEvent(const char* eventType, const char* details) {
  EventLogEntry entry;
  entry.timestamp = (ntpSettings.enabled && timeClient.isTimeSet()) ? timeClient.getEpochTime() : 0;
  strncpy(entry.eventType, eventType, sizeof(entry.eventType) - 1);
  entry.eventType[sizeof(entry.eventType) - 1] = '\0';
  strncpy(entry.details, details, sizeof(entry.details) - 1);
  entry.details[sizeof(entry.details) - 1] = '\0';
  entry.valid = true;
  
  // Записываем в циклический буфер
  eventLog[eventLogWriteIndex] = entry;
  eventLogWriteIndex = (eventLogWriteIndex + 1) % EVENT_LOG_MAX_ENTRIES;
  
  // Сохраняем в EEPROM
  saveEventLogToEEPROM();
  
  Serial.print("[Event] ");
  Serial.print(eventType);
  Serial.print(": ");
  Serial.println(details);
}

// Сохранение журнала событий в EEPROM
void saveEventLogToEEPROM() {
  for (int i = 0; i < EVENT_LOG_MAX_ENTRIES; i++) {
    EEPROM.put(EEPROM_ADDR_EVENT_LOG + i * EVENT_LOG_ENTRY_SIZE, eventLog[i]);
  }
  EEPROM.commit();
}

// Загрузка журнала событий из EEPROM
void loadEventLogFromEEPROM() {
  EEPROM.get(EEPROM_ADDR_EVENT_LOG, eventLog);
  // Проверяем валидность записей
  for (int i = 0; i < EVENT_LOG_MAX_ENTRIES; i++) {
    if (!eventLog[i].valid) {
      eventLog[i].timestamp = 0;
      eventLog[i].eventType[0] = '\0';
      eventLog[i].details[0] = '\0';
    }
  }
}

// Сохранение статистики вентилятора в EEPROM
void saveFanStatsToEEPROM() {
  EEPROM.put(EEPROM_ADDR_FAN_STATS, fanStats);
  EEPROM.commit();
}

// Загрузка статистики вентилятора из EEPROM
void loadFanStatsFromEEPROM() {
  EEPROM.get(EEPROM_ADDR_FAN_STATS, fanStats);
  // Проверяем валидность
  if (fanStats.totalWorkTime > 1000000000UL) {  // Нереалистичное значение
    fanStats.totalWorkTime = 0;
    fanStats.dailyWorkTime = 0;
    fanStats.lastDayReset = 0;
    fanStats.cycleCount = 0;
    fanStats.dailyCycleCount = 0;
  }
}

// Функция запуска розжига
void startIgnition() {
  if (boilerExtinguished || systemState == "КОТЕЛ_ПОГАС") {
    boilerExtinguished = false;
    ignitionInProgress = true;
    ignitionStartTime = millis();
    ignitionStartTemp = supplyTemp;
    fanStartTime = 0;
    maxTempDuringFan = 0.0;
    fanState = true;
    digitalWrite(PIN_RELAY_FAN, HIGH);
    systemState = "РОЗЖИГ";
    
    char details[50];
    snprintf(details, sizeof(details), "Температура: %.1f°C", ignitionStartTemp);
    logEvent("IGNITION_STARTED", details);
    
    // Публикация в MQTT
    if (mqttSettings.enabled && mqttClient.connected()) {
      String topic = mqttSettings.prefix + "/event/boiler_ignition_started";
      mqttClient.publish(topic.c_str(), details, false);
    }
    
    Serial.println("[Котел] Розжиг начат по нажатию кнопки");
  }
}

// Проверка погасания котла
void checkBoilerExtinguished(unsigned long now) {
  if (!fanState || boilerExtinguished) {
    return;
  }
  
  // Если вентилятор только что включился
  if (fanStartTime == 0) {
    fanStartTime = now;
    maxTempDuringFan = supplyTemp;
    return;
  }
  
  // Обновляем максимальную температуру
  if (supplyTemp > maxTempDuringFan) {
    maxTempDuringFan = supplyTemp;
  }
  
  // Проверяем время работы вентилятора
  unsigned long fanWorkTime = (now >= fanStartTime) ? (now - fanStartTime) : (ULONG_MAX - fanStartTime + now);
  
  if (fanWorkTime >= BOILER_EXTINGUISHED_CHECK_TIME) {
    // Проверяем тренд температуры
    int trend = getTemperatureTrend(&supplyHistory);
    
    // Если температура падает
    if (trend == -1) {
      float tempDrop = maxTempDuringFan - supplyTemp;
      
      // Если температура упала на заданное значение
      if (tempDrop >= BOILER_EXTINGUISHED_TEMP_DROP) {
        boilerExtinguished = true;
        fanState = false;
        digitalWrite(PIN_RELAY_FAN, LOW);
        systemState = "КОТЕЛ_ПОГАС";
        
        char details[50];
        snprintf(details, sizeof(details), "Падение: %.1f°C за %.1fч", tempDrop, fanWorkTime / 3600000.0);
        logEvent("BOILER_EXTINGUISHED", details);
        
        // Публикация в MQTT
        if (mqttSettings.enabled && mqttClient.connected()) {
          String topic = mqttSettings.prefix + "/event/boiler_extinguished";
          mqttClient.publish(topic.c_str(), details, false);
        }
        
        Serial.print("[Котел] Обнаружено погасание! Вентилятор отключен. Падение температуры: ");
        Serial.print(tempDrop);
        Serial.println("°C");
      }
    }
  }
}

// Проверка прогресса розжига
void checkIgnitionProgress(unsigned long now) {
  if (!ignitionInProgress) {
    return;
  }
  
  unsigned long ignitionElapsed = (now >= ignitionStartTime) ? (now - ignitionStartTime) : (ULONG_MAX - ignitionStartTime + now);
  float tempIncrease = supplyTemp - ignitionStartTemp;
  
  // Проверяем успешность розжига
  if (tempIncrease >= IGNITION_TEMP_INCREASE) {
    // Розжиг успешен
    ignitionInProgress = false;
    systemState = "HEATING";
    heatingStartTime = now;
    fanStartTime = now;
    maxTempDuringFan = supplyTemp;
    
    char details[50];
    snprintf(details, sizeof(details), "Успех за %lu мин, +%.1f°C", ignitionElapsed / 60000, tempIncrease);
    logEvent("IGNITION_SUCCESS", details);
    
    // Публикация в MQTT
    if (mqttSettings.enabled && mqttClient.connected()) {
      String topic = mqttSettings.prefix + "/event/boiler_ignition_success";
      mqttClient.publish(topic.c_str(), details, false);
    }
    
    Serial.print("[Котел] Розжиг успешен! Температура повысилась на ");
    Serial.print(tempIncrease);
    Serial.println("°C");
    return;
  }
  
  // Проверяем таймаут (10-20 минут в зависимости от начальной температуры)
  unsigned long timeout = (ignitionStartTemp < 30.0) ? IGNITION_TIMEOUT_MAX : IGNITION_TIMEOUT_MIN;
  
  if (ignitionElapsed >= timeout) {
    // Розжиг неудачен
    ignitionInProgress = false;
    fanState = false;
    digitalWrite(PIN_RELAY_FAN, LOW);
    systemState = "ОШИБКА_РОЗЖИГА";
    boilerExtinguished = true;
    
    char details[50];
    snprintf(details, sizeof(details), "Таймаут %lu мин, +%.1f°C", timeout / 60000, tempIncrease);
    logEvent("IGNITION_FAILED", details);
    
    // Публикация в MQTT
    if (mqttSettings.enabled && mqttClient.connected()) {
      String topic = mqttSettings.prefix + "/event/boiler_ignition_failed";
      mqttClient.publish(topic.c_str(), details, false);
    }
    
    Serial.print("[Котел] Розжиг неудачен! Таймаут. Температура повысилась только на ");
    Serial.print(tempIncrease);
    Serial.println("°C");
  }
}

// Настройка NTP клиента
void setupNTP() {
  if (!ntpSettings.enabled) {
    return;
  }
  
  timeClient.setTimeOffset(ntpSettings.timezone * 3600);  // Смещение в секундах
  timeClient.setUpdateInterval(ntpSettings.updateInterval * 1000);  // Интервал обновления в миллисекундах
  timeClient.begin();
  
  Serial.print("NTP клиент настроен: ");
  Serial.print(ntpSettings.server);
  Serial.print(", часовой пояс: UTC+");
  Serial.println(ntpSettings.timezone);
  
  // Первая синхронизация (может занять несколько секунд)
  Serial.println("Синхронизация времени с NTP сервером...");
  int attempts = 0;
  unsigned long lastCheckTime = millis();
  while (!timeClient.update() && attempts < 10) {
    unsigned long now = millis();
    // Неблокирующая задержка 500 мс с yield()
    if (now - lastCheckTime >= 500 || now < lastCheckTime) {
      lastCheckTime = now;
      attempts++;
      Serial.print(".");
    }
    yield(); // Позволяем другим задачам выполняться
  }
  
  if (timeClient.update()) {
    Serial.println("\nВремя синхронизировано!");
    Serial.print("Текущее время: ");
    // Используем прямое форматирование здесь
    unsigned long epochTime = timeClient.getEpochTime();
    time_t rawTime = epochTime;
    struct tm *timeInfo = localtime(&rawTime);
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
    Serial.println(timeStr);
  } else {
    Serial.println("\nОшибка синхронизации времени");
  }
}

// Получение отформатированного времени
String getFormattedTime() {
  if (!ntpSettings.enabled || !timeClient.isTimeSet()) {
    return "--:--:--";
  }
  
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawTime = epochTime;
  struct tm *timeInfo = localtime(&rawTime);
  
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  return String(timeStr);
}

// Получение отформатированной даты
String getFormattedDate() {
  if (!ntpSettings.enabled || !timeClient.isTimeSet()) {
    return "--.--.----";
  }
  
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawTime = epochTime;
  struct tm *timeInfo = localtime(&rawTime);
  
  char dateStr[11];
  sprintf(dateStr, "%02d.%02d.%04d", timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  return String(dateStr);
}

// MQTT функции
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  
  // Обработка установки уставки
  String setpointTopic = mqttSettings.prefix + "/setpoint/set";
  if (topicStr == setpointTopic) {
    float newSetpoint = message.toFloat();
    if (newSetpoint >= 40 && newSetpoint <= 80) {
      setpoint = newSetpoint;
      autoSettings.setpoint = newSetpoint;
      saveAutoSettingsToEEPROM();
    }
  }
  
  // Обработка температуры в доме от ESP01
  if (topicStr == "home/esp01/temperature") {
    float newHomeTemp = message.toFloat();
    if (newHomeTemp >= -50 && newHomeTemp <= 50) {  // Валидация диапазона
      homeTemp = newHomeTemp;
      lastHomeTempUpdate = millis();  // Обновляем время последнего получения данных
      addToHistory(&homeHistory, newHomeTemp);
    }
  }
  
  // Обработка LWT статуса датчика температуры дома
  if (topicStr == "home/esp01/status") {
    message.toLowerCase();
    message.trim();
    if (message == "online") {
      homeTempSensorLWTOnline = true;
      Serial.println("[MQTT] Home temperature sensor LWT: online");
    } else if (message == "offline") {
      homeTempSensorLWTOnline = false;
      Serial.println("[MQTT] Home temperature sensor LWT: offline");
      // Если режим Комфорт и датчик стал offline, переключаемся на Авто и сохраняем
      if (workMode == 1) {
        Serial.println("[MQTT] Switching from Comfort to Auto mode due to sensor offline");
        workMode = 0;
        comfortState = "WAIT";
        comfortStateStartTime = 0;
        saveWorkModeToEEPROM();
      }
    }
  }
  
  // Обработка управления реле датчиков
  String sensorsRelayTopic = mqttSettings.prefix + "/sensors/reset";
  if (topicStr == sensorsRelayTopic) {
    if (message == "1" || message == "on" || message == "reset") {
      // Выключаем реле для сброса
      sensorsRelayState = false;
      int sensorsLevel = relaySettings.sensorsOffIsLow ? LOW : HIGH;
      digitalWrite(PIN_RELAY_SENSORS, sensorsLevel);
      sensorsResetPending = true;
      sensorsResetStartTime = millis();
      Serial.println("[MQTT] Sensors reset command received");
    } else if (message == "0" || message == "off") {
      // Выключаем реле
      sensorsRelayState = false;
      int sensorsLevel = relaySettings.sensorsOffIsLow ? LOW : HIGH;
      digitalWrite(PIN_RELAY_SENSORS, sensorsLevel);
      sensorsResetPending = false;
      Serial.println("[MQTT] Sensors relay off command received");
    }
  }
  
  // Обработка запуска розжига
  String ignitionStartTopic = mqttSettings.prefix + "/ignition/start";
  if (topicStr == ignitionStartTopic) {
    message.toLowerCase();
    message.trim();
    if (message == "1" || message == "on" || message == "start") {
      startIgnition();
      Serial.println("[MQTT] Ignition start command received");
    }
  }
}

bool mqttConnect() {
  if (!mqttSettings.enabled) {
    return false;
  }
  
  if (mqttClient.connected()) {
    return true;
  }
  
  
  mqttClient.setServer(mqttSettings.server.c_str(), mqttSettings.port);
  mqttClient.setCallback(mqttCallback);
  // Увеличиваем размер буфера для больших JSON сообщений (ML данные ~540 байт)
  mqttClient.setBufferSize(1024);  // Увеличено с 256 до 1024 байт
  mqttClient.setSocketTimeout(2);  // Таймаут 2 секунды вместо дефолтных 15
  
  // Генерация уникального clientId
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  String clientId = "ESP32_Kotel_" + String(chipId, HEX);
  
  // Настройка LWT (Last Will and Testament) - статус offline при неожиданном отключении
  String willTopic = mqttSettings.prefix + "/status";
  String willMessage = "offline";
  bool willRetain = true;
  int willQoS = 1;
  
  // Неблокирующее подключение с LWT и коротким таймаутом
  if (mqttClient.connect(clientId.c_str(), mqttSettings.user.c_str(), mqttSettings.password.c_str(),
                         willTopic.c_str(), willQoS, willRetain, willMessage.c_str())) {
    // Публикация статуса online с retain
    mqttClient.publish(willTopic.c_str(), "online", true);  // true = retain
    
    // Публикация IP адреса при подключении (с retain)
    if (WiFi.status() == WL_CONNECTED) {
      String ipTopic = mqttSettings.prefix + "/simple/ip";
      mqttClient.publish(ipTopic.c_str(), WiFi.localIP().toString().c_str(), true);  // true = retain
    }
    
    // Подписка на уставку
    String setpointTopic = mqttSettings.prefix + "/setpoint/set";
    mqttClient.subscribe(setpointTopic.c_str());
    
    // Подписка на управление реле датчиков
    String sensorsRelayTopic = mqttSettings.prefix + "/sensors/reset";
    mqttClient.subscribe(sensorsRelayTopic.c_str());
    
    // Подписка на запуск розжига
    String ignitionStartTopic = mqttSettings.prefix + "/ignition/start";
    mqttClient.subscribe(ignitionStartTopic.c_str());
    
    // Подписка на температуру в доме от ESP01
    mqttClient.subscribe("home/esp01/temperature");
    
    // Подписка на LWT статус датчика температуры дома
    mqttClient.subscribe("home/esp01/status");
    
    // Проверка режима работы после подключения к MQTT
    // Если режим Комфорт, но датчик температуры дома недоступен, переключаемся на Авто
    if (workMode == 1 && !homeTempSensorLWTOnline) {
      Serial.println("[MQTT] Work mode is Comfort, but home temp sensor is offline. Switching to Auto mode.");
      workMode = 0;
      comfortState = "WAIT";
      comfortStateStartTime = 0;
      saveWorkModeToEEPROM();
    }
    
    return true;
  } else {
    return false;
  }
}

void publishMqttState() {
  if (!mqttSettings.enabled || !mqttClient.connected()) {
    return;
  }
  
  // Полный JSON со всеми данными
  DynamicJsonDocument doc(2048);
  doc["supplyTemp"] = supplyTemp;
  doc["returnTemp"] = returnTemp;
  doc["boilerTemp"] = boilerTemp;
  doc["outdoorTemp"] = outdoorTemp;
  doc["homeTemp"] = homeTemp;  // Температура в доме (от ESP01)
  doc["setpoint"] = setpoint;
  doc["fan"] = fanState;
  doc["pump"] = pumpState;
  doc["systemEnabled"] = systemEnabled;
  doc["state"] = systemState;
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["wifiSSID"] = WiFi.SSID();
  doc["wifiIP"] = WiFi.localIP().toString();
  doc["wifiMAC"] = WiFi.macAddress();
  doc["uptime"] = millis() / 1000;
  doc["freeMem"] = ESP.getFreeHeap();
  
  String json;
  serializeJson(doc, json);
  
  String topic = mqttSettings.prefix + "/state";
  mqttClient.publish(topic.c_str(), json.c_str(), false);  // false = не ждать подтверждения
}

void publishMqttSimple() {
  if (!mqttSettings.enabled || !mqttClient.connected()) {
    return;
  }
  
  // Простые публикации (неблокирующие, без подтверждения)
  String tempTopic = mqttSettings.prefix + "/simple/temp";
  String returnTempTopic = mqttSettings.prefix + "/simple/returnTemp";
  String boilerTempTopic = mqttSettings.prefix + "/simple/boilerTemp";
  String outdoorTempTopic = mqttSettings.prefix + "/simple/outdoorTemp";
  String homeTempTopic = mqttSettings.prefix + "/simple/homeTemp";
  String enabledTopic = mqttSettings.prefix + "/simple/enabled";
  String setpointTopic = mqttSettings.prefix + "/simple/setpoint";
  String workModeTopic = mqttSettings.prefix + "/simple/workMode";
  String targetHomeTempTopic = mqttSettings.prefix + "/simple/targetHomeTemp";
  String ipTopic = mqttSettings.prefix + "/simple/ip";
  
  // Публикация температур датчиков
  if (supplyTemp > 0) {
    mqttClient.publish(tempTopic.c_str(), String(supplyTemp, 1).c_str(), false);
  }
  if (returnTemp > 0) {
    mqttClient.publish(returnTempTopic.c_str(), String(returnTemp, 1).c_str(), false);
  }
  if (boilerTemp > 0) {
    mqttClient.publish(boilerTempTopic.c_str(), String(boilerTemp, 1).c_str(), false);
  }
  if (outdoorTemp > -50.0 && outdoorTemp < 150.0) {  // Валидный диапазон для уличной температуры
    mqttClient.publish(outdoorTempTopic.c_str(), String(outdoorTemp, 1).c_str(), false);
  }
  if (homeTemp > 0 && homeTemp < 50.0) {  // Валидный диапазон для домашней температуры
    mqttClient.publish(homeTempTopic.c_str(), String(homeTemp, 1).c_str(), false);
  }
  
  // Публикация состояния системы
  mqttClient.publish(enabledTopic.c_str(), systemEnabled ? "1" : "0", false);
  mqttClient.publish(setpointTopic.c_str(), String(setpoint, 1).c_str(), false);
  
  // Публикация режима работы (0 = Авто, 1 = Комфорт)
  mqttClient.publish(workModeTopic.c_str(), String(workMode).c_str(), false);
  
  // Публикация уставки температуры дома
  mqttClient.publish(targetHomeTempTopic.c_str(), String(comfortSettings.targetHomeTemp, 1).c_str(), false);
  
  // Публикация IP адреса (с retain для сохранения последнего значения)
  if (WiFi.status() == WL_CONNECTED) {
    mqttClient.publish(ipTopic.c_str(), WiFi.localIP().toString().c_str(), true);  // true = retain
  }
}

// Публикация детального JSON для обучения ML модели
void publishMqttML() {
  if (!mlSettings.enabled || !mqttSettings.enabled || !mqttClient.connected()) {
    return;
  }
  
  // Минимальные вычисления - только разница температур (простое вычитание)
  float tempDiff = supplyTemp - returnTemp;
  
  // Получение времени от NTP если доступно
  unsigned long currentTime = 0;
  if (ntpSettings.enabled && WiFi.isConnected()) {
    currentTime = timeClient.getEpochTime();
  }
  
  // Детальный JSON для обучения модели
  DynamicJsonDocument doc(3072);  // Увеличенный размер для всех данных
  
  // 1. Температуры
  doc["supplyTemp"] = supplyTemp;
  doc["returnTemp"] = returnTemp;
  doc["boilerTemp"] = boilerTemp;
  doc["outdoorTemp"] = outdoorTemp;
  doc["homeTemp"] = homeTemp;  // Температура в доме (от ESP01)
  doc["tempDiff"] = tempDiff;  // Единственное вычисление - разница температур
  
  // 2. Состояния устройств
  doc["fan"] = fanState;
  doc["pump"] = pumpState;
  doc["systemEnabled"] = systemEnabled;
  doc["state"] = systemState;
  
  // 2.1. Режим работы
  doc["workMode"] = workMode;  // 0 = Авто, 1 = Комфорт
  doc["workModeName"] = (workMode == 0) ? "Авто" : "Комфорт";
  
  // 2.2. Статус датчика температуры дома
  // Проверяем валидность датчика: LWT online и температура в допустимом диапазоне
  bool homeTempValid = homeTempSensorLWTOnline && homeTemp > 0 && homeTemp < 50.0;
  doc["homeTempSensorValid"] = homeTempValid;
  doc["homeTempSensorLWTOnline"] = homeTempSensorLWTOnline;
  
  // 2.3. Настройки Comfort режима (если активен)
  if (workMode == 1) {
    doc["comfortState"] = comfortState;
    doc["targetHomeTemp"] = comfortSettings.targetHomeTemp;
  } else {
    doc["comfortState"] = "";
    doc["targetHomeTemp"] = 0.0;
  }
  
  // 3. Настройки Auto режима (для контекста)
  doc["setpoint"] = autoSettings.setpoint;
  doc["minTemp"] = autoSettings.minTemp;
  doc["maxTemp"] = autoSettings.maxTemp;
  doc["hysteresis"] = autoSettings.hysteresis;
  doc["inertiaTemp"] = autoSettings.inertiaTemp;
  doc["inertiaTime"] = autoSettings.inertiaTime;
  doc["overheatTemp"] = autoSettings.overheatTemp;
  doc["heatingTimeout"] = autoSettings.heatingTimeout;
  
  // 4. Подброс угля
  doc["coalFeedingActive"] = coalFeedingActive;
  if (coalFeedingActive) {
    doc["coalFeedingElapsed"] = (millis() - coalFeedingStartTime) / 1000;  // секунды
  } else {
    doc["coalFeedingElapsed"] = 0;
  }
  
  // 5. Временные метки
  doc["timestamp"] = currentTime;
  doc["uptime"] = millis() / 1000;
  if (ntpSettings.enabled && WiFi.isConnected()) {
    doc["time"] = getFormattedTime();
    doc["date"] = getFormattedDate();
  } else {
    doc["time"] = "";
    doc["date"] = "";
  }
  
  // 6. WiFi и сеть
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["wifiSSID"] = WiFi.SSID();
  doc["wifiIP"] = WiFi.localIP().toString();
  
  // 7. Системные параметры
  doc["freeMem"] = ESP.getFreeHeap();
  doc["heapSize"] = ESP.getHeapSize();
  
  // 8. ML настройки (интервал публикации)
  doc["mlPublishInterval"] = mlSettings.publishInterval;
  
  // 9. Дополнительная информация
  doc["sensorCount"] = sensors1.getDeviceCount() + sensors2.getDeviceCount();  // Количество найденных датчиков на обеих шинах
  doc["sensorCountBus1"] = sensors1.getDeviceCount();  // Количество на шине 1
  doc["sensorCountBus2"] = sensors2.getDeviceCount();  // Количество на шине 2
  doc["mqttConnected"] = mqttClient.connected();
  
  String json;
  serializeJson(doc, json);
  
  String topic = mqttSettings.prefix + "/ml/data";
  mqttClient.publish(topic.c_str(), json.c_str(), false);  // false = не ждать подтверждения
}

// Обработка поворота энкодера для изменения уставки
void handleEncoderRotation() {
  if (encoderPosition != lastEncoderPosition) {
    int delta = encoderPosition - lastEncoderPosition;
    
    // Улучшенная нормализация: обрабатываем все шаги, но ограничиваем максимальную скорость
    // Это делает работу более плавной и отзывчивой
    int normalizedDelta = 0;
    
    // Ограничиваем максимальную дельту за один раз (защита от пропусков)
    if (abs(delta) > 3) {
      // Если дельта слишком большая - нормализуем до ±3
      normalizedDelta = (delta > 0) ? 3 : -3;
    } else {
      // Используем реальную дельту (до 3 шагов)
      normalizedDelta = delta;
    }
    
    // Обновляем позицию на нормализованную дельту
    lastEncoderPosition += normalizedDelta;
    encoderPosition = lastEncoderPosition;
    
    // Изменяем уставку с учетом нормализованной дельты (шаг 0.5 градуса на один клик)
    float newSetpoint = setpoint + (normalizedDelta * 0.5);
    
    // Ограничение диапазона
    if (newSetpoint < 40) newSetpoint = 40;
    if (newSetpoint > 80) newSetpoint = 80;
    
    setpoint = newSetpoint;
    autoSettings.setpoint = newSetpoint;
    
    // Отложенное сохранение в EEPROM (не блокируем сразу)
    autoSettingsDirty = true;
    lastAutoSettingsChange = millis();
    
    // Публикация уставки в MQTT (неблокирующая)
    if (mqttSettings.enabled && mqttClient.connected()) {
      String topic = mqttSettings.prefix + "/setpoint";
      mqttClient.publish(topic.c_str(), String(setpoint, 1).c_str(), false);
    }
    
    lastEncoderRotation = millis(); // Запоминаем время поворота
  }
}

// Обработка кнопки энкодера - подброс угля (переключатель)
void handleEncoderButton() {
  // Читаем состояние кнопки (LOW = нажата, т.к. INPUT_PULLUP)
  bool currentButtonState = (digitalRead(PIN_ENCODER_SW) == LOW);
  
  // Статические переменные для обработки кнопки
  static unsigned long buttonPressStartTime = 0;
  static bool buttonWasPressed = false;
  static bool buttonHandled = false;
  static bool longPressHandled = false;
  
  const unsigned long BUTTON_DEBOUNCE_MS = 20;  // Время дребезга контактов
  const unsigned long BUTTON_MIN_PRESS_MS = 30;  // Минимальное время нажатия для регистрации
  const unsigned long BUTTON_MIN_INTERVAL_MS = 200;  // Минимальный интервал между нажатиями
  const unsigned long BUTTON_LONG_PRESS_MS = 3000;  // Время удержания для запуска розжига (3 секунды)
  
  unsigned long currentTime = millis();
  
  // Обработка нажатия кнопки
  if (currentButtonState) {
    // Кнопка нажата
    if (!buttonWasPressed) {
      // Кнопка только что нажата - запоминаем время
      buttonPressStartTime = currentTime;
      buttonWasPressed = true;
      buttonHandled = false;
      longPressHandled = false;
    } else {
      // Кнопка удерживается - проверяем длительное нажатие
      unsigned long pressDuration = currentTime - buttonPressStartTime;
      if (pressDuration >= BUTTON_LONG_PRESS_MS && !longPressHandled) {
        // Длительное нажатие (3 секунды) - запуск розжига
        longPressHandled = true;
        startIgnition();
        Serial.println("[ENCODER] Long press detected - starting ignition");
      }
    }
  } 
  else {
    // Кнопка отпущена
    if (buttonWasPressed) {
      // Кнопка только что отпущена
      unsigned long pressDuration = currentTime - buttonPressStartTime;
      
      // Проверяем минимальное время нажатия (защита от дребезга)
      // Короткое нажатие обрабатываем только если не было длительного
      if (pressDuration >= BUTTON_MIN_PRESS_MS && 
          pressDuration < BUTTON_LONG_PRESS_MS &&
          !buttonHandled &&
          !longPressHandled &&
          (currentTime - lastButtonPress > BUTTON_MIN_INTERVAL_MS)) {
        
        // Валидное короткое нажатие
        buttonHandled = true;
        lastButtonPress = currentTime;
        
        // Если котел погас - запускаем розжиг
        if (boilerExtinguished || systemState == "КОТЕЛ_ПОГАС") {
          startIgnition();
        }
        // Иначе переключение подброса угля
        else if (!coalFeedingActive) {
          startCoalFeeding();
        } else {
          stopCoalFeeding();
        }
      }
      
      buttonWasPressed = false;
      buttonPressStartTime = 0;
    }
  }
}

// Главная функция обработки энкодера
void handleEncoder() {
  handleEncoderRotation(); // Сначала обрабатываем поворот
  handleEncoderButton();   // Затем обрабатываем кнопку
}

// Обработка команд через Serial для отладки энкодера
void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "1") {
      // Тест: поворот влево (уменьшение)
      Serial.println("[ТЕСТ] Симуляция поворота влево");
      encoderPosition--;
      lastEncoderRotation = millis(); // Сбрасываем время поворота для теста
      Serial.print("[ТЕСТ] encoderPosition = ");
      Serial.println(encoderPosition);
      handleEncoderRotation(); // Обрабатываем изменение
    }
    else if (command == "2") {
      // Тест: поворот вправо (увеличение)
      Serial.println("[ТЕСТ] Симуляция поворота вправо");
      encoderPosition++;
      lastEncoderRotation = millis(); // Сбрасываем время поворота для теста
      Serial.print("[ТЕСТ] encoderPosition = ");
      Serial.println(encoderPosition);
      handleEncoderRotation(); // Обрабатываем изменение
    }
    else if (command == "3") {
      // Тест: нажатие кнопки
      Serial.println("[ТЕСТ] Симуляция нажатия кнопки");
      Serial.print("[ТЕСТ] Текущее состояние подброса: ");
      Serial.println(coalFeedingActive ? "АКТИВЕН" : "ВЫКЛ");
      Serial.println("[ТЕСТ] Переключение подброса угля...");
      if (!coalFeedingActive) {
        startCoalFeeding();
        Serial.println("[ТЕСТ] ✓ Подброс угля АКТИВИРОВАН");
      } else {
        stopCoalFeeding();
        Serial.println("[ТЕСТ] ✓ Подброс угля ОСТАНОВЛЕН");
      }
    }
    else if (command == "s" || command == "status") {
      // Показать статус энкодера
      Serial.println("\n=== СТАТУС ЭНКОДЕРА ===");
      Serial.print("encoderPosition: ");
      Serial.println(encoderPosition);
      Serial.print("lastEncoderPosition: ");
      Serial.println(lastEncoderPosition);
      Serial.print("lastEncoderChange: ");
      Serial.print(millis() - lastEncoderChange);
      Serial.println(" мс назад");
      Serial.print("lastEncoderRotation: ");
      Serial.print(millis() - lastEncoderRotation);
      Serial.println(" мс назад");
      Serial.print("lastEncoderISRTime: ");
      Serial.print(millis() - lastEncoderISRTime);
      Serial.println(" мс назад");
      Serial.print("encoderISRCount (всего прерываний): ");
      Serial.println(encoderISRCount);
      Serial.print("lastEncoderState (бинарный): ");
      Serial.println(lastEncoderState, BIN);
      Serial.print("Уставка: ");
      Serial.print(setpoint, 1);
      Serial.println("°C");
      Serial.print("Состояние кнопки (GPIO ");
      Serial.print(PIN_ENCODER_SW);
      Serial.print("): ");
      bool btnState = digitalRead(PIN_ENCODER_SW);
      Serial.println(btnState == LOW ? "НАЖАТА" : "ОТПУЩЕНА");
      Serial.print("Состояние CLK (GPIO ");
      Serial.print(PIN_ENCODER_CLK);
      Serial.print("): ");
      Serial.println(digitalRead(PIN_ENCODER_CLK));
      Serial.print("Состояние DT (GPIO ");
      Serial.print(PIN_ENCODER_DT);
      Serial.print("): ");
      Serial.println(digitalRead(PIN_ENCODER_DT));
      Serial.print("Подброс угля: ");
      Serial.println(coalFeedingActive ? "АКТИВЕН" : "ВЫКЛ");
      Serial.println("=====================\n");
    }
    else if (command == "h" || command == "help" || command == "?") {
      // Справка
      Serial.println("\n=== КОМАНДЫ ОТЛАДКИ ЭНКОДЕРА ===");
      Serial.println("1 - Тест: поворот влево (уменьшение уставки)");
      Serial.println("2 - Тест: поворот вправо (увеличение уставки)");
      Serial.println("3 - Тест: нажатие кнопки (переключение подброса)");
      Serial.println("s - Показать статус энкодера");
      Serial.println("h - Показать эту справку");
      Serial.println("===============================\n");
    }
    else if (command.length() > 0) {
      Serial.print("[ОШИБКА] Неизвестная команда: ");
      Serial.println(command);
      Serial.println("Введите 'h' для справки");
    }
  }
}

// Функция синхронизации состояния реле с переменными
void syncRelays() {
  // Проверка таймаута ручного управления (2 минуты)
  if (lastManualControlTime > 0) {
    unsigned long elapsed = millis() - lastManualControlTime;
    if (elapsed > MANUAL_CONTROL_TIMEOUT) {
      // Возврат к автоматическому управлению
      if (manualFanControl) {
        manualFanControl = false;
        Serial.println("[Реле] Ручное управление вентилятором отключено (таймаут 2 мин)");
      }
      if (manualPumpControl) {
        manualPumpControl = false;
        Serial.println("[Реле] Ручное управление насосом отключено (таймаут 2 мин)");
      }
      lastManualControlTime = 0;
    }
  }
  
  // Если система выключена, принудительно выключаем реле
  if (!systemEnabled) {
    if (fanState || pumpState) {
      fanState = false;
      pumpState = false;
      // Используем настройки логики для выключенного состояния
      digitalWrite(PIN_RELAY_FAN, relaySettings.fanOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_PUMP, relaySettings.pumpOffIsLow ? LOW : HIGH);
    }
    return;  // Не синхронизируем, если система выключена
  }
  
  // Синхронизация реле вентилятора с переменной fanState
  // Если ручное управление активно, не меняем состояние
  if (!manualFanControl) {
    static bool lastFanState = false;
    if (fanState != lastFanState) {
      // Включено = HIGH, выключено = зависит от настройки
      int fanLevel = fanState ? HIGH : (relaySettings.fanOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_FAN, fanLevel);
      lastFanState = fanState;
    }
  }
  
  // Синхронизация реле насоса с переменной pumpState
  // Если ручное управление активно, не меняем состояние
  if (!manualPumpControl) {
    static bool lastPumpState = false;
    if (pumpState != lastPumpState) {
      // Включено = HIGH, выключено = зависит от настройки
      int pumpLevel = pumpState ? HIGH : (relaySettings.pumpOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_PUMP, pumpLevel);
      lastPumpState = pumpState;
    }
  }
}

// Функция запуска подброса угля
void startCoalFeeding() {
  if (coalFeedingActive) {
    return;  // Уже активен
  }
  
  coalFeedingActive = true;
  coalFeedingStartTime = millis();
  fanStateBeforeCoalFeeding = fanState;
  
  // Выключаем вентилятор
  fanState = false;
  digitalWrite(PIN_RELAY_FAN, LOW);
  
  
  // Публикация в MQTT
  if (mqttSettings.enabled && mqttClient.connected()) {
    String topic = mqttSettings.prefix + "/coalFeeding";
    mqttClient.publish(topic.c_str(), "1");
  }
  
  updateDisplay();
}

// Функция остановки подброса угля
void stopCoalFeeding() {
  if (!coalFeedingActive) {
    return;  // Не активен
  }
  
  coalFeedingActive = false;
  
  // Восстанавливаем состояние вентилятора (или оставляем выключенным, если система выключена)
  if (systemEnabled) {
    // Восстанавливаем предыдущее состояние или оставляем управление автоматике
    // fanState = fanStateBeforeCoalFeeding;  // Можно раскомментировать для восстановления
  } else {
    fanState = false;
  }
  
  digitalWrite(PIN_RELAY_FAN, fanState ? HIGH : LOW);
  
  
  // Публикация в MQTT
  if (mqttSettings.enabled && mqttClient.connected()) {
    String topic = mqttSettings.prefix + "/coalFeeding";
    mqttClient.publish(topic.c_str(), "0");
  }
  
  updateDisplay();
}

// Функция проверки и автоматического завершения подброса угля (резерв на случай забытого включения)
void checkCoalFeeding() {
  if (!coalFeedingActive) {
    return;
  }
  
  unsigned long elapsed = millis() - coalFeedingStartTime;
  
  // Автоматическое завершение через 30 минут (резерв, если забыли выключить)
  if (elapsed >= (30 * 60 * 1000)) {
    stopCoalFeeding();
  }
}

// Получение времени работы подброса угля в секундах (с момента начала)
int getCoalFeedingRemainingSeconds() {
  if (!coalFeedingActive) {
    return 0;
  }
  
  unsigned long elapsed = millis() - coalFeedingStartTime;
  unsigned long remaining = COAL_FEEDING_DURATION - elapsed;
  
  if (remaining > COAL_FEEDING_DURATION) {
    // Защита от переполнения (если elapsed больше длительности)
    return 0;
  }
  
  return (int)(remaining / 1000);  // Возвращаем оставшиеся секунды
}

// Проверка валидности датчика температуры дома
bool isHomeTempSensorValid(unsigned long now) {
  // Сначала проверяем LWT статус - если offline, датчик невалиден
  if (!homeTempSensorLWTOnline) {
    return false;
  }
  
  // Если LWT online, разрешаем режим комфорт, даже если температура еще не получена
  // Это позволяет включить режим комфорт сразу после появления датчика в сети
  // Проверку температуры и таймаута используем только как дополнительную информацию
  
  return true;
}

// Функция управления режимом "Комфорт"
void handleComfortMode(unsigned long now) {
  // Проверка наличия температуры в доме - если датчик offline, переключаемся на режим Авто
  if (!isHomeTempSensorValid(now)) {
    Serial.println("[Comfort] Home temperature sensor offline, switching to Auto mode");
    workMode = 0;
    saveWorkModeToEEPROM();
    comfortState = "WAIT";
    comfortStateStartTime = 0;
    homeTempAtStateStart = 0.0;
    return;
  }
  
  // Вычисляем промежуточную температуру
  float intermediateTemp = (comfortSettings.minBoilerTemp + comfortSettings.maxBoilerTemp) / 2.0;
  
  // Адаптивная температура поддержания
  float maintainTemp = comfortSettings.minBoilerTemp + 5.0;
  if (homeTemp < comfortSettings.targetHomeTemp) {
    maintainTemp = comfortSettings.minBoilerTemp + 3.0;
  } else {
    maintainTemp = comfortSettings.minBoilerTemp + 7.0;
  }
  
  // Защита от перегрева
  if (supplyTemp >= comfortSettings.warningTemp) {
    fanState = false;
    systemState = "OVERHEAT";
    comfortState = "OVERHEAT";
    return;
  }
  
  // Восстановление после перегрева
  if (comfortState == "OVERHEAT" && supplyTemp < comfortSettings.warningTemp) {
    comfortState = "WAIT";
    comfortStateStartTime = now;
    systemState = "Ожидание";
    Serial.println("[Comfort] Восстановление после перегрева");
  }
  
  unsigned long stateElapsed = (comfortStateStartTime == 0) ? 0 : 
    ((now >= comfortStateStartTime) ? (now - comfortStateStartTime) : (ULONG_MAX - comfortStateStartTime + now)) / 60000;
  
  if (comfortState == "WAIT") {
    systemState = "Ожидание";
    fanState = false;
    if (homeTemp < (comfortSettings.targetHomeTemp - comfortSettings.hysteresisOn)) {
      comfortState = "HEATING_1";
      comfortStateStartTime = now;
      homeTempAtStateStart = homeTemp;
    }
  } else if (comfortState == "HEATING_1") {
    systemState = "Разогрев 1";
    if (supplyTemp < intermediateTemp) {
      if (!boilerExtinguished) {
        fanState = true;
        if (fanStartTime == 0) {
          fanStartTime = now;
          maxTempDuringFan = supplyTemp;
        }
      }
    } else {
      fanState = false;
      fanStartTime = 0;
      maxTempDuringFan = 0.0;
      comfortState = "WAIT_COOLING";
      comfortStateStartTime = now;
      homeTempAtStateStart = homeTemp;
    }
  } else if (comfortState == "WAIT_COOLING") {
    systemState = "Ожидание охлаждения";
    if (supplyTemp < 63.0) {
      fanState = true;
    } else if (supplyTemp >= comfortSettings.waitTemp) {
      fanState = false;
    }
    if (stateElapsed >= comfortSettings.waitCoolingTime) {
      if (supplyTemp <= comfortSettings.waitTemp && supplyTemp >= 60.0) {
        comfortState = "WAIT_HEATING";
        comfortStateStartTime = now;
        homeTempAtStateStart = homeTemp;
      } else if (supplyTemp < 55.0) {
        comfortState = "HEATING_1";
        comfortStateStartTime = now;
      }
    }
  } else if (comfortState == "WAIT_HEATING") {
    systemState = "Ожидание прогрева";
    if (supplyTemp < 63.0) {
      fanState = true;
    } else if (supplyTemp >= comfortSettings.waitTemp) {
      fanState = false;
    }
    if (stateElapsed >= comfortSettings.waitAfterHeating1Time) {
      float homeTempChange = homeTemp - homeTempAtStateStart;
      if (homeTempChange >= 0.5 || homeTemp >= comfortSettings.catchUpTemp) {
        comfortState = "COMFORT";
        comfortStateStartTime = now;
        homeTempAtStateStart = homeTemp;
      } else {
        comfortState = "HEATING_2";
        comfortStateStartTime = now;
        homeTempAtStateStart = homeTemp;
      }
    }
  } else if (comfortState == "HEATING_2") {
    systemState = "Разогрев 2";
    if (supplyTemp < comfortSettings.maxBoilerTemp) {
      fanState = true;
    } else {
      fanState = false;
      comfortState = "WAIT_HEATING";
      comfortStateStartTime = now;
      homeTempAtStateStart = homeTemp;
    }
  } else if (comfortState == "COMFORT") {
    systemState = "Комфорт";
    // Проверяем, достигли ли целевой температуры - если да, выключаем вентилятор и переходим в MAINTAIN
    if (homeTemp >= comfortSettings.targetHomeTemp) {
      fanState = false;
      comfortState = "MAINTAIN";
      comfortStateStartTime = now;
      homeTempAtStateStart = homeTemp;
    } else {
      float comfortLow = intermediateTemp - comfortSettings.hysteresisBoiler;
      float comfortHigh = intermediateTemp + comfortSettings.hysteresisBoiler;
      if (supplyTemp < comfortLow) {
        fanState = true;
      } else if (supplyTemp >= comfortHigh) {
        fanState = false;
      }
      if (stateElapsed > 0 && stateElapsed % comfortSettings.inertiaCheckInterval == 0) {
        float homeTempChange = homeTemp - homeTempAtStateStart;
        if (homeTemp < 23.0) {
          comfortState = "HEATING_1";
          comfortStateStartTime = now;
          homeTempAtStateStart = homeTemp;
        } else if (stateElapsed >= comfortSettings.waitAfterReductionTime && homeTempChange < 0.3) {
          comfortState = "HEATING_2";
          comfortStateStartTime = now;
          homeTempAtStateStart = homeTemp;
        }
      }
    }
  } else if (comfortState == "MAINTAIN") {
    systemState = "Поддержание";
    // Если температура дома уже выше целевой с гистерезисом выключения, выключаем вентилятор
    if (homeTemp >= (comfortSettings.targetHomeTemp + comfortSettings.hysteresisOff)) {
      fanState = false;
    } else if (homeTemp < (comfortSettings.targetHomeTemp - comfortSettings.hysteresisOn)) {
      // Если температура упала ниже целевой, начинаем нагрев
      comfortState = "HEATING_1";
      comfortStateStartTime = now;
      homeTempAtStateStart = homeTemp;
      fanState = true;
    } else {
      // Поддержание температуры котла в диапазоне
      float maintainLow = maintainTemp - comfortSettings.hysteresisBoiler;
      float maintainHigh = maintainTemp + comfortSettings.hysteresisBoiler;
      if (supplyTemp < maintainLow) {
        fanState = true;
      } else if (supplyTemp >= maintainHigh) {
        fanState = false;
      }
    }
  }
  
  // Проверка погасания котла в режиме Комфорт (только если не в режиме розжига)
  if (!ignitionInProgress && !boilerExtinguished && fanState) {
    checkBoilerExtinguished(now);
    // Если котел погас, переключаемся в режим Авто
    if (boilerExtinguished) {
      workMode = 0;
      saveWorkModeToEEPROM();
      comfortState = "WAIT";
      comfortStateStartTime = 0;
      homeTempAtStateStart = 0.0;
    }
  }
}

// Функция для отправки HTML интерфейса (потоковая передача)
// Оптимизировано: убраны лишние проверки и отладочные сообщения
void handleWebInterface() {
  // Оптимизированная загрузка веб-интерфейса
  File file = SPIFFS.open("/index.html", "r");
  if (file) {
    // Заголовки для оптимизации загрузки
    server.sendHeader("Cache-Control", "public, max-age=3600");  // Кеш на 1 час
    server.sendHeader("Connection", "keep-alive");  // Keep-alive для быстрых последующих запросов
    server.sendHeader("Content-Encoding", "identity");  // Явно указываем отсутствие сжатия
    
    // Увеличиваем размер буфера для более быстрой передачи
    // Потоковая передача файла (не загружает весь файл в память)
    // streamFile автоматически обрабатывает большие файлы по частям
    server.streamFile(file, "text/html; charset=utf-8");
    file.close();
    return;
  }
  
  // Fallback - простая версия HTML (только при ошибке)
  bool isAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  String ipStr = isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Котел</title></head><body><h1>ESP32 Boiler Control</h1><p>HTML интерфейс не загружен. Загрузите файл index.html в SPIFFS.</p><p>IP: " + ipStr + "</p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// API: Получение статуса
void handleStatus() {
  DynamicJsonDocument doc(1024);
  doc["supplyTemp"] = supplyTemp;
  doc["returnTemp"] = returnTemp;
  doc["boilerTemp"] = boilerTemp;
  doc["outdoorTemp"] = outdoorTemp;
  doc["hysteresis"] = autoSettings.hysteresis;
  doc["homeTemp"] = homeTemp;  // Температура в доме (от ESP01)
  doc["setpoint"] = setpoint;
  doc["fan"] = fanState;
  doc["pump"] = pumpState;
  doc["systemEnabled"] = systemEnabled;
  doc["state"] = systemState;
  doc["workMode"] = workMode;
  doc["workModeName"] = (workMode == 0) ? "Авто" : "Комфорт";
  doc["homeTempSensorValid"] = isHomeTempSensorValid(millis());  // Статус датчика температуры дома
  doc["homeTempSensorLWTOnline"] = homeTempSensorLWTOnline;  // LWT статус датчика (online/offline)
  if (workMode == 1) {
    doc["comfortState"] = comfortState;
    doc["targetHomeTemp"] = comfortSettings.targetHomeTemp;  // Уставка для дома в режиме Комфорт
  }
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Подключен" : "Отключен";
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["mqttStatus"] = (mqttSettings.enabled && mqttClient.connected()) ? "Подключен" : (mqttSettings.enabled ? "Отключен" : "Выключен");
  doc["coalFeeding"] = coalFeedingActive;
  doc["coalFeedingRemaining"] = getCoalFeedingRemainingSeconds();
  
  // Предупреждение о низкой температуре обратки
  bool lowReturnTemp = (pumpState && returnTemp > 0 && returnTemp < 40.0);
  doc["lowReturnTemp"] = lowReturnTemp;
  
  // Предупреждение о прогорании угля
  bool coalBurned = (systemState == "COAL_BURNED");
  doc["coalBurned"] = coalBurned;
  
  // Информация о погасании котла и розжиге
  doc["boilerExtinguished"] = boilerExtinguished;
  doc["ignitionInProgress"] = ignitionInProgress;
  if (ignitionInProgress) {
    unsigned long ignitionElapsed = (millis() >= ignitionStartTime) ? (millis() - ignitionStartTime) : (ULONG_MAX - ignitionStartTime + millis());
    doc["ignitionElapsed"] = ignitionElapsed / 1000;  // секунды
    doc["ignitionStartTemp"] = ignitionStartTemp;
    doc["ignitionTempIncrease"] = supplyTemp - ignitionStartTemp;
  }
  
  // Статистика работы вентилятора
  doc["fanStats"] = JsonObject();
  doc["fanStats"]["totalWorkTime"] = fanStats.totalWorkTime / 1000;  // секунды
  doc["fanStats"]["dailyWorkTime"] = fanStats.dailyWorkTime / 1000;  // секунды
  doc["fanStats"]["cycleCount"] = fanStats.cycleCount;
  doc["fanStats"]["dailyCycleCount"] = fanStats.dailyCycleCount;
  if (fanStartTime > 0 && fanState) {
    unsigned long currentWorkTime = (millis() >= fanStartTime) ? (millis() - fanStartTime) : (ULONG_MAX - fanStartTime + millis());
    doc["fanStats"]["currentWorkTime"] = currentWorkTime / 1000;  // секунды
  } else {
    doc["fanStats"]["currentWorkTime"] = 0;
  }
  
  // Диагностическая информация
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["cpuLoad"] = cpuLoad;
  doc["uptime"] = millis() / 1000;  // Время работы в секундах
  
  // Информация о тренде температуры (1 = рост, 0 = стабильно, -1 = падение)
  // Показываем только если есть рост (1) или падение (-1)
  int supplyTrend = getTemperatureTrend(&supplyHistory);
  int returnTrend = getTemperatureTrend(&returnHistory);
  int boilerTrend = getTemperatureTrend(&boilerHistory);
  int outdoorTrend = getTemperatureTrend(&outdoorHistory);
  int homeTrend = getTemperatureTrend(&homeHistory);
  
  // Добавляем тренды только если они показывают рост (1) или падение (-1)
  if (supplyTrend == 1) doc["supplyTrend"] = 1;
  else if (supplyTrend == -1) doc["supplyTrend"] = -1;
  
  if (returnTrend == 1) doc["returnTrend"] = 1;
  else if (returnTrend == -1) doc["returnTrend"] = -1;
  
  if (boilerTrend == 1) doc["boilerTrend"] = 1;
  else if (boilerTrend == -1) doc["boilerTrend"] = -1;
  
  if (outdoorTrend == 1) doc["outdoorTrend"] = 1;
  else if (outdoorTrend == -1) doc["outdoorTrend"] = -1;
  
  if (homeTrend == 1) doc["homeTrend"] = 1;
  else if (homeTrend == -1) doc["homeTrend"] = -1;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Диагностика системы (для обнаружения зависаний)
void handleDiagnostics() {
  DynamicJsonDocument doc(2048);
  unsigned long now = millis();
  
  // Информация о памяти
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
  doc["heapSize"] = ESP.getHeapSize();
  
  // Информация о времени работы
  unsigned long uptime = now / 1000;
  doc["uptime"] = uptime;
  unsigned long hours = uptime / 3600;
  unsigned long minutes = (uptime % 3600) / 60;
  unsigned long seconds = uptime % 60;
  char uptimeStr[32];
  snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
  doc["uptimeFormatted"] = String(uptimeStr);
  
  // Загрузка CPU
  doc["cpuLoad"] = cpuLoad;
  
  // Информация о WiFi
  doc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
  doc["wifiRSSI"] = WiFi.RSSI();
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifiIP"] = WiFi.localIP().toString();
  } else {
    doc["wifiIP"] = WiFi.softAPIP().toString();
  }
  
  // Информация о MQTT
  doc["mqttConnected"] = mqttClient.connected();
  doc["mqttEnabled"] = mqttSettings.enabled;
  
  // Heartbeat - счетчик итераций loop()
  static unsigned long heartbeatCounter = 0;
  static unsigned long lastHeartbeatReset = 0;
  if (now - lastHeartbeatReset > 60000 || now < lastHeartbeatReset) {
    heartbeatCounter = 0;
    lastHeartbeatReset = now;
  }
  heartbeatCounter++;
  doc["heartbeatPerMinute"] = heartbeatCounter;
  
  // Информация о системе
  doc["systemEnabled"] = systemEnabled;
  doc["systemState"] = systemState;
  
  // Предупреждения
  JsonArray warnings = doc.createNestedArray("warnings");
  if (ESP.getFreeHeap() < 50000) warnings.add("Low free heap memory");
  if (ESP.getMinFreeHeap() < 30000) warnings.add("Very low minimum free heap");
  if (cpuLoad > 80.0) warnings.add("High CPU load");
  if (heartbeatCounter < 100) warnings.add("Low heartbeat - possible freeze");
  if (WiFi.status() != WL_CONNECTED) warnings.add("WiFi disconnected");
  if (mqttSettings.enabled && !mqttClient.connected()) warnings.add("MQTT disconnected");
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Установка уставки
void handleSetpoint() {
  if (server.hasArg("value")) {
    float value = server.arg("value").toFloat();
    // В режиме Комфорт уставка не меняется через этот API (используется targetHomeTemp)
    if (workMode == 1) {
      server.send(400, "application/json", "{\"error\":\"Use comfort settings API in Comfort mode\"}");
      return;
    }
    if (value >= 40 && value <= 80) {
      setpoint = value;
      autoSettings.setpoint = value;
      
      // Отложенное сохранение в EEPROM
      autoSettingsDirty = true;
      lastAutoSettingsChange = millis();
      
      // Публикация уставки в MQTT (неблокирующая)
      if (mqttSettings.enabled && mqttClient.connected()) {
        String topic = mqttSettings.prefix + "/setpoint";
        mqttClient.publish(topic.c_str(), String(setpoint, 1).c_str(), false);
      }
      
      DynamicJsonDocument doc(200);
      doc["success"] = true;
      doc["setpoint"] = setpoint;
      String response;
      serializeJson(doc, response);
      server.send(200, "application/json", response);
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid value\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing value parameter\"}");
  }
}

// API: Управление устройствами
void handleControl() {
  if (server.hasArg("device") && server.hasArg("state")) {
    String device = server.arg("device");
    bool state = server.arg("state").toInt() == 1;
    
    bool manual = server.hasArg("manual") && server.arg("manual").toInt() == 1;  // Инженерное управление
    
    if (device == "fan") {
      fanState = state;
      // Управление реле вентилятора с учетом логики
      int fanLevel = state ? HIGH : (relaySettings.fanOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_FAN, fanLevel);
      
      if (manual) {
        manualFanControl = true;
        lastManualControlTime = millis();
        Serial.print("[Инженерное] Вентилятор: ");
      } else {
        manualFanControl = false;
        Serial.print("Вентилятор: ");
      }
      Serial.print(state ? "ВКЛ (HIGH)" : "ВЫКЛ (");
      Serial.print(relaySettings.fanOffIsLow ? "LOW" : "HIGH");
      Serial.println(")");
    } else if (device == "pump") {
      pumpState = state;
      // Управление реле насоса с учетом логики
      int pumpLevel = state ? HIGH : (relaySettings.pumpOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_PUMP, pumpLevel);
      
      if (manual) {
        manualPumpControl = true;
        lastManualControlTime = millis();
        Serial.print("[Инженерное] Насос: ");
      } else {
        manualPumpControl = false;
        Serial.print("Насос: ");
      }
      Serial.print(state ? "ВКЛ (HIGH)" : "ВЫКЛ (");
      Serial.print(relaySettings.pumpOffIsLow ? "LOW" : "HIGH");
      Serial.println(")");
    } else if (device == "sensors") {
      sensorsRelayState = state;
      // Управление реле датчиков с учетом логики (обратная логика вентилятора)
      int sensorsLevel = state ? HIGH : (relaySettings.sensorsOffIsLow ? LOW : HIGH);
      digitalWrite(PIN_RELAY_SENSORS, sensorsLevel);
      Serial.print("[Реле датчиков] ");
      Serial.print(state ? "ВКЛ (HIGH)" : "ВЫКЛ (");
      Serial.print(relaySettings.sensorsOffIsLow ? "LOW" : "HIGH");
      Serial.println(")");
      
      // Если выключили реле датчиков, планируем автоматическое включение через 500мс для сброса ошибки
      if (!state) {
        sensorsResetPending = true;
        sensorsResetStartTime = millis();
        Serial.println("[Реле датчиков] Запланирован сброс через 500мс");
      }
    }
    
    DynamicJsonDocument doc(200);
    doc["success"] = true;
    doc["device"] = device;
    doc["state"] = state;
    doc["manual"] = manual;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
  }
}

// API: Настройки реле - GET
void handleRelaySettingsGet() {
  DynamicJsonDocument doc(256);
  doc["fanOffIsLow"] = relaySettings.fanOffIsLow;
  doc["pumpOffIsLow"] = relaySettings.pumpOffIsLow;
  doc["sensorsOffIsLow"] = relaySettings.sensorsOffIsLow;
  doc["sensorsRelayState"] = sensorsRelayState;
  doc["manualFanControl"] = manualFanControl;
  doc["manualPumpControl"] = manualPumpControl;
  if (lastManualControlTime > 0) {
    unsigned long elapsed = millis() - lastManualControlTime;
    doc["manualControlRemaining"] = (MANUAL_CONTROL_TIMEOUT - elapsed) / 1000;  // секунды
  } else {
    doc["manualControlRemaining"] = 0;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Настройки реле - POST
void handleRelaySettingsPost() {
  if (server.hasArg("plain")) {
    String plainData = server.arg("plain");
    Serial.print("[Реле] Получены данные: ");
    Serial.println(plainData);
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, plainData);
    
    if (error) {
      Serial.print("[Реле] Ошибка парсинга JSON: ");
      Serial.println(error.c_str());
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    Serial.print("[Реле] Текущие настройки: fanOffIsLow=");
    Serial.print(relaySettings.fanOffIsLow ? "true" : "false");
    Serial.print(", pumpOffIsLow=");
    Serial.println(relaySettings.pumpOffIsLow ? "true" : "false");
    
    bool changed = false;
    if (doc.containsKey("fanOffIsLow")) {
      bool newValue = doc["fanOffIsLow"].as<bool>();
      Serial.print("[Реле] Новое значение fanOffIsLow: ");
      Serial.println(newValue ? "true" : "false");
      if (relaySettings.fanOffIsLow != newValue) {
        relaySettings.fanOffIsLow = newValue;
        changed = true;
        Serial.println("[Реле] ✓ fanOffIsLow изменено");
      }
    }
    if (doc.containsKey("pumpOffIsLow")) {
      bool newValue = doc["pumpOffIsLow"].as<bool>();
      Serial.print("[Реле] Новое значение pumpOffIsLow: ");
      Serial.println(newValue ? "true" : "false");
      if (relaySettings.pumpOffIsLow != newValue) {
        relaySettings.pumpOffIsLow = newValue;
        changed = true;
        Serial.println("[Реле] ✓ pumpOffIsLow изменено");
      }
    }
    if (doc.containsKey("sensorsOffIsLow")) {
      bool newValue = doc["sensorsOffIsLow"].as<bool>();
      Serial.print("[Реле] Новое значение sensorsOffIsLow: ");
      Serial.println(newValue ? "true" : "false");
      if (relaySettings.sensorsOffIsLow != newValue) {
        relaySettings.sensorsOffIsLow = newValue;
        changed = true;
        Serial.println("[Реле] ✓ sensorsOffIsLow изменено");
      }
    }
    
    // Сохраняем всегда, даже если значения не изменились (для надежности)
    saveRelaySettingsToEEPROM();
    Serial.println("[Реле] Настройки сохранены в EEPROM");
    // Применяем новые настройки к текущему состоянию реле
    syncRelays();
    
    DynamicJsonDocument responseDoc(256);
    responseDoc["success"] = true;
    responseDoc["fanOffIsLow"] = relaySettings.fanOffIsLow;
    responseDoc["pumpOffIsLow"] = relaySettings.pumpOffIsLow;
    responseDoc["sensorsOffIsLow"] = relaySettings.sensorsOffIsLow;
    String response;
    serializeJson(responseDoc, response);
    Serial.print("[Реле] Отправка ответа: ");
    Serial.println(response);
    server.send(200, "application/json", response);
  } else {
    Serial.println("[Реле] POST запрос без данных");
    server.send(400, "application/json", "{\"error\":\"No data\"}");
  }
}

// API: Настройки Авто - GET
void handleAutoSettingsGet() {
  DynamicJsonDocument doc(512);
  doc["setpoint"] = autoSettings.setpoint;
  doc["minTemp"] = autoSettings.minTemp;
  doc["maxTemp"] = autoSettings.maxTemp;
  doc["hysteresis"] = autoSettings.hysteresis;
  doc["inertiaTemp"] = autoSettings.inertiaTemp;
  doc["inertiaTime"] = autoSettings.inertiaTime;
  doc["overheatTemp"] = autoSettings.overheatTemp;
  doc["heatingTimeout"] = autoSettings.heatingTimeout;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Настройки Авто - POST
void handleAutoSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("setpoint")) {
      float newSetpoint = doc["setpoint"].as<float>();
      if (newSetpoint != autoSettings.setpoint) {
        // Уставка изменилась - сбрасываем таймеры переключения вентилятора
        lastFanToggleTime = 0;
        lastFanToggleTemp = supplyTemp;
        Serial.println("[Auto] Setpoint changed - resetting fan toggle timers");
      }
      autoSettings.setpoint = newSetpoint;
    }
    if (doc.containsKey("minTemp")) autoSettings.minTemp = doc["minTemp"];
    if (doc.containsKey("maxTemp")) autoSettings.maxTemp = doc["maxTemp"];
    if (doc.containsKey("hysteresis")) autoSettings.hysteresis = doc["hysteresis"];
    if (doc.containsKey("inertiaTemp")) autoSettings.inertiaTemp = doc["inertiaTemp"];
    if (doc.containsKey("inertiaTime")) autoSettings.inertiaTime = doc["inertiaTime"];
    if (doc.containsKey("overheatTemp")) autoSettings.overheatTemp = doc["overheatTemp"];
    if (doc.containsKey("heatingTimeout")) autoSettings.heatingTimeout = doc["heatingTimeout"];
    
    setpoint = autoSettings.setpoint;
    
    // Сохраняем сразу при изменении через веб-интерфейс (это редкая операция)
    saveAutoSettingsToEEPROM();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Получение режима работы
void handleWorkModeGet() {
  DynamicJsonDocument doc(256);
  doc["mode"] = workMode;
  doc["modeName"] = (workMode == 0) ? "Авто" : "Комфорт";
  doc["comfortState"] = comfortState;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Установка режима работы
void handleWorkModePost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("mode")) {
      int newMode = doc["mode"];
      if (newMode == 0 || newMode == 1) {
        // Проверка: нельзя переключиться в режим Комфорт, если датчик температуры дома offline
        if (newMode == 1 && !isHomeTempSensorValid(millis())) {
          server.send(400, "application/json", "{\"error\":\"Home temperature sensor offline. Cannot switch to Comfort mode.\"}");
          return;
        }
        
        workMode = newMode;
        // Сброс состояний при переключении
        comfortState = "WAIT";
        comfortStateStartTime = 0;
        homeTempAtStateStart = 0.0;
        heatingStartTime = 0;
        
        saveWorkModeToEEPROM();
        
        server.send(200, "application/json", "{\"success\":true,\"mode\":" + String(workMode) + "}");
      } else {
        server.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Missing mode parameter\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Получение настроек комфорт
void handleComfortSettingsGet() {
  DynamicJsonDocument doc(1024);
  doc["targetHomeTemp"] = comfortSettings.targetHomeTemp;
  doc["minBoilerTemp"] = comfortSettings.minBoilerTemp;
  doc["maxBoilerTemp"] = comfortSettings.maxBoilerTemp;
  doc["waitTemp"] = comfortSettings.waitTemp;
  doc["catchUpTemp"] = comfortSettings.catchUpTemp;
  doc["waitCoolingTime"] = comfortSettings.waitCoolingTime;
  doc["waitAfterHeating1Time"] = comfortSettings.waitAfterHeating1Time;
  doc["waitAfterReductionTime"] = comfortSettings.waitAfterReductionTime;
  doc["inertiaCheckInterval"] = comfortSettings.inertiaCheckInterval;
  doc["hysteresisOn"] = comfortSettings.hysteresisOn;
  doc["hysteresisOff"] = comfortSettings.hysteresisOff;
  doc["hysteresisBoiler"] = comfortSettings.hysteresisBoiler;
  doc["warningTemp"] = comfortSettings.warningTemp;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сохранение настроек комфорт
void handleComfortSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    bool targetHomeTempChanged = false;
    if (doc.containsKey("targetHomeTemp")) {
      float oldTarget = comfortSettings.targetHomeTemp;
      comfortSettings.targetHomeTemp = doc["targetHomeTemp"];
      if (abs(oldTarget - comfortSettings.targetHomeTemp) > 0.1) {
        targetHomeTempChanged = true;
      }
    }
    if (doc.containsKey("minBoilerTemp")) comfortSettings.minBoilerTemp = doc["minBoilerTemp"];
    if (doc.containsKey("maxBoilerTemp")) comfortSettings.maxBoilerTemp = doc["maxBoilerTemp"];
    if (doc.containsKey("waitTemp")) comfortSettings.waitTemp = doc["waitTemp"];
    if (doc.containsKey("catchUpTemp")) comfortSettings.catchUpTemp = doc["catchUpTemp"];
    if (doc.containsKey("waitCoolingTime")) comfortSettings.waitCoolingTime = doc["waitCoolingTime"];
    if (doc.containsKey("waitAfterHeating1Time")) comfortSettings.waitAfterHeating1Time = doc["waitAfterHeating1Time"];
    if (doc.containsKey("waitAfterReductionTime")) comfortSettings.waitAfterReductionTime = doc["waitAfterReductionTime"];
    if (doc.containsKey("inertiaCheckInterval")) comfortSettings.inertiaCheckInterval = doc["inertiaCheckInterval"];
    if (doc.containsKey("hysteresisOn")) comfortSettings.hysteresisOn = doc["hysteresisOn"];
    if (doc.containsKey("hysteresisOff")) comfortSettings.hysteresisOff = doc["hysteresisOff"];
    if (doc.containsKey("hysteresisBoiler")) comfortSettings.hysteresisBoiler = doc["hysteresisBoiler"];
    if (doc.containsKey("warningTemp")) comfortSettings.warningTemp = doc["warningTemp"];
    
    // При изменении целевой температуры сбрасываем счетчики ожиданий
    if (targetHomeTempChanged && workMode == 1) {
      comfortStateStartTime = 0;
      homeTempAtStateStart = homeTemp;
      // Если температура уже достигла целевой, переходим в MAINTAIN
      if (homeTemp >= comfortSettings.targetHomeTemp) {
        comfortState = "MAINTAIN";
      } else {
        comfortState = "WAIT";
      }
    }
    
    saveComfortSettingsToEEPROM();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Настройки MQTT - GET
void handleMqttSettingsGet() {
  DynamicJsonDocument doc(512);
  doc["enabled"] = mqttSettings.enabled;
  doc["server"] = mqttSettings.server;
  doc["port"] = mqttSettings.port;
  doc["useTLS"] = mqttSettings.useTLS;
  doc["user"] = mqttSettings.user;
  doc["password"] = mqttSettings.password;
  doc["prefix"] = mqttSettings.prefix;
  doc["tempInterval"] = mqttSettings.tempInterval;
  doc["stateInterval"] = mqttSettings.stateInterval;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Настройки MQTT - POST
void handleMqttSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("enabled")) mqttSettings.enabled = doc["enabled"];
    if (doc.containsKey("server")) mqttSettings.server = doc["server"].as<String>();
    if (doc.containsKey("port")) mqttSettings.port = doc["port"];
    if (doc.containsKey("useTLS")) mqttSettings.useTLS = doc["useTLS"];
    if (doc.containsKey("user")) mqttSettings.user = doc["user"].as<String>();
    if (doc.containsKey("password")) mqttSettings.password = doc["password"].as<String>();
    if (doc.containsKey("prefix")) mqttSettings.prefix = doc["prefix"].as<String>();
    if (doc.containsKey("tempInterval")) mqttSettings.tempInterval = doc["tempInterval"];
    if (doc.containsKey("stateInterval")) mqttSettings.stateInterval = doc["stateInterval"];
    
    saveMqttSettingsToEEPROM();
    
    // Переподключение MQTT клиента
    if (mqttSettings.enabled) {
      // Публикуем offline перед отключением
      if (mqttClient.connected()) {
        String statusTopic = mqttSettings.prefix + "/status";
        mqttClient.publish(statusTopic.c_str(), "offline", true);  // true = retain
        // Неблокирующая задержка для публикации MQTT
        unsigned long startTime = millis();
        for (int i = 0; i < 10; i++) {
          yield();
          mqttClient.loop();
          if (millis() - startTime >= 100 || millis() < startTime) break;
        }
      }
      mqttClient.disconnect();
      mqttConnect();
    } else {
      // Публикуем offline перед отключением
      if (mqttClient.connected()) {
        String statusTopic = mqttSettings.prefix + "/status";
        mqttClient.publish(statusTopic.c_str(), "offline", true);  // true = retain
        // Неблокирующая задержка для публикации MQTT
        unsigned long startTime = millis();
        for (int i = 0; i < 10; i++) {
          yield();
          mqttClient.loop();
          if (millis() - startTime >= 100 || millis() < startTime) break;
        }
      }
      mqttClient.disconnect();
    }
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Тест MQTT подключения
void handleMqttTest() {
  bool connected = mqttConnect();
  DynamicJsonDocument doc(200);
  doc["success"] = connected;
  if (connected) {
    doc["message"] = "Подключение успешно";
  } else {
    doc["message"] = "Ошибка подключения: " + String(mqttClient.state());
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Запуск розжига
void handleIgnition() {
  if (boilerExtinguished || systemState == "КОТЕЛ_ПОГАС" || systemState == "ОШИБКА_РОЗЖИГА") {
    startIgnition();
    DynamicJsonDocument doc(200);
    doc["success"] = true;
    doc["message"] = "Розжиг запущен";
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    DynamicJsonDocument doc(200);
    doc["success"] = false;
    doc["message"] = "Розжиг недоступен. Котел не погас.";
    String response;
    serializeJson(doc, response);
    server.send(400, "application/json", response);
  }
}

// Функция сброса всех таймеров при выключении системы
void resetAllTimers() {
  fanStartTime = 0;
  heatingStartTime = 0;
  coalFeedingStartTime = 0;
  ignitionStartTime = 0;
  sensorsResetStartTime = 0;
  lastManualControlTime = 0;
  lastAutoSettingsChange = 0;
  lastHomeTempUpdate = 0;
  lastSensorsDetectedTime = 0;
  lastValidSupplyTempTime = 0;
  lastValidReturnTempTime = 0;
  lastValidBoilerTempTime = 0;
  lastValidOutdoorTempTime = 0;
  tempRequestTime = 0;
  lastPumpRunTime = 0;
  coalBurnedCheckStart = 0;
  wifiScanStartTime = 0;
  lastFanToggleTime = 0;
  pendingRebootTime = 0;
  maxTempDuringFan = 0.0;
  boilerExtinguished = false;
  ignitionInProgress = false;
  coalFeedingActive = false;
  sensorsResetPending = false;
  sensorsAutoResetInProgress = false;
  systemState = "IDLE";
  Serial.println("[TIMERS] Все таймеры сброшены");
}

// Функция определения состояния системы при запуске
void determineSystemStateOnStartup() {
  Serial.println("[STARTUP] Определение состояния системы...");
  
  // Если система выключена, сбрасываем все таймеры
  if (!systemEnabled) {
    resetAllTimers();
    fanState = false;
    pumpState = false;
    systemState = "IDLE";
    Serial.println("[STARTUP] Система выключена - все таймеры сброшены");
    return;
  }
  
  // Инициализируем время обнаружения датчиков, если оно не было установлено
  if (lastSensorsDetectedTime == 0) {
    lastSensorsDetectedTime = millis();
  }
  
  // Инициализируем время валидных показаний, если они не были установлены
  unsigned long now = millis();
  if (lastValidSupplyTempTime == 0) {
    lastValidSupplyTempTime = now;
  }
  if (lastValidReturnTempTime == 0) {
    lastValidReturnTempTime = now;
  }
  if (lastValidBoilerTempTime == 0) {
    lastValidBoilerTempTime = now;
  }
  if (lastValidOutdoorTempTime == 0) {
    lastValidOutdoorTempTime = now;
  }
  
  // Определяем начальное состояние на основе текущих условий
  // Если вентилятор или насос включены, но таймеры не установлены - сбрасываем состояние
  if (fanState && fanStartTime == 0) {
    // Вентилятор включен, но таймер не установлен - возможно, это было до перезагрузки
    // Устанавливаем таймер с текущего времени
    fanStartTime = now;
    heatingStartTime = now;
    Serial.println("[STARTUP] Вентилятор включен - устанавливаем таймеры");
  }
  
  if (pumpState && lastPumpRunTime == 0) {
    lastPumpRunTime = now;
    Serial.println("[STARTUP] Насос включен - устанавливаем таймер");
  }
  
  // Если розжиг был в процессе, но система перезагрузилась - сбрасываем
  if (ignitionInProgress) {
    ignitionInProgress = false;
    ignitionStartTime = 0;
    Serial.println("[STARTUP] Розжиг был в процессе - сбрасываем (перезагрузка)");
  }
  
  // Если подброс угля был активен, но система перезагрузилась - сбрасываем
  if (coalFeedingActive) {
    coalFeedingActive = false;
    coalFeedingStartTime = 0;
    Serial.println("[STARTUP] Подброс угля был активен - сбрасываем (перезагрузка)");
  }
  
  // Если система была в состоянии "КОТЕЛ ПОГАС" или "ОШИБКА_РОЗЖИГА" - сбрасываем
  if (systemState == "КОТЕЛ_ПОГАС" || systemState == "ОШИБКА_РОЗЖИГА") {
    boilerExtinguished = false;
    systemState = "IDLE";
    Serial.println("[STARTUP] Сбрасываем состояние погасания/ошибки розжига");
  }
  
  // Устанавливаем начальное состояние, если оно не определено
  if (systemState == "" || systemState == "IDLE") {
    if (fanState) {
      systemState = "HEATING";
    } else {
      systemState = "IDLE";
    }
  }
  
  Serial.print("[STARTUP] Текущее состояние: ");
  Serial.println(systemState);
  Serial.print("[STARTUP] Система включена: ");
  Serial.println(systemEnabled ? "Да" : "Нет");
  Serial.print("[STARTUP] Вентилятор: ");
  Serial.println(fanState ? "Включен" : "Выключен");
  Serial.print("[STARTUP] Насос: ");
  Serial.println(pumpState ? "Включен" : "Выключен");
}

// API: Управление системой (включение/выключение)
void handleSystemControl() {
  if (server.hasArg("enabled")) {
    systemEnabled = server.arg("enabled").toInt() == 1;
    saveSystemEnabledToEEPROM();
    
    // Если система выключена, выключаем реле и сбрасываем таймеры
    if (!systemEnabled) {
      fanState = false;
      pumpState = false;
      // Управление реле: выключено = LOW
      digitalWrite(PIN_RELAY_FAN, LOW);
      digitalWrite(PIN_RELAY_PUMP, LOW);
      resetAllTimers();  // Сбрасываем все таймеры
      Serial.println("Система выключена - реле отключены, таймеры сброшены");
    }
    
    DynamicJsonDocument doc(200);
    doc["success"] = true;
    doc["systemEnabled"] = systemEnabled;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing enabled parameter\"}");
  }
}

// API: Сброс состояния системы (погас, ошибка розжига и т.д.)
void handleSystemReset() {
  // Сбрасываем состояние погасания и ошибок
  boilerExtinguished = false;
  ignitionInProgress = false;
  ignitionStartTime = 0;
  
  // Сбрасываем состояние системы, если оно было в ошибке
  if (systemState == "КОТЕЛ_ПОГАС" || systemState == "ОШИБКА_РОЗЖИГА") {
    systemState = "IDLE";
  }
  
  // Сбрасываем таймеры, связанные с погасанием
  fanStartTime = 0;
  maxTempDuringFan = 0.0;
  
  Serial.println("[API] Сброс состояния системы выполнен");
  
  DynamicJsonDocument doc(200);
  doc["success"] = true;
  doc["message"] = "Состояние сброшено";
  doc["boilerExtinguished"] = false;
  doc["ignitionInProgress"] = false;
  doc["systemState"] = systemState;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: WiFi информация
void handleWiFiInfo() {
  DynamicJsonDocument doc(256);
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["mac"] = WiFi.macAddress();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сканирование WiFi сетей (асинхронное)
void handleWiFiScan() {
  // Если сканирование уже идет, возвращаем статус
  if (wifiScanInProgress) {
    DynamicJsonDocument doc(200);
    doc["scanning"] = true;
    doc["message"] = "Сканирование в процессе...";
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    return;
  }
  
  // Запускаем асинхронное сканирование
  wifiScanInProgress = true;
  wifiScanStartTime = millis();
  WiFi.scanNetworks(true, true);  // async=true, show_hidden=true
  
  DynamicJsonDocument doc(200);
  doc["scanning"] = true;
  doc["message"] = "Сканирование запущено";
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Функция обработки результатов сканирования WiFi
void processWiFiScanResults() {
  if (!wifiScanInProgress) {
    return;
  }
  
  int n = WiFi.scanComplete();
  
  // Сканирование еще не завершено
  if (n == WIFI_SCAN_RUNNING) {
    // Проверка таймаута (максимум 10 секунд) с защитой от переполнения millis()
    unsigned long now = millis();
    unsigned long elapsed = (now >= wifiScanStartTime) ? (now - wifiScanStartTime) : (ULONG_MAX - wifiScanStartTime + now);
    if (elapsed > 10000) {
      WiFi.scanDelete();
      wifiScanInProgress = false;
    }
    return;
  }
  
  // Сканирование завершено
  wifiScanInProgress = false;
  wifiScanResult = n;
  
  
  // Результаты будут отправлены при следующем запросе
}

// API: Получение результатов сканирования WiFi
void handleWiFiScanResults() {
  DynamicJsonDocument doc(4096);
  
  if (wifiScanInProgress) {
    doc["scanning"] = true;
    doc["message"] = "Сканирование в процессе...";
  } else {
    // Читаем результаты напрямую из WiFi
    int n = WiFi.scanComplete();
    
    // Если сканирование не было выполнено, возвращаем пустой результат
    if (n == WIFI_SCAN_FAILED || n == WIFI_SCAN_RUNNING) {
      doc["count"] = 0;
      doc["success"] = true;
      doc["scanning"] = false;
      JsonArray networks = doc.createNestedArray("networks");
    } else {
      JsonArray networks = doc.createNestedArray("networks");
      
      if (n > 0) {
        for (int i = 0; i < n && i < 20; i++) {  // Ограничение до 20 сетей
          JsonObject network = networks.createNestedObject();
          String ssid = WiFi.SSID(i);
          network["ssid"] = ssid.length() > 0 ? ssid : "(скрытая сеть)";
          network["rssi"] = WiFi.RSSI(i);
          network["channel"] = WiFi.channel(i);
          
          // Определение типа шифрования
          wifi_auth_mode_t auth = WiFi.encryptionType(i);
          String authStr = "Неизвестно";
          if (auth == WIFI_AUTH_OPEN) authStr = "Открытая";
          else if (auth == WIFI_AUTH_WEP) authStr = "WEP";
          else if (auth == WIFI_AUTH_WPA_PSK) authStr = "WPA";
          else if (auth == WIFI_AUTH_WPA2_PSK) authStr = "WPA2";
          else if (auth == WIFI_AUTH_WPA_WPA2_PSK) authStr = "WPA/WPA2";
          else if (auth == WIFI_AUTH_WPA2_ENTERPRISE) authStr = "WPA2 Enterprise";
          else if (auth == WIFI_AUTH_WPA3_PSK) authStr = "WPA3";
          else if (auth == WIFI_AUTH_WPA2_WPA3_PSK) authStr = "WPA2/WPA3";
          
          network["encryption"] = authStr;
          network["hidden"] = (ssid.length() == 0);
        }
      }
      
      doc["count"] = n;
      doc["success"] = true;
      doc["scanning"] = false;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Получение настроек WiFi
void handleWiFiSettingsGet() {
  DynamicJsonDocument doc(512);
  doc["primarySSID"] = wifiSettings.primarySSID;
  doc["primaryPassword"] = wifiSettings.primaryPassword;
  doc["backupSSID"] = wifiSettings.backupSSID;
  doc["backupPassword"] = wifiSettings.backupPassword;
  doc["useBackup"] = wifiSettings.useBackup;
  doc["antennaTuning"] = wifiSettings.antennaTuning;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сохранение настроек WiFi
void handleWiFiSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("primarySSID")) wifiSettings.primarySSID = doc["primarySSID"].as<String>();
    if (doc.containsKey("primaryPassword")) wifiSettings.primaryPassword = doc["primaryPassword"].as<String>();
    if (doc.containsKey("backupSSID")) wifiSettings.backupSSID = doc["backupSSID"].as<String>();
    if (doc.containsKey("backupPassword")) wifiSettings.backupPassword = doc["backupPassword"].as<String>();
    if (doc.containsKey("useBackup")) wifiSettings.useBackup = doc["useBackup"];
    if (doc.containsKey("antennaTuning")) wifiSettings.antennaTuning = doc["antennaTuning"];
    
    saveWiFiSettingsToEEPROM();
    
    server.send(200, "application/json", "{\"success\":true}");
    
    // Перезагрузка для применения настроек
    pendingRebootTime = millis() + 1000;
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Получение уровня сигнала WiFi для настройки антенны
void handleWiFiSignalStrength() {
  DynamicJsonDocument doc(128);
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    doc["rssi"] = rssi;
    // Конвертация RSSI в проценты: -100 dBm = 0%, -50 dBm = 100%
    // Формула: percentage = 2 * (RSSI + 100) для диапазона -100 до -50
    int percentage = 0;
    if (rssi >= -50) {
      percentage = 100;
    } else if (rssi <= -100) {
      percentage = 0;
    } else {
      percentage = 2 * (rssi + 100);
    }
    doc["percentage"] = percentage;
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
  } else {
    doc["rssi"] = 0;
    doc["percentage"] = 0;
    doc["connected"] = false;
    doc["ssid"] = "";
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сброс WiFi
void handleWiFiReset() {
  server.send(200, "application/json", "{\"success\":true}");
  // Неблокирующий сброс - перезагрузка произойдет после отправки ответа
  wifiManager.resetSettings();
  // Очищаем сохраненные настройки
  wifiSettings.primarySSID = "";
  wifiSettings.primaryPassword = "";
  wifiSettings.backupSSID = "";
  wifiSettings.backupPassword = "";
  saveWiFiSettingsToEEPROM();
  // Устанавливаем время для перезагрузки
  pendingRebootTime = millis() + 500;  // Перезагрузка через 500мс
}

// API: Сканирование датчиков (обе шины)
void handleSensorsScan() {
  Serial.println("Сканирование датчиков DS18B20 на обеих шинах...");
  
  DynamicJsonDocument doc(2048);
  JsonArray sensorsArray = doc.createNestedArray("sensors");
  
  int totalCount = 0;
  
  // Сканирование первой шины (GPIO 4: Подача, Обратка)
  Serial.println("Шина 1 (GPIO 4):");
  sensors1.begin();
  int deviceCount1 = sensors1.getDeviceCount();
  Serial.print("Найдено датчиков на шине 1: ");
  Serial.println(deviceCount1);
  
  if (deviceCount1 > 0) {
    DeviceAddress deviceAddress;
    for (int i = 0; i < deviceCount1 && i < 10; i++) {
      if (sensors1.getAddress(deviceAddress, i)) {
        JsonObject sensor = sensorsArray.createNestedObject();
        
        String addressStr = "";
        for (uint8_t j = 0; j < 8; j++) {
          if (deviceAddress[j] < 16) addressStr += "0";
          addressStr += String(deviceAddress[j], HEX);
        }
        addressStr.toUpperCase();
        
        sensor["address"] = addressStr;
        sensor["bus"] = 1;  // Номер шины
        sensor["pin"] = PIN_DS18B20_1;
        
        sensors1.requestTemperatures();
        float temp = sensors1.getTempC(deviceAddress);
        
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          sensor["temperature"] = temp;
        }
        
        Serial.print("  Датчик ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(addressStr);
        Serial.print(" = ");
        Serial.print(temp);
        Serial.println("°C");
        totalCount++;
      }
    }
  }
  
  // Сканирование второй шины (GPIO 5: Котельная, Улица)
  Serial.println("Шина 2 (GPIO 5):");
  sensors2.begin();
  int deviceCount2 = sensors2.getDeviceCount();
  Serial.print("Найдено датчиков на шине 2: ");
  Serial.println(deviceCount2);
  
  if (deviceCount2 > 0) {
    DeviceAddress deviceAddress;
    for (int i = 0; i < deviceCount2 && i < 10; i++) {
      if (sensors2.getAddress(deviceAddress, i)) {
        JsonObject sensor = sensorsArray.createNestedObject();
        
        String addressStr = "";
        for (uint8_t j = 0; j < 8; j++) {
          if (deviceAddress[j] < 16) addressStr += "0";
          addressStr += String(deviceAddress[j], HEX);
        }
        addressStr.toUpperCase();
        
        sensor["address"] = addressStr;
        sensor["bus"] = 2;  // Номер шины
        sensor["pin"] = PIN_DS18B20_2;
        
        sensors2.requestTemperatures();
        float temp = sensors2.getTempC(deviceAddress);
        
        if (temp != DEVICE_DISCONNECTED_C && temp != -127.0) {
          sensor["temperature"] = temp;
        }
        
        Serial.print("  Датчик ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(addressStr);
        Serial.print(" = ");
        Serial.print(temp);
        Serial.println("°C");
        totalCount++;
      }
    }
  }
  
  Serial.print("Всего найдено датчиков: ");
  Serial.println(totalCount);
  
  doc["count"] = totalCount;
  doc["countBus1"] = deviceCount1;
  doc["countBus2"] = deviceCount2;
  doc["success"] = true;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Привязка датчиков - GET
void handleSensorsMappingGet() {
  DynamicJsonDocument doc(256);
  doc["supply"] = sensorMapping.supply;
  doc["return"] = sensorMapping.return_sensor;
  doc["boiler"] = sensorMapping.boiler;
  doc["outside"] = sensorMapping.outside;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Привязка датчиков - POST
void handleSensorsMappingPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    
    // Сохраняем старые значения для проверки изменений
    String oldSupply = sensorMapping.supply;
    String oldReturn = sensorMapping.return_sensor;
    String oldBoiler = sensorMapping.boiler;
    String oldOutside = sensorMapping.outside;
    
    if (doc.containsKey("supply")) sensorMapping.supply = doc["supply"].as<String>();
    if (doc.containsKey("return")) sensorMapping.return_sensor = doc["return"].as<String>();
    if (doc.containsKey("boiler")) sensorMapping.boiler = doc["boiler"].as<String>();
    if (doc.containsKey("outside")) sensorMapping.outside = doc["outside"].as<String>();
    
    // Сбрасываем таймеры и значения для измененных датчиков
    unsigned long now = millis();
    if (sensorMapping.supply != oldSupply) {
      lastValidSupplyTempTime = 0;
      supplyTemp = 0.0;
      Serial.println("[Привязка датчиков] Сброшен датчик подачи");
    }
    if (sensorMapping.return_sensor != oldReturn) {
      lastValidReturnTempTime = 0;
      returnTemp = 0.0;
      Serial.println("[Привязка датчиков] Сброшен датчик обратки");
    }
    if (sensorMapping.boiler != oldBoiler) {
      lastValidBoilerTempTime = 0;
      boilerTemp = 0.0;
      Serial.println("[Привязка датчиков] Сброшен датчик котельной");
    }
    if (sensorMapping.outside != oldOutside) {
      lastValidOutdoorTempTime = 0;
      outdoorTemp = 0.0;
      Serial.println("[Привязка датчиков] Сброшен датчик улицы");
    }
    
    saveSensorMappingToEEPROM();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// Функции для работы с обновлениями через GitHub
void saveUpdateSettingsToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String json;
  DynamicJsonDocument doc(256);
  doc["autoCheckEnabled"] = updateSettings.autoCheckEnabled;
  doc["checkInterval"] = updateSettings.checkInterval;
  serializeJson(doc, json);
  
  int len = json.length();
  EEPROM.put(EEPROM_ADDR_UPDATE, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(EEPROM_ADDR_UPDATE + 4 + i, json[i]);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Update settings saved to EEPROM");
}

void loadUpdateSettingsFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  
  if (magic == EEPROM_MAGIC) {
    int len = 0;
    EEPROM.get(EEPROM_ADDR_UPDATE, len);
    if (len > 0 && len < 250) {
      String json = "";
      for (int i = 0; i < len; i++) {
        json += (char)EEPROM.read(EEPROM_ADDR_UPDATE + 4 + i);
      }
      DynamicJsonDocument doc(256);
      deserializeJson(doc, json);
      if (doc.containsKey("autoCheckEnabled")) updateSettings.autoCheckEnabled = doc["autoCheckEnabled"];
      if (doc.containsKey("checkInterval")) updateSettings.checkInterval = doc["checkInterval"];
    }
  }
  EEPROM.end();
}

// Проверка обновлений через GitHub
String checkForUpdates() {
  // Проверяем WiFi соединение
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Update] ERROR: WiFi not connected!");
    Serial.print("[Update] WiFi status: ");
    Serial.println(WiFi.status());
    return "";
  }
  
  Serial.print("[Update] WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[Update] Checking for updates from: ");
  Serial.println(GITHUB_VERSION_URL);
  
  WiFiClientSecure client;
  HTTPClient http;
  
  // Настройка клиента для HTTPS
  client.setInsecure();  // Отключаем проверку сертификата
  client.setTimeout(20000);  // 20 секунд таймаут
  
  // Пробуем подключиться
  Serial.println("[Update] Initializing HTTP client...");
  bool beginResult = http.begin(client, GITHUB_VERSION_URL);
  
  if (!beginResult) {
    Serial.println("[Update] ERROR: http.begin() returned false");
    Serial.println("[Update] Possible causes:");
    Serial.println("[Update]   - DNS resolution failed");
    Serial.println("[Update]   - Invalid URL");
    Serial.println("[Update]   - Memory issue");
    return "";
  }
  
  Serial.println("[Update] HTTP client initialized successfully");
  
  http.setTimeout(20000);  // 20 секунд таймаут
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "ESP32-Kotel/1.0");
  http.addHeader("Accept", "text/plain");
  
  Serial.println("[Update] Sending GET request...");
  unsigned long startTime = millis();
  int httpCode = http.GET();
  unsigned long elapsed = millis() - startTime;
  
  Serial.print("[Update] Request completed in ");
  Serial.print(elapsed);
  Serial.println(" ms");
  Serial.print("[Update] HTTP response code: ");
  Serial.println(httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("[Update] Reading response body...");
    String version = http.getString();
    version.trim();
    
    Serial.print("[Update] Received version string (length=");
    Serial.print(version.length());
    Serial.print("): ");
    Serial.println(version);
    
    // Проверяем, что версия не пустая
    if (version.length() == 0) {
      Serial.println("[Update] WARNING: Received empty version string");
      http.end();
      return "";
    }
    
    // Проверяем формат версии (должен быть типа "4.2.1")
    if (version.indexOf('.') == -1) {
      Serial.println("[Update] WARNING: Version format seems invalid");
    }
    
    // Сравнение версий (простое строковое сравнение)
    if (version != String(FIRMWARE_VERSION)) {
      Serial.print("[Update] ✓ New version available: ");
      Serial.print(version);
      Serial.print(" (current: ");
      Serial.print(FIRMWARE_VERSION);
      Serial.println(")");
      http.end();
      return version;  // Есть новая версия
    } else {
      Serial.println("[Update] ✓ Already on latest version");
    }
  } else {
    Serial.print("[Update] ✗ ERROR: HTTP code ");
    Serial.println(httpCode);
    
    if (httpCode < 0) {
      Serial.print("[Update] Error code: ");
      Serial.print(httpCode);
      Serial.print(" - ");
      String errorStr = http.errorToString(httpCode);
      Serial.println(errorStr);
      
      // Дополнительная диагностика для распространенных ошибок
      if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
        Serial.println("[Update] Connection refused - server may be down");
      } else if (httpCode == HTTPC_ERROR_SEND_HEADER_FAILED) {
        Serial.println("[Update] Failed to send header");
      } else if (httpCode == HTTPC_ERROR_SEND_PAYLOAD_FAILED) {
        Serial.println("[Update] Failed to send payload");
      } else if (httpCode == HTTPC_ERROR_NOT_CONNECTED) {
        Serial.println("[Update] Not connected to server");
      } else if (httpCode == HTTPC_ERROR_CONNECTION_LOST) {
        Serial.println("[Update] Connection lost");
      } else if (httpCode == HTTPC_ERROR_NO_STREAM) {
        Serial.println("[Update] No stream available");
      } else if (httpCode == HTTPC_ERROR_NO_HTTP_SERVER) {
        Serial.println("[Update] No HTTP server");
      } else if (httpCode == HTTPC_ERROR_TOO_LESS_RAM) {
        Serial.println("[Update] Not enough RAM");
      } else if (httpCode == HTTPC_ERROR_ENCODING) {
        Serial.println("[Update] Transfer encoding error");
      } else if (httpCode == HTTPC_ERROR_STREAM_WRITE) {
        Serial.println("[Update] Stream write error");
      } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
        Serial.println("[Update] Read timeout");
      }
    } else {
      // Получаем тело ответа для диагностики
      String errorBody = http.getString();
      Serial.print("[Update] Response body: ");
      Serial.println(errorBody.substring(0, 200));  // Первые 200 символов
    }
  }
  
  http.end();
  Serial.println("[Update] HTTP connection closed");
  return "";  // Нет обновлений или ошибка
}

// Загрузка и установка обновления (прошивка и SPIFFS)
bool downloadAndInstallUpdate(String version) {
  WiFiClientSecure client;
  HTTPClient http;
  
  Serial.println("[Update] Starting update download...");
  
  // Инициализация прогресса обновления
  updateProgress.isUpdating = true;
  updateProgress.stage = "firmware";
  updateProgress.percent = 0;
  updateProgress.message = "Загрузка прошивки...";
  updateProgress.startTime = millis();
  
  // Отключаем watchdog на время загрузки, чтобы избежать перезагрузки
  Serial.println("[Update] Disabling watchdog timer during download...");
  esp_task_wdt_delete(NULL);  // Удаляем текущую задачу из watchdog
  
  // Отключаем проверку сертификата для упрощения
  client.setInsecure();
  client.setTimeout(30000);
  
  bool success = true;
  
  // 1. Загружаем прошивку
  Serial.println("[Update] Step 1: Downloading firmware...");
  if (!http.begin(client, GITHUB_FIRMWARE_URL)) {
    Serial.println("[Update] Failed to connect to GitHub for firmware");
    esp_task_wdt_add(NULL);
    return false;
  }
  
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.print("[Update] Firmware size: ");
    Serial.print(contentLength);
    Serial.println(" bytes");
    
    if (contentLength > 0) {
      // Начинаем обновление прошивки
      if (!Update.begin(contentLength, U_FLASH)) {
        Serial.println("[Update] Not enough space to begin firmware OTA");
        http.end();
        esp_task_wdt_add(NULL);
        return false;
      }
      
      // Инициализируем информацию о размере файла
      updateProgress.totalBytes = contentLength;
      updateProgress.bytesDownloaded = 0;
      
      // Загружаем прошивку по частям
      WiFiClient* stream = http.getStreamPtr();
      size_t written = 0;
      uint8_t buff[512] = { 0 };  // Увеличен буфер с 128 до 512 байт для ускорения
      unsigned long lastProgressUpdate = 0;
      unsigned long lastDataTime = millis();  // Время последнего получения данных
      const unsigned long DATA_TIMEOUT = 60000;  // 60 секунд таймаут без данных
      
      Serial.println("[Update] Starting firmware download...");
      
      while (http.connected() && (written < contentLength)) {
        size_t available = stream->available();
        
        if (available) {
          int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
          if (c > 0) {
            size_t writtenBytes = Update.write(buff, c);
            if (writtenBytes != c) {
              Serial.print("[Update] ERROR: Write mismatch! Expected ");
              Serial.print(c);
              Serial.print(", wrote ");
              Serial.println(writtenBytes);
              Update.abort();
              http.end();
              esp_task_wdt_add(NULL);
              return false;
            }
            written += c;
            updateProgress.bytesDownloaded = written;
            lastDataTime = millis();  // Обновляем время получения данных
            
            // Прогресс на дисплее (каждые 5% или каждую секунду)
            unsigned long now = millis();
            int percent = (written * 100) / contentLength;
            
            // Вычисляем скорость загрузки
            if (updateProgress.startTime > 0 && written > 0) {
              unsigned long elapsed = now - updateProgress.startTime;
              if (elapsed > 0) {
                updateProgress.speedKBps = (written / 1024.0) / (elapsed / 1000.0);
              }
            }
            
            // Обновляем глобальный прогресс (каждую секунду или при изменении на 1%)
            if (now - lastProgressUpdate > 1000 || percent != updateProgress.percent) {
              updateProgress.percent = percent;
              updateProgress.stage = "firmware";
              updateProgress.message = "Загрузка прошивки...";
              
              if (percent % 5 == 0 || lastProgressUpdate == 0) {
                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_ncenB10_tr);
                u8g2.drawStr(0, 20, "FW Download...");
                char progressStr[20];
                snprintf(progressStr, sizeof(progressStr), "%d%%", percent);
                u8g2.drawStr(0, 40, progressStr);
                u8g2.sendBuffer();
                lastProgressUpdate = now;
                Serial.print("[Update] Firmware progress: ");
                Serial.print(percent);
                Serial.print("% (");
                Serial.print(written);
                Serial.print("/");
                Serial.print(contentLength);
                Serial.print(" bytes, ");
                Serial.print(updateProgress.speedKBps, 1);
                Serial.println(" KB/s)");
              }
            }
          }
        } else {
          // Проверка таймаута: если долго нет данных, прерываем
          unsigned long now = millis();
          unsigned long timeSinceData = (now >= lastDataTime) ? (now - lastDataTime) : (ULONG_MAX - lastDataTime + now);
          
          if (timeSinceData > DATA_TIMEOUT) {
            Serial.println("[Update] ERROR: Timeout waiting for data! Connection may be lost.");
            Serial.print("[Update] Last data received ");
            Serial.print(timeSinceData / 1000);
            Serial.println(" seconds ago");
            Update.abort();  // Отменяем обновление
            http.end();
            esp_task_wdt_add(NULL);
            return false;
          }
        }
        
        yield();  // Даём другим задачам поработать
      }
      
      // Проверка: загружены ли все данные?
      if (written < contentLength) {
        Serial.print("[Update] ERROR: Download incomplete! Expected ");
        Serial.print(contentLength);
        Serial.print(" bytes, got ");
        Serial.print(written);
        Serial.println(" bytes");
        Update.abort();  // Отменяем обновление
        http.end();
        esp_task_wdt_add(NULL);
        return false;
      }
      
      Serial.println("[Update] Firmware download complete, finalizing...");
      Serial.print("[Update] Total bytes written: ");
      Serial.println(written);
      
      if (!Update.end()) {
        Serial.println("[Update] Firmware update failed during finalization!");
        Serial.print("[Update] Error: ");
        Serial.println(Update.errorString());
        http.end();
        esp_task_wdt_add(NULL);
        return false;
      }
      
      Serial.println("[Update] Firmware update complete!");
    }
  } else {
    Serial.print("[Update] Error downloading firmware: ");
    Serial.println(httpCode);
    success = false;
  }
  
  http.end();
  
  // 2. Загружаем SPIFFS (если доступен)
  if (success) {
    Serial.println("[Update] Step 2: Downloading SPIFFS...");
    delay(1000);  // Небольшая пауза между загрузками
    
    if (!http.begin(client, GITHUB_SPIFFS_URL)) {
      Serial.println("[Update] Warning: Failed to connect to GitHub for SPIFFS, continuing...");
      // SPIFFS не критичен, продолжаем
    } else {
      http.setTimeout(30000);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      
      httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.print("[Update] SPIFFS size: ");
        Serial.print(contentLength);
        Serial.println(" bytes");
        
        if (contentLength > 0) {
          // Начинаем обновление SPIFFS
          if (!Update.begin(contentLength, U_SPIFFS)) {
            Serial.println("[Update] Warning: Not enough space for SPIFFS update, continuing...");
            // SPIFFS не критичен, продолжаем
          } else {
            // Обновляем информацию о размере файла для SPIFFS
            updateProgress.totalBytes = contentLength;
            updateProgress.bytesDownloaded = 0;
            updateProgress.percent = 0;
            
            WiFiClient* stream = http.getStreamPtr();
            size_t written = 0;
            uint8_t buff[512] = { 0 };  // Увеличен буфер с 128 до 512 байт
            unsigned long lastProgressUpdate = 0;
            unsigned long lastDataTime = millis();  // Время последнего получения данных
            const unsigned long DATA_TIMEOUT = 60000;  // 60 секунд таймаут без данных
            
            Serial.println("[Update] Starting SPIFFS download...");
            updateProgress.stage = "spiffs";
            updateProgress.message = "Загрузка файловой системы...";
            
            while (http.connected() && (written < contentLength)) {
              size_t available = stream->available();
              
              if (available) {
                int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
                if (c > 0) {
                  size_t writtenBytes = Update.write(buff, c);
                  if (writtenBytes != c) {
                    Serial.print("[Update] ERROR: SPIFFS write mismatch! Expected ");
                    Serial.print(c);
                    Serial.print(", wrote ");
                    Serial.println(writtenBytes);
                    Update.abort();
                    http.end();
                    // Прошивка уже обновлена, продолжаем
                    break;
                  }
                  written += c;
                  updateProgress.bytesDownloaded = written;
                  lastDataTime = millis();
                  
                  // Прогресс на дисплее (каждые 5%)
                  unsigned long now = millis();
                  int percent = (written * 100) / contentLength;
                  
                  // Вычисляем скорость загрузки
                  if (updateProgress.startTime > 0 && written > 0) {
                    unsigned long elapsed = now - updateProgress.startTime;
                    if (elapsed > 0) {
                      updateProgress.speedKBps = (written / 1024.0) / (elapsed / 1000.0);
                    }
                  }
                  
                  // Обновляем глобальный прогресс (каждую секунду или при изменении на 1%)
                  if (now - lastProgressUpdate > 1000 || percent != updateProgress.percent) {
                    updateProgress.percent = percent;
                    updateProgress.stage = "spiffs";
                    updateProgress.message = "Загрузка файловой системы...";
                    
                    if (percent % 5 == 0 || lastProgressUpdate == 0) {
                      u8g2.clearBuffer();
                      u8g2.setFont(u8g2_font_ncenB10_tr);
                      u8g2.drawStr(0, 20, "FS Download...");
                      char progressStr[20];
                      snprintf(progressStr, sizeof(progressStr), "%d%%", percent);
                      u8g2.drawStr(0, 40, progressStr);
                      u8g2.sendBuffer();
                      lastProgressUpdate = now;
                      Serial.print("[Update] SPIFFS progress: ");
                      Serial.print(percent);
                      Serial.print("% (");
                      Serial.print(written);
                      Serial.print("/");
                      Serial.print(contentLength);
                      Serial.print(" bytes, ");
                      Serial.print(updateProgress.speedKBps, 1);
                      Serial.println(" KB/s)");
                    }
                  }
                }
              } else {
                // Проверка таймаута
                unsigned long now = millis();
                unsigned long timeSinceData = (now >= lastDataTime) ? (now - lastDataTime) : (ULONG_MAX - lastDataTime + now);
                
                if (timeSinceData > DATA_TIMEOUT) {
                  Serial.println("[Update] WARNING: SPIFFS download timeout! Aborting SPIFFS update.");
                  Serial.print("[Update] Last data received ");
                  Serial.print(timeSinceData / 1000);
                  Serial.println(" seconds ago");
                  Update.abort();
                  http.end();
                  // Прошивка уже обновлена, продолжаем без SPIFFS
                  break;
                }
              }
              
              yield();
            }
            
            // Проверка: загружены ли все данные SPIFFS?
            if (written < contentLength) {
              Serial.print("[Update] WARNING: SPIFFS download incomplete! Expected ");
              Serial.print(contentLength);
              Serial.print(" bytes, got ");
              Serial.print(written);
              Serial.println(" bytes");
              Update.abort();
              http.end();
              // Прошивка уже обновлена, продолжаем без SPIFFS
            } else {
              Serial.println("[Update] SPIFFS download complete, finalizing...");
              Serial.print("[Update] Total SPIFFS bytes written: ");
              Serial.println(written);
              
              if (Update.end()) {
                Serial.println("[Update] SPIFFS update complete!");
              } else {
                Serial.println("[Update] WARNING: SPIFFS update failed during finalization!");
                Serial.print("[Update] Error: ");
                Serial.println(Update.errorString());
                // Прошивка уже обновлена, продолжаем без SPIFFS
              }
            }
          }
        }
      } else {
        Serial.print("[Update] Warning: SPIFFS not available (HTTP ");
        Serial.print(httpCode);
        Serial.println("), continuing with firmware only...");
      }
      
      http.end();
    }
  }
  
  if (success) {
    Serial.println("[Update] All updates complete! Rebooting...");
    updateProgress.percent = 100;
    updateProgress.message = "Обновление завершено! Перезагрузка...";
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 30, "Update Done!");
    u8g2.sendBuffer();
    delay(1000);
    // Watchdog не возвращаем, т.к. устройство перезагрузится
    return true;
  } else {
    updateProgress.isUpdating = false;
    updateProgress.message = "Ошибка обновления";
    esp_task_wdt_add(NULL);  // Возвращаем watchdog обратно
    return false;
  }
}

// API: Проверка обновлений
void handleUpdateCheck() {
  Serial.println("[Update] API: Update check requested");
  
  // Проверяем WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Update] API: WiFi not connected");
    DynamicJsonDocument doc(256);
    doc["currentVersion"] = FIRMWARE_VERSION;
    doc["latestVersion"] = FIRMWARE_VERSION;
    doc["updateAvailable"] = false;
    doc["error"] = "WiFi not connected";
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    return;
  }
  
  String latestVersion = checkForUpdates();
  
  DynamicJsonDocument doc(512);
  doc["currentVersion"] = FIRMWARE_VERSION;
  doc["latestVersion"] = latestVersion.length() > 0 ? latestVersion : String(FIRMWARE_VERSION);
  doc["updateAvailable"] = (latestVersion.length() > 0);
  
  // Добавляем информацию об ошибке, если версия пустая
  if (latestVersion.length() == 0) {
    doc["error"] = "Could not check for updates. Check serial output for details.";
    doc["wifiStatus"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  Serial.println("[Update] API: Response sent");
}

// API: Установка обновления
void handleUpdateInstall() {
  String latestVersion = checkForUpdates();
  
  if (latestVersion.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"No update available\"}");
    return;
  }
  
  // Инициализируем прогресс перед началом обновления
  updateProgress.isUpdating = true;
  updateProgress.stage = "firmware";
  updateProgress.percent = 0;
  updateProgress.message = "Начало обновления...";
  updateProgress.startTime = millis();
  
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Update started\"}");
  
  // Небольшая задержка перед началом загрузки
  delay(1000);
  
  if (downloadAndInstallUpdate(latestVersion)) {
    Serial.println("[Update] Update successful, rebooting...");
    delay(2000);
    ESP.restart();
  } else {
    Serial.println("[Update] Update failed!");
    updateProgress.isUpdating = false;
    updateProgress.message = "Ошибка обновления";
  }
}

// API: Получение прогресса обновления
void handleUpdateProgress() {
  DynamicJsonDocument doc(512);
  doc["isUpdating"] = updateProgress.isUpdating;
  doc["stage"] = updateProgress.stage;
  doc["percent"] = updateProgress.percent;
  doc["message"] = updateProgress.message;
  doc["bytesDownloaded"] = updateProgress.bytesDownloaded;
  doc["totalBytes"] = updateProgress.totalBytes;
  doc["speedKBps"] = updateProgress.speedKBps;
  
  if (updateProgress.isUpdating && updateProgress.startTime > 0) {
    unsigned long elapsed = millis() - updateProgress.startTime;
    doc["elapsedSeconds"] = elapsed / 1000;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Настройки обновлений - GET
void handleUpdateSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["autoCheckEnabled"] = updateSettings.autoCheckEnabled;
  doc["checkInterval"] = updateSettings.checkInterval / 3600000;  // Конвертируем в часы
  doc["lastCheckTime"] = updateSettings.lastCheckTime;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Настройки обновлений - POST
void handleUpdateSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("autoCheckEnabled")) updateSettings.autoCheckEnabled = doc["autoCheckEnabled"];
    if (doc.containsKey("checkInterval")) {
      int hours = doc["checkInterval"];
      updateSettings.checkInterval = hours * 3600000;  // Конвертируем в миллисекунды
    }
    
    saveUpdateSettingsToEEPROM();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Системная информация
void handleSystemInfo() {
  DynamicJsonDocument doc(512);
  doc["version"] = FIRMWARE_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  
  unsigned long uptime = millis() / 1000;
  unsigned long hours = uptime / 3600;
  unsigned long minutes = (uptime % 3600) / 60;
  unsigned long seconds = uptime % 60;
  doc["uptime"] = String(hours) + "ч " + String(minutes) + "м " + String(seconds) + "с";
  
  doc["freeMem"] = String(ESP.getFreeHeap() / 1024) + " KB";
  
  // Добавляем счетчик перезагрузок и причину
  doc["bootCount"] = bootCount;
  doc["resetReason"] = lastResetReason;
  
  // Добавляем время NTP
  if (ntpSettings.enabled && timeClient.isTimeSet()) {
    doc["time"] = getFormattedTime();
    doc["date"] = getFormattedDate();
    doc["ntpSynced"] = true;
  } else {
    doc["time"] = "--:--:--";
    doc["date"] = "--.--.----";
    doc["ntpSynced"] = false;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Получение информации о таймерах
void handleTimers() {
  unsigned long now = millis();
  DynamicJsonDocument doc(4096);
  JsonArray timers = doc.createNestedArray("timers");
  
  // Функция для форматирования времени
  auto formatTime = [](unsigned long ms) -> String {
    if (ms == 0) return "N/A";
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    seconds = seconds % 60;
    minutes = minutes % 60;
    if (hours > 0) {
      return String(hours) + "ч " + String(minutes) + "м " + String(seconds) + "с";
    } else if (minutes > 0) {
      return String(minutes) + "м " + String(seconds) + "с";
    } else {
      return String(seconds) + "с";
    }
  };
  
  // Функция для вычисления прошедшего времени
  auto getElapsed = [now](unsigned long startTime) -> unsigned long {
    if (startTime == 0) return 0;
    if (now >= startTime) return now - startTime;
    return ULONG_MAX - startTime + now;
  };
  
  // Таймеры работы вентилятора
  JsonObject timer1 = timers.createNestedObject();
  timer1["name"] = "fanStartTime";
  timer1["active"] = (fanStartTime > 0 && fanState);
  timer1["value"] = fanStartTime > 0 ? formatTime(getElapsed(fanStartTime)) : "N/A";
  timer1["description"] = "Время работы вентилятора (с момента включения)";
  
  // Таймер разогрева
  JsonObject timer2 = timers.createNestedObject();
  timer2["name"] = "heatingStartTime";
  timer2["active"] = (heatingStartTime > 0);
  timer2["value"] = heatingStartTime > 0 ? formatTime(getElapsed(heatingStartTime)) : "N/A";
  timer2["description"] = "Время начала разогрева (для проверки таймаута)";
  
  // Таймер подброса угля
  JsonObject timer3 = timers.createNestedObject();
  timer3["name"] = "coalFeedingStartTime";
  timer3["active"] = (coalFeedingStartTime > 0 && coalFeedingActive);
  timer3["value"] = coalFeedingStartTime > 0 ? formatTime(getElapsed(coalFeedingStartTime)) : "N/A";
  timer3["description"] = "Время начала подброса угля (длительность: 10 минут)";
  
  // Таймер розжига
  JsonObject timer4 = timers.createNestedObject();
  timer4["name"] = "ignitionStartTime";
  timer4["active"] = (ignitionStartTime > 0 && ignitionInProgress);
  timer4["value"] = ignitionStartTime > 0 ? formatTime(getElapsed(ignitionStartTime)) : "N/A";
  timer4["description"] = "Время начала розжига (таймаут: 10-20 минут)";
  
  // Таймер сброса датчиков
  JsonObject timer5 = timers.createNestedObject();
  timer5["name"] = "sensorsResetStartTime";
  timer5["active"] = (sensorsResetStartTime > 0 && sensorsResetPending);
  timer5["value"] = sensorsResetStartTime > 0 ? formatTime(getElapsed(sensorsResetStartTime)) : "N/A";
  timer5["description"] = "Время начала ожидания сброса датчиков (задержка: 3 секунды)";
  
  // Таймер ручного управления
  JsonObject timer6 = timers.createNestedObject();
  timer6["name"] = "lastManualControlTime";
  timer6["active"] = (lastManualControlTime > 0);
  timer6["value"] = lastManualControlTime > 0 ? formatTime(getElapsed(lastManualControlTime)) : "N/A";
  timer6["description"] = "Время последнего ручного управления (таймаут: 2 минуты)";
  
  // Таймер изменения настроек авто
  JsonObject timer7 = timers.createNestedObject();
  timer7["name"] = "lastAutoSettingsChange";
  timer7["active"] = (lastAutoSettingsChange > 0);
  timer7["value"] = lastAutoSettingsChange > 0 ? formatTime(getElapsed(lastAutoSettingsChange)) : "N/A";
  timer7["description"] = "Время последнего изменения настроек авто (для сохранения в EEPROM)";
  
  // Таймер обновления температуры дома
  JsonObject timer8 = timers.createNestedObject();
  timer8["name"] = "lastHomeTempUpdate";
  timer8["active"] = (lastHomeTempUpdate > 0);
  timer8["value"] = lastHomeTempUpdate > 0 ? formatTime(getElapsed(lastHomeTempUpdate)) : "N/A";
  timer8["description"] = "Время последнего обновления температуры дома (таймаут: 5 минут)";
  
  // Таймер обнаружения датчиков
  JsonObject timer9 = timers.createNestedObject();
  timer9["name"] = "lastSensorsDetectedTime";
  timer9["active"] = (lastSensorsDetectedTime > 0);
  timer9["value"] = lastSensorsDetectedTime > 0 ? formatTime(getElapsed(lastSensorsDetectedTime)) : "N/A";
  timer9["description"] = "Время последнего успешного обнаружения датчиков (таймаут: 60 секунд)";
  
  // Таймеры валидных показаний датчиков
  JsonObject timer10 = timers.createNestedObject();
  timer10["name"] = "lastValidSupplyTempTime";
  timer10["active"] = (lastValidSupplyTempTime > 0);
  timer10["value"] = lastValidSupplyTempTime > 0 ? formatTime(getElapsed(lastValidSupplyTempTime)) : "N/A";
  timer10["description"] = "Время последнего валидного показания подачи (таймаут: 60 секунд)";
  
  JsonObject timer11 = timers.createNestedObject();
  timer11["name"] = "lastValidReturnTempTime";
  timer11["active"] = (lastValidReturnTempTime > 0);
  timer11["value"] = lastValidReturnTempTime > 0 ? formatTime(getElapsed(lastValidReturnTempTime)) : "N/A";
  timer11["description"] = "Время последнего валидного показания обратки (таймаут: 60 секунд)";
  
  JsonObject timer12 = timers.createNestedObject();
  timer12["name"] = "lastValidBoilerTempTime";
  timer12["active"] = (lastValidBoilerTempTime > 0);
  timer12["value"] = lastValidBoilerTempTime > 0 ? formatTime(getElapsed(lastValidBoilerTempTime)) : "N/A";
  timer12["description"] = "Время последнего валидного показания котельной (таймаут: 60 секунд)";
  
  JsonObject timer13 = timers.createNestedObject();
  timer13["name"] = "lastValidOutdoorTempTime";
  timer13["active"] = (lastValidOutdoorTempTime > 0);
  timer13["value"] = lastValidOutdoorTempTime > 0 ? formatTime(getElapsed(lastValidOutdoorTempTime)) : "N/A";
  timer13["description"] = "Время последнего валидного показания улицы (таймаут: 60 секунд)";
  
  // Таймер запроса температуры
  JsonObject timer14 = timers.createNestedObject();
  timer14["name"] = "tempRequestTime";
  timer14["active"] = (tempRequestTime > 0 && tempRequestPending);
  timer14["value"] = tempRequestTime > 0 ? formatTime(getElapsed(tempRequestTime)) : "N/A";
  timer14["description"] = "Время запроса температуры (задержка конвертации: 800 мс)";
  
  // Таймер последнего запуска насоса
  JsonObject timer15 = timers.createNestedObject();
  timer15["name"] = "lastPumpRunTime";
  timer15["active"] = (lastPumpRunTime > 0);
  timer15["value"] = lastPumpRunTime > 0 ? formatTime(getElapsed(lastPumpRunTime)) : "N/A";
  timer15["description"] = "Время последнего запуска насоса (для защиты от застоя)";
  
  // Таймер проверки прогорания угля
  JsonObject timer16 = timers.createNestedObject();
  timer16["name"] = "coalBurnedCheckStart";
  timer16["active"] = (coalBurnedCheckStart > 0);
  timer16["value"] = coalBurnedCheckStart > 0 ? formatTime(getElapsed(coalBurnedCheckStart)) : "N/A";
  timer16["description"] = "Время начала отслеживания падения температуры (для определения прогорания угля)";
  
  // Таймер сканирования WiFi
  JsonObject timer17 = timers.createNestedObject();
  timer17["name"] = "wifiScanStartTime";
  timer17["active"] = (wifiScanStartTime > 0);
  timer17["value"] = wifiScanStartTime > 0 ? formatTime(getElapsed(wifiScanStartTime)) : "N/A";
  timer17["description"] = "Время начала сканирования WiFi (таймаут: 10 секунд)";
  
  // Таймер последнего переключения вентилятора
  JsonObject timer18 = timers.createNestedObject();
  timer18["name"] = "lastFanToggleTime";
  timer18["active"] = (lastFanToggleTime > 0);
  timer18["value"] = lastFanToggleTime > 0 ? formatTime(getElapsed(lastFanToggleTime)) : "N/A";
  timer18["description"] = "Время последнего переключения вентилятора (минимальный интервал: 10 секунд)";
  
  // Таймер запланированной перезагрузки
  JsonObject timer19 = timers.createNestedObject();
  timer19["name"] = "pendingRebootTime";
  timer19["active"] = (pendingRebootTime > 0 && now < pendingRebootTime);
  if (pendingRebootTime > 0) {
    unsigned long remaining = (pendingRebootTime > now) ? (pendingRebootTime - now) : 0;
    timer19["value"] = remaining > 0 ? formatTime(remaining) : "N/A";
  } else {
    timer19["value"] = "N/A";
  }
  timer19["description"] = "Время запланированной перезагрузки (оставшееся время)";
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Получение настроек NTP
void handleNTPSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = ntpSettings.enabled;
  doc["server"] = ntpSettings.server;
  doc["timezone"] = ntpSettings.timezone;
  doc["updateInterval"] = ntpSettings.updateInterval;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сохранение настроек NTP
void handleNTPSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    bool settingsChanged = false;
    
    if (doc.containsKey("enabled")) {
      bool newEnabled = doc["enabled"];
      if (ntpSettings.enabled != newEnabled) {
        ntpSettings.enabled = newEnabled;
        settingsChanged = true;
      }
    }
    if (doc.containsKey("server")) {
      String newServer = doc["server"].as<String>();
      // Проверяем, что сервер не пустой
      if (newServer.length() > 0 && ntpSettings.server != newServer) {
        ntpSettings.server = newServer;
        settingsChanged = true;
      } else if (newServer.length() == 0) {
        // Если сервер пустой, используем значение по умолчанию
        ntpSettings.server = "ru.pool.ntp.org";
        settingsChanged = true;
      }
    }
    if (doc.containsKey("timezone")) {
      int newTimezone = doc["timezone"];
      if (ntpSettings.timezone != newTimezone) {
        ntpSettings.timezone = newTimezone;
        settingsChanged = true;
      }
    }
    if (doc.containsKey("updateInterval")) {
      int newInterval = doc["updateInterval"];
      if (ntpSettings.updateInterval != newInterval) {
        ntpSettings.updateInterval = newInterval;
        settingsChanged = true;
      }
    }
    
    // Сохраняем в EEPROM всегда (даже если не было изменений, чтобы убедиться что настройки сохранены)
    saveNTPSettingsToEEPROM();
    
    // Переинициализация NTP с новыми настройками
    if (WiFi.status() == WL_CONNECTED) {
      if (ntpSettings.enabled) {
        setupNTP();
      } else {
        timeClient.end();
      }
    }
    
    DynamicJsonDocument responseDoc(128);
    responseDoc["success"] = true;
    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

// API: Получение текущего времени
void handleNTPTime() {
  DynamicJsonDocument doc(256);
  doc["time"] = getFormattedTime();
  doc["date"] = getFormattedDate();
  doc["enabled"] = ntpSettings.enabled;
  doc["synced"] = (ntpSettings.enabled && timeClient.isTimeSet());
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Получение настроек ML
void handleMLSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = mlSettings.enabled;
  doc["publishInterval"] = mlSettings.publishInterval;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API: Сохранение настроек ML
void handleMLSettingsPost() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    bool settingsChanged = false;
    
    if (doc.containsKey("enabled")) {
      bool newEnabled = doc["enabled"];
      if (mlSettings.enabled != newEnabled) {
        mlSettings.enabled = newEnabled;
        settingsChanged = true;
      }
    }
    if (doc.containsKey("publishInterval")) {
      int newInterval = doc["publishInterval"];
      if (newInterval >= 5 && newInterval <= 300) {  // Валидация: от 5 до 300 секунд
        if (mlSettings.publishInterval != newInterval) {
          mlSettings.publishInterval = newInterval;
          settingsChanged = true;
        }
      } else {
        server.send(400, "application/json", "{\"error\":\"Invalid interval (5-300 seconds)\"}");
        return;
      }
    }
    
    // Сохраняем в EEPROM только если были изменения
    if (settingsChanged) {
      saveMLSettingsToEEPROM();
      Serial.print(mlSettings.enabled);
    }
    
    DynamicJsonDocument responseDoc(128);
    responseDoc["success"] = true;
    responseDoc["enabled"] = mlSettings.enabled;
    responseDoc["publishInterval"] = mlSettings.publishInterval;
    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
  }
}

// Инициализация OTA (Over The Air обновление)
void setupOTA() {
  // Генерация уникального hostname на основе MAC адреса
  String hostname = "KotelESP32_" + WiFi.macAddress();
  hostname.replace(":", "");
  
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword("kotel12345");  // Пароль для OTA обновления
  
  // Обработчик начала обновления
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    
    // Обновление дисплея
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0, 30, "OTA Update...");
    u8g2.sendBuffer();
  });
  
  // Обработчик завершения обновления
  ArduinoOTA.onEnd([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0, 30, "OTA Done!");
    u8g2.sendBuffer();
  });
  
  // Обработчик прогресса обновления
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress / (total / 100));
    
    // Обновление дисплея с прогрессом
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 20, "OTA Update");
    char progressStr[20];
    snprintf(progressStr, sizeof(progressStr), "%d%%", percent);
    u8g2.drawStr(0, 40, progressStr);
    
    // Прогресс-бар
    int barWidth = (percent * 100) / 100;
    u8g2.drawBox(0, 50, barWidth, 8);
    u8g2.sendBuffer();
  });
  
  // Обработчик ошибок
  ArduinoOTA.onError([](ota_error_t error) {
    // Ошибка OTA (отображается на дисплее)
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 30, "OTA Error!");
    u8g2.sendBuffer();
  });
  
  // Запуск OTA
  ArduinoOTA.begin();
  
  // OTA готов к обновлению
}

// API: Перезагрузка
void handleReboot() {
  server.send(200, "application/json", "{\"success\":true}");
  // Неблокирующая перезагрузка
  pendingRebootTime = millis() + 500;
}

// API: Управление подбросом угля
void handleCoalFeeding() {
  if (server.method() == HTTP_POST) {
    // Переключение подброса угля
    if (coalFeedingActive) {
      stopCoalFeeding();
    } else {
      startCoalFeeding();
    }
    
    DynamicJsonDocument doc(200);
    doc["success"] = true;
    doc["coalFeeding"] = coalFeedingActive;
    doc["remaining"] = getCoalFeedingRemainingSeconds();
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else if (server.method() == HTTP_GET) {
    // Получение статуса подброса угля
    DynamicJsonDocument doc(200);
    doc["coalFeeding"] = coalFeedingActive;
    doc["remaining"] = getCoalFeedingRemainingSeconds();
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);  // Уменьшена задержка для быстрого старта
  
  // Инициализация Watchdog Timer для обнаружения зависаний
  // Таймаут: 30 секунд (если loop() не выполнится за это время, ESP32 перезагрузится)
  esp_task_wdt_init(30, true);  // 30 секунд, enable panic handler
  esp_task_wdt_add(NULL);  // Добавляем текущую задачу (loop) в watchdog
  
  Serial.println("[DIAG] Watchdog timer initialized (30s timeout)");
  Serial.print("[DIAG] Free heap at startup: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  // Минимальный вывод при старте - только энкодер для отладки
  
  // Инициализация пинов реле
  pinMode(PIN_RELAY_FAN, OUTPUT);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_RELAY_SENSORS, OUTPUT);
  // Выключено = LOW, включено = HIGH
  digitalWrite(PIN_RELAY_FAN, LOW);
  digitalWrite(PIN_RELAY_PUMP, LOW);
  // Реле датчиков по умолчанию включено (питание датчиков)
  sensorsRelayState = true;
  digitalWrite(PIN_RELAY_SENSORS, HIGH);
  lastSensorsDetectedTime = millis();  // Инициализируем время обнаружения при старте
  // Инициализируем время валидных показаний при старте
  unsigned long startupTime = millis();
  lastValidSupplyTempTime = startupTime;
  lastValidReturnTempTime = startupTime;
  lastValidBoilerTempTime = startupTime;
  lastValidOutdoorTempTime = startupTime;
  
  // Инициализация энкодера
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
  
  // Читаем начальное состояние и устанавливаем как валидное
  int initialClk = digitalRead(PIN_ENCODER_CLK);
  int initialDt = digitalRead(PIN_ENCODER_DT);
  lastEncoderState = (initialClk << 1) | initialDt;
  lastValidEncoderState = lastEncoderState;
  
  // Настройка прерывания для энкодера (на оба пина)
  // Используем CHANGE для отслеживания всех изменений
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), encoderISR, CHANGE);
  
  // Инициализация встроенного светодиода
  pinMode(PIN_LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_BUILTIN, LOW);  // Выключаем по умолчанию
  
  // Инициализация I2C для OLED (если нужно явно указать пины)
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  
  // Инициализация OLED дисплея (SSD1306, адрес 0x3C)
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(0, 30, "Loading...");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 50, "by Pavel");
  u8g2.sendBuffer();
  
  // Инициализация датчиков температуры DS18B20 (две шины)
  sensors1.begin();
  sensors1.setResolution(12);  // 12 бит = 0.0625°C точность
  sensors1.setWaitForConversion(false);  // Неблокирующий режим
  
  sensors2.begin();
  sensors2.setResolution(12);  // 12 бит = 0.0625°C точность
  sensors2.setWaitForConversion(false);  // Неблокирующий режим
  
  // Инициализация EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Загрузка счетчика перезагрузок и получение причины перезагрузки
  loadBootCountFromEEPROM();
  bootCount++;
  saveBootCountToEEPROM();
  
  // Получение причины перезагрузки
  lastResetReason = getResetReasonString();
  Serial.print("[Boot] Reset reason: ");
  Serial.println(lastResetReason);
  Serial.print("[Boot] Boot count: ");
  Serial.println(bootCount);
  
  // Загрузка настроек из EEPROM
  loadAutoSettingsFromEEPROM();
  loadMqttSettingsFromEEPROM();
  loadSensorMappingFromEEPROM();
  loadSystemEnabledFromEEPROM();
  loadWiFiSettingsFromEEPROM();
  loadNTPSettingsFromEEPROM();
  loadComfortSettingsFromEEPROM();
  loadWorkModeFromEEPROM();
  loadBootLogFromEEPROM();
  loadEventLogFromEEPROM();
  loadFanStatsFromEEPROM();
  
  // Определение состояния системы при запуске
  determineSystemStateOnStartup();
  
  // Инициализация SPIFFS (оптимизировано: убраны лишние проверки)
  if (!SPIFFS.begin(true)) {
    Serial.println("[ОШИБКА] SPIFFS не смонтирован!");
  }
  
  // Попытка подключения к WiFi с приоритетом
  bool wifiConnected = false;
  
  // Если есть сохраненные настройки WiFi, используем их
  if (wifiSettings.primarySSID.length() > 0 || wifiSettings.backupSSID.length() > 0) {
    wifiConnected = connectToWiFi();
  }
  
  // Если не подключились, используем WiFiManager
  if (!wifiConnected) {
    wifiManager.setConfigPortalTimeout(180);
    
    if (!wifiManager.autoConnect("KotelAP", "kotel12345")) {
      delay(1000);  // Уменьшена задержка перед перезагрузкой
      ESP.restart();
    }
  }
  
  // Определение режима WiFi (STA или AP)
  bool isAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  
  // WiFi подключен (информация доступна через веб-интерфейс)
  
  // Инициализация mDNS для доступа по kotel.local (только в режиме STA)
  if (!isAPMode) {
    // Установка hostname для WiFi
    WiFi.setHostname("kotel");
    
    // Инициализация mDNS
    if (MDNS.begin("kotel")) {
      Serial.println("[mDNS] mDNS responder started: kotel.local");
      // Добавляем сервис HTTP
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("[mDNS] Error setting up mDNS responder!");
    }
  }
  
  // Инициализация NTP (после подключения к WiFi)
  if (!isAPMode && ntpSettings.enabled) {
    setupNTP();
  }
  
  // Загрузка настроек ML
  loadMLSettingsFromEEPROM();
  loadRelaySettingsFromEEPROM();
  loadUpdateSettingsFromEEPROM();
  
  // Инициализация OTA (Over The Air обновление) - работает в обоих режимах
  setupOTA();
  
  // OTA инициализирован
  
  // Настройка веб-сервера
  server.on("/", handleWebInterface);
  
  // OTA обновление через веб-интерфейс
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
      } else {
        Update.printError(Serial);
      }
    }
  });
  
  // API endpoints
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/api/setpoint", HTTP_POST, handleSetpoint);
  server.on("/api/control", HTTP_POST, handleControl);
  server.on("/api/system/enable", HTTP_POST, handleSystemControl);
  server.on("/api/system/reset", HTTP_POST, handleSystemReset);
  server.on("/api/settings/relay", HTTP_GET, handleRelaySettingsGet);
  server.on("/api/settings/relay", HTTP_POST, handleRelaySettingsPost);
  server.on("/api/settings/auto", HTTP_GET, handleAutoSettingsGet);
  server.on("/api/settings/auto", HTTP_POST, handleAutoSettingsPost);
  server.on("/api/system/mode", HTTP_GET, handleWorkModeGet);
  server.on("/api/system/mode", HTTP_POST, handleWorkModePost);
  server.on("/api/system/ignition", HTTP_POST, handleIgnition);
  server.on("/api/settings/comfort", HTTP_GET, handleComfortSettingsGet);
  server.on("/api/settings/comfort", HTTP_POST, handleComfortSettingsPost);
  server.on("/api/settings/mqtt", HTTP_GET, handleMqttSettingsGet);
  server.on("/api/settings/mqtt", HTTP_POST, handleMqttSettingsPost);
  server.on("/api/mqtt/test", HTTP_POST, handleMqttTest);
  server.on("/api/wifi/info", HTTP_GET, handleWiFiInfo);
  server.on("/api/wifi/settings", HTTP_GET, handleWiFiSettingsGet);
  server.on("/api/wifi/settings", HTTP_POST, handleWiFiSettingsPost);
  server.on("/api/wifi/signal", HTTP_GET, handleWiFiSignalStrength);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/scan/results", HTTP_GET, handleWiFiScanResults);
  server.on("/api/wifi/reset", HTTP_POST, handleWiFiReset);
  server.on("/api/ml/settings", HTTP_GET, handleMLSettingsGet);
  server.on("/api/ml/settings", HTTP_POST, handleMLSettingsPost);
  server.on("/api/ntp/settings", HTTP_GET, handleNTPSettingsGet);
  server.on("/api/ntp/settings", HTTP_POST, handleNTPSettingsPost);
  server.on("/api/ntp/time", HTTP_GET, handleNTPTime);
  server.on("/api/sensors/scan", HTTP_POST, handleSensorsScan);
  server.on("/api/sensors/mapping", HTTP_GET, handleSensorsMappingGet);
  server.on("/api/sensors/mapping", HTTP_POST, handleSensorsMappingPost);
  server.on("/api/system/info", HTTP_GET, handleSystemInfo);
  server.on("/api/system/reboot", HTTP_POST, handleReboot);
  server.on("/api/system/bootcount/reset", HTTP_POST, handleBootCountReset);
  server.on("/api/system/log", HTTP_GET, handleBootLog);
  server.on("/api/system/timers", HTTP_GET, handleTimers);
  server.on("/api/coalFeeding", HTTP_GET, handleCoalFeeding);
  server.on("/api/coalFeeding", HTTP_POST, handleCoalFeeding);
  server.on("/api/update/check", HTTP_GET, handleUpdateCheck);
  server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
  server.on("/api/update/progress", HTTP_GET, handleUpdateProgress);
  server.on("/api/update/settings", HTTP_GET, handleUpdateSettingsGet);
  server.on("/api/update/settings", HTTP_POST, handleUpdateSettingsPost);
  
  // Обработчик для всех несуществующих путей (404)
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
  
  server.begin();
  
  // Подключение к MQTT (неблокирующее - будет выполнено в loop)
  // Не вызываем mqttConnect() здесь, чтобы не блокировать запуск
  
  // Первоначальное обновление дисплея
  updateDisplay();
}

void loop() {
  // Измерение времени начала выполнения loop() для вычисления загрузки CPU
  loopStartTime = micros();
  
  // Единая переменная времени для всего цикла (защита от переполнения millis())
  unsigned long now = millis();
  
  // Watchdog timer - сбрасываем каждый цикл для обнаружения зависаний
  esp_task_wdt_reset();
  
  // Heartbeat - счетчик итераций loop() для диагностики
  static unsigned long heartbeatCounter = 0;
  static unsigned long lastHeartbeatLog = 0;
  heartbeatCounter++;
  
  // Логирование heartbeat каждые 10000 итераций (примерно раз в минуту при нормальной работе)
  if (heartbeatCounter % 10000 == 0) {
    unsigned long heartbeatElapsed = (now >= lastHeartbeatLog) ? (now - lastHeartbeatLog) : (ULONG_MAX - lastHeartbeatLog + now);
    if (heartbeatElapsed > 0) {
      Serial.print("[DIAG] Heartbeat #");
      Serial.print(heartbeatCounter);
      Serial.print(" | Free heap: ");
      Serial.print(ESP.getFreeHeap());
      Serial.print(" bytes | Min free: ");
      Serial.print(ESP.getMinFreeHeap());
      Serial.print(" bytes | Uptime: ");
      Serial.print(now / 1000);
      Serial.println(" sec");
    }
    lastHeartbeatLog = now;
  }
  
  // Обработка OTA обновлений (должен быть первым)
  ArduinoOTA.handle();
  
  // mDNS обновляется автоматически, не требует явного вызова update()
  
  server.handleClient();
  
  // Автоматическая проверка обновлений
  if (updateSettings.autoCheckEnabled && WiFi.status() == WL_CONNECTED) {
    unsigned long timeSinceLastCheck = (updateSettings.lastCheckTime == 0) ? ULONG_MAX :
      ((now >= updateSettings.lastCheckTime) ? (now - updateSettings.lastCheckTime) : 
       (ULONG_MAX - updateSettings.lastCheckTime + now));
    
    if (timeSinceLastCheck >= updateSettings.checkInterval) {
      Serial.println("[Update] Auto-checking for updates...");
      String latestVersion = checkForUpdates();
      updateSettings.lastCheckTime = now;
      saveUpdateSettingsToEEPROM();
      
      if (latestVersion.length() > 0) {
        Serial.print("[Update] New version available: ");
        Serial.println(latestVersion);
        // Можно добавить уведомление через MQTT или просто логировать
      }
    }
  }
  
  // Обработка MQTT (неблокирующая)
  if (mqttSettings.enabled) {
    if (!mqttClient.connected()) {
      static unsigned long lastReconnect = 0;
      // Защита от переполнения millis()
      if (now - lastReconnect > 10000 || now < lastReconnect) {  // Пробуем реже - каждые 10 секунд
        lastReconnect = now;
        mqttConnect();
      }
    } else {
      // loop() не должен блокировать, но ограничим время
      mqttClient.loop();
    }
  }
  
  // Периодический вывод IP адреса убран - только энкодер для отладки
  
  // Публикация простых топиков MQTT (каждые 10 секунд, только если подключен)
  static unsigned long lastSimpleMqtt = 0;
  if (mqttSettings.enabled && mqttClient.connected() && 
      (now - lastSimpleMqtt > (unsigned long)(mqttSettings.tempInterval * 1000) || now < lastSimpleMqtt)) {
    lastSimpleMqtt = now;
    publishMqttSimple();
  }
  
  // Обработка автоматического включения реле датчиков после ручного сброса (через MQTT/веб)
  if (sensorsResetPending && !sensorsAutoResetInProgress) {
    unsigned long elapsed = (now >= sensorsResetStartTime) ? (now - sensorsResetStartTime) : (ULONG_MAX - sensorsResetStartTime + now);
    if (elapsed >= SENSORS_RESET_DELAY) {
      sensorsRelayState = true;
      digitalWrite(PIN_RELAY_SENSORS, HIGH);
      sensorsResetPending = false;
      lastSensorsDetectedTime = now;  // Обновляем время обнаружения после ручного сброса
    }
  }
  
  // Публикация полного состояния MQTT (каждые 30 секунд, только если подключен)
  static unsigned long lastStateMqtt = 0;
  if (mqttSettings.enabled && mqttClient.connected() && 
      (now - lastStateMqtt > (unsigned long)(mqttSettings.stateInterval * 1000) || now < lastStateMqtt)) {
    lastStateMqtt = now;
    publishMqttState();
  }
  
  // Публикация детального JSON для ML (с настраиваемым интервалом)
  static unsigned long lastMLMqtt = 0;
  if (mlSettings.enabled && mqttSettings.enabled && mqttClient.connected()) {
    unsigned long elapsed = (lastMLMqtt == 0) ? 999999 : ((now >= lastMLMqtt) ? (now - lastMLMqtt) : (ULONG_MAX - lastMLMqtt + now));
    if (elapsed >= (unsigned long)(mlSettings.publishInterval * 1000)) {
      lastMLMqtt = now;
      publishMqttML();
    }
  }
  
  // Обработка результатов сканирования WiFi (асинхронное)
  processWiFiScanResults();
  
  // Обработка команд через Serial (для отладки)
  handleSerialCommands();
  
  // Обработка энкодера
  handleEncoder();
  
  // Проверка и обработка подброса угля
  checkCoalFeeding();
  
  // Управление вентилятором с учетом подброса угля
  if (coalFeedingActive) {
    // Во время подброса угля вентилятор должен быть выключен
    if (fanState) {
      fanState = false;
      digitalWrite(PIN_RELAY_FAN, LOW);
    }
  } else if (systemEnabled && !manualFanControl) {
    // Автоматическое управление вентилятором
    static int lastWorkMode = -1;
    if (lastWorkMode != workMode) {
      // Переключение режима - сброс состояний
      comfortState = "WAIT";
      comfortStateStartTime = 0;
      homeTempAtStateStart = 0.0;
      heatingStartTime = 0;
      lastWorkMode = workMode;
    }
    
    if (workMode == 1) {
      // Режим "Комфорт" - управление по температуре в доме
      handleComfortMode(now);
    } else {
      // Режим "Авто" - стандартная логика
      // Включаем вентилятор, если температура подачи ниже (уставка - гистерезис)
      // Выключаем, если температура достигла (уставка + гистерезис)
      if (supplyTemp > 0) {  // Только если есть показания датчика
      
      // Управление вентилятором
      // Защита от частых переключений
      unsigned long timeSinceLastToggle = (lastFanToggleTime == 0) ? 999999 : 
        ((now >= lastFanToggleTime) ? (now - lastFanToggleTime) : (ULONG_MAX - lastFanToggleTime + now));
      float tempDelta = abs(supplyTemp - lastFanToggleTemp);
      
      // Проверяем условия для включения/выключения с защитой от дребезга
      bool shouldTurnOn = !fanState && 
                          supplyTemp < (autoSettings.setpoint - autoSettings.hysteresis) &&
                          (timeSinceLastToggle >= FAN_TOGGLE_MIN_INTERVAL_MS || lastFanToggleTime == 0) &&
                          (tempDelta >= FAN_TOGGLE_MIN_TEMP_DELTA || lastFanToggleTime == 0);
      
      bool shouldTurnOff = fanState && 
                           supplyTemp >= (autoSettings.setpoint + autoSettings.hysteresis) &&
                           (timeSinceLastToggle >= FAN_TOGGLE_MIN_INTERVAL_MS || lastFanToggleTime == 0) &&
                           (tempDelta >= FAN_TOGGLE_MIN_TEMP_DELTA || lastFanToggleTime == 0);
      
      if (shouldTurnOn && !boilerExtinguished) {
        fanState = true;
        systemState = "HEATING";
        heatingStartTime = now;  // Запоминаем время начала разогрева
        fanStartTime = now;  // Запоминаем время начала работы вентилятора
        maxTempDuringFan = supplyTemp;  // Инициализируем максимальную температуру
        lastFanToggleTime = now;
        lastFanToggleTemp = supplyTemp;
        fanStats.cycleCount++;
        fanStats.dailyCycleCount++;
        saveFanStatsToEEPROM();
      } else if (shouldTurnOff) {
        heatingStartTime = 0;  // Сбрасываем таймер разогрева
        fanState = false;
        systemState = "IDLE";
        fanStartTime = 0;  // Сбрасываем таймер работы вентилятора
        maxTempDuringFan = 0.0;  // Сбрасываем максимальную температуру
        lastFanToggleTime = now;
        lastFanToggleTemp = supplyTemp;
      }
      
      // 3. Проверка таймаута разогрева
      if (heatingStartTime > 0 && fanState) {
        unsigned long heatingElapsed = ((now >= heatingStartTime) ? (now - heatingStartTime) : (ULONG_MAX - heatingStartTime + now)) / 60000;  // минуты
        if (heatingElapsed >= autoSettings.heatingTimeout) {
          if (supplyTemp < autoSettings.setpoint - 5) {
            systemState = "HEATING_TIMEOUT";
          }
        }
      }
      
      // 4. Обнаружение прогорания угля (проверяем реже, чтобы не нагружать систему)
      static unsigned long lastCoalBurnedCheck = 0;
      if (fanState && supplyTemp > 0 && (now - lastCoalBurnedCheck > 60000 || now < lastCoalBurnedCheck)) {  // Раз в минуту
        lastCoalBurnedCheck = now;
        int trend = getTemperatureTrend(&supplyHistory);
        if (trend == -1) {  // Падение температуры
          if (coalBurnedCheckStart == 0) {
            coalBurnedCheckStart = now;
          }
          unsigned long coalElapsed = (now >= coalBurnedCheckStart) ? (now - coalBurnedCheckStart) : (ULONG_MAX - coalBurnedCheckStart + now);
          if (coalElapsed > COAL_BURNED_CHECK_TIME) {
            systemState = "COAL_BURNED";
          }
        } else {
          coalBurnedCheckStart = 0;  // Сбрасываем если температура не падает
        }
      } else if (!fanState) {
        coalBurnedCheckStart = 0;
      }
      
      // 5. Защита от перегрева
      if (supplyTemp >= autoSettings.overheatTemp) {
        fanState = false;
        systemState = "OVERHEAT";
        Serial.print("[Безопасность] Перегрев! Температура ");
        Serial.print(supplyTemp);
        Serial.print(" >= ");
        Serial.println(autoSettings.overheatTemp);
      } else if (systemState == "OVERHEAT" && supplyTemp < autoSettings.overheatTemp) {
        // Восстановление из состояния перегрева
        systemState = "IDLE";
        Serial.println("[Безопасность] Восстановление после перегрева");
      }
      
      // 6. Предупреждение о высокой температуре (maxTemp)
      if (supplyTemp >= autoSettings.maxTemp && supplyTemp < autoSettings.overheatTemp) {
        if (systemState != "HEATING_TIMEOUT" && systemState != "COAL_BURNED" && systemState != "OVERHEAT") {
          systemState = "HIGH_TEMP";
        }
      }
      
      // 7. Проверка погасания котла (только если не в режиме розжига)
      if (!ignitionInProgress && !boilerExtinguished) {
        checkBoilerExtinguished(now);
      }
    }
    }
  }
  
  // Проверка прогресса розжига (для всех режимов)
  if (ignitionInProgress) {
    checkIgnitionProgress(now);
  }
  
  // Обновление статистики вентилятора
  static unsigned long lastStatsUpdate = 0;
  if (now - lastStatsUpdate > 60000 || now < lastStatsUpdate) {  // Раз в минуту
    lastStatsUpdate = now;
    if (fanState) {
      fanStats.totalWorkTime += 60000;
      fanStats.dailyWorkTime += 60000;
    }
    // Сброс дневной статистики (раз в сутки)
    if (fanStats.lastDayReset == 0 || (now - fanStats.lastDayReset > 86400000UL)) {
      fanStats.dailyWorkTime = 0;
      fanStats.dailyCycleCount = 0;
      fanStats.lastDayReset = now;
      saveFanStatsToEEPROM();
    }
  }
  
  // Автоматическое управление насосом
  if (systemEnabled && !manualPumpControl) {
    bool shouldPumpRun = false;
    
    // Проверяем уличную температуру для определения режима работы насоса
    bool outdoorTempValid = (outdoorTemp > -50.0 && outdoorTemp < 150.0);
    bool outdoorTempBelowZero = outdoorTempValid && outdoorTemp < 0.0;
    
    if (supplyTemp > 0) {  // Только если есть показания датчика подачи
      // Логика работы насоса:
      // 1. Если уличная температура < 0°C - насос на постоянке
      // 2. Если датчик уличной температуры в ошибке - насос на постоянке
      // 3. Если уличная температура >= 0°C - насос от 45°C
      
      if (!outdoorTempValid || outdoorTempBelowZero) {
        // Насос на постоянке (уличная < 0°C или датчик в ошибке)
        shouldPumpRun = true;
      } else {
        // Насос от 45°C (уличная >= 0°C)
        if (fanState) {
          // Если вентилятор работает, насос должен работать
          shouldPumpRun = true;
        } else if (supplyTemp >= autoSettings.minTemp) {
          // Если температура выше минимальной, насос работает для циркуляции
          shouldPumpRun = true;
        }
      }
    } else {
      // Если датчик подачи в ошибке - насос на постоянке
      shouldPumpRun = true;
    }
    
    // Защита от застоя насоса - периодическое включение (только когда насос простаивает)
    if (!shouldPumpRun && !pumpState) {
      unsigned long timeSinceLastRun = (now >= lastPumpRunTime) ? (now - lastPumpRunTime) : (ULONG_MAX - lastPumpRunTime + now);
      if (timeSinceLastRun > PUMP_ANTI_STAGNATION_INTERVAL) {
        // Включаем насос на 2 минуты для предотвращения застоя
        shouldPumpRun = true;
        lastPumpRunTime = now;
      }
    }
    
    if (shouldPumpRun && !pumpState) {
      pumpState = true;
      lastPumpRunTime = now;
    } else if (!shouldPumpRun && pumpState) {
      // Проверяем, не идет ли защита от застоя
      unsigned long pumpRunTime = (now >= lastPumpRunTime) ? (now - lastPumpRunTime) : (ULONG_MAX - lastPumpRunTime + now);
      if (pumpRunTime < PUMP_ANTI_STAGNATION_DURATION) {
        // Еще идет защита от застоя - не выключаем
        shouldPumpRun = true;
      } else {
        pumpState = false;
      }
    }
  }
  
  // Синхронизация состояния реле с переменными (важно для надежности)
  syncRelays();
  
  // Проверка необходимости перезагрузки (для handleWiFiReset)
  if (pendingRebootTime > 0 && now >= pendingRebootTime) {
    ESP.restart();
  }
  
  // Отложенное сохранение в EEPROM (через 2 секунды после последнего изменения)
  if (autoSettingsDirty && (now - lastAutoSettingsChange > 2000 || now < lastAutoSettingsChange)) {
    saveAutoSettingsToEEPROM();
    autoSettingsDirty = false;
  }
  
  // Обновление дисплея (каждые 1 секунду, чтобы не блокировать веб-сервер)
  static unsigned long lastDisplayUpdate = 0;
  if (now - lastDisplayUpdate > 1000 || now < lastDisplayUpdate) {
    lastDisplayUpdate = now;
    updateDisplay();
  }
  
  // Обновление NTP времени (если включено)
  if (ntpSettings.enabled && WiFi.status() == WL_CONNECTED) {
    timeClient.update();
  }
  
  // Обновление температур с датчиков (каждые 3 секунды для сбора данных за 30 секунд)
  static unsigned long lastTempUpdate = 0;
  if (now - lastTempUpdate > 3000 || now < lastTempUpdate) {
    lastTempUpdate = now;
    updateTemperatures();
  }
  
  // Проверка обнаружения датчиков и автоматический сброс при отсутствии
  checkSensorsDetection();
  
  // Проверка зависания датчиков (0 или 85 градусов) и автоматический сброс питания
  checkSensorsFreeze();
  
  // Вычисление загрузки CPU (обновление раз в секунду)
  // Измеряем время выполнения текущего loop()
  unsigned long loopEndTime = micros();
  unsigned long currentLoopTime;
  
  // Защита от переполнения micros() (происходит каждые ~70 минут)
  if (loopEndTime >= loopStartTime) {
    currentLoopTime = loopEndTime - loopStartTime;
  } else {
    // Переполнение произошло
    currentLoopTime = (ULONG_MAX - loopStartTime) + loopEndTime;
  }
  
  // Накапливаем время выполнения и счетчик циклов
  totalLoopTime += currentLoopTime;
  loopCount++;
  
  // Обновляем загрузку CPU раз в секунду
  if (now - lastCpuUpdate >= CPU_UPDATE_INTERVAL || now < lastCpuUpdate) {
    if (loopCount > 0) {
      // Вычисляем среднее время выполнения loop() за период обновления
      unsigned long avgLoopTime = totalLoopTime / loopCount;
      
      // Вычисляем загрузку CPU как процент от периода обновления
      // Период обновления = 1 секунда = 1,000,000 мкс
      // Загрузка = (среднее время выполнения / период обновления) * 100
      cpuLoad = (avgLoopTime / 10000.0);  // 10000 мкс = 1% от 1 секунды
      if (cpuLoad > 100.0) cpuLoad = 100.0;
      if (cpuLoad < 0.0) cpuLoad = 0.0;
    } else {
      cpuLoad = 0.0;
    }
    
    // Сбрасываем счетчики для следующего периода
    totalLoopTime = 0;
    loopCount = 0;
    lastCpuUpdate = now;
  }
  
  // Управление встроенным светодиодом в зависимости от статуса WiFi
  static unsigned long lastLedToggle = 0;
  static bool ledState = false;
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  if (wifiConnected) {
    // WiFi подключен: мигание раз в секунду (500мс вкл, 500мс выкл)
    unsigned long ledInterval = 500;
    if (now - lastLedToggle >= ledInterval || now < lastLedToggle) {
      ledState = !ledState;
      digitalWrite(PIN_LED_BUILTIN, ledState ? HIGH : LOW);
      lastLedToggle = now;
    }
  } else {
    // WiFi не подключен: быстрое мерцание (100мс вкл, 100мс выкл)
    unsigned long ledInterval = 100;
    if (now - lastLedToggle >= ledInterval || now < lastLedToggle) {
      ledState = !ledState;
      digitalWrite(PIN_LED_BUILTIN, ledState ? HIGH : LOW);
      lastLedToggle = now;
    }
  }
  
  // Минимальная задержка для стабильности (уменьшена для лучшей отзывчивости)
  delay(1);
}
