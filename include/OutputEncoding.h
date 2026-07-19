#pragma once

#include <stdint.h>

#include "ControlTypes.h"

inline uint8_t encodeOutputLevels(const OutputState& outputs) {
  const uint8_t heaterMask = outputs.heaterPhases == 0
                                  ? 0
                                  : static_cast<uint8_t>(
                                        (1U << outputs.heaterPhases) - 1U);
  const uint8_t pumpMask = outputs.circulationPump ? (1U << 3) : 0;
  return static_cast<uint8_t>(~(heaterMask | pumpMask));
}

