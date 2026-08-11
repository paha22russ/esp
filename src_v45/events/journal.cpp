#include "journal.h"
#include <string.h>

void EventJournal::clear() {
  _head = 0;
  _count = 0;
}

void EventJournal::add(const char* code, const char* detail, uint32_t unixTs) {
  Entry e{};
  e.ms = millis();
  e.unixTs = unixTs;
  strncpy(e.code, code ? code : "", sizeof(e.code) - 1);
  strncpy(e.detail, detail ? detail : "", sizeof(e.detail) - 1);
  _buf[_head] = e;
  _head = (_head + 1) % CAP;
  if (_count < CAP) _count++;
}

const EventJournal::Entry& EventJournal::at(size_t i) const {
  // i=0 newest
  const size_t idx = (_head + CAP - 1 - (i % CAP)) % CAP;
  return _buf[idx];
}
