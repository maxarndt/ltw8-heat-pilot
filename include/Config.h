#pragma once

#include <Arduino.h>

namespace config {

inline constexpr char kHostname[] = "heat-pilot";

// Development credential for the trusted local network. Change this before
// permanent deployment and keep platformio.ini in sync.
inline constexpr char kOtaPassword[] = "heat-pilot-dev";

inline constexpr uint16_t kLogPort = 23;
inline constexpr uint32_t kManualOutputTimeoutMs = 60000;

namespace control {
inline constexpr int32_t kHeaterPhasePowerW = 1500;
inline constexpr int32_t kPhaseEnableSurplusW = 1700;
inline constexpr int32_t kPhaseDisableSurplusW = 1300;
inline constexpr uint32_t kPhaseChangeStableMs = 30000;
inline constexpr uint32_t kPumpOverrunMs = 90000;
inline constexpr float kTargetTemperatureC = 80.0F;
inline constexpr float kTemperatureHysteresisC = 4.0F;
}  // namespace control

namespace outputs {
inline constexpr uint8_t kI2cAddress = 0x20;
inline constexpr int8_t kSdaPin = 42;
inline constexpr int8_t kSclPin = 41;
}  // namespace outputs

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
