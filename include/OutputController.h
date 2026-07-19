#pragma once

#include <Arduino.h>

struct OutputState {
  uint8_t heaterPhases = 0;
  bool circulationPump = false;
};

class OutputController {
 public:
  bool begin();
  bool set(uint8_t heaterPhases, bool circulationPump);
  bool allOff();

  const OutputState& state() const { return state_; }
  bool healthy() const { return healthy_; }

 private:
  static constexpr uint8_t kOutputRegister = 0x01;
  static constexpr uint8_t kConfigurationRegister = 0x03;

  bool writeRegister(uint8_t reg, uint8_t value);

  OutputState state_{};
  bool healthy_ = false;
};

