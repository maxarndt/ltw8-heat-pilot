#include "OutputController.h"

#include <Wire.h>

#include "Config.h"
#include "OutputEncoding.h"

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

  const OutputState requested{heaterPhases, circulationPump};
  const uint8_t outputLevels = encodeOutputLevels(requested);

  if (!writeRegister(kOutputRegister, outputLevels)) {
    healthy_ = false;
    state_ = {};
    return false;
  }

  state_ = requested;
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
