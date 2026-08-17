#include "modes.h"
#include <math.h>
#include <string.h>

void AutoModeController::reset() { _st = St::Idle; }

ControlOutput AutoModeController::tick(const PlantState& plant, uint32_t /*nowMs*/) {
  ControlOutput out;
  strncpy(out.stateName, "auto_idle", sizeof(out.stateName) - 1);
  out.pumpOn = true;  // в авто насос обычно крутится при системе вкл

  if (!plant.systemEnabled) {
    out.fanPowerPct = 0;
    out.pumpOn = false;
    strncpy(out.stateName, "disabled", sizeof(out.stateName) - 1);
    return out;
  }

  if (plant.supply.quality != SensorQuality::Ok) {
    out.fanPowerPct = 0;
    out.requestAfterheat = true;
    strncpy(out.stateName, "supply_invalid", sizeof(out.stateName) - 1);
    return out;
  }

  const float t = plant.supply.celsius;
  const float mn = plant.autoMinC;
  const float mx = plant.autoMaxC;

  if (t >= mx) {
    _st = St::CoolWait;
    out.fanPowerPct = 0;
    out.requestAfterheat = true;
    strncpy(out.stateName, "auto_max", sizeof(out.stateName) - 1);
  } else if (t <= mn) {
    _st = St::Heat;
    out.fanPowerPct = 100;  // HW пока ON/OFF
    strncpy(out.stateName, "auto_heat", sizeof(out.stateName) - 1);
  } else {
    // внутри коридора
    if (_st == St::Heat) {
      out.fanPowerPct = 100;
      strncpy(out.stateName, "auto_heat", sizeof(out.stateName) - 1);
    } else {
      out.fanPowerPct = 0;
      strncpy(out.stateName, "auto_hold", sizeof(out.stateName) - 1);
    }
  }
  return out;
}

void ComfortModeController::reset() { _st = St::Wait; _fuelEpochMs = 0; }

ControlOutput ComfortModeController::tick(const PlantState& plant, uint32_t nowMs) {
  ControlOutput out;
  out.pumpOn = plant.systemEnabled;
  strncpy(out.stateName, "comfort_wait", sizeof(out.stateName) - 1);

  if (!plant.systemEnabled) {
    out.fanPowerPct = 0;
    out.pumpOn = false;
    return out;
  }

  if (plant.supply.quality != SensorQuality::Ok) {
    out.fanPowerPct = 0;
    out.requestAfterheat = true;
    strncpy(out.stateName, "comfort_no_supply", sizeof(out.stateName) - 1);
    return out;
  }

  const float supply = plant.supply.celsius;
  // Comfort целится ТОЛЬКО в температуру дома (MQTT). Котельная — вспомогательный сенсор.
  if (plant.home.quality != SensorQuality::Ok) {
    out.fanPowerPct = 0;
    out.pumpOn = plant.systemEnabled;
    strncpy(out.stateName, "comfort_home_offline", sizeof(out.stateName) - 1);
    return out;  // orchestrator переключит на Auto
  }
  const float room = plant.home.celsius;
  const float rate = plant.supplyRateCPerMin;
  const float bMin = plant.comfortBoilerMinC;
  const float bMax = plant.comfortBoilerMaxC;
  const float target = plant.comfortRoomTargetC;

  // Жёсткий потолок котла — заранее гасим с учётом инерции
  if (supply >= bMax || (supply > bMax - 3.0f && rate > 0.4f)) {
    _st = St::HoldMax;
    out.fanPowerPct = 0;
    out.requestAfterheat = true;
    strncpy(out.stateName, "comfort_inertia_cut", sizeof(out.stateName) - 1);
    return out;
  }

  const float roomErr = target - room;
  const uint32_t sinceFuel = (plant.fuelLoadedAtMs > 0) ? (nowMs - plant.fuelLoadedAtMs) : 0;

  if (roomErr > 0.4f && supply < bMax - 2.0f) {
    _st = St::Heat;
    out.fanPowerPct = 100;
    strncpy(out.stateName, "comfort_heat", sizeof(out.stateName) - 1);
  } else if (roomErr < -0.2f || supply > (bMin + bMax) * 0.5f) {
    _st = St::Coast;
    out.fanPowerPct = 0;
    out.requestAfterheat = true;
    strncpy(out.stateName, "comfort_coast", sizeof(out.stateName) - 1);
  } else {
    _st = St::Maintain;
    // Пока ON/OFF: кратковременные импульсы можно добавить позже через fan_power
    out.fanPowerPct = (supply < bMin) ? 100 : 0;
    strncpy(out.stateName, "comfort_maintain", sizeof(out.stateName) - 1);
  }

  // Сразу после загрузки топлива — осторожнее (инерция разгона)
  if (sinceFuel > 0 && sinceFuel < 15UL * 60UL * 1000UL && rate > 0.8f && out.fanPowerPct > 0) {
    out.fanPowerPct = 0;
    strncpy(out.stateName, "comfort_post_fuel_guard", sizeof(out.stateName) - 1);
  }

  (void)_fuelEpochMs;
  return out;
}

void NeuroModeController::reset() {}

ControlOutput NeuroModeController::tick(const PlantState& /*plant*/, uint32_t /*nowMs*/) {
  ControlOutput out;
  out.fanPowerPct = 0;
  out.pumpOn = false;
  strncpy(out.stateName, "neuro_observe", sizeof(out.stateName) - 1);
  return out;
}
