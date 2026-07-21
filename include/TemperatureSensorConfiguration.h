#pragma once

#include <stddef.h>
#include <stdint.h>

struct TemperatureSensorDefinition {
  const char* label;
  uint8_t address[8];
};

struct TemperatureSensorConfigurationMatch {
  size_t missing = 0;
  size_t unknown = 0;
  bool duplicateConfiguredAddress = false;

  bool valid() const {
    return missing == 0 && unknown == 0 && !duplicateConfiguredAddress;
  }
};

inline bool temperatureSensorAddressesEqual(const uint8_t* left,
                                            const uint8_t* right) {
  for (size_t index = 0; index < 8; ++index) {
    if (left[index] != right[index]) {
      return false;
    }
  }
  return true;
}

inline int findTemperatureSensorDefinition(
    const uint8_t* address, const TemperatureSensorDefinition* definitions,
    const size_t definitionCount) {
  for (size_t index = 0; index < definitionCount; ++index) {
    if (temperatureSensorAddressesEqual(address, definitions[index].address)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

inline TemperatureSensorConfigurationMatch matchTemperatureSensors(
    const uint8_t* detectedAddresses, const size_t detectedCount,
    const TemperatureSensorDefinition* definitions,
    const size_t definitionCount) {
  TemperatureSensorConfigurationMatch result;

  for (size_t left = 0; left < definitionCount; ++left) {
    for (size_t right = left + 1; right < definitionCount; ++right) {
      if (temperatureSensorAddressesEqual(definitions[left].address,
                                          definitions[right].address)) {
        result.duplicateConfiguredAddress = true;
      }
    }
  }

  for (size_t detected = 0; detected < detectedCount; ++detected) {
    if (findTemperatureSensorDefinition(detectedAddresses + detected * 8,
                                        definitions, definitionCount) < 0) {
      ++result.unknown;
    }
  }

  for (size_t configured = 0; configured < definitionCount; ++configured) {
    bool found = false;
    for (size_t detected = 0; detected < detectedCount; ++detected) {
      if (temperatureSensorAddressesEqual(
              definitions[configured].address,
              detectedAddresses + detected * 8)) {
        found = true;
        break;
      }
    }
    if (!found) {
      ++result.missing;
    }
  }

  return result;
}
