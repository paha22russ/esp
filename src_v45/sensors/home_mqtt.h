#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "../app/types.h"

// Температура дома с ESP01 по MQTT.
// При offline/invalid Comfort обязан уйти в Auto (см. orchestrator).
class HomeMqttSensor {
 public:
  void begin(WiFiClient* wifi);
  void setBroker(const char* host, uint16_t port, const char* user, const char* pass);
  void setTopics(const char* tempTopic, const char* lwtTopic);
  void tick(uint32_t nowMs);
  void applyTo(PlantState& plant) const;
  bool online() const { return _lwtOnline && _tempValid; }

 private:
  WiFiClient* _wifi = nullptr;
  PubSubClient _mqtt;
  String _host;
  uint16_t _port = 1883;
  String _user;
  String _pass;
  String _tempTopic = "home/esp01/temp";
  String _lwtTopic = "home/esp01/status";

  float _temp = NAN;
  bool _tempValid = false;
  bool _lwtOnline = false;
  uint32_t _lastTempMs = 0;
  uint32_t _lastReconnectMs = 0;

  static void _thunk(char* topic, byte* payload, unsigned int len);
  void _onMessage(char* topic, byte* payload, unsigned int len);
  void _ensureConnected(uint32_t nowMs);
};

extern HomeMqttSensor* gHomeMqttPtr;
