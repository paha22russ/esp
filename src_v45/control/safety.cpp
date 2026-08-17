#include "safety.h"
#include <string.h>

void SafetyGuard::begin(FanActuator* fan, PumpActuator* pump) {
  _fan = fan;
  _pump = pump;
  _tripped = false;
  _reason[0] = 0;
}

void SafetyGuard::_trip(const char* why, PlantState& plant) {
  _tripped = true;
  strncpy(_reason, why, sizeof(_reason) - 1);
  plant.safetyTrip = true;
  strncpy(plant.safetyReason, why, sizeof(plant.safetyReason) - 1);
  if (_fan) _fan->emergencyOff();
  plant.fanPowerPct = 0;
  plant.fanRelayOn = false;
  // Насос оставляем/включаем для отвода тепла
  if (_pump) {
    _pump->setOn(true);
    _pump->requestAfterheat();
  }
  plant.pumpOn = true;
  Serial.printf("[SAFETY] TRIP: %s\n", why);
}

void SafetyGuard::resetTrip() {
  _tripped = false;
  _reason[0] = 0;
}

bool SafetyGuard::evaluate(PlantState& plant, uint32_t /*nowMs*/) {
  if (_tripped) {
    // удерживаем безопасное состояние
    if (_fan) _fan->emergencyOff();
    plant.fanPowerPct = 0;
    plant.safetyTrip = true;
    strncpy(plant.safetyReason, _reason, sizeof(plant.safetyReason) - 1);
    return true;
  }

  const bool supplyOk = plant.supply.quality == SensorQuality::Ok;
  if (supplyOk && plant.supply.celsius >= cfg::SAFETY_SUPPLY_CRITICAL_C) {
    _trip("supply_critical", plant);
    return true;
  }
  if (supplyOk && plant.supply.celsius >= cfg::SAFETY_SUPPLY_OVERHEAT_C) {
    _trip("supply_overheat", plant);
    return true;
  }

  if (plant.flue.quality == SensorQuality::Ok &&
      plant.flue.celsius >= cfg::SAFETY_FLUE_CRITICAL_C) {
    _trip("flue_critical", plant);
    return true;
  }

  // Потеря критического датчика подачи при работающей системе — снижаем мощность
  if (plant.systemEnabled && plant.supply.quality != SensorQuality::Ok) {
    if (_fan) _fan->setPowerPercent(0);
    plant.fanPowerPct = 0;
    // не полный trip — ждём восстановления PT1000
  }

  plant.safetyTrip = false;
  plant.safetyReason[0] = 0;
  return false;
}
