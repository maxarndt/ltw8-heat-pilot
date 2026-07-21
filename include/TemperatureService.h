#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include "Config.h"

struct TemperatureSensorReading {
  DeviceAddress address{};
  const char* label = nullptr;
  float temperatureC = DEVICE_DISCONNECTED_C;
  bool configured = false;
  bool valid = false;
  uint32_t measuredAtMs = 0;
};

class TemperatureService {
 public:
  explicit TemperatureService(Print& log)
      : log_(log), oneWire_(config::temperature::kOneWirePin),
        sensors_(&oneWire_) {}

  void begin();
  void update(uint32_t nowMs);

  uint8_t count() const { return sensorCount_; }
  uint8_t detectedCount() const { return detectedSensorCount_; }
  uint8_t missingCount() const { return missingSensorCount_; }
  uint8_t unknownCount() const { return unknownSensorCount_; }
  bool configurationValid() const { return configurationValid_; }
  const TemperatureSensorReading& reading(uint8_t index) const {
    return readings_[index];
  }
  static void formatAddress(const uint8_t* address, char* target,
                            size_t targetSize);

 private:
  void discoverSensors();
  void logConfiguration() const;
  void startConversion(uint32_t nowMs);
  void finishConversion(uint32_t nowMs);

  Print& log_;
  OneWire oneWire_;
  DallasTemperature sensors_;
  TemperatureSensorReading
      readings_[config::temperature::kMaximumSensors]{};
  uint8_t sensorCount_ = 0;
  uint8_t detectedSensorCount_ = 0;
  uint8_t missingSensorCount_ = 0;
  uint8_t unknownSensorCount_ = 0;
  uint32_t conversionStartedAtMs_ = 0;
  uint32_t lastMeasurementAtMs_ = 0;
  uint32_t lastConfigurationLogAtMs_ = 0;
  bool configurationValid_ = false;
  bool conversionPending_ = false;
};
