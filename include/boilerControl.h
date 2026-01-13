#ifndef BOILER_CONTROL_H
#define BOILER_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "eepromStorage.h"
#include "temperatureSensor.h"

class BoilerControl {
public:
  BoilerControl();
  void begin(TemperatureSensor* tempSensor);
  void update();
  void setParams(const ExtendedAutoParams& params);
  ExtendedAutoParams getParams() const;
  BoilerState getState() const;
  bool isFanOn() const;
  bool isPumpOn() const;
  bool isSystemOn() const;
  void setSystemOn(bool on);
  void activateFuelFeed();
  void setManualFan(bool on);
  void setManualPump(bool on);
  void clearManualControl();
  bool isManualControl() const;
  unsigned long getManualControlTimeRemaining() const;
  bool isFuelFeedActive() const;
  unsigned long getFuelFeedTimeRemaining() const;
  String getStateString() const;
  String getWarningMessage() const;
  String getErrorMessage() const;
  
private:
  TemperatureSensor* tempSensor;
  ExtendedAutoParams params;
  BoilerState state;
  bool fanOn;
  bool pumpOn;
  bool systemOn;
  bool fuelFeedActive;
  unsigned long fuelFeedStartTime;
  bool manualFanControl;
  bool manualPumpControl;
  unsigned long manualControlStartTime;
  unsigned long heatingStartTime;
  unsigned long inertiaStartTime;
  unsigned long lastPumpAntiStick;
  String warningMessage;
  String errorMessage;
  
  void updateState();
  void updateFan();
  void updatePump();
  void checkSafety();
  void smartStartup();
  bool shouldPumpRun();
};

#endif // BOILER_CONTROL_H

