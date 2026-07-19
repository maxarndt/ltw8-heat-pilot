#include <stdint.h>

#include <limits>

#include <unity.h>

#include "ApiValidation.h"
#include "Config.h"
#include "ControlEngine.h"
#include "OutputEncoding.h"

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
  engine.setMeasurements(surplusW, temperatureC);
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));
  return engine;
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

void test_automatic_waits_for_measurements() {
  ControlEngine engine;
  engine.begin();
  TEST_ASSERT_TRUE(engine.setOperatingMode(OperatingMode::Automatic, 0));
  engine.update(0);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::WaitingForData),
                        static_cast<int>(engine.snapshot(0).state));
  assertOutputs(engine, 0, false);
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

  engine.setMeasurements(1600, 60.0F);
  engine.update(20000);
  engine.setMeasurements(1700, 60.0F);
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

  engine.setMeasurements(-201, 60.0F);
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

  engine.setMeasurements(-200, 60.0F);  // available power is exactly 1300 W
  engine.update(30001);
  engine.update(70000);
  assertOutputs(engine, 1, true);

  engine.setMeasurements(-201, 60.0F);
  engine.update(70001);
  engine.update(100000);
  assertOutputs(engine, 1, true);
  engine.update(100001);
  assertOutputs(engine, 0, true);
}

void test_target_temperature_stops_heater_and_overruns_pump() {
  ControlEngine engine = automaticEngine(1700);
  engine.update(0);
  engine.update(30000);

  engine.setMeasurements(200, 80.0F);
  engine.update(30001);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::PumpOverrun),
                        static_cast<int>(engine.snapshot(30001).state));

  engine.update(120000);
  assertOutputs(engine, 0, true);
  engine.update(120001);
  assertOutputs(engine, 0, false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::TemperatureHold),
                        static_cast<int>(engine.snapshot(120001).state));
}

void test_temperature_hysteresis_releases_at_76_degrees() {
  ControlEngine engine = automaticEngine(1700, 80.0F);
  engine.update(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplicationState::TemperatureHold),
                        static_cast<int>(engine.snapshot(0).state));

  engine.setMeasurements(1700, 77.0F);
  engine.update(1);
  engine.update(60000);
  assertOutputs(engine, 0, false);

  engine.setMeasurements(1700, 76.0F);
  engine.update(60001);
  engine.update(90000);
  assertOutputs(engine, 0, false);
  engine.update(90001);
  assertOutputs(engine, 1, true);
}

void test_manual_interlock_rejects_heater_without_pump() {
  ControlEngine engine;
  engine.begin();

  TEST_ASSERT_FALSE(engine.setManualOutput(1, false, 0));
  assertOutputs(engine, 0, false);
}

void test_manual_heater_timeout_starts_pump_overrun() {
  ControlEngine engine;
  engine.begin();
  TEST_ASSERT_TRUE(engine.setManualOutput(2, true, 100));

  engine.update(60099);
  assertOutputs(engine, 2, true);
  engine.update(60100);
  assertOutputs(engine, 0, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OperatingMode::Disabled),
                        static_cast<int>(engine.snapshot(60100).mode));

  engine.update(150099);
  assertOutputs(engine, 0, true);
  engine.update(150100);
  assertOutputs(engine, 0, false);
}

void test_manual_off_command_starts_pump_overrun() {
  ControlEngine engine;
  engine.begin();
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
  TEST_ASSERT_TRUE(engine.setManualOutput(2, true, 0));

  engine.setFault();
  assertOutputs(engine, 0, false);
  TEST_ASSERT_FALSE(engine.setManualOutput(1, true, 1));
  TEST_ASSERT_FALSE(engine.setOperatingMode(OperatingMode::Automatic, 1));
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

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_disabled_and_safe);
  RUN_TEST(test_automatic_waits_for_measurements);
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
  RUN_TEST(test_manual_off_command_starts_pump_overrun);
  RUN_TEST(test_manual_pump_only_timeout_stops_without_overrun);
  RUN_TEST(test_disabling_active_heater_keeps_pump_running);
  RUN_TEST(test_timeouts_work_across_millis_wraparound);
  RUN_TEST(test_fault_is_safe_and_rejects_commands);
  RUN_TEST(test_active_low_output_encoding);
  RUN_TEST(test_manual_api_validation);
  RUN_TEST(test_temperature_api_validation);
  return UNITY_END();
}
