#include "Application.h"

#include "Config.h"

void Application::begin() {
  mode_ = OperatingMode::Disabled;
  state_ = ApplicationState::Starting;

  if (!outputs_.begin()) {
    state_ = ApplicationState::Fault;
    log_.println("[outputs] failed to initialize TCA9554; state=fault");
  } else {
    setAllOutputsOff();
    state_ = ApplicationState::Disabled;
    log_.println("[outputs] TCA9554 ready; all outputs are OFF");
  }

  log_.println();
  log_.println("LTW8 Heat Pilot");
  log_.println("Firmware started; all outputs are OFF.");
  printStatus(millis());
}

void Application::update(const uint32_t nowMs) {
  if (state_ != ApplicationState::Fault) {
    if (mode_ == OperatingMode::Automatic) {
      updateAutomatic(nowMs);
    } else if (mode_ == OperatingMode::Manual) {
      updateManual(nowMs);
    }

    updatePumpOverrun(nowMs);
  }

  if (nowMs - lastStatusAtMs_ >= kStatusIntervalMs) {
    lastStatusAtMs_ = nowMs;
    printStatus(nowMs);
  }
}

bool Application::setAllOutputsOff() {
  if (!outputs_.allOff()) {
    state_ = ApplicationState::Fault;
    return false;
  }
  return true;
}

bool Application::setManualOutput(const uint8_t heaterPhases, const bool pump,
                                  const uint32_t nowMs) {
  if (heaterPhases > 3 || (heaterPhases > 0 && !pump) ||
      state_ == ApplicationState::Fault || !outputs_.healthy()) {
    return false;
  }

  clearPhaseCandidate();
  manualCommandActive_ = false;

  if (heaterPhases > 0) {
    if (!outputs_.set(heaterPhases, true)) {
      state_ = ApplicationState::Fault;
      mode_ = OperatingMode::Disabled;
      return false;
    }

    pumpOverrunActive_ = false;
    mode_ = OperatingMode::Manual;
    state_ = ApplicationState::ManualControl;
    lastManualCommandAtMs_ = nowMs;
    manualCommandActive_ = true;
    return true;
  }

  if (outputs_.state().heaterPhases > 0) {
    mode_ = OperatingMode::Disabled;
    return stopHeatingWithPumpOverrun(nowMs);
  }

  if (!outputs_.set(0, pump)) {
    mode_ = OperatingMode::Disabled;
    state_ = ApplicationState::Fault;
    return false;
  }

  pumpOverrunActive_ = false;
  if (!pump) {
    mode_ = OperatingMode::Disabled;
    state_ = ApplicationState::Disabled;
    return true;
  }

  mode_ = OperatingMode::Manual;
  state_ = ApplicationState::ManualControl;
  lastManualCommandAtMs_ = nowMs;
  manualCommandActive_ = true;
  return true;
}

bool Application::setOperatingMode(const OperatingMode mode,
                                   const uint32_t nowMs) {
  if (mode == OperatingMode::Manual || state_ == ApplicationState::Fault ||
      !outputs_.healthy()) {
    return false;
  }

  manualCommandActive_ = false;
  clearPhaseCandidate();

  if (outputs_.state().heaterPhases > 0 &&
      !stopHeatingWithPumpOverrun(nowMs)) {
    return false;
  }

  mode_ = mode;
  if (pumpOverrunActive_) {
    state_ = ApplicationState::PumpOverrun;
  } else if (mode_ == OperatingMode::Automatic) {
    state_ = measurementsValid_ ? ApplicationState::Monitoring
                                : ApplicationState::WaitingForData;
  } else {
    if (!setAllOutputsOff()) {
      return false;
    }
    state_ = ApplicationState::Disabled;
  }
  return true;
}

void Application::setSimulatedMeasurements(const int32_t surplusW,
                                            const float temperatureC) {
  surplusW_ = surplusW;
  temperatureC_ = temperatureC;
  measurementsValid_ = true;
}

