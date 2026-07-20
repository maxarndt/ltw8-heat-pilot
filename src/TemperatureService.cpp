#include "TemperatureService.h"

#include <cmath>
#include <cstdio>

void TemperatureService::begin() {
  sensors_.begin();
  sensors_.setWaitForConversion(false);
  discoverSensors();
  startConversion(millis());
}

void TemperatureService::update(const uint32_t nowMs) {
  if (conversionPending_ &&
      nowMs - conversionStartedAtMs_ >=
          config::temperature::kConversionTimeMs) {
    finishConversion(nowMs);
  }

  if (!conversionPending_ &&
      nowMs - lastMeasurementAtMs_ >=
          config::temperature::kMeasurementIntervalMs) {
    startConversion(nowMs);
  }
}

void TemperatureService::formatAddress(const uint8_t* address, char* target,
                                       const size_t targetSize) {
  if (targetSize < 17) {
    if (targetSize > 0) {
      target[0] = '\0';
    }
    return;
  }

  for (uint8_t index = 0; index < 8; ++index) {
    std::snprintf(target + index * 2, targetSize - index * 2, "%02X",
                  address[index]);
  }
}

void TemperatureService::discoverSensors() {
  const uint8_t detected = sensors_.getDeviceCount();
  sensorCount_ = detected < config::temperature::kMaximumSensors
                     ? detected
                     : config::temperature::kMaximumSensors;

  log_.printf("[temperature] 1-Wire GPIO=%d detected=%u",
              config::temperature::kOneWirePin, detected);
  if (detected > sensorCount_) {
    log_.printf(" using_first=%u", sensorCount_);
  }
  log_.println();

  for (uint8_t index = 0; index < sensorCount_; ++index) {
    if (!sensors_.getAddress(readings_[index].address, index)) {
      readings_[index].valid = false;
      continue;
    }

    sensors_.setResolution(readings_[index].address,
                           config::temperature::kResolutionBits);
    char address[17];
    formatAddress(readings_[index].address, address, sizeof(address));
    log_.printf("[temperature] sensor=%u address=%s\n", index, address);
  }
}

void TemperatureService::startConversion(const uint32_t nowMs) {
  sensors_.requestTemperatures();
  conversionStartedAtMs_ = nowMs;
  conversionPending_ = true;
}

void TemperatureService::finishConversion(const uint32_t nowMs) {
  for (uint8_t index = 0; index < sensorCount_; ++index) {
    TemperatureSensorReading& current = readings_[index];
    current.temperatureC = sensors_.getTempC(current.address);
    current.valid = std::isfinite(current.temperatureC) &&
                    current.temperatureC >= -55.0F &&
                    current.temperatureC <= 125.0F &&
                    current.temperatureC != DEVICE_DISCONNECTED_C;
    current.measuredAtMs = nowMs;

    char address[17];
    formatAddress(current.address, address, sizeof(address));
    if (current.valid) {
      log_.printf("[temperature] address=%s temperature_c=%.2f\n", address,
                  current.temperatureC);
    } else {
      log_.printf("[temperature] address=%s invalid_reading=%.2f\n", address,
                  current.temperatureC);
    }
  }

  lastMeasurementAtMs_ = nowMs;
  conversionPending_ = false;
}
