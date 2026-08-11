#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "config/version.h"
#include "config/pins.h"
#include "config/defaults.h"
#include "app/types.h"
#include "hal/actuators.h"
#include "sensors/sensor_hub.h"
#include "control/safety.h"
#include "control/orchestrator.h"
#include "telemetry/vps_client.h"
#include "events/journal.h"
#include "events/user_events.h"
#include "ml/hypothesis_engine.h"

FanActuator gFan;
PumpActuator gPump;
SensorPowerRelay gSensorPwr;
SensorHub gSensors;
SafetyGuard gSafety;
BoilerOrchestrator gOrch;
VpsClient gVps;
EventJournal gJournal;
UserEventService gUserEvents;
HypothesisEngine gHypotheses;
PlantState gPlant;
WebServer gServer(80);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C gOled(U8G2_R0, /* reset=*/U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

uint32_t gLastTelemetryMs = 0;
uint32_t gLastControlMs = 0;
uint32_t gLastOledMs = 0;

static void dsLog(const char* code, const char* detail) {
  gJournal.add(code, detail, 0);
}

static uint32_t unixNow() { return 0; }

static void drawOled() {
  gOled.clearBuffer();
  gOled.setFont(u8g2_font_6x12_tf);
  gOled.drawStr(0, 10, "Boiler 4.5-beta");

  char line[40];
  snprintf(line, sizeof(line), "Mode:%s", workModeNameRu(gPlant.mode));
  gOled.drawStr(0, 24, line);

  if (gPlant.supply.quality == SensorQuality::Ok)
    snprintf(line, sizeof(line), "Supply:%.1fC", gPlant.supply.celsius);
  else
    snprintf(line, sizeof(line), "Supply: --");
  gOled.drawStr(0, 38, line);

  if (gPlant.flue.quality == SensorQuality::Ok)
    snprintf(line, sizeof(line), "Flue:%.0f Fan:%u%%", gPlant.flue.celsius, (unsigned)gPlant.fanPowerPct);
  else
    snprintf(line, sizeof(line), "Flue: -- Fan:%u%%", (unsigned)gPlant.fanPowerPct);
  gOled.drawStr(0, 52, line);

  if (gPlant.safetyTrip) gOled.drawStr(0, 64, "SAFETY!");
  else gOled.drawStr(0, 64, gOrch.lastStateName());
  gOled.sendBuffer();
}

static void handleStatus() {
  StaticJsonDocument<1536> doc;
  doc["version"] = FIRMWARE_VERSION;
  doc["channel"] = FIRMWARE_CHANNEL;
  doc["mode"] = workModeName(gPlant.mode);
  doc["modeRu"] = workModeNameRu(gPlant.mode);
  doc["state"] = gOrch.lastStateName();
  doc["systemEnabled"] = gPlant.systemEnabled;
  doc["fanPowerPct"] = gPlant.fanPowerPct;
  doc["fan"] = gPlant.fanRelayOn;
  doc["pump"] = gPlant.pumpOn;
  doc["safetyTrip"] = gPlant.safetyTrip;
  doc["safetyReason"] = gPlant.safetyReason;

  if (gPlant.supply.quality == SensorQuality::Ok) doc["supplyTemp"] = gPlant.supply.celsius;
  else doc["supplyTemp"] = nullptr;
  if (gPlant.flue.quality == SensorQuality::Ok) doc["flueTemp"] = gPlant.flue.celsius;
  else doc["flueTemp"] = nullptr;
  if (gPlant.ret.quality == SensorQuality::Ok) doc["returnTemp"] = gPlant.ret.celsius;
  else doc["returnTemp"] = nullptr;
  if (gPlant.boilerRoom.quality == SensorQuality::Ok) doc["boilerTemp"] = gPlant.boilerRoom.celsius;
  else doc["boilerTemp"] = nullptr;
  if (gPlant.outdoor.quality == SensorQuality::Ok) doc["outdoorTemp"] = gPlant.outdoor.celsius;
  else doc["outdoorTemp"] = nullptr;

  doc["autoMin"] = gPlant.autoMinC;
  doc["autoMax"] = gPlant.autoMaxC;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "up" : "down";
  doc["vpsQueue"] = (int)gVps.queueSize();
  doc["hypotheses"] = (int)gHypotheses.count();

  String out;
  serializeJson(doc, out);
  gServer.send(200, "application/json", out);
}

static void handleModePost() {
  StaticJsonDocument<256> doc;
  DeserializationError err = DeserializationError::Ok;
  if (!gServer.hasArg("plain")) err = DeserializationError::EmptyInput;
  else err = deserializeJson(doc, gServer.arg("plain"));
  if (err) {
    gServer.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }
  const char* m = doc["mode"] | "auto";
  WorkMode wm = WorkMode::Auto;
  if (!strcmp(m, "comfort")) wm = WorkMode::Comfort;
  else if (!strcmp(m, "neuro")) wm = WorkMode::Neuro;
  else if (!strcmp(m, "auto")) wm = WorkMode::Auto;
  else {
    gServer.send(400, "application/json", "{\"error\":\"unknown_mode\"}");
    return;
  }
  gOrch.setMode(wm);
  gPlant.mode = wm;
  gJournal.add("mode_change", m, unixNow());
  gServer.send(200, "application/json", "{\"success\":true}");
}

static void handleEnable() {
  const bool en = gServer.hasArg("enabled") && gServer.arg("enabled") == "1";
  gPlant.systemEnabled = en;
  gJournal.add(en ? "system_on" : "system_off", "", unixNow());
  gServer.send(200, "application/json", "{\"success\":true}");
}

static void handleSetpoints() {
  StaticJsonDocument<512> doc;
  if (!gServer.hasArg("plain") || deserializeJson(doc, gServer.arg("plain"))) {
    gServer.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }
  if (doc.containsKey("autoMin")) gPlant.autoMinC = doc["autoMin"];
  if (doc.containsKey("autoMax")) gPlant.autoMaxC = doc["autoMax"];
  if (doc.containsKey("comfortRoom")) gPlant.comfortRoomTargetC = doc["comfortRoom"];
  if (doc.containsKey("comfortBoilerMin")) gPlant.comfortBoilerMinC = doc["comfortBoilerMin"];
  if (doc.containsKey("comfortBoilerMax")) gPlant.comfortBoilerMaxC = doc["comfortBoilerMax"];
  gServer.send(200, "application/json", "{\"success\":true}");
}

static void handleUserEvent() {
  StaticJsonDocument<512> doc;
  if (!gServer.hasArg("plain") || deserializeJson(doc, gServer.arg("plain"))) {
    gServer.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }
  const char* text = doc["text"] | "";
  const char* audio = doc["audio_url"] | "";
  gUserEvents.ingestText(text, unixNow(), audio[0] ? audio : nullptr);
  gServer.send(200, "application/json", "{\"success\":true}");
}

static void handleJournal() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.createNestedArray("entries");
  for (size_t i = 0; i < gJournal.size(); i++) {
    const auto& e = gJournal.at(i);
    JsonObject o = arr.createNestedObject();
    o["ms"] = e.ms;
    o["code"] = e.code;
    o["detail"] = e.detail;
  }
  String out;
  serializeJson(doc, out);
  gServer.send(200, "application/json", out);
}

