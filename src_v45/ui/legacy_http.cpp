#include "legacy_http.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Esp.h>

#include "../config/version.h"
#include "../config/pins.h"
#include "../config/ota_urls.h"

namespace {

WebServer* S = nullptr;
PlantState* P = nullptr;
BoilerOrchestrator* Orch = nullptr;
HomeMqttSensor* Home = nullptr;
VpsClient* Vps = nullptr;
EventJournal* Journal = nullptr;
UserEventService* UserEv = nullptr;
HypothesisEngine* Hyp = nullptr;
OtaChannelV45* Ota = nullptr;
FanActuator* Fan = nullptr;
PumpActuator* Pump = nullptr;
SensorPowerRelay* SensorPwr = nullptr;

// In-memory settings stubs (UI 4.2 shape). Persist later if needed.
float gSetpoint = 60.0f;
float gHysteresis = 2.0f;
float gComfortHystOn = 0.3f;
float gComfortHystOff = 0.3f;
float gComfortHystBoiler = 2.0f;
float gComfortWaitTemp = 45.0f;
bool gCoalFeeding = false;
uint32_t gCoalFeedingUntilMs = 0;

struct MqttUiSettings {
  bool enabled = false;
  String server = "";
  int port = 1883;
  String user = "";
  String pass = "";
  String prefix = "boiler";
  String homeTempTopic = "home/esp01/temp";
  String homeLwtTopic = "home/esp01/status";
} gMqtt;

struct NtpUiSettings {
  bool enabled = true;
  String server = "pool.ntp.org";
  int timezone = 3;
  int updateInterval = 3600;
} gNtp;

struct RelayUiSettings {
  bool fanActiveHigh = true;
  bool pumpActiveHigh = true;
  bool sensorPwrActiveHigh = true;
} gRelay;

struct TunnelUiSettings {
  bool enabled = false;
  String server = "";
  int port = 7000;
  String token = "";
} gTunnel;

struct UpdateUiSettings {
  bool autoCheck = true;
  int checkIntervalHours = 24;
} gUpdate;

struct MlUiSettings {
  bool enabled = false;
  bool observeOnly = true;
} gMl;

static void sendJson(int code, const String& body) {
  S->send(code, "application/json", body);
}

static void sendOk() { sendJson(200, "{\"success\":true}"); }

static void sendDoc(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

static void putTemp(JsonDocument& doc, const char* key, const SensorReading& r) {
  if (r.quality == SensorQuality::Ok) doc[key] = r.celsius;
  else doc[key] = nullptr;
}

static int workModeInt(WorkMode m) { return static_cast<int>(m); }

static WorkMode parseMode(JsonVariant v, bool& ok) {
  ok = true;
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>()) {
    const int m = v.as<int>();
    if (m == 0) return WorkMode::Auto;
    if (m == 1) return WorkMode::Comfort;
    if (m == 2) return WorkMode::Neuro;
    ok = false;
    return WorkMode::Auto;
  }
  const char* m = v.as<const char*>();
  if (!m) {
    ok = false;
    return WorkMode::Auto;
  }
  if (!strcmp(m, "auto") || !strcmp(m, "0")) return WorkMode::Auto;
  if (!strcmp(m, "comfort") || !strcmp(m, "1")) return WorkMode::Comfort;
  if (!strcmp(m, "neuro") || !strcmp(m, "2")) return WorkMode::Neuro;
  ok = false;
  return WorkMode::Auto;
}

static void applyMode(WorkMode wm) {
  Orch->setMode(wm);
  P->mode = wm;
  Journal->add("mode_change", workModeName(wm), 0);
}

static void handleRoot() {
  if (SPIFFS.exists("/index.html")) {
    File f = SPIFFS.open("/index.html", "r");
    S->streamFile(f, "text/html");
    f.close();
    return;
  }
  S->send(200, "text/html",
          "<!doctype html><meta charset=utf-8><title>Котел 4.5-beta</title>"
          "<body style='background:#111;color:#eee;font-family:sans-serif;padding:16px'>"
          "<h1>Котел 4.5-beta</h1><p>SPIFFS UI отсутствует. API: /api/status</p></body>");
}

static void handleStatus() {
  DynamicJsonDocument doc(2048);
  putTemp(doc, "supplyTemp", P->supply);
  putTemp(doc, "returnTemp", P->ret);
  putTemp(doc, "boilerTemp", P->boilerRoom);
  putTemp(doc, "outdoorTemp", P->outdoor);
  putTemp(doc, "homeTemp", P->home);
  putTemp(doc, "flueTemp", P->flue);

  doc["hysteresis"] = gHysteresis;
  doc["setpoint"] = gSetpoint;
  doc["fan"] = P->fanRelayOn;
  doc["fanPowerPct"] = P->fanPowerPct;
  doc["pump"] = P->pumpOn;
  doc["systemEnabled"] = P->systemEnabled;
  doc["state"] = Orch->lastStateName();
  doc["workMode"] = workModeInt(P->mode);
  doc["workModeName"] = workModeNameRu(P->mode);
  doc["mode"] = workModeName(P->mode);
  doc["modeRu"] = workModeNameRu(P->mode);
  doc["homeTempSensorValid"] = (P->home.quality == SensorQuality::Ok);
  doc["homeTempSensorLWTOnline"] = Home->online();
  doc["targetHomeTemp"] = P->comfortRoomTargetC;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["version"] = FIRMWARE_VERSION;
  doc["channel"] = FIRMWARE_CHANNEL;
  doc["otaChannel"] = OTA_CHANNEL;
  doc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "Подключен" : "Отключен";
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["mqttStatus"] = gMqtt.enabled ? (Home->online() ? "Подключен" : "Отключен") : "Выключен";
  doc["coalFeeding"] = gCoalFeeding && (millis() < gCoalFeedingUntilMs);
  doc["coalFeedingRemaining"] =
      (gCoalFeeding && millis() < gCoalFeedingUntilMs) ? (int)((gCoalFeedingUntilMs - millis()) / 1000) : 0;
  doc["lowReturnTemp"] = P->pumpOn && P->ret.quality == SensorQuality::Ok && P->ret.celsius > 0 && P->ret.celsius < 40.0f;
  doc["coalBurned"] = false;
  doc["boilerExtinguished"] = false;
  doc["ignitionInProgress"] = false;
  doc["safetyTrip"] = P->safetyTrip;
  doc["safetyReason"] = P->safetyReason;
  doc["sensorPowerOn"] = P->sensorPowerOn;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["autoMin"] = P->autoMinC;
  doc["autoMax"] = P->autoMaxC;
  doc["comfortRoom"] = P->comfortRoomTargetC;
  doc["vpsQueue"] = (int)Vps->queueSize();
  doc["hypotheses"] = (int)Hyp->count();
  doc["flashPolicy"] = "clean_only_no_migrate_from_4_2";

  JsonObject fanStats = doc.createNestedObject("fanStats");
  fanStats["totalWorkTime"] = 0;
  fanStats["dailyWorkTime"] = 0;
  fanStats["cycleCount"] = 0;
  fanStats["dailyCycleCount"] = 0;
  fanStats["currentWorkTime"] = 0;

  sendDoc(doc);
}

static void handleModeGet() {
  DynamicJsonDocument doc(256);
  doc["mode"] = workModeInt(P->mode);
  doc["modeName"] = workModeNameRu(P->mode);
  doc["success"] = true;
  sendDoc(doc);
}

static void handleModePost() {
  DynamicJsonDocument doc(256);
  if (!S->hasArg("plain") || deserializeJson(doc, S->arg("plain"))) {
    sendJson(400, "{\"error\":\"bad_json\",\"success\":false}");
    return;
  }
  bool ok = false;
  WorkMode wm = parseMode(doc["mode"], ok);
  if (!ok) {
    sendJson(400, "{\"error\":\"unknown_mode\",\"success\":false}");
    return;
  }
  applyMode(wm);
  DynamicJsonDocument out(256);
  out["success"] = true;
  out["mode"] = workModeInt(wm);
  out["modeName"] = workModeNameRu(wm);
  sendDoc(out);
}

static void handleEnable() {
  bool en = false;
  if (S->hasArg("enabled")) {
    en = (S->arg("enabled") == "1" || S->arg("enabled") == "true");
  } else if (S->hasArg("plain")) {
    DynamicJsonDocument doc(128);
    if (!deserializeJson(doc, S->arg("plain"))) en = doc["enabled"] | false;
  }
  P->systemEnabled = en;
  Journal->add(en ? "system_on" : "system_off", "web", 0);
  sendOk();
}

static void handleControl() {
  const String device = S->hasArg("device") ? S->arg("device") : "";
  const bool state = S->hasArg("state") && (S->arg("state") == "1");
  if (device == "fan") {
    if (state) Fan->setPowerPercent(100);
    else Fan->setPowerPercent(0);
    P->fanRelayOn = state;
    P->fanPowerPct = state ? 100 : 0;
  } else if (device == "pump") {
    Pump->setOn(state);
    P->pumpOn = state;
  } else if (device == "sensor_power" || device == "sensorPower") {
    SensorPwr->setPowered(state);
    P->sensorPowerOn = state;
  }
  sendOk();
}

static void handleSetpoints() {
  DynamicJsonDocument doc(512);
  if (!S->hasArg("plain") || deserializeJson(doc, S->arg("plain"))) {
    sendJson(400, "{\"error\":\"bad_json\"}");
    return;
  }
  if (doc.containsKey("autoMin")) P->autoMinC = doc["autoMin"];
  if (doc.containsKey("autoMax")) P->autoMaxC = doc["autoMax"];
  if (doc.containsKey("setpoint")) gSetpoint = doc["setpoint"];
  if (doc.containsKey("hysteresis")) gHysteresis = doc["hysteresis"];
  if (doc.containsKey("comfortRoom")) P->comfortRoomTargetC = doc["comfortRoom"];
  if (doc.containsKey("comfortBoilerMin")) P->comfortBoilerMinC = doc["comfortBoilerMin"];
  if (doc.containsKey("comfortBoilerMax")) P->comfortBoilerMaxC = doc["comfortBoilerMax"];
  sendOk();
}

static void handleSettingsAutoGet() {
  DynamicJsonDocument doc(256);
  doc["setpoint"] = gSetpoint;
  doc["hysteresis"] = gHysteresis;
  doc["autoMin"] = P->autoMinC;
  doc["autoMax"] = P->autoMaxC;
  sendDoc(doc);
}

static void handleSettingsAutoPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("setpoint")) gSetpoint = doc["setpoint"];
  if (doc.containsKey("hysteresis")) gHysteresis = doc["hysteresis"];
  if (doc.containsKey("autoMin")) P->autoMinC = doc["autoMin"];
  if (doc.containsKey("autoMax")) P->autoMaxC = doc["autoMax"];
  // UI often uses setpoint as mid/high of band
  if (doc.containsKey("setpoint") && !doc.containsKey("autoMax")) {
    P->autoMaxC = gSetpoint;
    P->autoMinC = gSetpoint - gHysteresis;
  }
  sendOk();
}

