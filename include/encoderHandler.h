#ifndef ENCODER_HANDLER_H
#define ENCODER_HANDLER_H

#include <Arduino.h>
#include "config.h"

class EncoderHandler {
public:
  EncoderHandler();
  void begin();
  EncoderEvent getEvent();
  
private:
  int lastCLK;
  int lastDT;
  int lastSW;
  unsigned long lastPressTime;
  bool pressHandled;
  bool menuMode;
  EncoderEvent currentEvent;
  
  void updateEncoder();
  void updateButton();
};

#endif // ENCODER_HANDLER_H

