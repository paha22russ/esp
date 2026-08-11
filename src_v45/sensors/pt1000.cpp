#include "pt1000.h"

void Pt1000Max31865::begin() {
  _okFlue = _flue.begin(MAX31865_2WIRE);
  _okSupply = _supply.begin(MAX31865_3WIRE);

  Serial.printf("[PT1000] flue(2w CS26)=%s supply(3w CS27)=%s\n",
                _okFlue ? "OK" : "FAIL",
                _okSupply ? "OK" : "FAIL");
}

SensorReading Pt1000Max31865::read(Channel ch, uint32_t nowMs) {
  SensorReading r;
  r.updatedAtMs = nowMs;

  Adafruit_MAX31865* dev = (ch == Channel::Flue) ? &_flue : &_supply;
  const bool ready = (ch == Channel::Flue) ? _okFlue : _okSupply;
  if (!ready) {
    r.quality = SensorQuality::Missing;
    return r;
  }

  uint8_t fault = dev->readFault();
  if (fault) {
    dev->clearFault();
    r.quality = SensorQuality::Implausible;
    return r;
  }

  float t = dev->temperature(RNOMINAL, RREF);
  if (isnan(t) || t < -80.0f || t > 500.0f) {
    r.quality = SensorQuality::Implausible;
    return r;
  }

  r.celsius = t;
  r.quality = SensorQuality::Ok;
  return r;
}
