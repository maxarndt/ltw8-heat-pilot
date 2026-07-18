#pragma once

#include <Arduino.h>

enum class OperatingMode : uint8_t {
  Disabled,
  Automatic,
};

enum class ApplicationState : uint8_t {
  Starting,
  Disabled,
  Monitoring,
  Heating,
  Fault,
};

struct OutputState {
  bool heaterPhase1 = false;
  bool heaterPhase2 = false;
  bool heaterPhase3 = false;
  bool circulationPump = false;
};

class Application {
 public:
  explicit Application(Print& log) : log_(log) {}

  void begin();
  void update(uint32_t nowMs);

 private:
  static constexpr uint32_t kStatusIntervalMs = 2000;

  void setAllOutputsOff();
  void printStatus(uint32_t nowMs) const;
  static const char* toString(OperatingMode mode);
  static const char* toString(ApplicationState state);

  OperatingMode mode_ = OperatingMode::Disabled;
  ApplicationState state_ = ApplicationState::Starting;
  OutputState outputs_{};
  uint32_t lastStatusAtMs_ = 0;
  Print& log_;
};
