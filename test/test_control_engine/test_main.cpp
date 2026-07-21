#include <stdint.h>

#include <limits>

#include <unity.h>

#include "ApiValidation.h"
#include "Config.h"
#include "ControlEngine.h"
#include "FroniusBattery.h"
#include "FroniusSmartMeter.h"
#include "HeaterEnergyMeter.h"
#include "ModbusRtu.h"
#include "ModbusRtuFraming.h"
#include "OutputEncoding.h"
#include "RetainedOperationalState.h"
#include "TemperaturePolicy.h"
#include "TemperatureSensorConfiguration.h"

namespace {

void assertOutputs(const ControlEngine& engine, const uint8_t phases,
                   const bool pump) {
  TEST_ASSERT_EQUAL_UINT8(phases, engine.desiredOutputs().heaterPhases);
  TEST_ASSERT_EQUAL(pump, engine.desiredOutputs().circulationPump);
}

ControlEngine automaticEngine(const int32_t surplusW = 0,
                              const float temperatureC = 60.0F) {
  ControlEngine engine;
  engine.begin();
  engine.setSurplusMeasurement(surplusW);
  engine.setBatteryMeasurement(8000, 0);
  engine.setTemperatureMeasurement(temperatureC, true, 0);
  engine.setTemperatureMeasurement(temperatureC, true, 0);
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));
  return engine;
}

void enableTemperature(ControlEngine& engine, const float temperatureC = 60.0F,
                       const uint32_t nowMs = 0) {
  engine.setTemperatureMeasurement(temperatureC, true, nowMs);
  engine.setTemperatureMeasurement(temperatureC, true, nowMs);
}

void test_starts_disabled_and_safe() {
  ControlEngine engine;
  engine.begin();

  const ControlSnapshot status = engine.snapshot(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OperatingMode::Disabled),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::Disabled),
                        static_cast<int>(status.state));
  assertOutputs(engine, 0, false);
}

void test_heater_energy_meter_accumulates_active_phases() {
  HeaterEnergyMeter meter(1625);
  meter.begin(1000);

  meter.update(2000, 1);
  TEST_ASSERT_EQUAL_UINT64(0, meter.wattMilliseconds(2000));
  TEST_ASSERT_EQUAL_UINT64(1625ULL * 1000ULL,
                           meter.wattMilliseconds(3000));

  meter.update(3000, 2);
  TEST_ASSERT_EQUAL_UINT64(1625ULL * 3000ULL,
                           meter.wattMilliseconds(4000));

  meter.update(4000, 0);
  TEST_ASSERT_EQUAL_UINT64(1625ULL * 3000ULL,
                           meter.wattMilliseconds(10000));
}

void test_heater_energy_meter_reports_watt_hours() {
  HeaterEnergyMeter meter(1625);
  meter.begin(0, 3);

  TEST_ASSERT_FLOAT_WITHIN(0.001F, 4875.0F,
                           static_cast<float>(meter.wattHours(3600000)));
}

void test_heater_energy_meter_handles_millis_wraparound() {
  HeaterEnergyMeter meter(1625);
  meter.begin(UINT32_MAX - 999U, 1);

  TEST_ASSERT_EQUAL_UINT64(1625ULL * 1500ULL,
                           meter.wattMilliseconds(500));
}

void test_automatic_waits_for_measurements() {
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));
  engine.update(0);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::WaitingForData),
                        static_cast<int>(engine.snapshot(0).state));
  assertOutputs(engine, 0, false);
}

void test_missing_battery_measurement_stops_heating_safely() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(config::control::kPhaseChangeStableMs);
  assertOutputs(engine, 1, true);

  engine.clearBatteryMeasurement();
  engine.update(config::control::kPhaseChangeStableMs + 1U);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_FALSE(
      engine.snapshot(config::control::kPhaseChangeStableMs + 1U)
          .measurementsValid);
}

ControlEngine twoPhaseEngine() {
  ControlEngine engine = automaticEngine(5000);
  engine.update(0);
  engine.update(30000);
  engine.update(30001);
  engine.update(60001);
  TEST_ASSERT_EQUAL_UINT8(2, engine.desiredOutputs().heaterPhases);
  engine.setSurplusMeasurement(0);
  return engine;
}

void test_small_battery_discharge_is_tolerated_for_15_seconds() {
  ControlEngine engine = twoPhaseEngine();
  engine.setBatteryMeasurement(8000, 400);
  engine.update(60002);
  engine.update(75001);
  assertOutputs(engine, 2, true);
  engine.update(75002);
  assertOutputs(engine, 1, true);
}

