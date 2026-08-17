#include "ds18b20_manager.h"
#include <string.h>

Ds18b20Manager::Ds18b20Manager()
    : _ow1(PIN_ONEWIRE_BUS1),
      _ow2(PIN_ONEWIRE_BUS2),
      _bus1(&_ow1),
      _bus2(&_ow2) {}

void Ds18b20Manager::begin(SensorPowerRelay* power) {
  _power = power;
  _reinitBuses();
}

void Ds18b20Manager::setMapping(const DeviceAddress& ret,
                                const DeviceAddress& boilerRoom,
                                const DeviceAddress& outdoor) {
  memcpy(_addrReturn, ret, 8);
  memcpy(_addrBoiler, boilerRoom, 8);
  memcpy(_addrOutdoor, outdoor, 8);
  _mapped = true;
}

void Ds18b20Manager::_logMsg(const char* code, const char* detail) {
  if (_log) _log(code, detail);
  Serial.printf("[DS] %s %s\n", code, detail ? detail : "");
}

void Ds18b20Manager::_reinitBuses() {
  _bus1.begin();
  _bus2.begin();
  _bus1.setWaitForConversion(false);
  _bus2.setWaitForConversion(false);
  _bus1.setResolution(12);
  _bus2.setResolution(12);

  // Если mapping нет — берём первые устройства на шинах
  if (!_mapped) {
    if (_bus1.getAddress(_addrReturn, 0)) {
      // ok
    }
    if (_bus2.getDeviceCount() >= 1) _bus2.getAddress(_addrBoiler, 0);
    if (_bus2.getDeviceCount() >= 2) _bus2.getAddress(_addrOutdoor, 1);
  }

  Serial.printf("[DS] reinit bus1=%u bus2=%u\n",
                _bus1.getDeviceCount(), _bus2.getDeviceCount());
}

void Ds18b20Manager::_requestTemps() {
  _bus1.requestTemperatures();
  _bus2.requestTemperatures();
}

static bool isImplausibleDs(float t, float last) {
  if (isnan(t)) return true;
  if (t <= -55.0f || t >= cfg::DS_IMPLAUSIBLE_HIGH_C) return true;
  if (!isnan(last) && fabsf(t - last) > cfg::DS_MAX_JUMP_C) return true;
  return false;
}

SensorReading Ds18b20Manager::_readAddr(DallasTemperature& bus,
                                        const DeviceAddress& addr,
                                        float& lastValid,
                                        uint32_t nowMs) {
  SensorReading r;
  r.updatedAtMs = nowMs;
  if (addr[0] == 0) {
    r.quality = SensorQuality::Missing;
    return r;
  }

  float t = bus.getTempC(addr);
  if (t == DEVICE_DISCONNECTED_C || isImplausibleDs(t, lastValid)) {
    r.quality = SensorQuality::Implausible;
    r.celsius = NAN;
    return r;
  }

  r.celsius = t;
  r.quality = SensorQuality::Ok;
  lastValid = t;
  return r;
}

void Ds18b20Manager::_noteBusResult(DsBusHealth& h, OneWireBusId id, bool ok, uint32_t nowMs) {
  if (ok) {
    h.consecutiveBusErrors = 0;
    return;
  }
  h.consecutiveBusErrors++;
  if (h.consecutiveBusErrors >= cfg::DS_ERROR_STREAK_BEFORE_RECOVERY &&
      !h.recovering &&
      (h.lastRecoveryMs == 0 || (nowMs - h.lastRecoveryMs) > cfg::DS_RECOVERY_COOLDOWN_MS) &&
      _recPhase == RecPhase::Idle) {
    _startRecovery(id, nowMs);
  }
}

void Ds18b20Manager::_startRecovery(OneWireBusId id, uint32_t nowMs) {
  _recBus = id;
  _recPhase = RecPhase::PowerOff;
  _recNextMs = nowMs;
  if (id == OneWireBusId::Bus1Return) _h1.recovering = true;
  else _h2.recovering = true;

  char detail[64];
  snprintf(detail, sizeof(detail), "bus=%u streak recovery start", (unsigned)id);
  _logMsg("ds_recovery_start", detail);

  if (_power) _power->startPowerCycle(cfg::DS_RECOVERY_POWER_OFF_MS);
}

void Ds18b20Manager::_tickRecovery(uint32_t nowMs) {
  if (_recPhase == RecPhase::Idle) return;

  if (_recPhase == RecPhase::PowerOff) {
    if (_power && _power->cycling()) {
      _power->tick(nowMs);
      return;
    }
    // питание вернулось
    _recPhase = RecPhase::WaitReinit;
    _recNextMs = nowMs + 500;
    return;
  }

  if (_recPhase == RecPhase::WaitReinit) {
    if ((int32_t)(nowMs - _recNextMs) < 0) return;
    _reinitBuses();
    _requestTemps();
    _recPhase = RecPhase::Done;
  }

  if (_recPhase == RecPhase::Done) {
    DsBusHealth& h = (_recBus == OneWireBusId::Bus1Return) ? _h1 : _h2;
    h.recovering = false;
    h.consecutiveBusErrors = 0;
    h.lastRecoveryMs = nowMs;
    h.recoveryCount++;
    _logMsg("ds_recovery_done",
            _recBus == OneWireBusId::Bus1Return ? "bus1" : "bus2");
    _recPhase = RecPhase::Idle;
  }
}

void Ds18b20Manager::tick(uint32_t nowMs) {
  _tickRecovery(nowMs);
  if (_recPhase != RecPhase::Idle) {
    _return.quality = SensorQuality::Recovering;
    _boilerRoom.quality = SensorQuality::Recovering;
    _outdoor.quality = SensorQuality::Recovering;
    return;
  }

  if (_power && !_power->isPowered()) {
    _return.quality = SensorQuality::Recovering;
    return;
  }

  static uint32_t lastReq = 0;
  static uint32_t lastRead = 0;
  if (lastReq == 0 || (nowMs - lastReq) > 1000) {
    _requestTemps();
    lastReq = nowMs;
    lastRead = nowMs + 750;  // время конверсии 12-bit
  }
  if ((int32_t)(nowMs - lastRead) < 0) return;

  _return = _readAddr(_bus1, _addrReturn, _lastReturn, nowMs);
  _boilerRoom = _readAddr(_bus2, _addrBoiler, _lastBoiler, nowMs);
  _outdoor = _readAddr(_bus2, _addrOutdoor, _lastOutdoor, nowMs);

  const bool bus1Ok = (_return.quality == SensorQuality::Ok) || (_addrReturn[0] == 0);
  const bool bus2Ok =
      ((_boilerRoom.quality == SensorQuality::Ok) || (_addrBoiler[0] == 0)) &&
      ((_outdoor.quality == SensorQuality::Ok) || (_addrOutdoor[0] == 0));

  // Ошибка шины: если адрес задан, но чтение плохое
  _noteBusResult(_h1, OneWireBusId::Bus1Return,
                 _addrReturn[0] == 0 || _return.quality == SensorQuality::Ok, nowMs);
  _noteBusResult(_h2, OneWireBusId::Bus2RoomOutdoor,
                 (_addrBoiler[0] == 0 || _boilerRoom.quality == SensorQuality::Ok) &&
                     (_addrOutdoor[0] == 0 || _outdoor.quality == SensorQuality::Ok),
                 nowMs);

  (void)bus1Ok;
  (void)bus2Ok;
}
