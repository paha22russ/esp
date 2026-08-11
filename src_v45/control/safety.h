#pragma once

#include "../app/types.h"
#include "../hal/actuators.h"
#include "../config/defaults.h"

// Аппаратный safety layer. Работает независимо от Wi-Fi / VPS / Telegram / Neuro.
// AI НЕ может обойти эти ограничения.
class SafetyGuard {
 public:
  void begin(FanActuator* fan, PumpActuator* pump);
  // Возвращает true если сработала авария (trip)
  bool evaluate(PlantState& plant, uint32_t nowMs);
  bool tripped() const { return _tripped; }
  const char* reason() const { return _reason; }
  void resetTrip();  // только ручной сброс / явная команда оператора

 private:
  FanActuator* _fan = nullptr;
  PumpActuator* _pump = nullptr;
  bool _tripped = false;
  char _reason[48] = {0};

  void _trip(const char* why, PlantState& plant);
};
