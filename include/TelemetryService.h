#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Application;

struct TelemetryStatus {
  bool started = false;
  bool timeSynchronized = false;
  uint32_t exportSuccesses = 0;
  uint32_t exportFailures = 0;
  int lastHttpStatus = 0;
  uint32_t lastAttemptAtMs = 0;
  uint32_t lastSuccessAtMs = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minimumFreeHeapBytes = 0;
  uint32_t maximumLoopDurationUs = 0;
  uint32_t lastIntervalMaximumLoopDurationUs = 0;
};

class TelemetryService {
 public:
  TelemetryService(Application& application, Print& log)
      : application_(application), log_(log) {}

  void update(bool networkOnline, uint32_t nowMs);
  void observeLoopDuration(uint32_t durationUs);
  TelemetryStatus status() const;

 private:
  struct Snapshot {
    uint64_t observedAtUnixNano = 0;
    uint64_t startedAtUnixNano = 0;
    uint32_t uptimeMs = 0;
    uint32_t freeHeapBytes = 0;
    uint32_t minimumFreeHeapBytes = 0;
    uint32_t intervalMaximumLoopDurationUs = 0;
    uint32_t exportSuccesses = 0;
    uint32_t exportFailures = 0;
    uint32_t modbusCrcErrors = 0;
    uint32_t modbusOverflows = 0;
    uint32_t smartMeterTimeouts = 0;
    uint32_t batteryTimeouts = 0;
    int32_t surplusW = 0;
    int32_t batteryPowerW = 0;
    double estimatedHeaterEnergyWh = 0.0;
    float controlTemperatureC = 0.0F;
    float batteryStateOfChargePercent = 0.0F;
    uint8_t heaterPhases = 0;
    bool pump = false;
    bool measurementsValid = false;
    bool temperatureValid = false;
    bool batteryValid = false;
    bool surplusValid = false;
    bool outputDriverHealthy = false;
  };

  void begin(uint32_t nowMs);
  bool captureSnapshot(uint32_t nowMs, Snapshot& snapshot);
  static void taskEntry(void* context);
  void taskLoop();
  bool exportSnapshot(const Snapshot& snapshot, int& httpStatus);
  String buildPayload(const Snapshot& snapshot) const;
  void reportTaskResult(bool success, int httpStatus, uint32_t nowMs);
  void logNewResult();

  Application& application_;
  Print& log_;
  QueueHandle_t queue_ = nullptr;
  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
  TelemetryStatus status_{};
  uint64_t startedAtUnixNano_ = 0;
  uint32_t lastQueuedAtMs_ = 0;
  uint32_t maximumLoopDurationUs_ = 0;
  uint32_t intervalMaximumLoopDurationUs_ = 0;
  uint32_t resultSequence_ = 0;
  uint32_t loggedResultSequence_ = 0;
  bool beginAttempted_ = false;
  bool loggedFirstSuccess_ = false;
  char instanceId_[18]{};
};