static void handleSettingsComfortGet() {
  DynamicJsonDocument doc(512);
  doc["targetHomeTemp"] = P->comfortRoomTargetC;
  doc["minBoilerTemp"] = P->comfortBoilerMinC;
  doc["maxBoilerTemp"] = P->comfortBoilerMaxC;
  doc["waitTemp"] = gComfortWaitTemp;
  doc["hysteresisOn"] = gComfortHystOn;
  doc["hysteresisOff"] = gComfortHystOff;
  doc["hysteresisBoiler"] = gComfortHystBoiler;
  sendDoc(doc);
}

static void handleSettingsComfortPost() {
  DynamicJsonDocument doc(512);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("targetHomeTemp")) P->comfortRoomTargetC = doc["targetHomeTemp"];
  if (doc.containsKey("minBoilerTemp")) P->comfortBoilerMinC = doc["minBoilerTemp"];
  if (doc.containsKey("maxBoilerTemp")) P->comfortBoilerMaxC = doc["maxBoilerTemp"];
  if (doc.containsKey("waitTemp")) gComfortWaitTemp = doc["waitTemp"];
  if (doc.containsKey("hysteresisOn")) gComfortHystOn = doc["hysteresisOn"];
  if (doc.containsKey("hysteresisOff")) gComfortHystOff = doc["hysteresisOff"];
  if (doc.containsKey("hysteresisBoiler")) gComfortHystBoiler = doc["hysteresisBoiler"];
  sendOk();
}

