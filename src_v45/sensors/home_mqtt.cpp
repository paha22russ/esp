#include "home_mqtt.h"
#include <WiFi.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../config/defaults.h"

HomeMqttSensor* gHomeMqttPtr = nullptr;

void HomeMqttSensor::begin(WiFiClient* wifi) {
  _wifi = wifi;
  _mqtt.setClient(*_wifi);
  _mqtt.setCallback(_thunk);
  gHomeMqttPtr = this;
}

void HomeMqttSensor::setBroker(const char* host, uint16_t port, const char* user, const char* pass) {
  _host = host ? host : "";
  _port = port;
  _user = user ? user : "";
  _pass = pass ? pass : "";
}

void HomeMqttSensor::setTopics(const char* tempTopic, const char* lwtTopic) {
  if (tempTopic) _tempTopic = tempTopic;
  if (lwtTopic) _lwtTopic = lwtTopic;
}

void HomeMqttSensor::_thunk(char* topic, byte* payload, unsigned int len) {
  if (gHomeMqttPtr) gHomeMqttPtr->_onMessage(topic, payload, len);
}

void HomeMqttSensor::_onMessage(char* topic, byte* payload, unsigned int len) {
  char buf[96];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = 0;

  if (_lwtTopic.length() && strcmp(topic, _lwtTopic.c_str()) == 0) {
    // online/offline
    if (strcasecmp(buf, "online") == 0 || strcmp(buf, "1") == 0) _lwtOnline = true;
    else _lwtOnline = false;
    return;
  }

  if (_tempTopic.length() && strcmp(topic, _tempTopic.c_str()) == 0) {
    char* end = nullptr;
    float t = strtof(buf, &end);
    if (end != buf && isfinite(t) && t > -40.0f && t < 80.0f) {
      _temp = t;
      _tempValid = true;
      _lastTempMs = millis();
      _lwtOnline = true;  // получение температуры = признак живости, если LWT нет
    } else {
      _tempValid = false;
    }
  }
}

void HomeMqttSensor::_ensureConnected(uint32_t nowMs) {
  if (_host.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (_mqtt.connected()) {
    _mqtt.loop();
    return;
  }
  if (_lastReconnectMs != 0 && (nowMs - _lastReconnectMs) < 5000) return;
  _lastReconnectMs = nowMs;
  _mqtt.setServer(_host.c_str(), _port);
  const bool ok = _user.length()
                      ? _mqtt.connect("esp32-boiler-v45", _user.c_str(), _pass.c_str())
                      : _mqtt.connect("esp32-boiler-v45");
  if (ok) {
    _mqtt.subscribe(_tempTopic.c_str());
    if (_lwtTopic.length()) _mqtt.subscribe(_lwtTopic.c_str());
    Serial.println("[HomeMQTT] connected");
  }
}

void HomeMqttSensor::tick(uint32_t nowMs) {
  _ensureConnected(nowMs);
  if (_mqtt.connected()) _mqtt.loop();

  // stale timeout
  if (_tempValid && _lastTempMs != 0 &&
      (nowMs - _lastTempMs) > cfg::HOME_TEMP_STALE_MS) {
    _tempValid = false;
  }
}

void HomeMqttSensor::applyTo(PlantState& plant) const {
  plant.home.celsius = _temp;
  plant.home.updatedAtMs = _lastTempMs;
  if (online()) {
    plant.home.quality = SensorQuality::Ok;
  } else if (_lwtOnline && !_tempValid) {
    plant.home.quality = SensorQuality::Stale;
  } else {
    plant.home.quality = SensorQuality::Missing;
  }
}
