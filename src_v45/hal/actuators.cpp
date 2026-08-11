#include "actuators.h"

void FanActuator::begin() {
  pinMode(PIN_RELAY_FAN, OUTPUT);
  digitalWrite(PIN_RELAY_FAN, LOW);
  _pct = 0;
  _relayOn = false;
}

bool FanActuator::_applyHardware() {
  // Сейчас только ON/OFF. Позже: карта pct → таймер/диммер/SSR.
  const bool wantOn = _pct >= cfg::FAN_ON_THRESHOLD_PCT;
  if (wantOn != _relayOn) {
    digitalWrite(PIN_RELAY_FAN, wantOn ? HIGH : LOW);
    _relayOn = wantOn;
  }
  return true;
}

void FanActuator::setPowerPercent(uint8_t pct, bool /*force*/) {
  if (pct > 100) pct = 100;
  _pct = pct;
  _applyHardware();
}

void FanActuator::emergencyOff() {
  _pct = 0;
  digitalWrite(PIN_RELAY_FAN, LOW);
  _relayOn = false;
}

void PumpActuator::begin() {
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  digitalWrite(PIN_RELAY_PUMP, LOW);
  _on = false;
  _afterheatUntilMs = 0;
}

void PumpActuator::setOn(bool on) {
  if (on == _on) return;
  _on = on;
  digitalWrite(PIN_RELAY_PUMP, on ? HIGH : LOW);
}

void PumpActuator::requestAfterheat(uint32_t durationMs) {
  const uint32_t now = millis();
  const uint32_t until = now + durationMs;
  if (until > _afterheatUntilMs) _afterheatUntilMs = until;
  setOn(true);
}

void PumpActuator::tick(uint32_t nowMs) {
  if (_afterheatUntilMs != 0 && (int32_t)(nowMs - _afterheatUntilMs) >= 0) {
    _afterheatUntilMs = 0;
    // afterheat закончился — контроллер решает, держать ли насос дальше
  }
}

bool PumpActuator::afterheatActive(uint32_t nowMs) const {
  return _afterheatUntilMs != 0 && (int32_t)(nowMs - _afterheatUntilMs) < 0;
}

void SensorPowerRelay::begin() {
  pinMode(PIN_RELAY_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_RELAY_SENSOR_PWR, HIGH);  // питание датчиков подано
  _powered = true;
  _cycling = false;
}

void SensorPowerRelay::setPowered(bool on) {
  _powered = on;
  digitalWrite(PIN_RELAY_SENSOR_PWR, on ? HIGH : LOW);
}

void SensorPowerRelay::startPowerCycle(uint32_t offMs) {
  if (_cycling) return;
  _cycling = true;
  _offMs = offMs;
  _offUntilMs = millis() + offMs;
  setPowered(false);
}

bool SensorPowerRelay::tick(uint32_t nowMs) {
  if (!_cycling) return false;
  if ((int32_t)(nowMs - _offUntilMs) >= 0) {
    setPowered(true);
    _cycling = false;
    return true;  // цикл завершён — питание снова включено
  }
  return false;
}