static void handleHypotheses() {
  StaticJsonDocument<3072> doc;
  JsonArray arr = doc.createNestedArray("items");
  for (size_t i = 0; i < gHypotheses.count(); i++) {
    const Hypothesis& h = gHypotheses.at(i);
    JsonObject o = arr.createNestedObject();
    o["id"] = h.id;
    o["text"] = h.text;
    o["basis"] = h.basis;
    o["confidence"] = h.confidence;
    o["confirmed"] = h.confirmedByUser;
  }
  String out;
  serializeJson(doc, out);
  gServer.send(200, "application/json", out);
}

static void handlePins() {
  StaticJsonDocument<1024> doc;
  doc["version"] = FIRMWARE_VERSION;
  doc["note"] = "GPIO25 = реле снятия питания DS18B20, не питание от GPIO";
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
  String out;
  serializeJson(doc, out);
  gServer.send(200, "application/json", out);
}

static void handleVpsSettings() {
  StaticJsonDocument<512> doc;
  if (!gServer.hasArg("plain") || deserializeJson(doc, gServer.arg("plain"))) {
    gServer.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }
  if (doc.containsKey("baseUrl")) gVps.setBaseUrl(String((const char*)doc["baseUrl"]));
  if (doc.containsKey("token")) gVps.setAuthToken(String((const char*)doc["token"]));
  if (doc.containsKey("deviceId")) gVps.setDeviceId(String((const char*)doc["deviceId"]));
  gJournal.add("vps_settings", "updated", unixNow());
  gServer.send(200, "application/json", "{\"success\":true}");
}