ApplicationStatus Application::status(const uint32_t nowMs) const {
  return {
      nowMs,
      toString(mode_),
      toString(state_),
      outputs_.state(),
      outputs_.healthy(),
      manualTimeoutRemaining(nowMs),
      pumpOverrunRemaining(nowMs),
      phaseChangeRemaining(nowMs),
      measurementsValid_,
      surplusW_,
      temperatureC_,
  };
}

void Application::updateAutomatic(const uint32_t nowMs) {
  if (!measurementsValid_) {
    clearPhaseCandidate();
    if (outputs_.state().heaterPhases > 0) {
      stopHeatingWithPumpOverrun(nowMs);
    }
    if (!pumpOverrunActive_) {
      state_ = ApplicationState::WaitingForData;
    }
    return;
  }

  if (temperatureC_ >= config::control::kTargetTemperatureC) {
    temperatureLockout_ = true;
  } else if (temperatureLockout_ &&
             temperatureC_ <= config::control::kTargetTemperatureC -
                                  config::control::kTemperatureHysteresisC) {
    temperatureLockout_ = false;
  }

  if (temperatureLockout_) {
    clearPhaseCandidate();
    if (outputs_.state().heaterPhases > 0) {
      stopHeatingWithPumpOverrun(nowMs);
    }
    if (!pumpOverrunActive_) {
      state_ = ApplicationState::TemperatureHold;
    }
    return;
  }

  const uint8_t currentPhases = outputs_.state().heaterPhases;
  const int32_t availablePowerW =
      surplusW_ + currentPhases * config::control::kHeaterPhasePowerW;
  uint8_t candidate = currentPhases;

  if (currentPhases < 3 &&
      availablePowerW >=
          currentPhases * config::control::kHeaterPhasePowerW +
              config::control::kPhaseEnableSurplusW) {
    candidate = currentPhases + 1;
  } else if (currentPhases > 0 &&
             availablePowerW <
                 (currentPhases - 1) * config::control::kHeaterPhasePowerW +
                     config::control::kPhaseDisableSurplusW) {
    candidate = currentPhases - 1;
  }

  if (candidate == currentPhases) {
    clearPhaseCandidate();
  } else if (!phaseCandidateActive_ || phaseCandidate_ != candidate) {
    phaseCandidateActive_ = true;
    phaseCandidate_ = candidate;
    phaseCandidateSinceMs_ = nowMs;
    log_.printf("[control] phase candidate=%u stable_for_ms=%lu\n", candidate,
                static_cast<unsigned long>(
                    config::control::kPhaseChangeStableMs));
  } else if (nowMs - phaseCandidateSinceMs_ >=
             config::control::kPhaseChangeStableMs) {
    if (applyHeaterPhases(candidate, nowMs)) {
      log_.printf("[control] heater phases changed to %u\n", candidate);
    }
    clearPhaseCandidate();
  }

  if (!pumpOverrunActive_) {
    state_ = outputs_.state().heaterPhases > 0 ? ApplicationState::Heating
                                               : ApplicationState::Monitoring;
  }
}

void Application::updateManual(const uint32_t nowMs) {
  if (!manualCommandActive_ ||
      nowMs - lastManualCommandAtMs_ < config::kManualOutputTimeoutMs) {
    return;
  }

  manualCommandActive_ = false;
  mode_ = OperatingMode::Disabled;
  if (outputs_.state().heaterPhases > 0) {
    stopHeatingWithPumpOverrun(nowMs);
    log_.println(
        "[outputs] manual command timed out; heater OFF, pump overrun started");
  } else {
    setAllOutputsOff();
    state_ = outputs_.healthy() ? ApplicationState::Disabled
                                : ApplicationState::Fault;
    log_.println("[outputs] manual pump command timed out; pump is OFF");
  }
}

