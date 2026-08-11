#include "vps_client.h"
#include <WiFi.h>
#include <HTTPClient.h>

void VpsClient::begin() {
  _qHead = 0;
  _qCount = 0;
}

bool VpsClient::_push(const String& payload) {
  if (_qCount >= Q_CAP) {
    // drop oldest
    _qHead = (_qHead + 1) % Q_CAP;
    _qCount--;
  }
  const size_t idx = (_qHead + _qCount) % Q_CAP;
  _queue[idx] = payload;
  _qCount++;
  return true;
}

bool VpsClient::enqueueTelemetry(const PlantState& plant, const char* stateName, uint32_t unixTs) {
  StaticJsonDocument<1536> doc;
  doc["type"] = "telemetry";
  doc["device_id"] = _deviceId;
  JsonObject sample = doc.createNestedObject("sample");
  telemetry::fillSample(sample, plant, stateName, unixTs);
  String out;
  serializeJson(doc, out);
  return _push(out);
}

bool VpsClient::enqueueUserEvent(UserEventType type, const char* text, const char* rawAudioUrl,
                                 uint32_t unixTs, const PlantState* snapshot) {
  StaticJsonDocument<2048> doc;
  doc["type"] = "user_event";
  doc["device_id"] = _deviceId;
  doc["ts"] = unixTs;
  doc["event"] = userEventTypeName(type);
  doc["text"] = text ? text : "";
  if (rawAudioUrl && rawAudioUrl[0]) doc["audio_url"] = rawAudioUrl;
  if (snapshot) {
    JsonObject snap = doc.createNestedObject("snapshot");
    telemetry::fillSample(snap, *snapshot, nullptr, unixTs);
  }
  String out;
  serializeJson(doc, out);
  return _push(out);
}

bool VpsClient::_flushOne() {
  if (_qCount == 0 || _base.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const String& payload = _queue[_qHead];
  HTTPClient http;
  const String url = _base + "/api/v1/ingest";
  if (!http.begin(url)) return false;
  http.setTimeout(4000);
  http.addHeader("Content-Type", "application/json");
  if (_token.length()) http.addHeader("Authorization", "Bearer " + _token);

  const int code = http.POST(payload);
  http.end();
  if (code >= 200 && code < 300) {
    _qHead = (_qHead + 1) % Q_CAP;
    _qCount--;
    return true;
  }
  return false;
}

void VpsClient::tick(uint32_t nowMs) {
  if (_qCount == 0) return;
  if (_lastFlushMs != 0 && (nowMs - _lastFlushMs) < 1000) return;
  _lastFlushMs = nowMs;
  // неблокирующе — по одному сообщению за тик
  _flushOne();
}