static void handleSettingsMqttGet() {
  DynamicJsonDocument doc(512);
  doc["enabled"] = gMqtt.enabled;
  doc["server"] = gMqtt.server;
  doc["port"] = gMqtt.port;
  doc["user"] = gMqtt.user;
  doc["password"] = gMqtt.pass;
  doc["prefix"] = gMqtt.prefix;
  doc["homeTempTopic"] = gMqtt.homeTempTopic;
  doc["homeLwtTopic"] = gMqtt.homeLwtTopic;
  sendDoc(doc);
}

static void handleSettingsMqttPost() {
  DynamicJsonDocument doc(768);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("enabled")) gMqtt.enabled = doc["enabled"];
  if (doc.containsKey("server")) gMqtt.server = (const char*)doc["server"];
  if (doc.containsKey("port")) gMqtt.port = doc["port"];
  if (doc.containsKey("user")) gMqtt.user = (const char*)doc["user"];
  if (doc.containsKey("password")) gMqtt.pass = (const char*)doc["password"];
  if (doc.containsKey("prefix")) gMqtt.prefix = (const char*)doc["prefix"];
  if (doc.containsKey("homeTempTopic")) gMqtt.homeTempTopic = (const char*)doc["homeTempTopic"];
  if (doc.containsKey("homeLwtTopic")) gMqtt.homeLwtTopic = (const char*)doc["homeLwtTopic"];
  Home->setTopics(gMqtt.homeTempTopic.c_str(), gMqtt.homeLwtTopic.c_str());
  if (gMqtt.enabled && gMqtt.server.length()) {
    Home->setBroker(gMqtt.server.c_str(), gMqtt.port, gMqtt.user.c_str(), gMqtt.pass.c_str());
  }
  sendOk();
}

