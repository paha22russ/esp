#pragma once

#include <Arduino.h>
#include "../config/pins.h"
#include "../config/defaults.h"

// Абстракция мощности вентилятора 0–100%.
// Текущая HW: реле ON/OFF. Будущий AC-диммер подключается сюда же.
class FanActuator {
 public:
  void begin();
  // Желаемая мощность 0..100. Safety может форсировать 0.
  void setPowerPercent(uint8_t pct, bool force = false);
  uint8_t powerPercent() const { return _pct; }
  bool relayOn() const { return _relayOn; }
  void emergencyOff();

 private:
  uint8_t _pct = 0;
  bool _relayOn = false;
  bool _applyHardware();
};

class PumpActuator {
 public:
  void begin();
  void setOn(bool on);
  bool isOn() const { return _on; }
  // После выключения вентилятора — доработать тепло
  void requestAfterheat(uint32_t durationMs = cfg::PUMP_AFTERHEAT_MS);
  void tick(uint32_t nowMs);
  bool afterheatActive(uint32_t nowMs) const;

 private:
  bool _on = false;
  uint32_t _afterheatUntilMs = 0;
};

// Реле полного отключения питания шин DS18B20 (не питание с GPIO!)
class SensorPowerRelay {
 public:
  void begin();
  void setPowered(bool on);
  bool isPowered() const { return _powered; }
  // Блокирующий цикл недопустим в loop — используем state machine
  void startPowerCycle(uint32_t offMs);
  bool tick(uint32_t nowMs);  // true когда цикл завершён
  bool cycling() const { return _cycling; }

 private:
  bool _powered = true;
  bool _cycling = false;
  uint32_t _offUntilMs = 0;
  uint32_t _offMs = 0;
};
