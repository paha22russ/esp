#pragma once

// Распиновка 4.5-beta (твердотопливный котёл)

// Реле 220 В / силовые
#define PIN_RELAY_FAN          16  // Вентилятор (сейчас ON/OFF)
#define PIN_RELAY_PUMP         17  // Насос
#define PIN_RELAY_SENSOR_PWR   25  // Реле полного снятия/подачи питания DS18B20
                                   // НЕ питание от GPIO — только управление реле

// 1-Wire DS18B20
#define PIN_ONEWIRE_BUS1       4   // Шина №1: обратка
#define PIN_ONEWIRE_BUS2       5   // Шина №2: котельная + улица

// OLED SSD1306 I2C (addr 0x3C)
#define PIN_OLED_SDA           21
#define PIN_OLED_SCL           22
#define OLED_I2C_ADDR          0x3C

// Энкодер
#define PIN_ENCODER_CLK        18
#define PIN_ENCODER_DT         19
#define PIN_ENCODER_SW         23

// MAX31865 ×2 (PT1000), общая SPI-шина
// #1 дымоход (2-wire), #2 подача (3-wire)
#define PIN_MAX31865_CS_FLUE   26
#define PIN_MAX31865_CS_SUPPLY 27
#define PIN_SPI_SCK            32
#define PIN_SPI_MOSI           33
#define PIN_SPI_MISO           34  // input-only на ESP32 — подходит для MISO

// Свободен
#define PIN_GPIO2_FREE         2
