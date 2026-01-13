#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "eepromStorage.h"

class NTPTime {
public:
  NTPTime();
  void begin(const NTPSettings& settings);
  void update();
  void setSettings(const NTPSettings& settings);
  unsigned long getUnixTime() const;
  bool isTimeSet() const;
  
private:
  WiFiUDP udp;
  NTPClient* timeClient;
  NTPSettings settings;
  unsigned long lastUpdate;
  bool timeSet;
};

#endif // NTP_TIME_H

