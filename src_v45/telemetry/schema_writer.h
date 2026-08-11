#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../app/types.h"
#include "../config/version.h"

// Сериализация телеметрии / событий по schema v1 (см. schemas/v45/)
namespace telemetry {

inline void fillSample(JsonObject obj, const PlantState& p, const char* stateName, uint32_t unixTs) {
  obj["schema"] = PROTOCOL_SCHEMA_VERSION;
  obj["fw"] = FIRMWARE_VERSION;
  obj["channel"] = FIRMWARE_CHANNEL;
  obj["ts"] = unixTs;
  obj["mode"] = workModeName(p.mode);
  obj["state"] = stateName ? stateName : "";
  obj["system_enabled"] = p.systemEnabled;

  JsonObject temps = obj.createNestedObject("temps");
  if (p.supply.quality == SensorQuality::Ok) temps["supply"] = p.supply.celsius; else temps["supply"] = nullptr;
  if (p.flue.quality == SensorQuality::Ok) temps["flue"] = p.flue.celsius; else temps["flue"] = nullptr;
  if (p.ret.quality == SensorQuality::Ok) temps["return"] = p.ret.celsius; else temps["return"] = nullptr;
  if (p.boilerRoom.quality == SensorQuality::Ok) temps["boiler_room"] = p.boilerRoom.celsius; else temps["boiler_room"] = nullptr;
  if (p.outdoor.quality == SensorQuality::Ok) temps["outdoor"] = p.outdoor.celsius; else temps["outdoor"] = nullptr;
  if (p.home.quality == SensorQuality::Ok) temps["home"] = p.home.celsius; else temps["home"] = nullptr;

  JsonObject rates = obj.createNestedObject("rates_c_per_min");
  rates["supply"] = p.supplyRateCPerMin;
  rates["flue"] = p.flueRateCPerMin;
  rates["boiler_room"] = p.boilerRoomRateCPerMin;

  JsonObject act = obj.createNestedObject("actuators");
  act["fan_power_pct"] = p.fanPowerPct;
  act["fan_relay"] = p.fanRelayOn;
  act["pump"] = p.pumpOn;
  act["sensor_power"] = p.sensorPowerOn;

  JsonObject sp = obj.createNestedObject("setpoints");
  sp["auto_min"] = p.autoMinC;
  sp["auto_max"] = p.autoMaxC;
  sp["comfort_room"] = p.comfortRoomTargetC;
  sp["comfort_boiler_min"] = p.comfortBoilerMinC;
  sp["comfort_boiler_max"] = p.comfortBoilerMaxC;

  JsonObject q = obj.createNestedObject("sensor_quality");
  auto qn = [](SensorQuality sq) -> const char* {
    switch (sq) {
      case SensorQuality::Ok: return "ok";
      case SensorQuality::Stale: return "stale";
      case SensorQuality::Implausible: return "implausible";
      case SensorQuality::Recovering: return "recovering";
      default: return "missing";
    }
  };
  q["supply"] = qn(p.supply.quality);
  q["flue"] = qn(p.flue.quality);
  q["return"] = qn(p.ret.quality);
  q["boiler_room"] = qn(p.boilerRoom.quality);
  q["outdoor"] = qn(p.outdoor.quality);

  obj["safety_trip"] = p.safetyTrip;
  obj["safety_reason"] = p.safetyReason;
  obj["fuel_loaded_at_ms"] = p.fuelLoadedAtMs;
}

}  // namespace telemetry