void test_battery_idle_power_at_deadband_is_ignored() {
  ControlEngine engine = twoPhaseEngine();
  constexpr uint32_t startedAtMs = 60002;
  engine.setBatteryMeasurement(
      8000, config::battery::kDischargeIgnoreThresholdW);
  engine.update(startedAtMs);
  engine.update(startedAtMs + 60000U);
  assertOutputs(engine, 2, true);

  engine.setBatteryMeasurement(
      8000, config::battery::kDischargeIgnoreThresholdW + 1);
  engine.update(startedAtMs + 60001U);
  engine.update(startedAtMs + 60001U +
                config::battery::kTransientDischargeMaximumMs);
  assertOutputs(engine, 1, true);
}

void test_battery_discharge_energy_budget_reduces_phase() {
  ControlEngine engine = twoPhaseEngine();
  engine.setBatteryMeasurement(8000, 500);
  engine.update(60002);
  engine.update(74402);
  assertOutputs(engine, 1, true);
}

void test_high_battery_discharge_reduces_only_once_per_sample() {
  ControlEngine engine = twoPhaseEngine();
  engine.setBatteryMeasurement(8000, 501);
  engine.update(60002);
  assertOutputs(engine, 1, true);

  engine.update(60003);
  assertOutputs(engine, 1, true);
  engine.setBatteryMeasurement(8000, 501);
  engine.update(60004);
  assertOutputs(engine, 0, true);
}

void test_battery_charging_does_not_reduce_phases() {
  ControlEngine engine = twoPhaseEngine();
  engine.setBatteryMeasurement(8000, -2000);
  engine.update(60002);
  engine.update(90002);
  assertOutputs(engine, 2, true);
}

void test_lost_surplus_measurement_stops_heating_safely() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(config::control::kPhaseChangeStableMs);
  assertOutputs(engine, 1, true);

  engine.clearSurplusMeasurement();
  engine.update(config::control::kPhaseChangeStableMs + 1U);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(
                            engine.snapshot(config::control::kPhaseChangeStableMs +
                                            1U)
                                .state));
  TEST_ASSERT_FALSE(
      engine.snapshot(config::control::kPhaseChangeStableMs + 1U)
          .measurementsValid);
}

void test_phase_requires_stable_surplus_and_forces_pump() {
  ControlEngine engine = automaticEngine(1700);

  engine.update(0);
  engine.update(config::control::kPhaseChangeStableMs - 1);
  assertOutputs(engine, 0, false);

  engine.update(config::control::kPhaseChangeStableMs);
  assertOutputs(engine, 1, true);
}

void test_phases_are_added_one_at_a_time() {
  ControlEngine engine = automaticEngine(5000);
  engine.update(0);
  engine.update(30000);
  assertOutputs(engine, 1, true);

  engine.update(30001);
  engine.update(60000);
  assertOutputs(engine, 1, true);
  engine.update(60001);
  assertOutputs(engine, 2, true);
}

void test_unstable_surplus_restarts_phase_timer() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);

  engine.setSurplusMeasurement(1600);
  engine.update(20000);
  engine.setSurplusMeasurement(1700);
  engine.update(25000);
  engine.update(54999);
  assertOutputs(engine, 0, false);
  engine.update(55000);
  assertOutputs(engine, 1, true);
}

void test_phases_are_removed_one_at_a_time() {
  ControlEngine engine = automaticEngine(5000);
  engine.update(0);
  engine.update(30000);
  engine.update(30001);
  engine.update(60001);
  assertOutputs(engine, 2, true);

  const int32_t belowDisableThreshold =
      config::control::kPhaseDisableSurplusW -
      config::control::kHeaterPhasePowerW - 1;
  engine.setSurplusMeasurement(belowDisableThreshold);
  engine.update(60002);
  engine.update(90002);
  assertOutputs(engine, 1, true);

  engine.update(90003);
  engine.update(120003);
  assertOutputs(engine, 0, true);
}

void test_automatic_never_exceeds_three_phases() {
  ControlEngine engine = automaticEngine(100000);
  engine.update(0);
  engine.update(30000);
  engine.update(30001);
  engine.update(60001);
  engine.update(60002);
  engine.update(90002);
  assertOutputs(engine, 3, true);

  engine.update(90003);
  engine.update(200000);
  assertOutputs(engine, 3, true);
}

void test_disable_threshold_is_strict_and_stable() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(30000);
  assertOutputs(engine, 1, true);

  const int32_t exactDisableThreshold =
      config::control::kPhaseDisableSurplusW -
      config::control::kHeaterPhasePowerW;
  engine.setSurplusMeasurement(exactDisableThreshold);
  engine.update(30001);
  engine.update(70000);
  assertOutputs(engine, 1, true);

  engine.setSurplusMeasurement(exactDisableThreshold - 1);
  engine.update(70001);
  engine.update(100000);
  assertOutputs(engine, 1, true);
  engine.update(100001);
  assertOutputs(engine, 0, true);
}

