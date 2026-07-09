/*
 * Движок GPIO ESP32 neiro: сигналы, ШИМ, последовательности.
 */

#include "pin_engine.h"

#include <Arduino.h>
#include <math.h>

#define MAX_SIGNALS 4
#define MAX_SEQ_STEPS 24
#define LEDC_RES_BITS 10
#define LEDC_MAX ((1 << LEDC_RES_BITS) - 1)
#define LEDC_CARRIER_HZ 5000

enum Waveform : uint8_t { WAVE_SQUARE = 0, WAVE_SINE, WAVE_SAW, WAVE_TRIANGLE };

struct PinSignal {
  bool active = false;
  uint8_t pin = 0;
  Waveform wave = WAVE_SQUARE;
  float hz = 1.0f;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
  uint8_t dutyPercent = 100;
  bool activeLow = false;
  bool useLedc = false;
  uint8_t ledcChannel = 0;
  uint32_t lastUpdateMs = 0;
  float phase = 0.0f;
  bool squareLevel = false;
  uint32_t lastEdgeMs = 0;
};

struct SeqStep {
  uint32_t delayMs = 0;
  char json[192];
};

static PinSignal g_signals[MAX_SIGNALS];
static uint8_t g_nextLedcCh = 0;
static bool g_seqActive = false;
static uint32_t g_seqStartMs = 0;
static size_t g_seqIndex = 0;
static size_t g_seqCount = 0;
static SeqStep g_seqSteps[MAX_SEQ_STEPS];

static bool isPinAllowed(uint8_t pin) {
  if (pin > 33) return false;
  if (pin >= 6 && pin <= 11) return false;
  if (pin == 21 || pin == 22) return false;
  if (pin >= 34) return false;
  return true;
}

static Waveform parseWave(const char *s) {
  if (!s) return WAVE_SQUARE;
  if (strcasecmp(s, "sine") == 0 || strcasecmp(s, "sin") == 0) return WAVE_SINE;
  if (strcasecmp(s, "saw") == 0 || strcasecmp(s, "sawtooth") == 0) return WAVE_SAW;
  if (strcasecmp(s, "triangle") == 0 || strcasecmp(s, "tri") == 0) return WAVE_TRIANGLE;
  return WAVE_SQUARE;
}

static float waveSample(Waveform w, float phase01) {
  switch (w) {
    case WAVE_SINE:
      return (sinf(phase01 * 2.0f * PI) + 1.0f) * 0.5f;
    case WAVE_SAW:
      return phase01;
    case WAVE_TRIANGLE:
      return phase01 < 0.5f ? phase01 * 2.0f : 2.0f - phase01 * 2.0f;
    default:
      return phase01 < 0.5f ? 1.0f : 0.0f;
  }
}

static PinSignal *findSignalByPin(uint8_t pin) {
  for (auto &s : g_signals) {
    if (s.active && s.pin == pin) return &s;
  }
  return nullptr;
}

static PinSignal *allocSignal() {
  for (auto &s : g_signals) {
    if (!s.active) return &s;
  }
  return nullptr;
}

static void releaseLedc(PinSignal &s) {
  if (!s.useLedc) return;
  ledcWrite(s.ledcChannel, 0);
  s.useLedc = false;
}

static void stopSignalSlot(PinSignal &s) {
  if (!s.active) return;
  if (s.useLedc) {
    ledcWrite(s.ledcChannel, s.activeLow ? LEDC_MAX : 0);
    releaseLedc(s);
  } else {
    digitalWrite(s.pin, s.activeLow ? HIGH : LOW);
  }
  s.active = false;
}

static void setupLedc(PinSignal &s) {
  s.ledcChannel = g_nextLedcCh % 8;
  g_nextLedcCh++;
  ledcSetup(s.ledcChannel, LEDC_CARRIER_HZ, LEDC_RES_BITS);
  ledcAttachPin(s.pin, s.ledcChannel);
  s.useLedc = true;
}

