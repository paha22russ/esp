#pragma once

#include <Arduino.h>
#include "../app/types.h"

// Локальный кольцевой журнал (recovery, safety, user, mode changes)
class EventJournal {
 public:
  static constexpr size_t CAP = 40;
  struct Entry {
    uint32_t ms;
    uint32_t unixTs;
    char code[28];
    char detail[96];
  };

  void clear();
  void add(const char* code, const char* detail, uint32_t unixTs = 0);
  size_t size() const { return _count; }
  const Entry& at(size_t i) const;  // 0 = newest

 private:
  Entry _buf[CAP];
  size_t _head = 0;
  size_t _count = 0;
};
