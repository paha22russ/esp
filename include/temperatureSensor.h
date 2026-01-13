#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "eepromStorage.h"

class TemperatureSensor {
public:
  TemperatureSensor();
  void begin();
  void update();
  float getTemperature(uint8_t sensorIndex);
  bool isSensorValid(uint8_t sensorIndex);
  void rescanSensors(bool fullScan = false);
  void setSensorMapping(const SensorMapping& mapping);
  SensorMapping getSensorMapping() const;
  // Методы для совместимости со старым кодом
  void getSensorMapping(uint8_t& supplyIndex, uint8_t& returnIndex, uint8_t& boilerIndex, uint8_t& outsideIndex) const;
  void setSensorMapping(uint8_t supplyIndex, uint8_t returnIndex, uint8_t boilerIndex, uint8_t outsideIndex);
  int getSensorCount() const;
  String getSensorAddress(int physicalIndex) const;
  
private:
  OneWire oneWire1;
  OneWire oneWire2;
  DallasTemperature sensors1;
  DallasTemperature sensors2;
  
  float temperatures[4];
  bool sensorValid[4];
  unsigned long lastUpdate;
  unsigned long lastRescan;
  uint8_t errorCount[4];
  
  SensorMapping sensorMapping;
  
  void updateTemperatures();
  void performSoftRescan();
  void performFullRescan();
  bool findSensorAddress(uint8_t sensorIndex, uint8_t* address);
  void assignSensorToBus(uint8_t sensorIndex, const uint8_t* address);
};

#endif // TEMPERATURE_SENSOR_H