static void handleMqttTest() {
  DynamicJsonDocument doc(128);
  doc["success"] = true;
  doc["connected"] = Home->online();
  doc["message"] = Home->online() ? "online" : "offline_or_no_broker";
  sendDoc(doc);
}

static void handleCoalFeedingGet() {
  DynamicJsonDocument doc(256);
  const bool active = gCoalFeeding && millis() < gCoalFeedingUntilMs;
  doc["active"] = active;
  doc["coalFeeding"] = active;
  doc["remaining"] = active ? (int)((gCoalFeedingUntilMs - millis()) / 1000) : 0;
  sendDoc(doc);
}

static void handleCoalFeedingPost() {
  gCoalFeeding = true;
  gCoalFeedingUntilMs = millis() + 180000;  // 3 мин stub
  Journal->add("coal_feeding", "web", 0);
  sendOk();
}

static void handleWifiInfo() {
  DynamicJsonDocument doc(512);
  const bool ap = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  doc["mode"] = ap ? "AP" : "STA";
  doc["ssid"] = ap ? WiFi.softAPSSID() : WiFi.SSID();
  doc["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["status"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["mac"] = WiFi.macAddress();
  sendDoc(doc);
}

static void handleWifiSignal() {
  DynamicJsonDocument doc(128);
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  sendDoc(doc);
}

static void handleWifiScan() {
  WiFi.scanNetworks(true);
  sendJson(200, "{\"success\":true,\"scanning\":true}");
}

static void handleWifiScanResults() {
  DynamicJsonDocument doc(2048);
  const int n = WiFi.scanComplete();
  doc["scanning"] = (n == WIFI_SCAN_RUNNING);
  JsonArray arr = doc.createNestedArray("networks");
  if (n > 0) {
    for (int i = 0; i < n && i < 20; i++) {
      JsonObject o = arr.createNestedObject();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
  }
  sendDoc(doc);
}

static void handleWifiSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["ssid"] = WiFi.SSID();
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  sendDoc(doc);
}

static void handleWifiSettingsPost() {
  // Soft accept — full WiFiManager flow not ported in beta stubs
  sendOk();
}

static void handleWifiReset() {
  sendOk();
}

static void handleSensorsScan() {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("sensors");
  auto add = [&](const char* id, const char* bus, const SensorReading& r) {
    JsonObject o = arr.createNestedObject();
    o["id"] = id;
    o["bus"] = bus;
    o["role"] = id;
    if (r.quality == SensorQuality::Ok) o["temp"] = r.celsius;
    else o["temp"] = nullptr;
    o["quality"] = (int)r.quality;
  };
  add("supply", "max31865:27", P->supply);
  add("flue", "max31865:26", P->flue);
  add("return", "ow1:4", P->ret);
  add("boiler_room", "ow2:5", P->boilerRoom);
  add("outdoor", "ow2:5", P->outdoor);
  add("home", "mqtt", P->home);
  doc["success"] = true;
  sendDoc(doc);
}

static void handleSensorsMappingGet() {
  DynamicJsonDocument doc(768);
  doc["supply"] = "pt1000_cs27";
  doc["flue"] = "pt1000_cs26";
  doc["return"] = "ow1";
  doc["boiler"] = "ow2_a";
  doc["outdoor"] = "ow2_b";
  doc["home"] = "mqtt";
  sendDoc(doc);
}

static void handleSensorsMappingPost() { sendOk(); }

static void handleSystemInfo() {
  DynamicJsonDocument doc(768);
  const bool ap = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  doc["version"] = FIRMWARE_VERSION;
  doc["channel"] = FIRMWARE_CHANNEL;
  doc["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  const unsigned long up = millis() / 1000;
  char uptime[48];
  snprintf(uptime, sizeof(uptime), "%luh %lum %lus", up / 3600, (up % 3600) / 60, up % 60);
  doc["uptime"] = uptime;
  doc["freeMem"] = String(ESP.getFreeHeap()) + " bytes";
  doc["time"] = "--:--:--";
  doc["date"] = "--.--.----";
  sendDoc(doc);
}

static void handleSystemLog() {
  DynamicJsonDocument doc(2048);
  doc["currentBootCount"] = 1;
  JsonArray arr = doc.createNestedArray("entries");
  JsonObject o = arr.createNestedObject();
  o["bootCount"] = 1;
  o["datetime"] = "boot";
  o["reason"] = "POWERON / 4.5-beta clean flash";
  sendDoc(doc);
}

static void handleBootcountReset() { sendOk(); }

static void handleReboot() {
  sendOk();
  delay(200);
  ESP.restart();
}

static void handleNtpSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = gNtp.enabled;
  doc["server"] = gNtp.server;
  doc["timezone"] = gNtp.timezone;
  doc["updateInterval"] = gNtp.updateInterval;
  sendDoc(doc);
}

static void handleNtpSettingsPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("enabled")) gNtp.enabled = doc["enabled"];
  if (doc.containsKey("server")) gNtp.server = (const char*)doc["server"];
  if (doc.containsKey("timezone")) gNtp.timezone = doc["timezone"];
  if (doc.containsKey("updateInterval")) gNtp.updateInterval = doc["updateInterval"];
  sendOk();
}

