#pragma once

#include <stdint.h>

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

struct OutputState {
  uint8_t heaterPhases = 0;
  bool circulationPump = false;
};

inline bool operator==(const OutputState& left, const OutputState& right) {
  return left.heaterPhases == right.heaterPhases &&
         left.circulationPump == right.circulationPump;
}

inline bool operator!=(const OutputState& left, const OutputState& right) {
  return !(left == right);
}

struct ControlSnapshot {
  OperatingMode mode;
  ApplicationState state;
  OutputState outputs;
  uint32_t manualTimeoutRemainingMs;
  uint32_t pumpOverrunRemainingMs;
  uint32_t phaseChangeRemainingMs;
  bool measurementsValid;
  int32_t surplusW;
  float temperatureC;
};

