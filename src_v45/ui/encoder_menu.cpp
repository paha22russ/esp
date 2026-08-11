#include "encoder_menu.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

void EncoderMenu::begin(U8G2* oled) {
  _oled = oled;
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
  _lastPos = (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);
  _lastBtn = digitalRead(PIN_ENCODER_SW) == LOW;
}

void EncoderMenu::tick(uint32_t nowMs, PlantState& plant, const char* stateName) {
  _pollEncoder(plant);
  if (_lastDrawMs == 0 || (nowMs - _lastDrawMs) >= 200) {
    _lastDrawMs = nowMs;
    _draw(plant, stateName);
  }
  (void)_lastBtnMs;
}

void EncoderMenu::_pollEncoder(PlantState& plant) {
  const int clk = digitalRead(PIN_ENCODER_CLK);
  const int dt = digitalRead(PIN_ENCODER_DT);
  const int st = (clk << 1) | dt;
  if (st != _lastPos) {
    // простой шаг по фронту CLK
    if (clk == LOW && (_lastPos & 0b10)) {
      const int dir = (dt == HIGH) ? 1 : -1;
      if (_screen == Screen::Status) {
        // на статусе вращение ничего не меняет
      } else if (_screen == Screen::Mode) {
        int m = (int)plant.mode + dir;
        if (m < 0) m = 2;
        if (m > 2) m = 0;
        plant.mode = (WorkMode)m;
        if (_onMode) _onMode(plant.mode);
      } else if (_screen == Screen::Setpoints) {
        if (_cursor == 0) plant.autoMinC = constrain(plant.autoMinC + dir, 40, 80);
        else if (_cursor == 1) plant.autoMaxC = constrain(plant.autoMaxC + dir, 45, 90);
        else plant.comfortRoomTargetC = constrain(plant.comfortRoomTargetC + dir * 0.5f, 10, 30);
      } else if (_screen == Screen::Actuators) {
        if (_cursor == 0) {
          plant.systemEnabled = !plant.systemEnabled;
          if (_onEnable) _onEnable(plant.systemEnabled);
        }
      }
    }
    _lastPos = st;
  }

  const bool btn = digitalRead(PIN_ENCODER_SW) == LOW;
  const uint32_t now = millis();
  if (btn && !_lastBtn && (now - _lastBtnMs) > 250) {
    _lastBtnMs = now;
    // короткое нажатие — следующий экран / пункт
    if (_screen == Screen::Setpoints || _screen == Screen::Actuators) {
      _cursor = (_cursor + 1) % 3;
      if (_screen == Screen::Actuators) _cursor %= 1;
    } else {
      _screen = (Screen)(((uint8_t)_screen + 1) % (uint8_t)Screen::Count);
      _cursor = 0;
    }
  }
  // длинное нажатие — домой (Status)
  static uint32_t btnDown = 0;
  if (btn && !_lastBtn) btnDown = now;
  if (!btn && _lastBtn && btnDown && (now - btnDown) > 800) {
    _screen = Screen::Status;
    _cursor = 0;
  }
  if (!btn) btnDown = 0;
  _lastBtn = btn;
}

void EncoderMenu::_draw(const PlantState& plant, const char* stateName) {
  if (!_oled) return;
  _oled->clearBuffer();
  _oled->setFont(u8g2_font_6x12_tf);
  switch (_screen) {
    case Screen::Status: _drawStatus(plant, stateName); break;
    case Screen::Mode: _drawMode(plant); break;
    case Screen::Setpoints: _drawSetpoints(plant); break;
    case Screen::Actuators: _drawActuators(plant); break;
    case Screen::Sensors: _drawSensors(plant); break;
    case Screen::JournalHint:
      _oled->drawStr(0, 12, "4.5-beta MENU");
      _oled->drawStr(0, 28, "Rotate=edit");
      _oled->drawStr(0, 42, "Click=next");
      _oled->drawStr(0, 56, "Long=status");
      break;
    default: break;
  }
  _oled->sendBuffer();
}

void EncoderMenu::_drawStatus(const PlantState& plant, const char* stateName) {
  _oled->drawStr(0, 10, "Boiler 4.5-beta");
  char line[40];
  snprintf(line, sizeof(line), "%s %s", workModeNameRu(plant.mode),
           plant.systemEnabled ? "ON" : "OFF");
  _oled->drawStr(0, 24, line);
  char homeBuf[8] = "--";
  if (plant.home.quality == SensorQuality::Ok) snprintf(homeBuf, sizeof(homeBuf), "%.1f", plant.home.celsius);
  if (plant.supply.quality == SensorQuality::Ok)
    snprintf(line, sizeof(line), "S:%.1f H:%s", plant.supply.celsius, homeBuf);
  else
    snprintf(line, sizeof(line), "S:-- H:%s", homeBuf);
  _oled->drawStr(0, 38, line);
  snprintf(line, sizeof(line), "Fan:%u%% Pump:%s", (unsigned)plant.fanPowerPct,
           plant.pumpOn ? "ON" : "OFF");
  _oled->drawStr(0, 52, line);
  _oled->drawStr(0, 64, plant.safetyTrip ? "SAFETY!" : (stateName ? stateName : ""));
}

void EncoderMenu::_drawMode(const PlantState& plant) {
  _oled->drawStr(0, 12, "MODE (rotate)");
  _oled->drawStr(0, 30, workModeNameRu(plant.mode));
  if (plant.mode == WorkMode::Comfort && plant.home.quality != SensorQuality::Ok) {
    _oled->drawStr(0, 48, "Home MQTT offline");
    _oled->drawStr(0, 62, "-> fallback AUTO");
  } else if (plant.mode == WorkMode::Neuro) {
    _oled->drawStr(0, 48, "Observe only");
  }
}

void EncoderMenu::_drawSetpoints(const PlantState& plant) {
  _oled->drawStr(0, 10, "SETPOINTS");
  char line[40];
  snprintf(line, sizeof(line), "%c AutoMin %.0f", _cursor == 0 ? '>' : ' ', plant.autoMinC);
  _oled->drawStr(0, 26, line);
  snprintf(line, sizeof(line), "%c AutoMax %.0f", _cursor == 1 ? '>' : ' ', plant.autoMaxC);
  _oled->drawStr(0, 40, line);
  snprintf(line, sizeof(line), "%c Home   %.1f", _cursor == 2 ? '>' : ' ', plant.comfortRoomTargetC);
  _oled->drawStr(0, 54, line);
}

void EncoderMenu::_drawActuators(const PlantState& plant) {
  _oled->drawStr(0, 12, "SYSTEM");
  char line[40];
  snprintf(line, sizeof(line), "> Enabled: %s", plant.systemEnabled ? "YES" : "NO");
  _oled->drawStr(0, 32, line);
  snprintf(line, sizeof(line), "Fan %u%%  Pump %s", (unsigned)plant.fanPowerPct,
           plant.pumpOn ? "ON" : "OFF");
  _oled->drawStr(0, 50, line);
}

void EncoderMenu::_drawSensors(const PlantState& plant) {
  _oled->drawStr(0, 10, "SENSORS");
  char line[42];
  auto one = [&](int y, const char* n, const SensorReading& r) {
    if (r.quality == SensorQuality::Ok) snprintf(line, sizeof(line), "%s %.1f", n, r.celsius);
    else snprintf(line, sizeof(line), "%s --", n);
    _oled->drawStr(0, y, line);
  };
  one(24, "Sup", plant.supply);
  one(36, "Flue", plant.flue);
  one(48, "Ret", plant.ret);
  one(60, "Home", plant.home);
}
