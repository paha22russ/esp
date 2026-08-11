#include "user_events.h"
#include <ctype.h>
#include <string.h>

void UserEventService::begin(EventJournal* journal, VpsClient* vps, PlantState* plant) {
  _journal = journal;
  _vps = vps;
  _plant = plant;
}

static void tolower_utf8_approx(char* s) {
  for (; *s; ++s) {
    if (*s >= 'A' && *s <= 'Z') *s = *s - 'A' + 'a';
  }
}

UserEventType UserEventService::classify(const char* text) const {
  if (!text) return UserEventType::Other;
  char buf[192];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  tolower_utf8_approx(buf);

  // Простые эвристики по подстрокам (кириллица в UTF-8 — ищем байтовые якоря из примеров)
  auto has = [&](const char* a) { return strstr(buf, a) != nullptr; };

  if (has("подкин") || has("загрузил") || has("ведра") || has("уголь добав") || has("подбросил"))
    return UserEventType::FuelAdded;
  if (has("почистил") || has("чистк"))
    return UserEventType::BoilerCleaned;
  if (has("дверц") || has("открыл двер"))
    return UserEventType::DoorOpened;
  if (has("плохой") || has("сырой") || has("качество"))
    return UserEventType::FuelQuality;
  if (has("тухн") || has("гасн") || has("прогорел") || has("закончил"))
    return UserEventType::FuelFinished;
  if (has("розжиг") || has("разжёг") || has("разжег") || has("запустил кот"))
    return UserEventType::Ignition;
  if (has("обслужив") || has("ремонт"))
    return UserEventType::Maintenance;
  if (has("котёл") || has("котел") || has("замеча"))
    return UserEventType::BoilerObservation;
  return UserEventType::Other;
}

void UserEventService::ingestText(const char* text, uint32_t unixTs, const char* audioUrl) {
  const UserEventType t = classify(text);
  if (_journal) _journal->add(userEventTypeName(t), text, unixTs);
  if (_plant && t == UserEventType::FuelAdded) {
    _plant->fuelLoadedAtMs = millis();
  }
  if (_vps) _vps->enqueueUserEvent(t, text, audioUrl, unixTs, _plant);
}
