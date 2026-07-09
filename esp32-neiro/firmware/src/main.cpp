/*
 * ESP32 neiro — экспериментальный ИИ-агент на микроконтроллере
 *
 * Прошивка для Arduino IDE (ESP32 Dev Module).
 * Отправляет телеметрию (GPIO, I2C) на Kali-сервер и выполняет команды от нейросети.
 *
 * Зависимости (Менеджер библиотек Arduino IDE):
 *   - ArduinoJson by Benoit Blanchon
 *   - U8g2 by olikraus
 *   - LiquidCrystal I2C by Frank de Brabander
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <LiquidCrystal_I2C.h>

// ==================== Константы Wi-Fi ====================

// Дефолтная сеть, к которой ESP32 пытается подключиться при первом запуске
#define DEFAULT_SSID     "OpenWrt"
#define DEFAULT_PASS     "00000001"

// Таймаут ожидания подключения к Wi-Fi (мс), после чего включается AP
#define WIFI_TIMEOUT_MS  15000

// Имя точки доступа для настройки (Captive Portal)
#define AP_SSID          "ESP32_AI_Setup"
#define AP_IP            "192.168.4.1"

// URL Kali-сервера по умолчанию (homeserv в локальной сети 192.168.1.x)
#define DEFAULT_SERVER   "http://192.168.1.112:8000"

// API-токен по умолчанию (должен совпадать с ESP32_API_TOKEN в .env сервера)
#define DEFAULT_API_TOKEN "esp32-neiro-change-me"

// Версия прошивки (отображается в дашборде)
#define FIRMWARE_VERSION "1.2.0"

// Таймаут HTTP-запроса телеметрии (мс) — не ждём долгий ответ LLM
#define HTTP_TIMEOUT_MS  3000

// Интервал отправки телеметрии (мс): 3–5 сек с небольшим джиттером
#define TELEMETRY_BASE_MS 4000
#define TELEMETRY_JITTER_MS 500

// I2C-пины ESP32 DevKit (стандартная распиновка)
#define I2C_SDA 21
#define I2C_SCL 22

// ==================== Список отслеживаемых GPIO ====================

// Пины для телеметрии (6–11 flash; 21/22 — I2C SDA/SCL, не трогаем)
const uint8_t TRACKED_PINS[] = {
  2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33
};
const size_t TRACKED_PINS_COUNT = sizeof(TRACKED_PINS) / sizeof(TRACKED_PINS[0]);

// ==================== Глобальные объекты ====================

Preferences prefs;           // Энергонезависимое хранилище настроек
WebServer portalServer(80);  // Веб-сервер Captive Portal
DNSServer dnsServer;         // DNS для перехвата всех запросов в AP-режиме

// Состояние дисплея (инициализируется командой init_display)
enum DisplayType { DISPLAY_NONE, DISPLAY_OLED, DISPLAY_LCD };
DisplayType activeDisplay = DISPLAY_NONE;
uint8_t displayAddress = 0;

// U8g2 для OLED SSD1306 (адрес 0x3C) — создаём указатель, инициализируем на лету
U8G2_SSD1306_128X64_NONAME_F_HW_I2C *oledDisplay = nullptr;

// LiquidCrystal для LCD 1602 (адрес 0x27)
LiquidCrystal_I2C *lcdDisplay = nullptr;

// Флаги режима работы
bool apMode = false;              // true = работаем как точка доступа
bool wifiConnected = false;       // true = подключены к домашней сети
String serverUrl = DEFAULT_SERVER; // URL бэкенда на Kali
String apiToken = DEFAULT_API_TOKEN; // Токен X-API-Token

// Таймеры
unsigned long lastTelemetryMs = 0;
unsigned long nextTelemetryInterval = TELEMETRY_BASE_MS;

// Кэш режимов пинов (для корректного чтения телеметрии)
int pinModes[40]; // индекс = номер пина; -1 = не настроен, INPUT=0, OUTPUT=1

// Состояние мигания светодиодом (неблокирующее, в loop)
bool blinkActive = false;
uint8_t blinkPin = 2;
unsigned long blinkHalfPeriodMs = 500;
unsigned long blinkEndMs = 0;
unsigned long blinkLastToggleMs = 0;
bool blinkLedOn = false;
bool blinkActiveLow = true;

// ==================== Preferences: сохранение/загрузка ====================

void loadSettings() {
  prefs.begin("esp32neiro", true); // read-only
  // Если SSID сохранён — используем его, иначе дефолт
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  String savedServer = prefs.getString("server", DEFAULT_SERVER);
  String savedToken = prefs.getString("token", DEFAULT_API_TOKEN);
  prefs.end();

  if (savedSsid.length() > 0) {
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    Serial.printf("[WiFi] Подключение к сохранённой сети: %s\n", savedSsid.c_str());
  } else {
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
    Serial.printf("[WiFi] Подключение к дефолтной сети: %s\n", DEFAULT_SSID);
  }
  serverUrl = savedServer;
  apiToken = savedToken;
}

void saveSettings(const String &ssid, const String &pass, const String &server, const String &token) {
  prefs.begin("esp32neiro", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("server", server);
  prefs.putString("token", token.length() > 0 ? token : DEFAULT_API_TOKEN);
  prefs.end();
  Serial.println("[Prefs] Настройки сохранены, перезагрузка…");
  delay(500);
  ESP.restart();
}

// ==================== Captive Portal (HTML-форма) ====================

// Страница настройки Wi-Fi и URL сервера (отдаётся на 192.168.4.1)
const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 AI Setup</title>
  <style>
    body { font-family: sans-serif; background: #0f172a; color: #e2e8f0; padding: 20px; max-width: 400px; margin: auto; }
    h1 { color: #38bdf8; font-size: 1.3rem; }
    label { display: block; margin-top: 12px; font-size: 0.9rem; }
    input { width: 100%; padding: 10px; margin-top: 4px; border-radius: 8px; border: 1px solid #334155; background: #1e293b; color: #fff; box-sizing: border-box; }
    button { margin-top: 20px; width: 100%; padding: 12px; background: #0ea5e9; color: #fff; border: none; border-radius: 8px; font-size: 1rem; cursor: pointer; }
    p { font-size: 0.85rem; color: #94a3b8; }
  </style>
</head>
<body>
  <h1>ESP32 AI — Настройка</h1>
  <p>Подключите ESP32 к вашей Wi-Fi сети и укажите адрес Kali-сервера.</p>
  <form action="/save" method="POST">
    <label>SSID (имя Wi-Fi)</label>
    <input name="ssid" required placeholder="OpenWrt">
    <label>Пароль Wi-Fi</label>
    <input name="pass" type="password" placeholder="00000001">
    <label>URL сервера (Kali)</label>
    <input name="server" required placeholder="http://192.168.1.112:8000" value="http://192.168.1.112:8000">
    <label>API-токен</label>
    <input name="token" placeholder="esp32-neiro-change-me" value="esp32-neiro-change-me">
    <button type="submit">Сохранить и перезагрузить</button>
  </form>
</body>
</html>
)rawliteral";

void handlePortalRoot() {
  portalServer.send(200, "text/html", PORTAL_HTML);
}

void handlePortalSave() {
  String ssid = portalServer.arg("ssid");
  String pass = portalServer.arg("pass");
  String server = portalServer.arg("server");
  String token = portalServer.arg("token");

  if (ssid.length() == 0) {
    portalServer.send(400, "text/plain", "SSID обязателен");
    return;
  }
  portalServer.send(200, "text/html", "<html><body><h2>Сохранено! ESP32 перезагружается…</h2></body></html>");
  delay(300);
  saveSettings(ssid, pass, server, token);
}

// Перехват любого неизвестного URL — редирект на главную (Captive Portal)
void handlePortalCatchAll() {
  portalServer.sendHeader("Location", String("http://") + AP_IP, true);
  portalServer.send(302, "text/plain", "");
}

void startAccessPoint() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.printf("[AP] Точка доступа: %s (IP %s)\n", AP_SSID, AP_IP);

  // DNS: все запросы → IP точки доступа (Captive Portal)
  dnsServer.start(53, "*", WiFi.softAPIP());

  portalServer.on("/", HTTP_GET, handlePortalRoot);
  portalServer.on("/save", HTTP_POST, handlePortalSave);
  portalServer.onNotFound(handlePortalCatchAll);
  portalServer.begin();
  Serial.println("[AP] Captive Portal запущен на http://192.168.4.1");
}

// ==================== Wi-Fi подключение ====================

bool connectWiFi() {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[WiFi] Подключено! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[WiFi] Не удалось подключиться за 15 секунд → AP режим");
  return false;
}

// ==================== I2C: динамический скан шины ====================

void scanI2C(JsonArray &outArray) {
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "0x%02X", addr);
      outArray.add(buf);
    }
  }
}

// ==================== GPIO: снимок состояния пинов ====================

void initPinModesCache() {
  for (int i = 0; i < 40; i++) {
    pinModes[i] = -1;
  }
  // Встроенный LED на GPIO 2 — выход по умолчанию для экспериментов
  pinMode(2, OUTPUT);
  pinModes[2] = 1; // OUTPUT
}

const char *modeToString(int mode) {
  if (mode == 0) return "INPUT";
  if (mode == 1) return "OUTPUT";
  return "UNSET";
}

void collectGpioTelemetry(JsonArray &gpioArray) {
  for (size_t i = 0; i < TRACKED_PINS_COUNT; i++) {
    uint8_t pin = TRACKED_PINS[i];
    JsonObject obj = gpioArray.createNestedObject();
    obj["pin"] = pin;
    int mode = pinModes[pin];
    if (mode >= 0) {
      obj["mode"] = modeToString(mode);
      if (mode == 1) {
        obj["value"] = digitalRead(pin);
      } else {
        obj["value"] = digitalRead(pin);
      }
    } else {
      obj["mode"] = "UNSET";
      obj["value"] = -1;
    }
  }
}

// ==================== Дисплей: инициализация на лету ====================

bool initOledDisplay(uint8_t addr) {
  if (oledDisplay != nullptr) {
    delete oledDisplay;
    oledDisplay = nullptr;
  }
  oledDisplay = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  oledDisplay->setI2CAddress(addr << 1);
  if (!oledDisplay->begin()) {
    Serial.println("[Display] OLED init FAILED");
    delete oledDisplay;
    oledDisplay = nullptr;
    return false;
  }
  oledDisplay->clearBuffer();
  oledDisplay->sendBuffer();
  activeDisplay = DISPLAY_OLED;
  displayAddress = addr;
  Serial.printf("[Display] OLED инициализирован на 0x%02X\n", addr);
  return true;
}

bool initLcdDisplay(uint8_t addr) {
  if (lcdDisplay != nullptr) {
    delete lcdDisplay;
    lcdDisplay = nullptr;
  }
  lcdDisplay = new LiquidCrystal_I2C(addr, 16, 2);
  lcdDisplay->init();
  lcdDisplay->backlight();
  lcdDisplay->clear();
  activeDisplay = DISPLAY_LCD;
  displayAddress = addr;
  Serial.printf("[Display] LCD инициализирован на 0x%02X\n", addr);
  return true;
}

void printOnDisplay(const String &text, int line) {
  if (activeDisplay == DISPLAY_OLED && oledDisplay != nullptr) {
    oledDisplay->clearBuffer();
    oledDisplay->setFont(u8g2_font_6x10_tf);
    oledDisplay->drawStr(0, 12 + line * 12, text.c_str());
    oledDisplay->sendBuffer();
  } else if (activeDisplay == DISPLAY_LCD && lcdDisplay != nullptr) {
    lcdDisplay->setCursor(0, line);
    lcdDisplay->print(text.substring(0, 16));
  }
}

// ==================== Мигание светодиодом (неблокирующее) ====================

void startBlink(uint8_t pin, float hz, unsigned long durationMs, bool activeLow) {
  if (hz <= 0.0f) hz = 1.0f;
  if (durationMs == 0) durationMs = 60000;

  blinkActive = true;
  blinkPin = pin;
  blinkHalfPeriodMs = (unsigned long)(500.0f / hz);
  if (blinkHalfPeriodMs < 10) blinkHalfPeriodMs = 10;
  blinkEndMs = millis() + durationMs;
  blinkLastToggleMs = millis();
  blinkLedOn = true;
  blinkActiveLow = activeLow;

  pinMode(pin, OUTPUT);
  pinModes[pin] = 1;
  digitalWrite(pin, activeLow ? LOW : HIGH);

  Serial.printf("[Blink] GPIO %u, %.2f Гц, %lu мс, active_low=%d\n",
                pin, hz, durationMs, activeLow);
}

void updateBlink() {
  if (!blinkActive) return;

  unsigned long now = millis();
  if ((long)(now - blinkEndMs) >= 0) {
    blinkActive = false;
    digitalWrite(blinkPin, blinkActiveLow ? HIGH : LOW);
    Serial.println("[Blink] Завершено");
    return;
  }

  if (now - blinkLastToggleMs >= blinkHalfPeriodMs) {
    blinkLastToggleMs = now;
    blinkLedOn = !blinkLedOn;
    int level = blinkLedOn ? (blinkActiveLow ? LOW : HIGH) : (blinkActiveLow ? HIGH : LOW);
    digitalWrite(blinkPin, level);
  }
}

// ==================== Обработчик команд от сервера ====================

void executeCommands(JsonArray &commands) {
  for (JsonObject cmd : commands) {
    const char *type = cmd["cmd"];
    if (!type) continue;

    Serial.printf("[CMD] %s\n", type);

    if (strcmp(type, "pin_mode") == 0) {
      int pin = cmd["pin"] | -1;
      const char *modeStr = cmd["mode"] | "INPUT";
      if (pin >= 0) {
        if (strcmp(modeStr, "OUTPUT") == 0) {
          pinMode(pin, OUTPUT);
          pinModes[pin] = 1;
        } else {
          pinMode(pin, INPUT);
          pinModes[pin] = 0;
        }
      }
    }
    else if (strcmp(type, "digital_write") == 0) {
      int pin = cmd["pin"] | -1;
      int val = cmd["value"] | 0;
      if (pin >= 0) {
        digitalWrite(pin, val ? HIGH : LOW);
      }
    }
    else if (strcmp(type, "blink") == 0) {
      int pin = cmd["pin"] | 2;
      float hz = cmd["hz"].isNull() ? 1.0f : cmd["hz"].as<float>();
      unsigned long durationMs = cmd["duration_ms"] | 60000UL;
      bool activeLow = cmd["active_low"].isNull() ? (pin == 2) : cmd["active_low"].as<bool>();
      startBlink((uint8_t)pin, hz, durationMs, activeLow);
    }
    else if (strcmp(type, "init_display") == 0) {
      const char *addrStr = cmd["address"] | "0x3C";
      uint8_t addr = (uint8_t)strtol(addrStr, nullptr, 0);
      if (addr == 0x3C || addr == 60) {
        initOledDisplay(0x3C);
      } else if (addr == 0x27 || addr == 39) {
        initLcdDisplay(0x27);
      } else {
        // Пробуем как OLED, если адрес нестандартный
        initOledDisplay(addr);
      }
    }
    else if (strcmp(type, "print_text") == 0) {
      const char *text = cmd["text"] | "";
      int line = cmd["line"] | 0;
      printOnDisplay(String(text), line);
    }
    else if (strcmp(type, "reboot") == 0) {
      Serial.println("[CMD] Перезагрузка по команде сервера…");
      delay(200);
      ESP.restart();
    }
    else {
      Serial.printf("[CMD] неизвестная команда: %s\n", type);
    }
  }
}

// ==================== HTTP POST телеметрии на Kali-сервер ====================

void sendTelemetry() {
  if (!wifiConnected) return;

  String url = serverUrl;
  if (!url.endsWith("/")) url += "/";
  url += "api/telemetry";

  // Телеметрия — буфер в куче (StaticJsonDocument 4K на стеке вызывал stack overflow)
  DynamicJsonDocument doc(4096);
  doc["device_id"] = WiFi.macAddress();
  doc["uptime_ms"] = millis();
  doc["firmware_version"] = FIRMWARE_VERSION;

  JsonArray gpio = doc.createNestedArray("gpio");
  collectGpioTelemetry(gpio);

  JsonArray i2c = doc.createNestedArray("i2c_devices");
  scanI2C(i2c);

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  if (apiToken.length() > 0) {
    http.addHeader("X-API-Token", apiToken);
  }

  int code = http.POST(body);
  if (code > 0) {
    String response = http.getString();
    if (code == 200 && response.length() > 0) {
      StaticJsonDocument<4096> respDoc;
      DeserializationError err = deserializeJson(respDoc, response);
      if (err) {
        Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
      } else if (respDoc.containsKey("commands")) {
        JsonArray commands = respDoc["commands"].as<JsonArray>();
        executeCommands(commands);
      }
    } else {
      Serial.printf("[HTTP] Ответ сервера: %d\n", code);
    }
  } else {
    Serial.printf("[HTTP] Ошибка POST: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

// ==================== Arduino setup / loop ====================

void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(esp_random());
  Serial.println("\n=== ESP32 neiro — Dynamic Agent ===");

  // Инициализация I2C для сканирования и дисплеев
  Wire.begin(I2C_SDA, I2C_SCL);
  initPinModesCache();

  // Загрузка настроек и попытка Wi-Fi
  loadSettings();
  if (!connectWiFi()) {
    startAccessPoint();
    return;
  }

  // Случайный джиттер для интервала телеметрии (3–5 сек)
  nextTelemetryInterval = TELEMETRY_BASE_MS + random(0, TELEMETRY_JITTER_MS * 2 + 1) - TELEMETRY_JITTER_MS;
  lastTelemetryMs = millis();
}

void loop() {
  // Режим точки доступа: обслуживаем Captive Portal
  if (apMode) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    return;
  }

  // Проверка Wi-Fi (переподключение при обрыве)
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("[WiFi] Соединение потеряно, переподключение…");
    if (!connectWiFi()) {
      startAccessPoint();
      return;
    }
  }

  // Периодическая отправка телеметрии
  unsigned long now = millis();
  updateBlink();
  if (now - lastTelemetryMs >= nextTelemetryInterval) {
    lastTelemetryMs = now;
    nextTelemetryInterval = TELEMETRY_BASE_MS + random(0, TELEMETRY_JITTER_MS * 2 + 1) - TELEMETRY_JITTER_MS;
    sendTelemetry();
  }
}
