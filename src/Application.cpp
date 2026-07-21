#include "Application.h"

#include "TemperaturePolicy.h"

void Application::begin(const bool recoverPumpOverrun) {
  const uint32_t nowMs = millis();
  control_.begin();
  if (recoverPumpOverrun) {
    control_.recoverPumpOverrun(nowMs);
  }
  heaterEnergyMeter_.begin(nowMs);
  if (!outputs_.begin() || !syncOutputs(nowMs)) {
    control_.setFault();
    log_.println("[outputs] failed to initialize TCA9554; state=fault");
  } else {
    if (recoverPumpOverrun) {
      pumpOverrunRecoveredAfterReset_ = true;
      log_.println(
          "[outputs] interrupted heating detected; pump overrun recovered");
    } else {
      log_.println("[outputs] TCA9554 ready; all outputs are OFF");
    }
  }

  temperatures_.begin();
  modbusSniffer_.begin();
  control_.setTemperatureMeasurement(0.0F, false, millis());

  log_.println();
  log_.println("LTW8 Heat Pilot");
  log_.printf("Firmware started; heater phases are OFF; pump is %s.\n",
              outputs_.state().circulationPump ? "ON" : "OFF");
  printStatus(millis());
}

void Application::update(const uint32_t nowMs) {
  heaterEnergyMeter_.update(nowMs, outputs_.state().heaterPhases);
  modbusSniffer_.update(nowMs);
  temperatures_.update(nowMs);
  updateTemperatureMeasurement(nowMs);
  updateSmartMeterMeasurement(nowMs);
  updateBatteryMeasurement(nowMs);
  const OutputState before = control_.desiredOutputs();
  control_.update(nowMs);
  if (control_.desiredOutputs() != before && !syncOutputs(nowMs)) {
    control_.setFault();
  }

  if (nowMs - lastStatusAtMs_ >= kStatusIntervalMs) {
    lastStatusAtMs_ = nowMs;
    printStatus(nowMs);
  }
}

bool Application::setManualOutput(const uint8_t heaterPhases, const bool pump,
                                  const uint32_t nowMs) {
  if (!outputs_.healthy() ||
      !control_.setManualOutput(heaterPhases, pump, nowMs)) {
    return false;
  }
  if (!syncOutputs(nowMs)) {
    control_.setFault();
    return false;
  }
  return true;
}

bool Application::setOperatingMode(const OperatingMode mode,
                                   const uint32_t nowMs) {
  if (!outputs_.healthy() || !control_.setOperatingMode(mode, nowMs)) {
    return false;
  }
  if (!syncOutputs(nowMs)) {
    control_.setFault();
    return false;
  }
  return true;
}

void Application::setSimulatedSurplus(const int32_t surplusW) {
  simulatedSurplusEnabled_ = true;
  control_.setSurplusMeasurement(surplusW);
  control_.setBatteryMeasurement(10000, 0);
}

void Application::disableSimulatedSurplus(const uint32_t nowMs) {
  simulatedSurplusEnabled_ = false;
  lastSmartMeterMeasurementAtMs_ = 0;
  smartMeterStaleReported_ = false;
  updateSmartMeterMeasurement(nowMs);
  lastBatteryMeasurementAtMs_ = 0;
  batteryStaleReported_ = false;
  updateBatteryMeasurement(nowMs);
}

ApplicationStatus Application::status(const uint32_t nowMs) const {
  const ControlSnapshot snapshot = control_.snapshot(nowMs);
  return {
      nowMs,
      toString(snapshot.mode),
      toString(snapshot.state),
      snapshot.outputs,
      outputs_.healthy(),
      snapshot.manualTimeoutRemainingMs,
      snapshot.pumpOverrunRemainingMs,
      snapshot.phaseChangeRemainingMs,
      snapshot.measurementsValid,
      snapshot.temperatureValid,
      snapshot.temperatureFault,
      snapshot.surplusW,
      snapshot.temperatureC,
      heaterEnergyMeter_.wattHours(nowMs),
      pumpOverrunRecoveredAfterReset_,
  };
}

void Application::updateTemperatureMeasurement(const uint32_t nowMs) {
  const uint8_t count = temperatures_.count();
  if (count == 0) {
    return;
  }

  const uint32_t measuredAtMs = temperatures_.reading(0).measuredAtMs;
  if (measuredAtMs == 0) {
    return;
  }
  if (measuredAtMs == lastTemperatureMeasurementAtMs_) {
    if (!temperatureStaleReported_ &&
        isTemperatureMeasurementStale(nowMs, measuredAtMs)) {
      control_.setTemperatureMeasurement(0.0F, false, measuredAtMs);
      temperatureStaleReported_ = true;
    }
    return;
  }

  bool allValid = true;
  float highestTemperatureC = -55.0F;
  for (uint8_t index = 0; index < count; ++index) {
    const TemperatureSensorReading& reading = temperatures_.reading(index);
    if (!reading.valid || reading.measuredAtMs != measuredAtMs) {
      allValid = false;
      continue;
    }
    if (reading.temperatureC > highestTemperatureC) {
      highestTemperatureC = reading.temperatureC;
    }
  }

  lastTemperatureMeasurementAtMs_ = measuredAtMs;
  temperatureStaleReported_ = false;
  control_.setTemperatureMeasurement(highestTemperatureC, allValid,
                                     measuredAtMs);
}

