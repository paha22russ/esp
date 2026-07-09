#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

extern int pinModes[40];

void pinEngineInit();
void pinEngineUpdate();
void pinEngineStopPin(uint8_t pin);
bool pinEngineHandleCommand(JsonObject cmd);
