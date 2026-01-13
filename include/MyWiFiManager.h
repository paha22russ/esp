#ifndef MY_WIFI_MANAGER_H
#define MY_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "eepromStorage.h"

class MyWiFiManager {
public:
  MyWiFiManager();
  void begin(const WiFiSettings& settings);
  bool connect();
  void resetSettings();
  WiFiSettings getSettings() const;
  void setSettings(const WiFiSettings& settings);
  bool isConnected() const;
  String getIP() const;
  int getRSSI() const;
  
private:
  WiFiManager wifiManager;
  WiFiSettings wifiSettings;
};

#endif // MY_WIFI_MANAGER_H

