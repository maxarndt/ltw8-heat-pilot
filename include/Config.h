#pragma once

#include <stdint.h>

namespace config {

constexpr char kHostname[] = "heat-pilot";

// Development credential for the trusted local network. Change this before
// permanent deployment and keep platformio.ini in sync.
constexpr char kOtaPassword[] = "heat-pilot-dev";

constexpr uint16_t kLogPort = 23;
constexpr uint32_t kManualOutputTimeoutMs = 60000;

namespace control {
constexpr int32_t kHeaterPhasePowerW = 1500;
constexpr int32_t kPhaseEnableSurplusW = 1700;
constexpr int32_t kPhaseDisableSurplusW = 1300;
constexpr uint32_t kPhaseChangeStableMs = 30000;
constexpr uint32_t kPumpOverrunMs = 90000;
constexpr float kTargetTemperatureC = 80.0F;
constexpr float kTemperatureHysteresisC = 4.0F;
}  // namespace control

namespace outputs {
constexpr uint8_t kI2cAddress = 0x20;
constexpr int8_t kSdaPin = 42;
constexpr int8_t kSclPin = 41;
}  // namespace outputs

namespace ethernet {
constexpr int8_t kInterruptPin = 12;
constexpr int8_t kMosiPin = 13;
constexpr int8_t kMisoPin = 14;
constexpr int8_t kClockPin = 15;
constexpr int8_t kChipSelectPin = 16;
constexpr int8_t kResetPin = 39;
constexpr int8_t kPhyAddress = 1;
}  // namespace ethernet

}  // namespace config
