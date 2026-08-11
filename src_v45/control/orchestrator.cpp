#include "orchestrator.h"
#include <string.h>

void BoilerOrchestrator::begin(FanActuator* fan, PumpActuator* pump, SafetyGuard* safety) {
  _fan = fan;
  _pump = pump;
  _safety = safety;
  _auto.reset();
  _comfort.reset();
  _neuro.reset();
  _shadowAuto.reset();
}

void BoilerOrchestrator::setMode(WorkMode mode) {
  if (_mode == mode) return;
  _mode = mode;
  _auto.reset();
  _comfort.reset();
  _neuro.reset();
  _shadowAuto.reset();
}

void BoilerOrchestrator::tick(PlantState& plant, uint32_t nowMs) {
  plant.mode = _mode;

  // 1) Safety всегда первым — может обесточить вентилятор
  if (_safety && _safety->evaluate(plant, nowMs)) {
    strncpy(_lastState, "SAFETY", sizeof(_lastState) - 1);
    _lastFanPct = 0;
    plant.fanPowerPct = 0;
    plant.fanRelayOn = _fan ? _fan->relayOn() : false;
    plant.pumpOn = _pump ? _pump->isOn() : false;
    return;
  }

  ControlOutput out;
  if (_mode == WorkMode::Comfort) {
    // Политика: Comfort только при валидном доме (MQTT). Иначе — Авто.
    if (plant.home.quality != SensorQuality::Ok) {
      out = _auto.tick(plant, nowMs);
      strncpy(out.stateName, "comfort_fallback_auto", sizeof(out.stateName) - 1);
    } else {
      out = _comfort.tick(plant, nowMs);
      if (strcmp(out.stateName, "comfort_home_offline") == 0) {
        out = _auto.tick(plant, nowMs);
        strncpy(out.stateName, "comfort_fallback_auto", sizeof(out.stateName) - 1);
      }
    }
  } else if (_mode == WorkMode::Neuro) {
    // Этап 1: Neuro только наблюдает. Железом управляет теневой Auto,
    // чтобы продолжать собирать (state, action) без отдельного «ручного» режима.
    // AI-команды в actuators НЕ идут.
    (void)_neuro.tick(plant, nowMs);
    out = _shadowAuto.tick(plant, nowMs);
    strncpy(out.stateName, "neuro_shadow_auto", sizeof(out.stateName) - 1);
  } else {
    out = _auto.tick(plant, nowMs);
  }

  if (_fan) _fan->setPowerPercent(out.fanPowerPct);
  if (_pump) {
    if (out.requestAfterheat) _pump->requestAfterheat();
    _pump->setOn(out.pumpOn || _pump->afterheatActive(nowMs));
    _pump->tick(nowMs);
  }

  _lastFanPct = out.fanPowerPct;
  strncpy(_lastState, out.stateName, sizeof(_lastState) - 1);
  plant.fanPowerPct = _fan ? _fan->powerPercent() : out.fanPowerPct;
  plant.fanRelayOn = _fan ? _fan->relayOn() : false;
  plant.pumpOn = _pump ? _pump->isOn() : false;
}
