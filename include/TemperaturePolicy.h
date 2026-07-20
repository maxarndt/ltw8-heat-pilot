#pragma once

#include <stdint.h>

#include "Config.h"

inline bool isTemperatureMeasurementStale(const uint32_t nowMs,
                                          const uint32_t measuredAtMs) {
  return nowMs - measuredAtMs > config::control::kTemperatureStaleMs;
}
