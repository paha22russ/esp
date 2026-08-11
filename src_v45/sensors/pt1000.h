#pragma once

#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include "../config/pins.h"
#include "../app/types.h"

// PT1000 через MAX31865.
// #1 дымоход: 2-wire, CS=GPIO26
// #2 подача:  3-wire, CS=GPIO27
// SPI (soft): SCK=32, MOSI=33, MISO=34
class Pt1000Max31865 {
 public:
  enum class Channel : uint8_t { Flue = 0, Supply = 1 };

  Pt1000Max31865()
      : _flue(PIN_MAX31865_CS_FLUE, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK),
        _supply(PIN_MAX31865_CS_SUPPLY, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK) {}

  void begin();
  SensorReading read(Channel ch, uint32_t nowMs);

 private:
  Adafruit_MAX31865 _flue;
  Adafruit_MAX31865 _supply;
  bool _okFlue = false;
  bool _okSupply = false;

  // Rref типичный для модулей; уточняется калибровкой на стенде
  static constexpr float RREF = 4300.0f;
  static constexpr float RNOMINAL = 1000.0f;
};
