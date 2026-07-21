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
  surplusValid_ = false;
  batteryValid_ = false;
  temperatureValid_ = false;
  temperatureFault_ = false;
  temperatureSampleSeen_ = false;
  temperatureUnavailableTimerActive_ = false;
  consecutiveValidTemperatureSamples_ = 0;
  lastTemperatureSampleAtMs_ = 0;
  temperatureUnavailableSinceMs_ = 0;
  surplusW_ = 0;
  batteryStateOfChargeHundredths_ = 0;
  batteryPowerW_ = 0;
  batteryProtectionWaitingForNewSample_ = false;
  resetBatteryDischargeTracking();
  temperatureC_ = 0.0F;
}

void ControlEngine::update(const uint32_t nowMs) {
  if (state_ == ApplicationState::Fault) {
    return;
  }

  updateTemperatureAvailability(nowMs);

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
      (heaterPhases > 0 && !temperatureUsable(nowMs)) ||
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
    if (!temperatureUnavailableTimerActive_ &&
        !temperatureUsable(nowMs)) {
      temperatureUnavailableTimerActive_ = true;
      temperatureUnavailableSinceMs_ = nowMs;
    }
    state_ = automaticIdleState(nowMs);
  } else {
    outputs_ = {};
    state_ = ApplicationState::Disabled;
  }
  return true;
}

void ControlEngine::setSurplusMeasurement(const int32_t surplusW) {
  surplusW_ = surplusW;
  surplusValid_ = true;
}

void ControlEngine::clearSurplusMeasurement() { surplusValid_ = false; }

void ControlEngine::setBatteryMeasurement(
    const uint16_t stateOfChargeHundredths, const int32_t powerW) {
  batteryStateOfChargeHundredths_ = stateOfChargeHundredths;
  batteryPowerW_ = powerW;
  batteryValid_ = true;
  batteryProtectionWaitingForNewSample_ = false;
}

void ControlEngine::clearBatteryMeasurement() {
  batteryValid_ = false;
  batteryProtectionWaitingForNewSample_ = false;
  resetBatteryDischargeTracking();
}

void ControlEngine::setTemperatureMeasurement(const float temperatureC,
                                              const bool valid,
                                              const uint32_t nowMs) {
  temperatureSampleSeen_ = true;
  lastTemperatureSampleAtMs_ = nowMs;

  if (!valid) {
    consecutiveValidTemperatureSamples_ = 0;
    temperatureValid_ = false;
    if (!temperatureUnavailableTimerActive_) {
      temperatureUnavailableTimerActive_ = true;
      temperatureUnavailableSinceMs_ = nowMs;
    }
    return;
  }

  temperatureC_ = temperatureC;
  if (consecutiveValidTemperatureSamples_ <
      config::control::kTemperatureRecoverySamples) {
    ++consecutiveValidTemperatureSamples_;
  }
  if (consecutiveValidTemperatureSamples_ >=
      config::control::kTemperatureRecoverySamples) {
    temperatureValid_ = true;
    temperatureFault_ = false;
    temperatureUnavailableTimerActive_ = false;
  }
}

void ControlEngine::recoverPumpOverrun(const uint32_t nowMs) {
  mode_ = OperatingMode::Disabled;
  outputs_ = {0, true};
  manualCommandActive_ = false;
  clearPhaseCandidate();
  startPumpOverrun(nowMs);
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
      surplusValid_ && batteryValid_ && temperatureUsable(nowMs),
      temperatureUsable(nowMs),
      temperatureFault_,
      surplusW_,
      temperatureC_,
  };
}

