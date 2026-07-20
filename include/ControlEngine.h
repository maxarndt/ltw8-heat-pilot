#pragma once

#include <stdint.h>

#include "ControlTypes.h"

class ControlEngine {
 public:
  void begin();
  void update(uint32_t nowMs);

  bool setManualOutput(uint8_t heaterPhases, bool pump, uint32_t nowMs);
  bool setOperatingMode(OperatingMode mode, uint32_t nowMs);
  void setSurplusMeasurement(int32_t surplusW);
  void setTemperatureMeasurement(float temperatureC, bool valid,
                                 uint32_t nowMs);
  void setFault();

  const OutputState& desiredOutputs() const { return outputs_; }
  ControlSnapshot snapshot(uint32_t nowMs) const;

 private:
  void updateAutomatic(uint32_t nowMs);
  void updateManual(uint32_t nowMs);
  void updatePumpOverrun(uint32_t nowMs);
  void applyHeaterPhases(uint8_t phases, uint32_t nowMs);
  void stopHeatingWithPumpOverrun(uint32_t nowMs);
  void startPumpOverrun(uint32_t nowMs);
  void clearPhaseCandidate();
  void updateTemperatureAvailability(uint32_t nowMs);
  bool temperatureUsable(uint32_t nowMs) const;
  ApplicationState temperatureUnavailableState() const;
  ApplicationState automaticIdleState(uint32_t nowMs) const;
  uint32_t manualTimeoutRemaining(uint32_t nowMs) const;
  uint32_t pumpOverrunRemaining(uint32_t nowMs) const;
  uint32_t phaseChangeRemaining(uint32_t nowMs) const;

  OperatingMode mode_ = OperatingMode::Disabled;
  ApplicationState state_ = ApplicationState::Starting;
  OutputState outputs_{};
  uint32_t lastManualCommandAtMs_ = 0;
  bool manualCommandActive_ = false;
  bool pumpOverrunActive_ = false;
  uint32_t pumpOverrunStartedAtMs_ = 0;
  bool phaseCandidateActive_ = false;
  uint8_t phaseCandidate_ = 0;
  uint32_t phaseCandidateSinceMs_ = 0;
  bool temperatureLockout_ = false;
  bool surplusValid_ = false;
  bool temperatureValid_ = false;
  bool temperatureFault_ = false;
  bool temperatureSampleSeen_ = false;
  bool temperatureUnavailableTimerActive_ = false;
  uint8_t consecutiveValidTemperatureSamples_ = 0;
  uint32_t lastTemperatureSampleAtMs_ = 0;
  uint32_t temperatureUnavailableSinceMs_ = 0;
  int32_t surplusW_ = 0;
  float temperatureC_ = 0.0F;
};
