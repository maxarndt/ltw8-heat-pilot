#include "OutputController.h"

#include <Wire.h>

#include "Config.h"

bool OutputController::begin() {
  Wire.begin(config::outputs::kSdaPin, config::outputs::kSclPin);

  // The optocoupler inputs are active-low: a high TCA9554 level means the
  // physical output is off. Preload the safe level before enabling outputs.
  if (!writeRegister(kOutputRegister, 0xFF) ||
      !writeRegister(kConfigurationRegister, 0x00)) {
    healthy_ = false;
    state_ = {};
    return false;
  }

  healthy_ = true;
  state_ = {};
  return true;
}

bool OutputController::set(const uint8_t heaterPhases,
                           const bool circulationPump) {
  if (heaterPhases > 3 || !healthy_) {
    return false;
  }

  const uint8_t heaterMask =
      heaterPhases == 0 ? 0 : static_cast<uint8_t>((1U << heaterPhases) - 1U);
  const uint8_t pumpMask = circulationPump ? (1U << 3) : 0;
  const uint8_t activeOutputs = heaterMask | pumpMask;
  const uint8_t outputLevels = static_cast<uint8_t>(~activeOutputs);

  if (!writeRegister(kOutputRegister, outputLevels)) {
    healthy_ = false;
    state_ = {};
    return false;
  }

  state_.heaterPhases = heaterPhases;
  state_.circulationPump = circulationPump;
  return true;
}

bool OutputController::allOff() {
  if (!writeRegister(kOutputRegister, 0xFF)) {
    healthy_ = false;
    state_ = {};
    return false;
  }

  state_ = {};
  return true;
}

bool OutputController::writeRegister(const uint8_t reg, const uint8_t value) {
  Wire.beginTransmission(config::outputs::kI2cAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