void ControlEngine::updateAutomatic(const uint32_t nowMs) {
  if (!temperatureUsable(nowMs)) {
    clearPhaseCandidate();
    if (outputs_.heaterPhases > 0) {
      stopHeatingWithPumpOverrun(nowMs);
    }
    if (!pumpOverrunActive_) {
      state_ = temperatureUnavailableState();
    }
    return;
  }

  if (!surplusValid_) {
    clearPhaseCandidate();
    if (outputs_.heaterPhases > 0) {
      stopHeatingWithPumpOverrun(nowMs);
    }
    if (!pumpOverrunActive_) {
      state_ = ApplicationState::WaitingForData;
    }
    return;
  }

  if (!batteryValid_) {
    clearPhaseCandidate();
    resetBatteryDischargeTracking();
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

  if (updateBatteryDischargeProtection(nowMs)) {
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
  if (outputs_.heaterPhases > 0 && !temperatureUsable(nowMs)) {
    manualCommandActive_ = false;
    mode_ = OperatingMode::Disabled;
    stopHeatingWithPumpOverrun(nowMs);
    return;
  }

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
    state_ = automaticIdleState(nowMs);
  } else {
    state_ = ApplicationState::Disabled;
  }
}

bool ControlEngine::updateBatteryDischargeProtection(const uint32_t nowMs) {
  if (outputs_.heaterPhases == 0 ||
      batteryPowerW_ <= config::battery::kDischargeIgnoreThresholdW) {
    resetBatteryDischargeTracking();
    return false;
  }
  if (batteryProtectionWaitingForNewSample_) {
    return false;
  }

  bool limitExceeded =
      batteryPowerW_ > config::battery::kTransientDischargeLimitW;
  if (!batteryDischargeTrackingActive_) {
    batteryDischargeTrackingActive_ = true;
    batteryDischargeStartedAtMs_ = nowMs;
    batteryDischargeLastIntegratedAtMs_ = nowMs;
    batteryDischargeWattMilliseconds_ = 0;
  } else {
    const uint32_t elapsedMs = nowMs - batteryDischargeLastIntegratedAtMs_;
    batteryDischargeLastIntegratedAtMs_ = nowMs;
    batteryDischargeWattMilliseconds_ +=
        static_cast<uint64_t>(batteryPowerW_) * elapsedMs;
  }

  limitExceeded =
      limitExceeded ||
      nowMs - batteryDischargeStartedAtMs_ >=
          config::battery::kTransientDischargeMaximumMs ||
      batteryDischargeWattMilliseconds_ >=
          config::battery::kTransientDischargeBudgetWattMilliseconds;
  if (!limitExceeded) {
    return false;
  }

  clearPhaseCandidate();
  const uint8_t reducedPhases = outputs_.heaterPhases - 1U;
  applyHeaterPhases(reducedPhases, nowMs);
  resetBatteryDischargeTracking();
  batteryProtectionWaitingForNewSample_ = true;
  return true;
}

void ControlEngine::resetBatteryDischargeTracking() {
  batteryDischargeTrackingActive_ = false;
  batteryDischargeStartedAtMs_ = 0;
  batteryDischargeLastIntegratedAtMs_ = 0;
  batteryDischargeWattMilliseconds_ = 0;
}

void ControlEngine::applyHeaterPhases(const uint8_t phases,
                                      const uint32_t nowMs) {
  resetBatteryDischargeTracking();
  batteryProtectionWaitingForNewSample_ = false;
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

void ControlEngine::updateTemperatureAvailability(const uint32_t nowMs) {
  if (!temperatureValid_ && temperatureUnavailableTimerActive_ &&
      nowMs - temperatureUnavailableSinceMs_ >=
          config::control::kTemperatureFaultDelayMs) {
    temperatureFault_ = true;
  }
}

bool ControlEngine::temperatureUsable(const uint32_t nowMs) const {
  (void)nowMs;
  return temperatureValid_ && temperatureSampleSeen_;
}

ApplicationState ControlEngine::temperatureUnavailableState() const {
  return temperatureFault_ ? ApplicationState::TemperatureFault
                           : ApplicationState::WaitingForTemperature;
}

ApplicationState ControlEngine::automaticIdleState(
    const uint32_t nowMs) const {
  if (!temperatureUsable(nowMs)) {
    return temperatureUnavailableState();
  }
  if (!surplusValid_) {
    return ApplicationState::WaitingForData;
  }
  if (!batteryValid_) {
    return ApplicationState::WaitingForData;
  }
  return temperatureLockout_ ? ApplicationState::TemperatureHold
                             : ApplicationState::Monitoring;
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
