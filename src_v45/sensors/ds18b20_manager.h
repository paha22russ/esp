#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../config/pins.h"
#include "../config/defaults.h"
#include "../app/types.h"
#include "../hal/actuators.h"

enum class OneWireBusId : uint8_t { Bus1Return = 1, Bus2RoomOutdoor = 2 };

struct DsBusHealth {
  uint8_t consecutiveBusErrors = 0;
  uint32_t lastRecoveryMs = 0;
  uint32_t recoveryCount = 0;
  bool recovering = false;
};

// Шина 1: только обратка
// Шина 2: котельная + улица
// Recovery через реле GPIO25 (полное снятие питания), не единичный выброс.
class Ds18b20Manager {
 public:
  Ds18b20Manager();
  void begin(SensorPowerRelay* power);
  void tick(uint32_t nowMs);

  SensorReading returnTemp() const { return _return; }
  SensorReading boilerRoomTemp() const { return _boilerRoom; }
  SensorReading outdoorTemp() const { return _outdoor; }

  DsBusHealth bus1Health() const { return _h1; }
  DsBusHealth bus2Health() const { return _h2; }

  // Адреса (ROM) задаются из EEPROM/UI; пока — по индексу на шине
  void setMapping(const DeviceAddress& ret,
                  const DeviceAddress& boilerRoom,
                  const DeviceAddress& outdoor);
  bool hasMapping() const { return _mapped; }

  // колбэк журнала
  using LogFn = void (*)(const char* code, const char* detail);
  void setLogger(LogFn fn) { _log = fn; }

 private:
  OneWire _ow1;
  OneWire _ow2;
  DallasTemperature _bus1;
  DallasTemperature _bus2;
  SensorPowerRelay* _power = nullptr;

  SensorReading _return;
  SensorReading _boilerRoom;
  SensorReading _outdoor;
  float _lastReturn = NAN;
  float _lastBoiler = NAN;
  float _lastOutdoor = NAN;

  DeviceAddress _addrReturn = {0};
  DeviceAddress _addrBoiler = {0};
  DeviceAddress _addrOutdoor = {0};
  bool _mapped = false;

  DsBusHealth _h1;
  DsBusHealth _h2;

  enum class RecPhase : uint8_t { Idle, PowerOff, WaitReinit, Done };
  RecPhase _recPhase = RecPhase::Idle;
  OneWireBusId _recBus = OneWireBusId::Bus1Return;
  uint32_t _recNextMs = 0;

  LogFn _log = nullptr;

  void _requestTemps();
  SensorReading _readAddr(DallasTemperature& bus, const DeviceAddress& addr,
                          float& lastValid, uint32_t nowMs);
  void _noteBusResult(DsBusHealth& h, OneWireBusId id, bool ok, uint32_t nowMs);
  void _startRecovery(OneWireBusId id, uint32_t nowMs);
  void _tickRecovery(uint32_t nowMs);
  void _reinitBuses();
  void _logMsg(const char* code, const char* detail);
};
