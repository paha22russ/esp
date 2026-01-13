#ifndef WEB_SERVER_HANDLER_H
#define WEB_SERVER_HANDLER_H

#include <Arduino.h>
#ifdef PLATFORM_ESP32
#include <WebServer.h>
#else
#include <ESP8266WebServer.h>
#define WebServer ESP8266WebServer
#endif
#include <ArduinoJson.h>
#include "config.h"
#include "eepromStorage.h"

// Ensure WebServer is defined before any WiFiManager includes
#ifdef PLATFORM_ESP32
// WebServer is already included above
#else
// ESP8266WebServer is already included above
#endif

// Forward declarations
class BoilerControl;
class TemperatureSensor;
class MQTTManager;
class MyWiFiManager;
class NTPTime;

class WebServerHandler {
public:
  WebServerHandler();
  void begin();
  void handleClient();
  void setBoilerControl(BoilerControl* boiler);
  void setTemperatureSensor(TemperatureSensor* tempSensor);
  void setMQTTManager(MQTTManager* mqtt);
  void setWiFiManager(MyWiFiManager* wifi);
  void setNTPTime(NTPTime* ntp);
  void setSavedParams(SavedParams* params);
  void setEEPROMStorage(EEPROMStorage* storage);
  
private:
  WebServer server;
  BoilerControl* boiler;
  TemperatureSensor* tempSensor;
  MQTTManager* mqtt;
  MyWiFiManager* wifi;
  NTPTime* ntp;
  SavedParams* savedParams;
  EEPROMStorage* eepromStorage;
  
  void handleRoot();
  void handleNotFound();
  void handleGetStatus();
  void handleGetAutoParams();
  void handleSetAutoParams();
  void handleResetWiFi();
  void handleSystemOn();
  void handleReboot();
  void handleFuelFeed();
  void handleGetMqttSettings();
  void handleSetMqttSettings();
  void handleGetSensorMapping();
  void handleSetSensorMapping();
  void handleRescanSensors();
  void handleGetSystemInfo();
  void handleScanWiFi();
  void handleGetWiFiSettings();
  void handleSetWiFiSettings();
  void handleGetNtpSettings();
  void handleSetNtpSettings();
  void handleManualRelayControl();
  void handleManualFan();
  void handleManualPump();
  void sendJSON(int code, const JsonDocument& doc);
};

#endif // WEB_SERVER_HANDLER_H

