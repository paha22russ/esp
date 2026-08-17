#pragma once

#include "../app/types.h"
#include "../hal/actuators.h"
#include "pt1000.h"
#include "ds18b20_manager.h"

class SensorHub {
 public:
  void begin(SensorPowerRelay* power);
  void tick(uint32_t nowMs);
  void updatePlant(PlantState& plant);

  Pt1000Max31865& pt1000() { return _pt; }
  Ds18b20Manager& ds() { return _ds; }

 private:
  Pt1000Max31865 _pt;
  Ds18b20Manager _ds;

  float _prevSupply = NAN;
  float _prevFlue = NAN;
  float _prevRoom = NAN;
  uint32_t _prevMs = 0;

  static float _rate(float cur, float prev, uint32_t dtMs);
};
