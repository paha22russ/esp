#include "sensor_hub.h"

void SensorHub::begin(SensorPowerRelay* power) {
  _pt.begin();
  _ds.begin(power);
}

void SensorHub::tick(uint32_t nowMs) {
  _ds.tick(nowMs);
}

float SensorHub::_rate(float cur, float prev, uint32_t dtMs) {
  if (isnan(cur) || isnan(prev) || dtMs < 500) return 0.0f;
  const float dtMin = dtMs / 60000.0f;
  if (dtMin <= 0.0f) return 0.0f;
  return (cur - prev) / dtMin;
}

void SensorHub::updatePlant(PlantState& plant) {
  const uint32_t now = millis();
  plant.supply = _pt.read(Pt1000Max31865::Channel::Supply, now);
  plant.flue = _pt.read(Pt1000Max31865::Channel::Flue, now);
  plant.ret = _ds.returnTemp();
  plant.boilerRoom = _ds.boilerRoomTemp();
  plant.outdoor = _ds.outdoorTemp();
  // home остаётся от MQTT / внешнего источника

  if (_prevMs != 0) {
    const uint32_t dt = now - _prevMs;
    if (plant.supply.quality == SensorQuality::Ok)
      plant.supplyRateCPerMin = _rate(plant.supply.celsius, _prevSupply, dt);
    if (plant.flue.quality == SensorQuality::Ok)
      plant.flueRateCPerMin = _rate(plant.flue.celsius, _prevFlue, dt);
    if (plant.boilerRoom.quality == SensorQuality::Ok)
      plant.boilerRoomRateCPerMin = _rate(plant.boilerRoom.celsius, _prevRoom, dt);
  }

  if (plant.supply.quality == SensorQuality::Ok) _prevSupply = plant.supply.celsius;
  if (plant.flue.quality == SensorQuality::Ok) _prevFlue = plant.flue.celsius;
  if (plant.boilerRoom.quality == SensorQuality::Ok) _prevRoom = plant.boilerRoom.celsius;
  _prevMs = now;
}