static void handleNtpTime() {
  DynamicJsonDocument doc(256);
  doc["time"] = "--:--:--";
  doc["date"] = "--.--.----";
  doc["synced"] = false;
  sendDoc(doc);
}

static void handleMlSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = gMl.enabled;
  doc["observeOnly"] = gMl.observeOnly;
  doc["hypotheses"] = (int)Hyp->count();
  sendDoc(doc);
}

static void handleMlSettingsPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("enabled")) gMl.enabled = doc["enabled"];
  if (doc.containsKey("observeOnly")) gMl.observeOnly = doc["observeOnly"];
  sendOk();
}

static void handleRelayGet() {
  DynamicJsonDocument doc(256);
  doc["fanActiveHigh"] = gRelay.fanActiveHigh;
  doc["pumpActiveHigh"] = gRelay.pumpActiveHigh;
  doc["sensorPwrActiveHigh"] = gRelay.sensorPwrActiveHigh;
  sendDoc(doc);
}

static void handleRelayPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("fanActiveHigh")) gRelay.fanActiveHigh = doc["fanActiveHigh"];
  if (doc.containsKey("pumpActiveHigh")) gRelay.pumpActiveHigh = doc["pumpActiveHigh"];
  if (doc.containsKey("sensorPwrActiveHigh")) gRelay.sensorPwrActiveHigh = doc["sensorPwrActiveHigh"];
  sendOk();
}