void test_target_temperature_stops_heater_and_overruns_pump() {
  constexpr uint32_t overrunStartedAtMs = 30001;
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(30000);

  engine.setSurplusMeasurement(200);
  engine.setTemperatureMeasurement(80.0F, true, overrunStartedAtMs);
  engine.update(overrunStartedAtMs);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(engine.snapshot(overrunStartedAtMs).state));

  engine.update(overrunStartedAtMs + config::control::kPumpOverrunMs - 1U);
  assertOutputs(engine, 0, true);
  engine.update(overrunStartedAtMs + config::control::kPumpOverrunMs);
  assertOutputs(engine, 0, false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::TemperatureHold),
                        static_cast<int>(
                            engine
                                .snapshot(overrunStartedAtMs +
                                          config::control::kPumpOverrunMs)
                                .state));
}

void test_temperature_hysteresis_releases_at_76_degrees() {
  ControlEngine engine = automaticEngine(1700, 80.0F);
  engine.update(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::TemperatureHold),
                        static_cast<int>(engine.snapshot(0).state));

  engine.setSurplusMeasurement(1700);
  engine.setTemperatureMeasurement(77.0F, true, 1);
  engine.update(1);
  engine.update(60000);
  assertOutputs(engine, 0, false);

  engine.setSurplusMeasurement(1700);
  engine.setTemperatureMeasurement(76.0F, true, 60001);
  engine.update(60001);
  engine.update(90000);
  assertOutputs(engine, 0, false);
  engine.update(90001);
  assertOutputs(engine, 1, true);
}

void test_manual_interlock_rejects_heater_without_pump() {
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);

  TEST_ASSERT_FALSE(engine.setManualOutput(1, false, 0));
  assertOutputs(engine, 0, false);
}

void test_manual_heater_timeout_starts_pump_overrun() {
  constexpr uint32_t overrunStartedAtMs = 60100;
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);
  TEST_ASSERT_TRUE(engine.setManualOutput(2, true, 100));

  engine.update(60099);
  assertOutputs(engine, 2, true);
  engine.update(overrunStartedAtMs);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OperatingMode::Disabled),
                        static_cast<int>(engine.snapshot(overrunStartedAtMs).mode));

  engine.update(overrunStartedAtMs + config::control::kPumpOverrunMs - 1U);
  assertOutputs(engine, 0, true);
  engine.update(overrunStartedAtMs + config::control::kPumpOverrunMs);
  assertOutputs(engine, 0, false);
}

void test_recovered_pump_overrun_runs_for_full_duration() {
  ControlEngine engine;
  engine.begin();
  engine.recoverPumpOverrun(100);

  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(engine.snapshot(100).state));
  TEST_ASSERT_EQUAL_UINT32(config::control::kPumpOverrunMs,
                           engine.snapshot(100).pumpOverrunRemainingMs);

  engine.update(100 + config::control::kPumpOverrunMs - 1U);
  assertOutputs(engine, 0, true);
  engine.update(100 + config::control::kPumpOverrunMs);
  assertOutputs(engine, 0, false);
}

void test_retained_activity_recovers_only_heating_and_overrun() {
  RetainedOperationalState state{};
  TEST_ASSERT_FALSE(retainedOperationalStateValid(state));
  TEST_ASSERT_FALSE(shouldRecoverPumpOverrun(state));

  writeRetainedActivity(state, RetainedActivity::Idle);
  TEST_ASSERT_TRUE(retainedOperationalStateValid(state));
  TEST_ASSERT_FALSE(shouldRecoverPumpOverrun(state));

  writeRetainedActivity(state, RetainedActivity::Heating);
  TEST_ASSERT_TRUE(shouldRecoverPumpOverrun(state));

  writeRetainedActivity(state, RetainedActivity::PumpOverrun);
  TEST_ASSERT_TRUE(shouldRecoverPumpOverrun(state));
}

void test_corrupted_retained_activity_is_rejected() {
  RetainedOperationalState state{};
  writeRetainedActivity(state, RetainedActivity::Heating);
  state.checksum ^= 1U;
  TEST_ASSERT_FALSE(retainedOperationalStateValid(state));
  TEST_ASSERT_FALSE(shouldRecoverPumpOverrun(state));
}

void test_manual_off_command_starts_pump_overrun() {
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);
  TEST_ASSERT_TRUE(engine.setManualOutput(3, true, 0));

  TEST_ASSERT_TRUE(engine.setManualOutput(0, false, 100));
  assertOutputs(engine, 0, true);
  const ControlSnapshot status = engine.snapshot(100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OperatingMode::Disabled),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(status.state));
  TEST_ASSERT_EQUAL_UINT32(config::control::kPumpOverrunMs,
                           status.pumpOverrunRemainingMs);
}

void test_manual_pump_only_timeout_stops_without_overrun() {
  ControlEngine engine;
  engine.begin();
  TEST_ASSERT_TRUE(engine.setManualOutput(0, true, 0));

  engine.update(config::kManualOutputTimeoutMs);
  assertOutputs(engine, 0, false);
  TEST_ASSERT_EQUAL_UINT32(0,
                           engine.snapshot(config::kManualOutputTimeoutMs)
                               .pumpOverrunRemainingMs);
}

