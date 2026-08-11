#pragma once

#include <Arduino.h>

// Проверка обновлений только внутри канала 4.5-beta.
class OtaChannelV45 {
 public:
  void begin();
  // Неблокирующая проверка (результат в last*)
  bool check(String& latestOut, String& errOut);
  const char* channel() const;

 private:
};