static void handleRoot() {
  if (SPIFFS.exists("/index.html")) {
    File f = SPIFFS.open("/index.html", "r");
    gServer.streamFile(f, "text/html");
    f.close();
    return;
  }
  gServer.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><title>Котел 4.5-beta</title>"
              "<body style='background:#111;color:#eee;font-family:sans-serif;padding:16px'>"
              "<h1>Котел 4.5-beta</h1><p>SPIFFS UI отсутствует. API: /api/status</p>"
              "</body>");
}

static void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // сохранённые credentials; иначе SoftAP позже
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }
  Serial.printf("[WiFi] %s\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s %s (%s) ===\n", FIRMWARE_PRODUCT, FIRMWARE_VERSION, FIRMWARE_CHANNEL);

  gFan.begin();
  gPump.begin();
  gSensorPwr.begin();
  gSensors.begin(&gSensorPwr);
  gSensors.ds().setLogger(dsLog);
  gSafety.begin(&gFan, &gPump);
  gOrch.begin(&gFan, &gPump, &gSafety);
  gVps.begin();
  gJournal.clear();
  gUserEvents.begin(&gJournal, &gVps, &gPlant);
  gHypotheses.begin(&gJournal);
  gJournal.add("boot", FIRMWARE_VERSION, unixNow());

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  gOled.setI2CAddress(OLED_I2C_ADDR << 1);
  gOled.begin();
  drawOled();

  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] mount failed");
  }

  setupWifi();

  gServer.on("/", HTTP_GET, handleRoot);
  gServer.on("/api/status", HTTP_GET, handleStatus);
  gServer.on("/api/system/mode", HTTP_POST, handleModePost);
  gServer.on("/api/system/enable", HTTP_POST, handleEnable);
  gServer.on("/api/setpoints", HTTP_POST, handleSetpoints);
  gServer.on("/api/events/user", HTTP_POST, handleUserEvent);
  gServer.on("/api/journal", HTTP_GET, handleJournal);
  gServer.on("/api/hypotheses", HTTP_GET, handleHypotheses);
  gServer.on("/api/pins", HTTP_GET, handlePins);
  gServer.on("/api/vps/settings", HTTP_POST, handleVpsSettings);
  gServer.begin();
  Serial.println("[HTTP] ready");
}

void loop() {
  const uint32_t now = millis();

  gSensors.tick(now);
  gSensors.updatePlant(gPlant);
  gPlant.sensorPowerOn = gSensorPwr.isPowered();

  if (now - gLastControlMs >= cfg::CONTROL_TICK_MS) {
    gLastControlMs = now;
    gOrch.tick(gPlant, now);
    gHypotheses.observe(gPlant, gPlant.fanPowerPct, now);
  }

  if (now - gLastTelemetryMs >= cfg::TELEMETRY_INTERVAL_MS) {
    gLastTelemetryMs = now;
    gVps.enqueueTelemetry(gPlant, gOrch.lastStateName(), unixNow());
  }
  gVps.tick(now);

  if (now - gLastOledMs >= 1000) {
    gLastOledMs = now;
    drawOled();
  }

  gServer.handleClient();
}