static void handleUpdateCheck() {
  String latest, err;
  const bool avail = Ota->check(latest, err);
  DynamicJsonDocument doc(512);
  doc["channel"] = OTA_CHANNEL;
  doc["currentVersion"] = FIRMWARE_VERSION;
  doc["latestVersion"] = latest.length() ? latest : FIRMWARE_VERSION;
  doc["updateAvailable"] = avail;
  doc["versionUrl"] = OTA_VERSION_URL;
  doc["firmwareUrl"] = OTA_FIRMWARE_URL;
  doc["spiffsUrl"] = OTA_SPIFFS_URL;
  doc["policy"] = "clean_flash_or_v45_ota_only_no_4_2_migrate";
  if (err.length()) doc["error"] = err;
  sendDoc(doc);
}

static void handleUpdateInstall() {
  sendJson(200, "{\"success\":false,\"error\":\"use_ota_channel_v45_manual_or_later\"}");
}

static void handleUpdateSettingsGet() {
  DynamicJsonDocument doc(256);
  doc["autoCheck"] = gUpdate.autoCheck;
  doc["checkIntervalHours"] = gUpdate.checkIntervalHours;
  sendDoc(doc);
}

static void handleUpdateSettingsPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("autoCheck")) gUpdate.autoCheck = doc["autoCheck"];
  if (doc.containsKey("checkIntervalHours")) gUpdate.checkIntervalHours = doc["checkIntervalHours"];
  sendOk();
}

static void handleTunnelGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = gTunnel.enabled;
  doc["server"] = gTunnel.server;
  doc["port"] = gTunnel.port;
  doc["token"] = gTunnel.token;
  sendDoc(doc);
}

static void handleTunnelPost() {
  DynamicJsonDocument doc(256);
  if (S->hasArg("plain")) deserializeJson(doc, S->arg("plain"));
  if (doc.containsKey("enabled")) gTunnel.enabled = doc["enabled"];
  if (doc.containsKey("server")) gTunnel.server = (const char*)doc["server"];
  if (doc.containsKey("port")) gTunnel.port = doc["port"];
  if (doc.containsKey("token")) gTunnel.token = (const char*)doc["token"];
  sendOk();
}

static void handleTunnelFrpc() {
  S->send(200, "text/plain", "# frpc not configured in 4.5-beta stub\n");
}

