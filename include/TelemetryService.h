#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Application;

enum class MainLoopStage : uint8_t {
  Idle,
  Setup,
  Network,
  Application,
  Http,
  Telemetry,
};

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
  uint32_t maximumNetworkDurationUs = 0;
  uint32_t maximumApplicationDurationUs = 0;
  uint32_t maximumHttpDurationUs = 0;
  uint32_t maximumTelemetryDurationUs = 0;
  uint32_t loopStalls = 0;
  uint32_t lastLoopStallAtMs = 0;
  uint32_t lastLoopStallDurationUs = 0;
  const char* lastLoopStallStage = "none";
  const char* previousResetReason = "unknown";
  const char* previousResetStage = "unknown";
};

class TelemetryService {
 public:
  TelemetryService(Application& application, Print& log)
      : application_(application), log_(log) {}

  void update(bool networkOnline, uint32_t nowMs);
  void setResetDiagnostics(const char* reason, MainLoopStage stage);
  void observeLoopDurations(uint32_t nowMs, uint32_t totalUs,
                            uint32_t networkUs, uint32_t applicationUs,
                            uint32_t httpUs, uint32_t telemetryUs);
  TelemetryStatus status() const;
  static const char* loopStageName(MainLoopStage stage);

 private:
  struct Snapshot {
    uint64_t observedAtUnixNano = 0;
    uint64_t startedAtUnixNano = 0;
    uint32_t uptimeMs = 0;
    uint32_t freeHeapBytes = 0;
    uint32_t minimumFreeHeapBytes = 0;
    uint32_t intervalMaximumLoopDurationUs = 0;
    uint32_t intervalMaximumNetworkDurationUs = 0;
    uint32_t intervalMaximumApplicationDurationUs = 0;
    uint32_t intervalMaximumHttpDurationUs = 0;
    uint32_t intervalMaximumTelemetryDurationUs = 0;
    uint32_t loopStalls = 0;
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
  uint32_t maximumNetworkDurationUs_ = 0;
  uint32_t maximumApplicationDurationUs_ = 0;
  uint32_t maximumHttpDurationUs_ = 0;
  uint32_t maximumTelemetryDurationUs_ = 0;
  uint32_t intervalMaximumNetworkDurationUs_ = 0;
  uint32_t intervalMaximumApplicationDurationUs_ = 0;
  uint32_t intervalMaximumHttpDurationUs_ = 0;
  uint32_t intervalMaximumTelemetryDurationUs_ = 0;
  uint32_t loopStalls_ = 0;
  uint32_t lastLoopStallAtMs_ = 0;
  uint32_t lastLoopStallDurationUs_ = 0;
  MainLoopStage lastLoopStallStage_ = MainLoopStage::Idle;
  const char* previousResetReason_ = "unknown";
  MainLoopStage previousResetStage_ = MainLoopStage::Idle;
  uint32_t resultSequence_ = 0;
  uint32_t loggedResultSequence_ = 0;
  bool beginAttempted_ = false;
  bool loggedFirstSuccess_ = false;
  char instanceId_[18]{};
};
