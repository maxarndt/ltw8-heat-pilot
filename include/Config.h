#pragma once

#include <stdint.h>

#include "TemperatureSensorConfiguration.h"

namespace config {

constexpr char kHostname[] = "heat-pilot";

// Development credential for the trusted local network. Change this before
// permanent deployment and keep platformio.ini in sync.
constexpr char kOtaPassword[] = "heat-pilot-dev";

constexpr uint16_t kLogPort = 23;
constexpr uint32_t kManualOutputTimeoutMs = 60000;

namespace diagnostics {
constexpr uint32_t kLoopStallThresholdUs = 100000;
}  // namespace diagnostics

namespace control {
constexpr int32_t kHeaterPhasePowerW = 1625;
constexpr int32_t kPhaseEnableSurplusW = 1700;
constexpr int32_t kPhaseDisableSurplusW = 1300;
constexpr uint32_t kPhaseChangeStableMs = 30000;
constexpr uint32_t kPumpOverrunMs = 45000;
constexpr float kTargetTemperatureC = 80.0F;
constexpr float kTemperatureHysteresisC = 4.0F;
constexpr uint32_t kTemperatureStaleMs = 15000;
constexpr uint32_t kTemperatureFaultDelayMs = 15000;
constexpr uint8_t kTemperatureRecoverySamples = 2;
}  // namespace control

namespace outputs {
constexpr uint8_t kI2cAddress = 0x20;
constexpr int8_t kSdaPin = 42;
constexpr int8_t kSclPin = 41;
}  // namespace outputs

namespace temperature {
constexpr int8_t kOneWirePin = 1;
constexpr uint8_t kResolutionBits = 12;
constexpr uint8_t kMaximumSensors = 8;
constexpr uint32_t kConversionTimeMs = 750;
constexpr uint32_t kMeasurementIntervalMs = 5000;
constexpr uint32_t kConfigurationLogIntervalMs = 10000;

// Replace these development sensors with the IDs and installation labels from
// the buffer tank. Adding or removing entries changes the expected count.
constexpr TemperatureSensorDefinition kSensors[] = {
    {"test_sensor_1", {0x28, 0xF1, 0x9A, 0xBA, 0x00, 0x00, 0x00, 0xDB}},
    {"test_sensor_2", {0x28, 0xA3, 0x91, 0xBA, 0x00, 0x00, 0x00, 0xEB}},
};
constexpr size_t kSensorCount = sizeof(kSensors) / sizeof(kSensors[0]);
static_assert(kSensorCount > 0, "At least one temperature sensor is required");
static_assert(kSensorCount <= kMaximumSensors,
              "Configured temperature sensor count exceeds storage");
}  // namespace temperature

namespace modbus {
constexpr int8_t kReceivePin = 18;
constexpr int8_t kTransmitPin = 17;
constexpr int8_t kDirectionPin = 21;
constexpr uint32_t kBaudRate = 9600;
constexpr uint32_t kFrameGapUs = 4000;
constexpr uint16_t kMaximumFrameLength = 256;
constexpr uint8_t kRecentFrameCount = 4;
constexpr uint32_t kLogIntervalMs = 5000;
constexpr uint32_t kSmartMeterStaleMs = 3000;
constexpr uint32_t kBatteryStaleMs = 12000;
}  // namespace modbus

namespace battery {
constexpr int32_t kDischargeIgnoreThresholdW = 100;
constexpr int32_t kTransientDischargeLimitW = 500;
constexpr uint32_t kTransientDischargeMaximumMs = 15000;
constexpr uint64_t kTransientDischargeBudgetWattMilliseconds =
    2ULL * 3600ULL * 1000ULL;
}  // namespace battery

namespace telemetry {
constexpr char kOtlpMetricsEndpoint[] =
    "http://otel.ltw8.net:4318/v1/metrics";
constexpr char kNtpServer[] = "pool.ntp.org";
constexpr char kServiceName[] = "heat-pilot";
constexpr uint32_t kExportIntervalMs = 15000;
constexpr uint16_t kHttpTimeoutMs = 1000;
constexpr uint32_t kTaskStackBytes = 12288;
constexpr uint8_t kTaskPriority = 1;
constexpr uint8_t kTaskCore = 0;
}  // namespace telemetry

namespace ethernet {
constexpr int8_t kInterruptPin = 12;
constexpr int8_t kMosiPin = 13;
constexpr int8_t kMisoPin = 14;
constexpr int8_t kClockPin = 15;
constexpr int8_t kChipSelectPin = 16;
constexpr int8_t kResetPin = 39;
constexpr int8_t kPhyAddress = 1;
}  // namespace ethernet

}  // namespace config