void Application::updateSmartMeterMeasurement(const uint32_t nowMs) {
  if (simulatedSurplusEnabled_) {
    return;
  }

  const FroniusSmartMeterReading& meter = modbusSniffer_.smartMeterReading();
  if (!meter.summaryValid) {
    control_.clearSurplusMeasurement();
    return;
  }

  if (!isFroniusSmartMeterSummaryFresh(
          meter, nowMs, config::modbus::kSmartMeterStaleMs)) {
    control_.clearSurplusMeasurement();
    if (!smartMeterStaleReported_) {
      smartMeterStaleReported_ = true;
      ++smartMeterTimeouts_;
      log_.println("[smart-meter] measurement timed out; surplus invalid");
    }
    return;
  }

  if (meter.summaryMeasuredAtMs != lastSmartMeterMeasurementAtMs_) {
    lastSmartMeterMeasurementAtMs_ = meter.summaryMeasuredAtMs;
    smartMeterStaleReported_ = false;
    control_.setSurplusMeasurement(
        froniusGridPowerToSurplusWatts(meter.realPowerDeciwatts));
  }
}

void Application::updateBatteryMeasurement(const uint32_t nowMs) {
  if (simulatedSurplusEnabled_) {
    return;
  }

  const FroniusBatteryReading& battery = modbusSniffer_.batteryReading();
  if (!battery.valid ||
      !isFroniusBatteryFresh(battery, nowMs,
                             config::modbus::kBatteryStaleMs)) {
    control_.clearBatteryMeasurement();
    if (battery.valid && !batteryStaleReported_) {
      batteryStaleReported_ = true;
      ++batteryTimeouts_;
      log_.println("[battery] measurement timed out; battery data invalid");
    }
    return;
  }

  if (battery.measuredAtMs != lastBatteryMeasurementAtMs_) {
    lastBatteryMeasurementAtMs_ = battery.measuredAtMs;
    batteryStaleReported_ = false;
    control_.setBatteryMeasurement(battery.stateOfChargeHundredths,
                                   battery.powerW);
  }
}

bool Application::syncOutputs(const uint32_t nowMs) {
  heaterEnergyMeter_.update(nowMs, outputs_.state().heaterPhases);
  const OutputState& desired = control_.desiredOutputs();
  if (outputs_.state() == desired) {
    persistAppliedOutputState(nowMs);
    return true;
  }
  // Record the safety-relevant intent before energizing a heater output. If a
  // reset occurs in the tiny interval after the I2C write, recovery must still
  // run the pump. A false-positive pump overrun is safer than missing one.
  if (desired.heaterPhases > 0) {
    writeRetainedActivity(retainedOperationalState_, RetainedActivity::Heating);
  }
  const bool updated =
      outputs_.set(desired.heaterPhases, desired.circulationPump);
  heaterEnergyMeter_.update(nowMs, outputs_.state().heaterPhases);
  if (updated) {
    persistAppliedOutputState(nowMs);
  }
  return updated;
}

void Application::persistAppliedOutputState(const uint32_t nowMs) {
  const OutputState& applied = outputs_.state();
  RetainedActivity activity = RetainedActivity::Idle;
  if (applied.heaterPhases > 0) {
    activity = RetainedActivity::Heating;
  } else if (applied.circulationPump &&
             control_.snapshot(nowMs).pumpOverrunRemainingMs > 0) {
    activity = RetainedActivity::PumpOverrun;
  }
  writeRetainedActivity(retainedOperationalState_, activity);
}

void Application::printStatus(const uint32_t nowMs) const {
  const ApplicationStatus current = status(nowMs);
  log_.printf(
      "[status] uptime_ms=%lu mode=%s state=%s heater_phases=%u pump=%u "
      "outputs_healthy=%u manual_timeout_ms=%lu pump_overrun_ms=%lu "
      "phase_change_ms=%lu surplus_w=%ld temperature_c=%.1f inputs_valid=%u "
      "temperature_valid=%u temperature_fault=%u heater_energy_wh=%.3f "
      "surplus_source=%s\n",
      static_cast<unsigned long>(current.uptimeMs), current.mode, current.state,
      current.outputs.heaterPhases, current.outputs.circulationPump,
      current.outputDriverHealthy,
      static_cast<unsigned long>(current.manualTimeoutRemainingMs),
      static_cast<unsigned long>(current.pumpOverrunRemainingMs),
      static_cast<unsigned long>(current.phaseChangeRemainingMs),
      static_cast<long>(current.surplusW), current.temperatureC,
      current.measurementsValid, current.temperatureValid,
      current.temperatureFault, current.estimatedHeaterEnergyWh,
      simulatedSurplusEnabled_
          ? "simulation"
          : (isFroniusSmartMeterSummaryFresh(
                 modbusSniffer_.smartMeterReading(), nowMs,
                 config::modbus::kSmartMeterStaleMs)
                 ? "smart_meter"
                 : "unavailable"));
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
    case ApplicationState::WaitingForTemperature:
      return "waiting_for_temperature";
    case ApplicationState::TemperatureFault:
      return "temperature_fault";
    case ApplicationState::Fault:
      return "fault";
  }
  return "unknown";
}
