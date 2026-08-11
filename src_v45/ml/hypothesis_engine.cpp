#include "hypothesis_engine.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

void HypothesisEngine::begin(EventJournal* journal) {
  _journal = journal;
  _n = 0;
}

void HypothesisEngine::_add(const char* id, const char* text, const char* basis, float conf, uint32_t unixTs) {
  for (size_t i = 0; i < _n; i++) {
    if (strncmp(_items[i].id, id, sizeof(_items[i].id)) == 0) return;  // уже есть
  }
  if (_n >= CAP) return;
  Hypothesis& h = _items[_n++];
  memset(&h, 0, sizeof(h));
  strncpy(h.id, id, sizeof(h.id) - 1);
  strncpy(h.text, text, sizeof(h.text) - 1);
  strncpy(h.basis, basis, sizeof(h.basis) - 1);
  h.confidence = conf;
  h.confirmedByUser = false;
  h.createdAtUnix = unixTs;
  if (_journal) _journal->add("hypothesis", id, unixTs);
}

void HypothesisEngine::observe(const PlantState& plant, uint8_t /*fanPct*/, uint32_t nowMs) {
  if (plant.fuelLoadedAtMs != 0 && !_watchingFuelInertia) {
    _watchingFuelInertia = true;
    _fuelWatchStart = plant.fuelLoadedAtMs;
    _peakSupplyAfterFuel = (plant.supply.quality == SensorQuality::Ok) ? plant.supply.celsius : NAN;
  }

  if (_watchingFuelInertia && plant.supply.quality == SensorQuality::Ok) {
    if (isnan(_peakSupplyAfterFuel) || plant.supply.celsius > _peakSupplyAfterFuel)
      _peakSupplyAfterFuel = plant.supply.celsius;

    const uint32_t elapsed = nowMs - _fuelWatchStart;
    if (elapsed > 20UL * 60UL * 1000UL) {
      // грубая гипотеза инерции после загрузки
      char text[192];
      snprintf(text, sizeof(text),
               "После загрузки топлива рост подачи наблюдался в окне ~20 мин (пик ~%.1fC).",
               _peakSupplyAfterFuel);
      _add("fuel_inertia_20m", text, "telemetry+fuel_added", 0.35f, 0);
      _watchingFuelInertia = false;
    }
  }
}
