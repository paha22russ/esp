#pragma once

#include "../app/types.h"
#include "journal.h"
#include "../telemetry/vps_client.h"

// Классификация простых RU-фраз → UserEventType (локальный эвристический слой).
// Полный NLP/ASR — на VPS. Сюда можно принимать уже готовый текст от бота.
class UserEventService {
 public:
  void begin(EventJournal* journal, VpsClient* vps, PlantState* plant);
  UserEventType classify(const char* text) const;
  void ingestText(const char* text, uint32_t unixTs, const char* audioUrl = nullptr);

 private:
  EventJournal* _journal = nullptr;
  VpsClient* _vps = nullptr;
  PlantState* _plant = nullptr;
};
