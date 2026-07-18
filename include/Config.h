#pragma once

#include <Arduino.h>

namespace config {

inline constexpr char kHostname[] = "heat-pilot";

// Development credential for the trusted local network. Change this before
// permanent deployment and keep platformio.ini in sync.
inline constexpr char kOtaPassword[] = "heat-pilot-dev";

inline constexpr uint16_t kLogPort = 23;

namespace ethernet {
inline constexpr int8_t kInterruptPin = 12;
inline constexpr int8_t kMosiPin = 13;
inline constexpr int8_t kMisoPin = 14;
inline constexpr int8_t kClockPin = 15;
inline constexpr int8_t kChipSelectPin = 16;
inline constexpr int8_t kResetPin = 39;
inline constexpr int8_t kPhyAddress = 1;
}  // namespace ethernet

}  // namespace config