void pinEngineStopPin(uint8_t pin) {
  if (pin > 33) {
    for (auto &s : g_signals) {
      if (s.active) stopSignalSlot(s);
    }
    return;
  }
  if (PinSignal *s = findSignalByPin(pin)) stopSignalSlot(*s);
  if (isPinAllowed(pin)) {
    pinMode(pin, OUTPUT);
    pinModes[pin] = 1;
    digitalWrite(pin, pin == 2 ? HIGH : LOW);
  }
}

static void startSignal(uint8_t pin, Waveform wave, float hz, uint32_t durationMs,
                        uint8_t dutyPercent, bool activeLow) {
  if (!isPinAllowed(pin)) return;
  if (hz <= 0.0f) hz = 1.0f;

  if (PinSignal *existing = findSignalByPin(pin)) stopSignalSlot(*existing);
  PinSignal *s = allocSignal();
  if (!s) return;

  s->active = true;
  s->pin = pin;
  s->wave = wave;
  s->hz = hz;
  s->durationMs = durationMs;
  s->dutyPercent = dutyPercent > 100 ? 100 : dutyPercent;
  s->activeLow = activeLow;
  s->startMs = millis();
  s->lastUpdateMs = s->startMs;
  s->lastEdgeMs = s->startMs;
  s->phase = 0.0f;
  s->squareLevel = true;

  pinMode(pin, OUTPUT);
  pinModes[pin] = 1;

  bool analogWave = (wave != WAVE_SQUARE) || hz > 50.0f || dutyPercent != 50;
  if (analogWave) {
    setupLedc(*s);
  } else {
    s->useLedc = false;
    digitalWrite(pin, activeLow ? LOW : HIGH);
  }

  Serial.printf("[Signal] GPIO %u wave=%u %.2fHz %lums\n", pin, wave, hz, durationMs);
}

static bool runSingleCommand(JsonObject cmd);

static void startSequence(JsonArray steps) {
  g_seqCount = 0;
  for (JsonObject step : steps) {
    if (g_seqCount >= MAX_SEQ_STEPS) break;
    JsonObject action = step.containsKey("action") ? step["action"].as<JsonObject>() : step;
    String serialized;
    serializeJson(action, serialized);
    if (serialized.length() >= sizeof(g_seqSteps[0].json)) continue;
    g_seqSteps[g_seqCount].delayMs = step["delay_ms"] | 0;
    strncpy(g_seqSteps[g_seqCount].json, serialized.c_str(), sizeof(g_seqSteps[0].json) - 1);
    g_seqSteps[g_seqCount].json[sizeof(g_seqSteps[0].json) - 1] = '\0';
    g_seqCount++;
  }
  g_seqIndex = 0;
  g_seqStartMs = millis();
  g_seqActive = g_seqCount > 0;
}

static void updateSequence(uint32_t now) {
  if (!g_seqActive) return;
  while (g_seqIndex < g_seqCount) {
    if (now - g_seqStartMs < g_seqSteps[g_seqIndex].delayMs) break;
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, g_seqSteps[g_seqIndex].json) == DeserializationError::Ok) {
      runSingleCommand(doc.as<JsonObject>());
    }
    g_seqIndex++;
  }
  if (g_seqIndex >= g_seqCount) g_seqActive = false;
}

