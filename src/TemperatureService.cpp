#include "TemperatureService.h"

#include <cmath>
#include <cstdio>

void TemperatureService::begin() {
  sensors_.begin();
  sensors_.setWaitForConversion(false);
  discoverSensors();
  if (configurationValid_) {
    startConversion(millis());
  }
}

void TemperatureService::update(const uint32_t nowMs) {
  if (!configurationValid_) {
    if (nowMs - lastConfigurationLogAtMs_ >=
        config::temperature::kConfigurationLogIntervalMs) {
      lastConfigurationLogAtMs_ = nowMs;
      logConfiguration();
    }
    return;
  }
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
  detectedSensorCount_ = detected;
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

  }

  uint8_t detectedAddresses[config::temperature::kMaximumSensors][8]{};
  for (uint8_t index = 0; index < sensorCount_; ++index) {
    for (uint8_t byte = 0; byte < 8; ++byte) {
      detectedAddresses[index][byte] = readings_[index].address[byte];
    }
  }
  const TemperatureSensorConfigurationMatch match = matchTemperatureSensors(
      &detectedAddresses[0][0], sensorCount_, config::temperature::kSensors,
      config::temperature::kSensorCount);
  missingSensorCount_ = static_cast<uint8_t>(match.missing);
  unknownSensorCount_ = static_cast<uint8_t>(
      match.unknown + (detected > sensorCount_ ? detected - sensorCount_ : 0));
  configurationValid_ = match.valid() && detected == sensorCount_;

  for (uint8_t index = 0; index < sensorCount_; ++index) {
    const int definition = findTemperatureSensorDefinition(
        readings_[index].address, config::temperature::kSensors,
        config::temperature::kSensorCount);
    readings_[index].configured = definition >= 0;
    readings_[index].label =
        definition >= 0 ? config::temperature::kSensors[definition].label
                        : nullptr;
    if (readings_[index].configured) {
      sensors_.setResolution(readings_[index].address,
                             config::temperature::kResolutionBits);
    }
  }

  logConfiguration();
}

void TemperatureService::logConfiguration() const {
  log_.printf(
      "[temperature] configuration=%s expected=%u detected=%u missing=%u "
      "unknown=%u\n",
      configurationValid_ ? "valid" : "INVALID",
      static_cast<unsigned>(config::temperature::kSensorCount),
      detectedSensorCount_, missingSensorCount_, unknownSensorCount_);
  for (uint8_t index = 0; index < sensorCount_; ++index) {
    char address[17];
    formatAddress(readings_[index].address, address, sizeof(address));
    log_.printf("[temperature] address=%s label=%s configured=%u\n", address,
                readings_[index].label != nullptr ? readings_[index].label
                                                  : "unknown",
                readings_[index].configured);
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
