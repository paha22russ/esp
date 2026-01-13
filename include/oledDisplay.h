#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "boilerControl.h"
#include "temperatureSensor.h"

class OLEDDisplay {
public:
  OLEDDisplay();
  void begin();
  void update(BoilerControl* boiler, TemperatureSensor* tempSensor, 
              bool setpointEditMode = false, float setpoint = 0);
  void showMenu(uint8_t menuItem, float setpoint, bool systemOn);
  void showOffScreen(float supplyTemp);
  
private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
  unsigned long lastBlink;
  bool blinkState;
  
  void drawMainScreen(BoilerControl* boiler, TemperatureSensor* tempSensor, 
                      bool setpointEditMode, float setpoint);
  String formatTemperature(float temp);
};

#endif // OLED_DISPLAY_H

