#pragma once

#include <stdint.h>

enum class ManualOutputValidation : uint8_t {
  Ok,
  InvalidPhaseCount,
  PumpRequired,
};

inline ManualOutputValidation validateManualOutput(const int heaterPhases,
                                                   const bool pump) {
  if (heaterPhases < 0 || heaterPhases > 3) {
    return ManualOutputValidation::InvalidPhaseCount;
  }
  if (heaterPhases > 0 && !pump) {
    return ManualOutputValidation::PumpRequired;
  }
  return ManualOutputValidation::Ok;
}

inline bool isValidTemperature(const float temperatureC) {
  return temperatureC >= -55.0F && temperatureC <= 125.0F;
}
