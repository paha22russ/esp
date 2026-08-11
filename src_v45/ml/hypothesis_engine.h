#pragma once

#include "../app/types.h"
#include "../events/journal.h"

// Этап 1: локальные черновые гипотезы (без управления).
class HypothesisEngine {
 public:
  void begin(EventJournal* journal);
  void observe(const PlantState& plant, uint8_t fanPct, uint32_t nowMs);
  size_t count() const { return _n; }
  const Hypothesis& at(size_t i) const { return _items[i]; }

 private:
  static constexpr size_t CAP = 8;
  Hypothesis _items[CAP];
  size_t _n = 0;
  EventJournal* _journal = nullptr;

  float _peakSupplyAfterFuel = NAN;
  uint32_t _fuelWatchStart = 0;
  bool _watchingFuelInertia = false;

  void _add(const char* id, const char* text, const char* basis, float conf, uint32_t unixTs);
};