static void handlePins() {
  DynamicJsonDocument doc(1024);
  doc["version"] = FIRMWARE_VERSION;
  doc["note"] = "GPIO25 = реле снятия питания DS18B20; GPIO2 свободен";
  doc["fan"] = PIN_RELAY_FAN;
  doc["pump"] = PIN_RELAY_PUMP;
  doc["sensor_power_relay"] = PIN_RELAY_SENSOR_PWR;
  doc["ow1_return"] = PIN_ONEWIRE_BUS1;
  doc["ow2_room_outdoor"] = PIN_ONEWIRE_BUS2;
  doc["oled_sda"] = PIN_OLED_SDA;
  doc["oled_scl"] = PIN_OLED_SCL;
  doc["enc_clk"] = PIN_ENCODER_CLK;
  doc["enc_dt"] = PIN_ENCODER_DT;
  doc["enc_sw"] = PIN_ENCODER_SW;
  doc["max31865_cs_flue"] = PIN_MAX31865_CS_FLUE;
  doc["max31865_cs_supply"] = PIN_MAX31865_CS_SUPPLY;
  doc["spi_sck"] = PIN_SPI_SCK;
  doc["spi_mosi"] = PIN_SPI_MOSI;
  doc["spi_miso"] = PIN_SPI_MISO;
  sendDoc(doc);
}

static void handleVpsSettings() {
  DynamicJsonDocument doc(512);
  if (!S->hasArg("plain") || deserializeJson(doc, S->arg("plain"))) {
    sendJson(400, "{\"error\":\"bad_json\"}");
    return;
  }
  if (doc.containsKey("baseUrl")) Vps->setBaseUrl(String((const char*)doc["baseUrl"]));
  if (doc.containsKey("token")) Vps->setAuthToken(String((const char*)doc["token"]));
  if (doc.containsKey("deviceId")) Vps->setDeviceId(String((const char*)doc["deviceId"]));
  Journal->add("vps_settings", "updated", 0);
  sendOk();
}

static void handleUserEvent() {
  DynamicJsonDocument doc(512);
  if (!S->hasArg("plain") || deserializeJson(doc, S->arg("plain"))) {
    sendJson(400, "{\"error\":\"bad_json\"}");
    return;
  }
  const char* text = doc["text"] | "";
  const char* audio = doc["audio_url"] | "";
  UserEv->ingestText(text, 0, audio[0] ? audio : nullptr);
  sendOk();
}

static void handleJournal() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("entries");
  for (size_t i = 0; i < Journal->size(); i++) {
    const auto& e = Journal->at(i);
    JsonObject o = arr.createNestedObject();
    o["ms"] = e.ms;
    o["code"] = e.code;
    o["detail"] = e.detail;
  }
  sendDoc(doc);
}

static void handleHypotheses() {
  DynamicJsonDocument doc(3072);
  JsonArray arr = doc.createNestedArray("items");
  for (size_t i = 0; i < Hyp->count(); i++) {
    const Hypothesis& h = Hyp->at(i);
    JsonObject o = arr.createNestedObject();
    o["id"] = h.id;
    o["text"] = h.text;
    o["basis"] = h.basis;
    o["confidence"] = h.confidence;
    o["confirmed"] = h.confirmedByUser;
  }
  sendDoc(doc);
}

static void handleSettingsGet() {
  DynamicJsonDocument doc(1024);
  doc["setpoint"] = gSetpoint;
  doc["hysteresis"] = gHysteresis;
  doc["workMode"] = workModeInt(P->mode);
  doc["systemEnabled"] = P->systemEnabled;
  sendDoc(doc);
}

static void handleSettingsSave() { sendOk(); }
static void handleSettingsReset() { sendOk(); }

}  // namespace

