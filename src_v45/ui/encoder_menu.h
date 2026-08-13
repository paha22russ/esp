#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include "../app/types.h"
#include "../config/pins.h"

// Полноценное OLED-меню на энкодере (не только статус).
class EncoderMenu {
 public:
  enum class Screen : uint8_t {
    Status = 0,
    Mode,
    Setpoints,
    Actuators,
    Sensors,
    JournalHint,
    Count
  };

  void begin(U8G2* oled);
  void tick(uint32_t nowMs, PlantState& plant, const char* stateName);
  Screen screen() const { return _screen; }

  // Вызывается orchestrator/API при смене режима с меню
  using ModeCb = void (*)(WorkMode);
  using EnableCb = void (*)(bool);
  void setCallbacks(ModeCb onMode, EnableCb onEnable) {
    _onMode = onMode;
    _onEnable = onEnable;
  }

 private:
  U8G2* _oled = nullptr;
  Screen _screen = Screen::Status;
  int _cursor = 0;
  int _lastPos = 0;
  bool _lastBtn = false;
  uint32_t _lastBtnMs = 0;
  uint32_t _lastDrawMs = 0;
  ModeCb _onMode = nullptr;
  EnableCb _onEnable = nullptr;

  void _pollEncoder(PlantState& plant);
  void _draw(const PlantState& plant, const char* stateName);
  void _drawStatus(const PlantState& plant, const char* stateName);
  void _drawMode(const PlantState& plant);
  void _drawSetpoints(const PlantState& plant);
  void _drawActuators(const PlantState& plant);
  void _drawSensors(const PlantState& plant);
};
