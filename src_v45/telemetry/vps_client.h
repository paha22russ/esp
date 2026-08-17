#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../app/types.h"
#include "schema_writer.h"

// Отправка телеметрии и событий на VPS. При отсутствии сети — локальный ring-buffer.
class VpsClient {
 public:
  void begin();
  void setBaseUrl(const String& url) { _base = url; }
  void setDeviceId(const String& id) { _deviceId = id; }
  void setAuthToken(const String& token) { _token = token; }

  void tick(uint32_t nowMs);
  bool enqueueTelemetry(const PlantState& plant, const char* stateName, uint32_t unixTs);
  bool enqueueUserEvent(UserEventType type, const char* text, const char* rawAudioUrl,
                        uint32_t unixTs, const PlantState* snapshot);

  // Опрос очереди команд с VPS (публичный веб → ESP)
  using CommandHandler = void (*)(const char* jsonCmd);
  void setCommandHandler(CommandHandler h) { _onCmd = h; }
  void pollCommands(uint32_t nowMs);

  size_t queueSize() const { return _qCount; }

 private:
  static constexpr size_t Q_CAP = 32;
  String _base;
  String _deviceId = "esp32-boiler-1";
  String _token;
  uint32_t _lastFlushMs = 0;
  uint32_t _lastCmdPollMs = 0;
  CommandHandler _onCmd = nullptr;

  String _queue[Q_CAP];
  size_t _qHead = 0;
  size_t _qCount = 0;

  bool _push(const String& payload);
  bool _flushOne();
};