void legacyHttpRegister(
    WebServer& server,
    PlantState& plant,
    BoilerOrchestrator& orch,
    HomeMqttSensor& homeMqtt,
    VpsClient& vps,
    EventJournal& journal,
    UserEventService& userEvents,
    HypothesisEngine& hypotheses,
    OtaChannelV45& ota,
    FanActuator& fan,
    PumpActuator& pump,
    SensorPowerRelay& sensorPwr) {
  S = &server;
  P = &plant;
  Orch = &orch;
  Home = &homeMqtt;
  Vps = &vps;
  Journal = &journal;
  UserEv = &userEvents;
  Hyp = &hypotheses;
  Ota = &ota;
  Fan = &fan;
  Pump = &pump;
  SensorPwr = &sensorPwr;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);

  server.on("/api/system/mode", HTTP_GET, handleModeGet);
  server.on("/api/system/mode", HTTP_POST, handleModePost);
  server.on("/api/system/enable", HTTP_POST, handleEnable);
  server.on("/api/system/info", HTTP_GET, handleSystemInfo);
  server.on("/api/system/log", HTTP_GET, handleSystemLog);
  server.on("/api/system/bootcount/reset", HTTP_POST, handleBootcountReset);
  server.on("/api/system/reboot", HTTP_POST, handleReboot);

  server.on("/api/control", HTTP_POST, handleControl);
  server.on("/api/setpoints", HTTP_POST, handleSetpoints);

  server.on("/api/settings/auto", HTTP_GET, handleSettingsAutoGet);
  server.on("/api/settings/auto", HTTP_POST, handleSettingsAutoPost);
  server.on("/api/settings/comfort", HTTP_GET, handleSettingsComfortGet);
  server.on("/api/settings/comfort", HTTP_POST, handleSettingsComfortPost);
  server.on("/api/settings/mqtt", HTTP_GET, handleSettingsMqttGet);
  server.on("/api/settings/mqtt", HTTP_POST, handleSettingsMqttPost);
  server.on("/api/settings/relay", HTTP_GET, handleRelayGet);
  server.on("/api/settings/relay", HTTP_POST, handleRelayPost);
  server.on("/api/settings/get", HTTP_GET, handleSettingsGet);
  server.on("/api/settings/save", HTTP_POST, handleSettingsSave);
  server.on("/api/settings/reset", HTTP_POST, handleSettingsReset);

  server.on("/api/mqtt/test", HTTP_POST, handleMqttTest);
  server.on("/api/coalFeeding", HTTP_GET, handleCoalFeedingGet);
  server.on("/api/coalFeeding", HTTP_POST, handleCoalFeedingPost);

  server.on("/api/wifi/info", HTTP_GET, handleWifiInfo);
  server.on("/api/wifi/signal", HTTP_GET, handleWifiSignal);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/scan", HTTP_POST, handleWifiScan);
  server.on("/api/wifi/scan/results", HTTP_GET, handleWifiScanResults);
  server.on("/api/wifi/settings", HTTP_GET, handleWifiSettingsGet);
  server.on("/api/wifi/settings", HTTP_POST, handleWifiSettingsPost);
  server.on("/api/wifi/reset", HTTP_POST, handleWifiReset);

  server.on("/api/sensors/scan", HTTP_POST, handleSensorsScan);
  server.on("/api/sensors/mapping", HTTP_GET, handleSensorsMappingGet);
  server.on("/api/sensors/mapping", HTTP_POST, handleSensorsMappingPost);

  server.on("/api/ntp/settings", HTTP_GET, handleNtpSettingsGet);
  server.on("/api/ntp/settings", HTTP_POST, handleNtpSettingsPost);
  server.on("/api/ntp/time", HTTP_GET, handleNtpTime);

  server.on("/api/ml/settings", HTTP_GET, handleMlSettingsGet);
  server.on("/api/ml/settings", HTTP_POST, handleMlSettingsPost);

  server.on("/api/update/check", HTTP_GET, handleUpdateCheck);
  server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
  server.on("/api/update/settings", HTTP_GET, handleUpdateSettingsGet);
  server.on("/api/update/settings", HTTP_POST, handleUpdateSettingsPost);

  server.on("/api/tunnel/settings", HTTP_GET, handleTunnelGet);
  server.on("/api/tunnel/settings", HTTP_POST, handleTunnelPost);
  server.on("/api/tunnel/frpc-config", HTTP_GET, handleTunnelFrpc);

  server.on("/api/pins", HTTP_GET, handlePins);
  server.on("/api/vps/settings", HTTP_POST, handleVpsSettings);
  server.on("/api/events/user", HTTP_POST, handleUserEvent);
  server.on("/api/journal", HTTP_GET, handleJournal);
  server.on("/api/hypotheses", HTTP_GET, handleHypotheses);
}
