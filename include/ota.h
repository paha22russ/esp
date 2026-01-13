#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#ifdef OTA_ENABLED
#include <ArduinoOTA.h>
#endif

class OTA {
public:
  OTA();
  void begin();
  void handle();
  
private:
#ifdef OTA_ENABLED
  bool initialized;
#endif
};

#endif // OTA_H

