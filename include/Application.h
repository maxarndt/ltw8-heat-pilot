#pragma once

#include <Arduino.h>

#include "ControlEngine.h"
#include "ModbusSniffer.h"
#include "OutputController.h"
#include "TemperatureService.h"

struct ApplicationStatus {
  uint32_t uptimeMs;
  const char* mode;
  const char* state;
  OutputState outputs;
  bool outputDriverHealthy;
  uint32_t manualTimeoutRemainingMs;
  uint32_t pumpOverrunRemainingMs;
  uint32_t phaseChangeRemainingMs;
  bool measurementsValid;
  bool temperatureValid;
  bool temperatureFault;
  int32_t surplusW;
  float temperatureC;
};

class Application {
 public:
  Application(Print& log, OutputController& outputs,
              TemperatureService& temperatures, ModbusSniffer& modbusSniffer)
      : log_(log), outputs_(outputs), temperatures_(temperatures),
        modbusSniffer_(modbusSniffer) {}

  void begin();
  void update(uint32_t nowMs);
  bool setManualOutput(uint8_t heaterPhases, bool pump, uint32_t nowMs);
  bool setOperatingMode(OperatingMode mode, uint32_t nowMs);
  void setSimulatedSurplus(int32_t surplusW);
  void disableSimulatedSurplus(uint32_t nowMs);
  ApplicationStatus status(uint32_t nowMs) const;
  bool simulatedSurplusEnabled() const { return simulatedSurplusEnabled_; }
  uint32_t smartMeterTimeouts() const { return smartMeterTimeouts_; }
  uint32_t batteryTimeouts() const { return batteryTimeouts_; }
  uint8_t temperatureSensorCount() const { return temperatures_.count(); }
  const TemperatureSensorReading& temperatureSensor(uint8_t index) const {
    return temperatures_.reading(index);
  }
  const ModbusSnifferStats& modbusSnifferStats() const {
    return modbusSniffer_.stats();
  }
  const FroniusSmartMeterReading& smartMeterReading() const {
    return modbusSniffer_.smartMeterReading();
  }
  const FroniusBatteryReading& batteryReading() const {
    return modbusSniffer_.batteryReading();
  }
  uint8_t recentModbusFrameCount() const {
    return modbusSniffer_.recentFrameCount();
  }
  const CapturedModbusFrame& recentModbusFrame(uint8_t index) const {
    return modbusSniffer_.recentFrame(index);
  }
  bool hasLastInvalidModbusFrame() const {
    return modbusSniffer_.hasLastInvalidFrame();
  }
  const CapturedModbusFrame& lastInvalidModbusFrame() const {
    return modbusSniffer_.lastInvalidFrame();
  }

 private:
  static constexpr uint32_t kStatusIntervalMs = 2000;

  bool syncOutputs();
  void updateTemperatureMeasurement(uint32_t nowMs);
  void updateSmartMeterMeasurement(uint32_t nowMs);
  void updateBatteryMeasurement(uint32_t nowMs);
  void printStatus(uint32_t nowMs) const;
  static const char* toString(OperatingMode mode);
  static const char* toString(ApplicationState state);

  uint32_t lastStatusAtMs_ = 0;
  uint32_t lastTemperatureMeasurementAtMs_ = 0;
  uint32_t lastSmartMeterMeasurementAtMs_ = 0;
  uint32_t lastBatteryMeasurementAtMs_ = 0;
  uint32_t smartMeterTimeouts_ = 0;
  uint32_t batteryTimeouts_ = 0;
  bool temperatureStaleReported_ = false;
  bool smartMeterStaleReported_ = false;
  bool batteryStaleReported_ = false;
  bool simulatedSurplusEnabled_ = false;
  Print& log_;
  OutputController& outputs_;
  TemperatureService& temperatures_;
  ModbusSniffer& modbusSniffer_;
  ControlEngine control_{};
};
