#pragma once

#include <stdint.h>

class HeaterEnergyMeter {
 public:
  explicit HeaterEnergyMeter(uint32_t phasePowerW)
      : phasePowerW_(phasePowerW) {}

  void begin(uint32_t nowMs, uint8_t activePhases = 0);
  void update(uint32_t nowMs, uint8_t activePhases);

  uint64_t wattMilliseconds(uint32_t nowMs) const;
  double wattHours(uint32_t nowMs) const;

 private:
  uint64_t accumulatedWattMilliseconds_ = 0;
  uint32_t phasePowerW_;
  uint32_t lastUpdatedAtMs_ = 0;
  uint8_t activePhases_ = 0;
};