void test_disabling_active_heater_keeps_pump_running() {
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);
  TEST_ASSERT_TRUE(engine.setManualOutput(1, true, 0));
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Disabled, 10));

  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_UINT32(config::control::kPumpOverrunMs,
                           engine.snapshot(10).pumpOverrunRemainingMs);
}

void test_timeouts_work_across_millis_wraparound() {
  ControlEngine engine;
  engine.begin();
  constexpr uint32_t start = UINT32_MAX - 1000U;
  TEST_ASSERT_TRUE(engine.setManualOutput(0, true, start));

  engine.update(static_cast<uint32_t>(start + 59999U));
  assertOutputs(engine, 0, true);
  engine.update(static_cast<uint32_t>(start + 60000U));
  assertOutputs(engine, 0, false);
}

void test_fault_is_safe_and_rejects_commands() {
  ControlEngine engine;
  engine.begin();
  enableTemperature(engine);
  TEST_ASSERT_TRUE(engine.setManualOutput(2, true, 0));

  engine.setFault();
  assertOutputs(engine, 0, false);
  TEST_ASSERT_FALSE(engine.setManualOutput(1, true, 1));
  TEST_ASSERT_FALSE(engine.setOperatingMode(OperatingMode::Automatic, 1));
}

void test_missing_temperature_becomes_temperature_fault() {
  ControlEngine engine;
  engine.begin();
  engine.setSurplusMeasurement(5000);
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));

  engine.update(config::control::kTemperatureFaultDelayMs - 1);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ApplicationState::WaitingForTemperature),
      static_cast<int>(engine.snapshot(
                                  config::control::kTemperatureFaultDelayMs - 1)
                           .state));
  assertOutputs(engine, 0, false);

  engine.update(config::control::kTemperatureFaultDelayMs);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ApplicationState::TemperatureFault),
      static_cast<int>(
          engine.snapshot(config::control::kTemperatureFaultDelayMs).state));
  TEST_ASSERT_TRUE(
      engine.snapshot(config::control::kTemperatureFaultDelayMs)
          .temperatureFault);
}

void test_temperature_fault_requires_two_valid_samples_to_recover() {
  ControlEngine engine;
  engine.begin();
  engine.setSurplusMeasurement(0);
  engine.setBatteryMeasurement(8000, 0);
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));
  engine.update(config::control::kTemperatureFaultDelayMs);

  engine.setTemperatureMeasurement(60.0F, true, 16000);
  engine.update(16000);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ApplicationState::TemperatureFault),
      static_cast<int>(engine.snapshot(16000).state));

  engine.setTemperatureMeasurement(60.0F, true, 17000);
  engine.update(17000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::Monitoring),
                        static_cast<int>(engine.snapshot(17000).state));
  TEST_ASSERT_TRUE(engine.snapshot(17000).temperatureValid);
  TEST_ASSERT_FALSE(engine.snapshot(17000).temperatureFault);
}

void test_temperature_loss_stops_heater_and_overruns_pump() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(config::control::kPhaseChangeStableMs);
  assertOutputs(engine, 1, true);

  engine.setTemperatureMeasurement(-127.0F, false, 30001);
  engine.update(30001);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(engine.snapshot(30001).state));
  TEST_ASSERT_FALSE(engine.snapshot(30001).temperatureValid);
}

void test_manual_heater_requires_valid_temperature() {
  ControlEngine engine;
  engine.begin();

  TEST_ASSERT_FALSE(engine.setManualOutput(1, true, 0));
  TEST_ASSERT_TRUE(engine.setManualOutput(0, true, 0));
  assertOutputs(engine, 0, true);
}

void test_temperature_staleness_boundary_and_millis_wraparound() {
  TEST_ASSERT_FALSE(isTemperatureMeasurementStale(
      config::control::kTemperatureStaleMs, 0));
  TEST_ASSERT_TRUE(isTemperatureMeasurementStale(
      config::control::kTemperatureStaleMs + 1, 0));

  constexpr uint32_t measuredAt = UINT32_MAX - 1000U;
  TEST_ASSERT_FALSE(isTemperatureMeasurementStale(
      static_cast<uint32_t>(measuredAt + config::control::kTemperatureStaleMs),
      measuredAt));
  TEST_ASSERT_TRUE(isTemperatureMeasurementStale(
      static_cast<uint32_t>(measuredAt +
                            config::control::kTemperatureStaleMs + 1U),
      measuredAt));
}

void test_active_low_output_encoding() {
  TEST_ASSERT_EQUAL_HEX8(0xFF, encodeOutputLevels({0, false}));
  TEST_ASSERT_EQUAL_HEX8(0xFE, encodeOutputLevels({1, false}));
  TEST_ASSERT_EQUAL_HEX8(0xF6, encodeOutputLevels({1, true}));
  TEST_ASSERT_EQUAL_HEX8(0xFC, encodeOutputLevels({2, false}));
  TEST_ASSERT_EQUAL_HEX8(0xF0, encodeOutputLevels({3, true}));
}

