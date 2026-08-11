#pragma once

#include "../app/types.h"

struct ControlOutput {
  uint8_t fanPowerPct = 0;  // 0..100
  bool pumpOn = false;
  bool requestAfterheat = false;
  char stateName[32] = "idle";
};

class IModeController {
 public:
  virtual ~IModeController() = default;
  virtual void reset() = 0;
  virtual ControlOutput tick(const PlantState& plant, uint32_t nowMs) = 0;
  virtual const char* name() const = 0;
};

// АВТО: регулирование по PT1000 подачи, MIN/MAX, аварии снаружи
class AutoModeController : public IModeController {
 public:
  void reset() override;
  ControlOutput tick(const PlantState& plant, uint32_t nowMs) override;
  const char* name() const override { return "auto"; }
 private:
  enum class St : uint8_t { Idle, Heat, Hold, CoolWait };
  St _st = St::Idle;
};

// КОМФОРТ: цель — комната/котельная, с учётом инерции и дымохода
class ComfortModeController : public IModeController {
 public:
  void reset() override;
  ControlOutput tick(const PlantState& plant, uint32_t nowMs) override;
  const char* name() const override { return "comfort"; }
 private:
  enum class St : uint8_t {
    Wait, Heat, Coast, Maintain, HoldMax
  };
  St _st = St::Wait;
  uint32_t _fuelEpochMs = 0;
};

// НЕЙРО (этап 1): только наблюдение, управление не выдаёт (fan=0 от режима;
// фактическое управление остаётся у Auto/Comfort либо ручное — orchestrator
// в neuro не применяет выход режима к актуаторам, только пишет гипотезы).
class NeuroModeController : public IModeController {
 public:
  void reset() override;
  ControlOutput tick(const PlantState& plant, uint32_t nowMs) override;
  const char* name() const override { return "neuro"; }
  // На этапе 1 Neuro работает поверх «теневого» Auto для сбора пар (state→action),
  // но НЕ командует железом. Orchestrator игнорирует fanPowerPct из Neuro.
};
