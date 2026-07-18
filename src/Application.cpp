#include "Application.h"

void Application::begin() {
  // Until the physical output driver is implemented, the logical outputs are
  // explicitly kept off. The device always starts disabled.
  setAllOutputsOff();
  mode_ = OperatingMode::Disabled;
  state_ = ApplicationState::Disabled;

  log_.println();
  log_.println("LTW8 Heat Pilot");
  log_.println("Firmware started; all outputs are OFF.");
  printStatus(millis());
}

void Application::update(const uint32_t nowMs) {
  if (nowMs - lastStatusAtMs_ >= kStatusIntervalMs) {
    lastStatusAtMs_ = nowMs;
    printStatus(nowMs);
  }
}

void Application::setAllOutputsOff() {
  outputs_ = {};
}

void Application::printStatus(const uint32_t nowMs) const {
  log_.printf(
      "[status] uptime_ms=%lu mode=%s state=%s heater=%u%u%u pump=%u\n",
      static_cast<unsigned long>(nowMs), toString(mode_), toString(state_),
      outputs_.heaterPhase1, outputs_.heaterPhase2, outputs_.heaterPhase3,
      outputs_.circulationPump);
}

const char* Application::toString(const OperatingMode mode) {
  switch (mode) {
    case OperatingMode::Disabled:
      return "disabled";
    case OperatingMode::Automatic:
      return "automatic";
  }
  return "unknown";
}

const char* Application::toString(const ApplicationState state) {
  switch (state) {
    case ApplicationState::Starting:
      return "starting";
    case ApplicationState::Disabled:
      return "disabled";
    case ApplicationState::Monitoring:
      return "monitoring";
    case ApplicationState::Heating:
      return "heating";
    case ApplicationState::Fault:
      return "fault";
  }
  return "unknown";
}
