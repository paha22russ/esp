#pragma once

#include <WebServer.h>
#include <WiFi.h>
#include "../app/types.h"
#include "../control/orchestrator.h"
#include "../sensors/home_mqtt.h"
#include "../telemetry/vps_client.h"
#include "../events/journal.h"
#include "../events/user_events.h"
#include "../ml/hypothesis_engine.h"
#include "../app/ota_channel.h"
#include "../hal/actuators.h"

// Регистрация HTTP API в формате UI 4.2 (+ Neuro/PT1000 для 4.5).
void legacyHttpRegister(
    WebServer& server,
    PlantState& plant,
    BoilerOrchestrator& orch,
    HomeMqttSensor& homeMqtt,
    VpsClient& vps,
    EventJournal& journal,
    UserEventService& userEvents,
    HypothesisEngine& hypotheses,
    OtaChannelV45& ota,
    FanActuator& fan,
    PumpActuator& pump,
    SensorPowerRelay& sensorPwr);
