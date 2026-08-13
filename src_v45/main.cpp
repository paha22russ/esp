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
#include "config/ota_urls.h"
#include "app/types.h"
#include "app/ota_channel.h"
#include "hal/actuators.h"
#include "sensors/sensor_hub.h"
#include "sensors/home_mqtt.h"
#include "control/safety.h"
#include "control/orchestrator.h"
#include "telemetry/vps_client.h"
#include "events/journal.h"
#include "events/user_events.h"
#include "ml/hypothesis_engine.h"
#include "ui/encoder_menu.h"
#include "ui/legacy_http.h"

FanActuator gFan;
PumpActuator gPump;
SensorPowerRelay gSensorPwr;
SensorHub gSensors;
HomeMqttSensor gHomeMqtt;
SafetyGuard gSafety;
BoilerOrchestrator gOrch;
VpsClient gVps;
EventJournal gJournal;
UserEventService gUserEvents;
HypothesisEngine gHypotheses;
OtaChannelV45 gOta;
EncoderMenu gMenu;
PlantState gPlant;
WebServer gServer(80);
WiFiClient gWifiClient;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C gOled(U8G2_R0, /* reset=*/U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

uint32_t gLastTelemetryMs = 0;
uint32_t gLastControlMs = 0;

static void dsLog(const char* code, const char* detail) {
  gJournal.add(code, detail, 0);
}

static uint32_t unixNow() { return 0; }

static void onMenuMode(WorkMode m) {
  gOrch.setMode(m);
  gJournal.add("mode_change", workModeName(m), unixNow());
}

static void onMenuEnable(bool en) {
  gPlant.systemEnabled = en;
  gJournal.add(en ? "system_on" : "system_off", "encoder", unixNow());
}

static void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }
  Serial.printf("[WiFi] %s\n",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected");
}

static void onVpsCommand(const char* jsonCmd) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, jsonCmd)) return;
  const char* type = doc["type"] | "";
  if (!strcmp(type, "set_mode")) {
    WorkMode wm = WorkMode::Auto;
    if (doc["mode"].is<int>()) {
      const int m = doc["mode"];
      if (m == 1) wm = WorkMode::Comfort;
      else if (m == 2) wm = WorkMode::Neuro;
    } else {
      const char* m = doc["mode"] | "auto";
      if (!strcmp(m, "comfort")) wm = WorkMode::Comfort;
      else if (!strcmp(m, "neuro")) wm = WorkMode::Neuro;
    }
    gOrch.setMode(wm);
    gPlant.mode = wm;
    gJournal.add("vps_cmd_mode", workModeName(wm), unixNow());
  } else if (!strcmp(type, "set_enabled")) {
    const bool en = doc["enabled"] | false;
    gPlant.systemEnabled = en;
    gJournal.add(en ? "vps_cmd_on" : "vps_cmd_off", "", unixNow());
  } else if (!strcmp(type, "user_event")) {
    const char* text = doc["text"] | "";
    gUserEvents.ingestText(text, unixNow(), nullptr);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s %s (%s) ===\n", FIRMWARE_PRODUCT, FIRMWARE_VERSION, FIRMWARE_CHANNEL);
  Serial.println("Flash policy: CLEAN ONLY — no migrate from 4.2.x");

  gFan.begin();
  gPump.begin();
  gSensorPwr.begin();
  gSensors.begin(&gSensorPwr);
  gSensors.ds().setLogger(dsLog);
  gHomeMqtt.begin(&gWifiClient);
  gHomeMqtt.setTopics("home/esp01/temp", "home/esp01/status");
  gSafety.begin(&gFan, &gPump);
  gOrch.begin(&gFan, &gPump, &gSafety);
  gVps.begin();
  gVps.setBaseUrl(cfg::DEFAULT_VPS_BASE_URL);
  gVps.setCommandHandler(onVpsCommand);
  gOta.begin();
  gJournal.clear();
  gUserEvents.begin(&gJournal, &gVps, &gPlant);
  gHypotheses.begin(&gJournal);
  gJournal.add("boot", FIRMWARE_VERSION, unixNow());

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  gOled.setI2CAddress(OLED_I2C_ADDR << 1);
  gOled.begin();
  gMenu.begin(&gOled);
  gMenu.setCallbacks(onMenuMode, onMenuEnable);

  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] mount failed");
  }

  setupWifi();

  legacyHttpRegister(gServer, gPlant, gOrch, gHomeMqtt, gVps, gJournal, gUserEvents, gHypotheses, gOta,
                     gFan, gPump, gSensorPwr);
  gServer.begin();
  Serial.println("[HTTP] legacy UI API ready");
}

void loop() {
  const uint32_t now = millis();

  gSensors.tick(now);
  gSensors.updatePlant(gPlant);
  gHomeMqtt.tick(now);
  gHomeMqtt.applyTo(gPlant);
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
  gVps.pollCommands(now);

  gMenu.tick(now, gPlant, gOrch.lastStateName());
  gServer.handleClient();
}
