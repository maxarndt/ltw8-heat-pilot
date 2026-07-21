#include "HeaterEnergyMeter.h"

void HeaterEnergyMeter::begin(const uint32_t nowMs,
                              const uint8_t activePhases) {
  accumulatedWattMilliseconds_ = 0;
  lastUpdatedAtMs_ = nowMs;
  activePhases_ = activePhases;
}

void HeaterEnergyMeter::update(const uint32_t nowMs,
                               const uint8_t activePhases) {
  accumulatedWattMilliseconds_ = wattMilliseconds(nowMs);
  lastUpdatedAtMs_ = nowMs;
  activePhases_ = activePhases;
}

uint64_t HeaterEnergyMeter::wattMilliseconds(const uint32_t nowMs) const {
  const uint32_t elapsedMs = nowMs - lastUpdatedAtMs_;
  const uint64_t activePowerW =
      static_cast<uint64_t>(activePhases_) * phasePowerW_;
  return accumulatedWattMilliseconds_ + activePowerW * elapsedMs;
}

double HeaterEnergyMeter::wattHours(const uint32_t nowMs) const {
  return wattMilliseconds(nowMs) / 3600000.0;
}
