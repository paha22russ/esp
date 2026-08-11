#pragma once

#include <Arduino.h>

enum class WorkMode : uint8_t {
  Auto = 0,
  Comfort = 1,
  Neuro = 2  // наблюдение / гипотезы, без прямого управления (этап 1)
};

inline const char* workModeName(WorkMode m) {
  switch (m) {
    case WorkMode::Auto: return "auto";
    case WorkMode::Comfort: return "comfort";
    case WorkMode::Neuro: return "neuro";
    default: return "unknown";
  }
}

inline const char* workModeNameRu(WorkMode m) {
  switch (m) {
    case WorkMode::Auto: return "Авто";
    case WorkMode::Comfort: return "Комфорт";
    case WorkMode::Neuro: return "Нейро";
    default: return "—";
  }
}

enum class SensorId : uint8_t {
  SupplyPt1000 = 0,   // критический
  FluePt1000,
  ReturnDs,
  BoilerRoomDs,
  OutdoorDs,
  HomeMqtt,           // опционально с ESP01 / MQTT
  Count
};

inline const char* sensorIdName(SensorId id) {
  switch (id) {
    case SensorId::SupplyPt1000: return "supply";
    case SensorId::FluePt1000: return "flue";
    case SensorId::ReturnDs: return "return";
    case SensorId::BoilerRoomDs: return "boiler_room";
    case SensorId::OutdoorDs: return "outdoor";
    case SensorId::HomeMqtt: return "home";
    default: return "unknown";
  }
}

enum class SensorQuality : uint8_t {
  Ok = 0,
  Stale,
  Implausible,
  Missing,
  Recovering
};

struct SensorReading {
  float celsius = NAN;
  SensorQuality quality = SensorQuality::Missing;
  uint32_t updatedAtMs = 0;
  uint8_t errorStreak = 0;
};

struct PlantState {
  SensorReading supply;
  SensorReading flue;
  SensorReading ret;
  SensorReading boilerRoom;
  SensorReading outdoor;
  SensorReading home;

  // dT/dt °C/min (сглаженная оценка)
  float supplyRateCPerMin = 0.0f;
  float flueRateCPerMin = 0.0f;
  float boilerRoomRateCPerMin = 0.0f;

  uint8_t fanPowerPct = 0;     // 0..100 архитектурно
  bool fanRelayOn = false;     // фактическое реле
  bool pumpOn = false;
  bool sensorPowerOn = true;

  WorkMode mode = WorkMode::Auto;
  bool systemEnabled = true;

  float autoMinC = 55.0f;
  float autoMaxC = 70.0f;
  float comfortRoomTargetC = 22.0f;
  float comfortBoilerMinC = 50.0f;
  float comfortBoilerMaxC = 75.0f;

  bool safetyTrip = false;
  char safetyReason[48] = {0};

  uint32_t fuelLoadedAtMs = 0;  // из user event
};

enum class UserEventType : uint8_t {
  FuelAdded = 0,
  BoilerCleaned,
  DoorOpened,
  FuelQuality,
  BoilerObservation,
  FuelFinished,
  Maintenance,
  Ignition,
  Other
};

inline const char* userEventTypeName(UserEventType t) {
  switch (t) {
    case UserEventType::FuelAdded: return "fuel_added";
    case UserEventType::BoilerCleaned: return "boiler_cleaned";
    case UserEventType::DoorOpened: return "door_opened";
    case UserEventType::FuelQuality: return "fuel_quality";
    case UserEventType::BoilerObservation: return "boiler_observation";
    case UserEventType::FuelFinished: return "fuel_finished";
    case UserEventType::Maintenance: return "maintenance";
    case UserEventType::Ignition: return "ignition";
    default: return "other";
  }
}

struct Hypothesis {
  char id[24];
  char text[192];
  char basis[128];
  float confidence;      // 0..1
  bool confirmedByUser;
  uint32_t createdAtUnix;
};
