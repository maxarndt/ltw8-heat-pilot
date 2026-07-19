#include "ControlEngine.h"

#include "Config.h"

void ControlEngine::begin() {
  mode_ = OperatingMode::Disabled;
  state_ = ApplicationState::Disabled;
  outputs_ = {};
  lastManualCommandAtMs_ = 0;
  manualCommandActive_ = false;
  pumpOverrunActive_ = false;
  pumpOverrunStartedAtMs_ = 0;
  phaseCandidateActive_ = false;
  phaseCandidate_ = 0;
  phaseCandidateSinceMs_ = 0;
  temperatureLockout_ = false;
  measurementsValid_ = false;
  surplusW_ = 0;
  temperatureC_ = 0.0F;
}

void ControlEngine::update(const uint32_t nowMs) {
  if (state_ == ApplicationState::Fault) {
    return;
  }

  if (mode_ == OperatingMode::Automatic) {
    updateAutomatic(nowMs);
  } else if (mode_ == OperatingMode::Manual) {
    updateManual(nowMs);
  }
  updatePumpOverrun(nowMs);
}

bool ControlEngine::setManualOutput(const uint8_t heaterPhases,
                                    const bool pump, const uint32_t nowMs) {
  if (heaterPhases > 3 || (heaterPhases > 0 && !pump) ||
      state_ == ApplicationState::Fault) {
    return false;
  }

  clearPhaseCandidate();
  manualCommandActive_ = false;

  if (heaterPhases > 0) {
    outputs_ = {heaterPhases, true};
    pumpOverrunActive_ = false;
    mode_ = OperatingMode::Manual;
    state_ = ApplicationState::ManualControl;
    lastManualCommandAtMs_ = nowMs;
    manualCommandActive_ = true;
    return true;
  }

  if (outputs_.heaterPhases > 0) {
    mode_ = OperatingMode::Disabled;
    stopHeatingWithPumpOverrun(nowMs);
    return true;
  }

  outputs_ = {0, pump};
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

bool ControlEngine::setOperatingMode(const OperatingMode mode,
                                     const uint32_t nowMs) {
  if (mode == OperatingMode::Manual || state_ == ApplicationState::Fault) {
    return false;
  }

  manualCommandActive_ = false;
  clearPhaseCandidate();
  if (outputs_.heaterPhases > 0) {
    stopHeatingWithPumpOverrun(nowMs);
  }

  mode_ = mode;
  if (pumpOverrunActive_) {
    state_ = ApplicationState::PumpOverrun;
  } else if (mode_ == OperatingMode::Automatic) {
    state_ = measurementsValid_ ? ApplicationState::Monitoring
                                : ApplicationState::WaitingForData;
  } else {
    outputs_ = {};
    state_ = ApplicationState::Disabled;
  }
  return true;
}

void ControlEngine::setMeasurements(const int32_t surplusW,
                                    const float temperatureC) {
  surplusW_ = surplusW;
  temperatureC_ = temperatureC;
  measurementsValid_ = true;
}

void ControlEngine::setFault() {
  mode_ = OperatingMode::Disabled;
  state_ = ApplicationState::Fault;
  outputs_ = {};
  manualCommandActive_ = false;
  pumpOverrunActive_ = false;
  clearPhaseCandidate();
}

ControlSnapshot ControlEngine::snapshot(const uint32_t nowMs) const {
  return {
      mode_,
      state_,
      outputs_,
      manualTimeoutRemaining(nowMs),
      pumpOverrunRemaining(nowMs),
      phaseChangeRemaining(nowMs),
      measurementsValid_,
      surplusW_,
      temperatureC_,
  };
}

void ControlEngine::updateAutomatic(const uint32_t nowMs) {
  if (!measurementsValid_) {
    clearPhaseCandidate();
    if (outputs_.heaterPhases > 0) {
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
    if (outputs_.heaterPhases > 0) {
      stopHeatingWithPumpOverrun(nowMs);
    }
    if (!pumpOverrunActive_) {
      state_ = ApplicationState::TemperatureHold;
    }
    return;
  }

  const uint8_t currentPhases = outputs_.heaterPhases;
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
  } else if (nowMs - phaseCandidateSinceMs_ >=
             config::control::kPhaseChangeStableMs) {
    applyHeaterPhases(candidate, nowMs);
    clearPhaseCandidate();
  }

  if (!pumpOverrunActive_) {
    state_ = outputs_.heaterPhases > 0 ? ApplicationState::Heating
                                       : ApplicationState::Monitoring;
  }
}

void ControlEngine::updateManual(const uint32_t nowMs) {
  if (!manualCommandActive_ ||
      nowMs - lastManualCommandAtMs_ < config::kManualOutputTimeoutMs) {
    return;
  }

  manualCommandActive_ = false;
  mode_ = OperatingMode::Disabled;
  if (outputs_.heaterPhases > 0) {
    stopHeatingWithPumpOverrun(nowMs);
  } else {
    outputs_ = {};
    state_ = ApplicationState::Disabled;
  }
}

void ControlEngine::updatePumpOverrun(const uint32_t nowMs) {
  if (!pumpOverrunActive_) {
    return;
  }
  if (outputs_.heaterPhases > 0) {
    pumpOverrunActive_ = false;
    return;
  }

  state_ = ApplicationState::PumpOverrun;
  if (nowMs - pumpOverrunStartedAtMs_ < config::control::kPumpOverrunMs) {
    return;
  }

  outputs_ = {};
  pumpOverrunActive_ = false;
  if (mode_ == OperatingMode::Automatic) {
    state_ = temperatureLockout_ ? ApplicationState::TemperatureHold
                                 : ApplicationState::Monitoring;
  } else {
    state_ = ApplicationState::Disabled;
  }
}

void ControlEngine::applyHeaterPhases(const uint8_t phases,
                                      const uint32_t nowMs) {
  if (phases > 0) {
    outputs_ = {phases, true};
    pumpOverrunActive_ = false;
  } else {
    stopHeatingWithPumpOverrun(nowMs);
  }
}

void ControlEngine::stopHeatingWithPumpOverrun(const uint32_t nowMs) {
  outputs_ = {0, true};
  startPumpOverrun(nowMs);
}

void ControlEngine::startPumpOverrun(const uint32_t nowMs) {
  pumpOverrunActive_ = true;
  pumpOverrunStartedAtMs_ = nowMs;
  state_ = ApplicationState::PumpOverrun;
}

void ControlEngine::clearPhaseCandidate() {
  phaseCandidateActive_ = false;
}

uint32_t ControlEngine::manualTimeoutRemaining(const uint32_t nowMs) const {
  if (!manualCommandActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - lastManualCommandAtMs_;
  return elapsed >= config::kManualOutputTimeoutMs
             ? 0
             : config::kManualOutputTimeoutMs - elapsed;
}

uint32_t ControlEngine::pumpOverrunRemaining(const uint32_t nowMs) const {
  if (!pumpOverrunActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - pumpOverrunStartedAtMs_;
  return elapsed >= config::control::kPumpOverrunMs
             ? 0
             : config::control::kPumpOverrunMs - elapsed;
}

uint32_t ControlEngine::phaseChangeRemaining(const uint32_t nowMs) const {
  if (!phaseCandidateActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - phaseCandidateSinceMs_;
  return elapsed >= config::control::kPhaseChangeStableMs
             ? 0
             : config::control::kPhaseChangeStableMs - elapsed;
}
