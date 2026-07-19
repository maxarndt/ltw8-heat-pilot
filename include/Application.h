#pragma once

#include <Arduino.h>

#include "OutputController.h"

enum class OperatingMode : uint8_t {
  Disabled,
  Manual,
  Automatic,
};

enum class ApplicationState : uint8_t {
  Starting,
  Disabled,
  ManualControl,
  Monitoring,
  Heating,
  PumpOverrun,
  TemperatureHold,
  WaitingForData,
  Fault,
};

struct ApplicationStatus {
  uint32_t uptimeMs;
  const char* mode;
  const char* state;
  OutputState outputs;
  bool outputDriverHealthy;
  uint32_t manualTimeoutRemainingMs;
  uint32_t pumpOverrunRemainingMs;
  uint32_t phaseChangeRemainingMs;
  bool measurementsValid;
  int32_t surplusW;
  float temperatureC;
};

class Application {
 public:
  Application(Print& log, OutputController& outputs)
      : log_(log), outputs_(outputs) {}

  void begin();
  void update(uint32_t nowMs);
  bool setManualOutput(uint8_t heaterPhases, bool pump, uint32_t nowMs);
  bool setOperatingMode(OperatingMode mode, uint32_t nowMs);
  void setSimulatedMeasurements(int32_t surplusW, float temperatureC);
  ApplicationStatus status(uint32_t nowMs) const;

 private:
  static constexpr uint32_t kStatusIntervalMs = 2000;

  bool setAllOutputsOff();
  void updateAutomatic(uint32_t nowMs);
  void updateManual(uint32_t nowMs);
  void updatePumpOverrun(uint32_t nowMs);
  bool applyHeaterPhases(uint8_t phases, uint32_t nowMs);
  bool stopHeatingWithPumpOverrun(uint32_t nowMs);
  void startPumpOverrun(uint32_t nowMs);
  void clearPhaseCandidate();
  void printStatus(uint32_t nowMs) const;
  uint32_t manualTimeoutRemaining(uint32_t nowMs) const;
  uint32_t pumpOverrunRemaining(uint32_t nowMs) const;
  uint32_t phaseChangeRemaining(uint32_t nowMs) const;
  static const char* toString(OperatingMode mode);
  static const char* toString(ApplicationState state);

  OperatingMode mode_ = OperatingMode::Disabled;
  ApplicationState state_ = ApplicationState::Starting;
  uint32_t lastStatusAtMs_ = 0;
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
  Print& log_;
  OutputController& outputs_;
};
