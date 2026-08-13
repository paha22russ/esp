#pragma once

#include "modes.h"
#include "safety.h"
#include "../hal/actuators.h"
#include "../app/types.h"

class BoilerOrchestrator {
 public:
  void begin(FanActuator* fan, PumpActuator* pump, SafetyGuard* safety);
  void setMode(WorkMode mode);
  WorkMode mode() const { return _mode; }
  void tick(PlantState& plant, uint32_t nowMs);
  const char* lastStateName() const { return _lastState; }

 private:
  FanActuator* _fan = nullptr;
  PumpActuator* _pump = nullptr;
  SafetyGuard* _safety = nullptr;
  WorkMode _mode = WorkMode::Auto;

  AutoModeController _auto;
  ComfortModeController _comfort;
  NeuroModeController _neuro;
  AutoModeController _shadowAuto;  // для Neuro: теневое управление + лог

  char _lastState[32] = {0};
  uint8_t _lastFanPct = 0;
};