void Application::updatePumpOverrun(const uint32_t nowMs) {
  if (!pumpOverrunActive_) {
    return;
  }

  if (outputs_.state().heaterPhases > 0) {
    pumpOverrunActive_ = false;
    return;
  }

  state_ = ApplicationState::PumpOverrun;
  if (nowMs - pumpOverrunStartedAtMs_ < config::control::kPumpOverrunMs) {
    return;
  }

  if (!outputs_.set(0, false)) {
    state_ = ApplicationState::Fault;
    mode_ = OperatingMode::Disabled;
    return;
  }

  pumpOverrunActive_ = false;
  if (mode_ == OperatingMode::Automatic) {
    state_ = temperatureLockout_ ? ApplicationState::TemperatureHold
                                 : ApplicationState::Monitoring;
  } else {
    state_ = ApplicationState::Disabled;
  }
  log_.println("[outputs] pump overrun complete; pump is OFF");
}

bool Application::applyHeaterPhases(const uint8_t phases,
                                    const uint32_t nowMs) {
  if (phases > 0) {
    if (!outputs_.set(phases, true)) {
      state_ = ApplicationState::Fault;
      mode_ = OperatingMode::Disabled;
      return false;
    }
    pumpOverrunActive_ = false;
    return true;
  }
  return stopHeatingWithPumpOverrun(nowMs);
}

bool Application::stopHeatingWithPumpOverrun(const uint32_t nowMs) {
  if (!outputs_.set(0, true)) {
    state_ = ApplicationState::Fault;
    mode_ = OperatingMode::Disabled;
    return false;
  }
  startPumpOverrun(nowMs);
  return true;
}

void Application::startPumpOverrun(const uint32_t nowMs) {
  pumpOverrunActive_ = true;
  pumpOverrunStartedAtMs_ = nowMs;
  state_ = ApplicationState::PumpOverrun;
}

void Application::clearPhaseCandidate() {
  phaseCandidateActive_ = false;
}

void Application::printStatus(const uint32_t nowMs) const {
  const ApplicationStatus current = status(nowMs);
  log_.printf(
      "[status] uptime_ms=%lu mode=%s state=%s heater_phases=%u pump=%u "
      "outputs_healthy=%u manual_timeout_ms=%lu pump_overrun_ms=%lu "
      "phase_change_ms=%lu surplus_w=%ld temperature_c=%.1f inputs_valid=%u\n",
      static_cast<unsigned long>(current.uptimeMs), current.mode, current.state,
      current.outputs.heaterPhases, current.outputs.circulationPump,
      current.outputDriverHealthy,
      static_cast<unsigned long>(current.manualTimeoutRemainingMs),
      static_cast<unsigned long>(current.pumpOverrunRemainingMs),
      static_cast<unsigned long>(current.phaseChangeRemainingMs),
      static_cast<long>(current.surplusW), current.temperatureC,
      current.measurementsValid);
}

uint32_t Application::pumpOverrunRemaining(const uint32_t nowMs) const {
  if (!pumpOverrunActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - pumpOverrunStartedAtMs_;
  return elapsed >= config::control::kPumpOverrunMs
             ? 0
             : config::control::kPumpOverrunMs - elapsed;
}

uint32_t Application::phaseChangeRemaining(const uint32_t nowMs) const {
  if (!phaseCandidateActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - phaseCandidateSinceMs_;
  return elapsed >= config::control::kPhaseChangeStableMs
             ? 0
             : config::control::kPhaseChangeStableMs - elapsed;
}

uint32_t Application::manualTimeoutRemaining(const uint32_t nowMs) const {
  if (!manualCommandActive_) {
    return 0;
  }

  const uint32_t elapsed = nowMs - lastManualCommandAtMs_;
  return elapsed >= config::kManualOutputTimeoutMs
             ? 0
             : config::kManualOutputTimeoutMs - elapsed;
}

const char* Application::toString(const OperatingMode mode) {
  switch (mode) {
    case OperatingMode::Disabled:
      return "disabled";
    case OperatingMode::Manual:
      return "manual";
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
    case ApplicationState::ManualControl:
      return "manual_control";
    case ApplicationState::Monitoring:
      return "monitoring";
    case ApplicationState::Heating:
      return "heating";
    case ApplicationState::PumpOverrun:
      return "pump_overrun";
    case ApplicationState::TemperatureHold:
      return "temperature_hold";
    case ApplicationState::WaitingForData:
      return "waiting_for_data";
    case ApplicationState::Fault:
      return "fault";
  }
  return "unknown";
}
