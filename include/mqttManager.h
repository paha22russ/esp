#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "eepromStorage.h"
#include "boilerControl.h"
#include "temperatureSensor.h"
#include "ntpTime.h"

class MQTTManager {
public:
  MQTTManager();
  void begin(const MQTTSettings& settings);
  void update();
  void setSettings(const MQTTSettings& settings);
  void publishState(BoilerControl* boiler, TemperatureSensor* tempSensor, NTPTime* ntp);
  void publishTemperatures(TemperatureSensor* tempSensor, NTPTime* ntp);
  void publishSimpleValues(BoilerControl* boiler, TemperatureSensor* tempSensor);
  void publishSetpoint(float setpoint, NTPTime* ntp);
  void publishNetworkInfo(NTPTime* ntp);
  void publishUptime(unsigned long uptime, NTPTime* ntp);
  bool isConnected() const;
  
private:
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  MQTTSettings settings;
  unsigned long lastPublish;
  unsigned long lastUptimePublish;
  bool connected;
  
  void reconnect();
  String getTopic(const String& suffix);
  void callback(char* topic, byte* payload, unsigned int length);
};

#endif // MQTT_MANAGER_H