void test_manual_api_validation() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ManualOutputValidation::InvalidPhaseCount),
      static_cast<int>(validateManualOutput(-1, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ManualOutputValidation::InvalidPhaseCount),
      static_cast<int>(validateManualOutput(4, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ManualOutputValidation::PumpRequired),
                        static_cast<int>(validateManualOutput(1, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ManualOutputValidation::Ok),
                        static_cast<int>(validateManualOutput(0, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ManualOutputValidation::Ok),
                        static_cast<int>(validateManualOutput(3, true)));
}

void test_temperature_api_validation() {
  TEST_ASSERT_TRUE(isValidTemperature(-55.0F));
  TEST_ASSERT_TRUE(isValidTemperature(125.0F));
  TEST_ASSERT_FALSE(isValidTemperature(-55.1F));
  TEST_ASSERT_FALSE(isValidTemperature(125.1F));
  TEST_ASSERT_FALSE(
      isValidTemperature(std::numeric_limits<float>::quiet_NaN()));
  TEST_ASSERT_FALSE(
      isValidTemperature(std::numeric_limits<float>::infinity()));
  TEST_ASSERT_FALSE(
      isValidTemperature(-std::numeric_limits<float>::infinity()));
}

void test_temperature_sensor_configuration_matches_dynamic_lists() {
  const TemperatureSensorDefinition definitions[] = {
      {"bottom", {0x28, 1, 2, 3, 4, 5, 6, 7}},
      {"top", {0x28, 8, 9, 10, 11, 12, 13, 14}},
  };
  const uint8_t detected[][8] = {
      {0x28, 8, 9, 10, 11, 12, 13, 14},
      {0x28, 1, 2, 3, 4, 5, 6, 7},
  };
  const TemperatureSensorConfigurationMatch match = matchTemperatureSensors(
      &detected[0][0], 2, definitions, 2);
  TEST_ASSERT_TRUE(match.valid());
  TEST_ASSERT_EQUAL_INT(1, findTemperatureSensorDefinition(
                               detected[0], definitions, 2));
}

void test_temperature_sensor_configuration_rejects_missing_and_unknown() {
  const TemperatureSensorDefinition definitions[] = {
      {"bottom", {0x28, 1, 2, 3, 4, 5, 6, 7}},
      {"top", {0x28, 8, 9, 10, 11, 12, 13, 14}},
  };
  const uint8_t detected[][8] = {
      {0x28, 1, 2, 3, 4, 5, 6, 7},
      {0x28, 20, 21, 22, 23, 24, 25, 26},
  };
  const TemperatureSensorConfigurationMatch match = matchTemperatureSensors(
      &detected[0][0], 2, definitions, 2);
  TEST_ASSERT_FALSE(match.valid());
  TEST_ASSERT_EQUAL_UINT32(1, match.missing);
  TEST_ASSERT_EQUAL_UINT32(1, match.unknown);
}

void test_temperature_sensor_configuration_rejects_duplicate_ids() {
  const TemperatureSensorDefinition definitions[] = {
      {"bottom", {0x28, 1, 2, 3, 4, 5, 6, 7}},
      {"top", {0x28, 1, 2, 3, 4, 5, 6, 7}},
  };
  const uint8_t detected[][8] = {
      {0x28, 1, 2, 3, 4, 5, 6, 7},
  };
  const TemperatureSensorConfigurationMatch match = matchTemperatureSensors(
      &detected[0][0], 1, definitions, 2);
  TEST_ASSERT_FALSE(match.valid());
  TEST_ASSERT_TRUE(match.duplicateConfiguredAddress);
}

void test_modbus_crc_accepts_official_request_and_response_examples() {
  const uint8_t request[] = {0x01, 0x03, 0x9C, 0x44,
                             0x00, 0x04, 0x2A, 0x4C};
  const uint8_t response[] = {0x01, 0x03, 0x08, 0x46, 0x72,
                              0x6F, 0x6E, 0x69, 0x75, 0x73,
                              0x00, 0x8A, 0x2A};

  TEST_ASSERT_EQUAL_HEX16(0x4C2A, modbusRtuCrc16(request, 6));
  TEST_ASSERT_TRUE(hasValidModbusRtuCrc(request, sizeof(request)));
  TEST_ASSERT_TRUE(hasValidModbusRtuCrc(response, sizeof(response)));
}

void test_modbus_crc_rejects_corruption_and_short_frames() {
  uint8_t frame[] = {0x01, 0x03, 0x9C, 0x44,
                     0x00, 0x04, 0x2A, 0x4C};
  frame[3] ^= 0x01;

  TEST_ASSERT_FALSE(hasValidModbusRtuCrc(frame, sizeof(frame)));
  TEST_ASSERT_FALSE(hasValidModbusRtuCrc(frame, 3));
}

void test_modbus_framing_splits_concatenated_read_request_and_response() {
  const uint8_t frames[] = {
      0x01, 0x03, 0x04, 0x00, 0x00, 0x10, 0x45, 0x36,
      0x01, 0x03, 0x20, 0x00, 0x23, 0x00, 0x00, 0x03, 0x53, 0x00,
      0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x4F, 0x00, 0x00, 0x0C,
      0xF2, 0x00, 0x00, 0x02, 0x17, 0x00, 0x00, 0x04, 0x59, 0x00,
      0x00, 0x03, 0x8F, 0x00, 0x00, 0xFB, 0x26};

  TEST_ASSERT_EQUAL_UINT32(8,
      completeModbusRtuFrameLength(frames, sizeof(frames)));
  TEST_ASSERT_EQUAL_UINT32(37,
      completeModbusRtuFrameLength(frames + 8, sizeof(frames) - 8));
}

void test_modbus_framing_recognizes_write_request_and_response() {
  const uint8_t request[] = {0x15, 0x10, 0x01, 0xF5, 0x00, 0x01,
                             0x02, 0x00, 0xFF, 0x1D, 0x75};
  const uint8_t response[] = {0x15, 0x10, 0x01, 0xF5,
                              0x00, 0x01, 0x13, 0x13};
  const uint8_t exception[] = {0x15, 0x83, 0x03, 0x41, 0x35};

  TEST_ASSERT_EQUAL_UINT32(sizeof(request),
      completeModbusRtuFrameLength(request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(response),
      completeModbusRtuFrameLength(response, sizeof(response)));
  TEST_ASSERT_EQUAL_UINT32(sizeof(exception),
      completeModbusRtuFrameLength(exception, sizeof(exception)));
}

void test_modbus_framing_waits_for_complete_or_valid_crc() {
  uint8_t request[] = {0x01, 0x03, 0x01, 0x02,
                       0x00, 0x10, 0xE4, 0x3A};
  TEST_ASSERT_EQUAL_UINT32(0,
      completeModbusRtuFrameLength(request, sizeof(request) - 1U));
  request[4] ^= 0x01;
  TEST_ASSERT_EQUAL_UINT32(0,
      completeModbusRtuFrameLength(request, sizeof(request)));
}

void test_fronius_smart_meter_decodes_captured_summary() {
  const uint8_t request[] = {0x01, 0x03, 0x01, 0x02,
                             0x00, 0x10, 0xE4, 0x3A};
  const uint8_t response[] = {
      0x01, 0x03, 0x20, 0x09, 0x28, 0x00, 0x00, 0x0F, 0xDC, 0x00,
      0x00, 0xF9, 0x1C, 0xFF, 0xFF, 0x76, 0x19, 0x00, 0x00, 0xEA,
      0xFA, 0xFF, 0xFF, 0xFF, 0xCA, 0xFF, 0xFF, 0x00, 0x00, 0x00,
      0x00, 0x01, 0xF4, 0x00, 0x00, 0xF5, 0xD0};

  FroniusSmartMeterDecoder decoder;
  TEST_ASSERT_FALSE(decoder.processFrame(request, sizeof(request), 100));
  TEST_ASSERT_TRUE(decoder.processFrame(response, sizeof(response), 110));

  const FroniusSmartMeterReading& reading = decoder.reading();
  TEST_ASSERT_TRUE(reading.summaryValid);
  TEST_ASSERT_EQUAL_UINT32(110, reading.summaryMeasuredAtMs);
  TEST_ASSERT_EQUAL_INT32(2344, reading.voltageDecivolts);
  TEST_ASSERT_EQUAL_INT32(4060, reading.voltagePhaseToPhaseDecivolts);
  TEST_ASSERT_EQUAL_INT32(-1764, reading.realPowerDeciwatts);
  TEST_ASSERT_EQUAL_INT32(500, reading.frequencyDecihertz);
}

void test_fronius_smart_meter_requires_matching_request_and_valid_crc() {
  const uint8_t request[] = {0x01, 0x03, 0x01, 0x02,
                             0x00, 0x10, 0xE4, 0x3A};
  uint8_t response[] = {
      0x01, 0x03, 0x20, 0x09, 0x28, 0x00, 0x00, 0x0F, 0xDC, 0x00,
      0x00, 0xF9, 0x1C, 0xFF, 0xFF, 0x76, 0x19, 0x00, 0x00, 0xEA,
      0xFA, 0xFF, 0xFF, 0xFF, 0xCA, 0xFF, 0xFF, 0x00, 0x00, 0x00,
      0x00, 0x01, 0xF4, 0x00, 0x00, 0xF5, 0xD0};

  FroniusSmartMeterDecoder decoder;
  TEST_ASSERT_FALSE(decoder.processFrame(response, sizeof(response), 100));
  TEST_ASSERT_FALSE(decoder.reading().summaryValid);
  TEST_ASSERT_FALSE(decoder.processFrame(request, sizeof(request), 101));
  response[5] ^= 0x01;
  TEST_ASSERT_FALSE(decoder.processFrame(response, sizeof(response), 102));
  TEST_ASSERT_FALSE(decoder.reading().summaryValid);
}

void test_fronius_smart_meter_decodes_phase_values() {
  const uint8_t request[] = {0x01, 0x03, 0x01, 0x1E,
                             0x00, 0x2A, 0xA5, 0xEF};
  uint8_t response[89]{};
  response[0] = 0x01;
  response[1] = 0x03;
  response[2] = 0x54;
  const uint8_t phaseOne[] = {
      0x0F, 0xE3, 0x00, 0x00, 0x09, 0x2B, 0x00, 0x00,
      0xF2, 0xC1, 0xFF, 0xFF, 0xE2, 0x42, 0xFF, 0xFF,
      0x1E, 0xD8, 0x00, 0x00, 0xF7, 0xD6, 0xFF, 0xFF,
      0xFC, 0x48, 0xFF, 0xFF};
  for (size_t index = 0; index < sizeof(phaseOne); ++index) {
    response[3 + index] = phaseOne[index];
  }
  const uint16_t crc = modbusRtuCrc16(response, sizeof(response) - 2U);
  response[sizeof(response) - 2U] = static_cast<uint8_t>(crc & 0xFFU);
  response[sizeof(response) - 1U] = static_cast<uint8_t>(crc >> 8U);

  FroniusSmartMeterDecoder decoder;
  TEST_ASSERT_FALSE(decoder.processFrame(request, sizeof(request), 200));
  TEST_ASSERT_TRUE(decoder.processFrame(response, sizeof(response), 210));

  const FroniusSmartMeterReading& reading = decoder.reading();
  TEST_ASSERT_TRUE(reading.phasesValid);
  TEST_ASSERT_EQUAL_UINT32(210, reading.phasesMeasuredAtMs);
  TEST_ASSERT_EQUAL_INT32(4067,
                          reading.phases[0].voltagePhaseToPhaseDecivolts);
  TEST_ASSERT_EQUAL_INT32(2347, reading.phases[0].voltageDecivolts);
  TEST_ASSERT_EQUAL_INT32(-3391, reading.phases[0].currentMilliamps);
  TEST_ASSERT_EQUAL_INT32(-7614, reading.phases[0].realPowerDeciwatts);
}

void test_fronius_smart_meter_freshness_and_surplus_conversion() {
  FroniusSmartMeterReading reading;
  reading.summaryValid = true;
  reading.summaryMeasuredAtMs = 100;

  TEST_ASSERT_TRUE(isFroniusSmartMeterSummaryFresh(reading, 3100, 3000));
  TEST_ASSERT_FALSE(isFroniusSmartMeterSummaryFresh(reading, 3101, 3000));
  reading.summaryMeasuredAtMs = UINT32_MAX - 100U;
  TEST_ASSERT_TRUE(isFroniusSmartMeterSummaryFresh(reading, 99, 200));
  TEST_ASSERT_FALSE(isFroniusSmartMeterSummaryFresh(reading, 100, 200));

  TEST_ASSERT_EQUAL_INT32(214, froniusGridPowerToSurplusWatts(-2137));
  TEST_ASSERT_EQUAL_INT32(-12, froniusGridPowerToSurplusWatts(117));
  TEST_ASSERT_EQUAL_INT32(0, froniusGridPowerToSurplusWatts(0));
}

void test_fronius_battery_decodes_captured_register_layout() {
  const uint8_t request[] = {0x15, 0x03, 0x01, 0x90,
                             0x00, 0x1E, 0xC7, 0x07};
  uint8_t response[65]{};
  response[0] = 0x15;
  response[1] = 0x03;
  response[2] = 0x3C;
  const uint16_t registers[] = {
      3, 0, 129, 8210, 0, 7675, 0, 6300, 0, 7200,
      0, 7700, 3250, 0xFFFF, 0xF72A, 3246, 0xFFFF, 0xF72D};
  for (size_t index = 0; index < sizeof(registers) / sizeof(registers[0]);
       ++index) {
    response[3 + index * 2U] = static_cast<uint8_t>(registers[index] >> 8U);
    response[4 + index * 2U] = static_cast<uint8_t>(registers[index]);
  }
  const uint16_t crc = modbusRtuCrc16(response, sizeof(response) - 2U);
  response[sizeof(response) - 2U] = static_cast<uint8_t>(crc);
  response[sizeof(response) - 1U] = static_cast<uint8_t>(crc >> 8U);

  FroniusBatteryDecoder decoder;
  TEST_ASSERT_FALSE(decoder.processFrame(request, sizeof(request), 100));
  TEST_ASSERT_TRUE(decoder.processFrame(response, sizeof(response), 110));
  const FroniusBatteryReading& reading = decoder.reading();
  TEST_ASSERT_TRUE(reading.valid);
  TEST_ASSERT_EQUAL_UINT16(8210, reading.stateOfChargeHundredths);
  TEST_ASSERT_EQUAL_UINT32(7675, reading.totalCapacityWh);
  TEST_ASSERT_EQUAL_INT32(-2262, reading.powerW);
  TEST_ASSERT_EQUAL_INT32(-2259, reading.internalPowerW);
  TEST_ASSERT_EQUAL_UINT16(3250, reading.voltageDecivolts);
}

void test_fronius_battery_freshness_handles_wraparound() {
  FroniusBatteryReading reading;
  reading.valid = true;
  reading.measuredAtMs = UINT32_MAX - 100U;
  TEST_ASSERT_TRUE(isFroniusBatteryFresh(reading, 99, 200));
  TEST_ASSERT_FALSE(isFroniusBatteryFresh(reading, 100, 200));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_heater_energy_meter_accumulates_active_phases);
  RUN_TEST(test_heater_energy_meter_reports_watt_hours);
  RUN_TEST(test_heater_energy_meter_handles_millis_wraparound);
  RUN_TEST(test_starts_disabled_and_safe);
  RUN_TEST(test_automatic_waits_for_measurements);
  RUN_TEST(test_missing_battery_measurement_stops_heating_safely);
  RUN_TEST(test_small_battery_discharge_is_tolerated_for_15_seconds);
  RUN_TEST(test_battery_idle_power_at_deadband_is_ignored);
  RUN_TEST(test_battery_discharge_energy_budget_reduces_phase);
  RUN_TEST(test_high_battery_discharge_reduces_only_once_per_sample);
  RUN_TEST(test_battery_charging_does_not_reduce_phases);
  RUN_TEST(test_lost_surplus_measurement_stops_heating_safely);
  RUN_TEST(test_phase_requires_stable_surplus_and_forces_pump);
  RUN_TEST(test_phases_are_added_one_at_a_time);
  RUN_TEST(test_unstable_surplus_restarts_phase_timer);
  RUN_TEST(test_phases_are_removed_one_at_a_time);
  RUN_TEST(test_automatic_never_exceeds_three_phases);
  RUN_TEST(test_disable_threshold_is_strict_and_stable);
  RUN_TEST(test_target_temperature_stops_heater_and_overruns_pump);
  RUN_TEST(test_temperature_hysteresis_releases_at_76_degrees);
  RUN_TEST(test_manual_interlock_rejects_heater_without_pump);
  RUN_TEST(test_manual_heater_timeout_starts_pump_overrun);
  RUN_TEST(test_recovered_pump_overrun_runs_for_full_duration);
  RUN_TEST(test_retained_activity_recovers_only_heating_and_overrun);
  RUN_TEST(test_corrupted_retained_activity_is_rejected);
  RUN_TEST(test_manual_off_command_starts_pump_overrun);
  RUN_TEST(test_manual_pump_only_timeout_stops_without_overrun);
  RUN_TEST(test_disabling_active_heater_keeps_pump_running);
  RUN_TEST(test_timeouts_work_across_millis_wraparound);
  RUN_TEST(test_fault_is_safe_and_rejects_commands);
  RUN_TEST(test_missing_temperature_becomes_temperature_fault);
  RUN_TEST(test_temperature_fault_requires_two_valid_samples_to_recover);
  RUN_TEST(test_temperature_loss_stops_heater_and_overruns_pump);
  RUN_TEST(test_manual_heater_requires_valid_temperature);
  RUN_TEST(test_temperature_staleness_boundary_and_millis_wraparound);
  RUN_TEST(test_active_low_output_encoding);
  RUN_TEST(test_manual_api_validation);
  RUN_TEST(test_temperature_api_validation);
  RUN_TEST(test_temperature_sensor_configuration_matches_dynamic_lists);
  RUN_TEST(test_temperature_sensor_configuration_rejects_missing_and_unknown);
  RUN_TEST(test_temperature_sensor_configuration_rejects_duplicate_ids);
  RUN_TEST(test_modbus_crc_accepts_official_request_and_response_examples);
  RUN_TEST(test_modbus_crc_rejects_corruption_and_short_frames);
  RUN_TEST(test_modbus_framing_splits_concatenated_read_request_and_response);
  RUN_TEST(test_modbus_framing_recognizes_write_request_and_response);
  RUN_TEST(test_modbus_framing_waits_for_complete_or_valid_crc);
  RUN_TEST(test_fronius_smart_meter_decodes_captured_summary);
  RUN_TEST(test_fronius_smart_meter_requires_matching_request_and_valid_crc);
  RUN_TEST(test_fronius_smart_meter_decodes_phase_values);
  RUN_TEST(test_fronius_smart_meter_freshness_and_surplus_conversion);
  RUN_TEST(test_fronius_battery_decodes_captured_register_layout);
  RUN_TEST(test_fronius_battery_freshness_handles_wraparound);
  return UNITY_END();
}
