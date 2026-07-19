#pragma once

#include <Arduino.h>

#include "ControlEngine.h"
#include "OutputController.h"

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

  bool syncOutputs();
  void printStatus(uint32_t nowMs) const;
  static const char* toString(OperatingMode mode);
  static const char* toString(ApplicationState state);

  uint32_t lastStatusAtMs_ = 0;
  Print& log_;
  OutputController& outputs_;
  ControlEngine control_{};
};
