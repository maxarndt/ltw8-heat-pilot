#pragma once

#include <stdint.h>

#include "ControlTypes.h"

class ControlEngine {
 public:
  void begin();
  void update(uint32_t nowMs);

  bool setManualOutput(uint8_t heaterPhases, bool pump, uint32_t nowMs);
  bool setOperatingMode(OperatingMode mode, uint32_t nowMs);
  void setMeasurements(int32_t surplusW, float temperatureC);
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
  bool measurementsValid_ = false;
  int32_t surplusW_ = 0;
  float temperatureC_ = 0.0F;
};