static bool runSingleCommand(JsonObject cmd) {
  const char *type = cmd["cmd"];
  if (!type) return false;

  if (strcmp(type, "digital_write") == 0) {
    int pin = cmd["pin"] | -1;
    if (!isPinAllowed(pin)) return true;
    pinEngineStopPin(pin);
    pinMode(pin, OUTPUT);
    pinModes[pin] = 1;
    digitalWrite(pin, (cmd["value"] | 0) ? HIGH : LOW);
    return true;
  }

  if (strcmp(type, "signal") == 0 || strcmp(type, "blink") == 0) {
    int pin = cmd["pin"] | 2;
    float hz = cmd["hz"].isNull() ? 1.0f : cmd["hz"].as<float>();
    uint32_t dur = cmd["duration_ms"] | 0;
    uint8_t duty = cmd["duty"] | 100;
    bool al = cmd["active_low"].isNull() ? (pin == 2) : cmd["active_low"].as<bool>();
    startSignal(pin, parseWave(cmd["wave"] | "square"), hz, dur, duty, al);
    return true;
  }

  if (strcmp(type, "pwm") == 0) {
    int pin = cmd["pin"] | 2;
    if (!isPinAllowed(pin)) return true;
    if (PinSignal *old = findSignalByPin(pin)) stopSignalSlot(*old);
    PinSignal *s = allocSignal();
    if (!s) return true;
    s->active = true;
    s->pin = pin;
    s->wave = WAVE_SQUARE;
    s->hz = cmd["hz"].isNull() ? 1000.0f : cmd["hz"].as<float>();
    s->durationMs = 0;
    s->dutyPercent = cmd["duty"] | 50;
    s->activeLow = cmd["active_low"].isNull() ? (pin == 2) : cmd["active_low"].as<bool>();
    s->startMs = millis();
    s->lastUpdateMs = s->startMs;
    pinMode(pin, OUTPUT);
    pinModes[pin] = 1;
    setupLedc(*s);
    uint32_t d = (uint32_t)((s->dutyPercent / 100.0f) * LEDC_MAX);
    if (s->activeLow) d = LEDC_MAX - d;
    ledcWrite(s->ledcChannel, d);
    return true;
  }

  if (strcmp(type, "stop") == 0) {
    pinEngineStopPin(cmd["pin"] | 255);
    return true;
  }

  if (strcmp(type, "sequence") == 0 && cmd.containsKey("steps")) {
    startSequence(cmd["steps"].as<JsonArray>());
    return true;
  }

  return false;
}

bool pinEngineHandleCommand(JsonObject cmd) {
  const char *type = cmd["cmd"];
  if (!type) return false;
  if (strcmp(type, "signal") == 0 || strcmp(type, "blink") == 0 ||
      strcmp(type, "pwm") == 0 || strcmp(type, "stop") == 0 || strcmp(type, "sequence") == 0) {
    runSingleCommand(cmd);
    return true;
  }
  return false;
}

static void updateSignal(PinSignal &s, uint32_t now) {
  if (!s.active) return;
  if (s.durationMs > 0 && (now - s.startMs) >= s.durationMs) {
    stopSignalSlot(s);
    return;
  }

  if (!s.useLedc) {
    uint32_t half = (uint32_t)(500.0f / s.hz);
    if (half < 5) half = 5;
    if (now - s.lastEdgeMs >= half) {
      s.lastEdgeMs = now;
      s.squareLevel = !s.squareLevel;
      digitalWrite(s.pin, s.squareLevel ? (s.activeLow ? LOW : HIGH) : (s.activeLow ? HIGH : LOW));
    }
    return;
  }

  if (s.durationMs == 0 && s.wave == WAVE_SQUARE) return;

  uint32_t delta = now - s.lastUpdateMs;
  if (delta < 4) return;
  s.lastUpdateMs = now;
  s.phase += s.hz * (delta / 1000.0f);
  s.phase -= floorf(s.phase);
  float amp = waveSample(s.wave, s.phase) * (s.dutyPercent / 100.0f);
  uint32_t duty = (uint32_t)(amp * LEDC_MAX);
  if (s.activeLow) duty = LEDC_MAX - duty;
  ledcWrite(s.ledcChannel, duty);
}

void pinEngineInit() {
  memset(g_signals, 0, sizeof(g_signals));
  g_seqActive = false;
  g_seqCount = 0;
}

void pinEngineUpdate() {
  uint32_t now = millis();
  for (auto &s : g_signals) updateSignal(s, now);
  updateSequence(now);
}
