#include "TelemetryService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <sys/time.h>
#include <time.h>

#include "Application.h"
#include "Config.h"

namespace {
constexpr time_t kMinimumValidUnixTime = 1704067200;  // 2024-01-01 UTC

JsonObject addMetric(JsonArray metrics, const char* name, const char* unit,
                     const char* description) {
  JsonObject metric = metrics.add<JsonObject>();
  metric["name"] = name;
  metric["unit"] = unit;
  metric["description"] = description;
  return metric;
}

void addGauge(JsonArray metrics, const char* name, const char* unit,
              const char* description, const char* timeUnixNano,
              const double value) {
  JsonObject metric = addMetric(metrics, name, unit, description);
  JsonObject point =
      metric["gauge"]["dataPoints"].to<JsonArray>().add<JsonObject>();
  point["timeUnixNano"] = timeUnixNano;
  point["asDouble"] = value;
}

void addSum(JsonArray metrics, const char* name, const char* unit,
            const char* description, const char* startTimeUnixNano,
            const char* timeUnixNano, const double value) {
  JsonObject metric = addMetric(metrics, name, unit, description);
  JsonObject sum = metric["sum"].to<JsonObject>();
  sum["aggregationTemporality"] = 2;  // Cumulative
  sum["isMonotonic"] = true;
  JsonObject point = sum["dataPoints"].to<JsonArray>().add<JsonObject>();
  point["startTimeUnixNano"] = startTimeUnixNano;
  point["timeUnixNano"] = timeUnixNano;
  point["asDouble"] = value;
}

void addResourceAttribute(JsonArray attributes, const char* key,
                          const char* value) {
  JsonObject attribute = attributes.add<JsonObject>();
  attribute["key"] = key;
  attribute["value"]["stringValue"] = value;
}
}  // namespace

void TelemetryService::update(const bool networkOnline,
                              const uint32_t nowMs) {
  if (networkOnline && !beginAttempted_) {
    beginAttempted_ = true;
    begin(nowMs);
  }
  logNewResult();
  if (!networkOnline || !status_.started) {
    return;
  }

  Snapshot snapshot;
  if (!captureSnapshot(nowMs, snapshot)) {
    return;
  }
  xQueueOverwrite(queue_, &snapshot);
}

void TelemetryService::setResetDiagnostics(const char* reason,
                                           const MainLoopStage stage) {
  previousResetReason_ = reason;
  previousResetStage_ = stage;
}

void TelemetryService::observeLoopDurations(
    const uint32_t nowMs, const uint32_t totalUs, const uint32_t networkUs,
    const uint32_t applicationUs, const uint32_t httpUs,
    const uint32_t telemetryUs) {
  maximumLoopDurationUs_ = max(maximumLoopDurationUs_, totalUs);
  intervalMaximumLoopDurationUs_ =
      max(intervalMaximumLoopDurationUs_, totalUs);
  maximumNetworkDurationUs_ = max(maximumNetworkDurationUs_, networkUs);
  maximumApplicationDurationUs_ =
      max(maximumApplicationDurationUs_, applicationUs);
  maximumHttpDurationUs_ = max(maximumHttpDurationUs_, httpUs);
  maximumTelemetryDurationUs_ =
      max(maximumTelemetryDurationUs_, telemetryUs);
  intervalMaximumNetworkDurationUs_ =
      max(intervalMaximumNetworkDurationUs_, networkUs);
  intervalMaximumApplicationDurationUs_ =
      max(intervalMaximumApplicationDurationUs_, applicationUs);
  intervalMaximumHttpDurationUs_ =
      max(intervalMaximumHttpDurationUs_, httpUs);
  intervalMaximumTelemetryDurationUs_ =
      max(intervalMaximumTelemetryDurationUs_, telemetryUs);

  if (totalUs < config::diagnostics::kLoopStallThresholdUs) {
    return;
  }
  ++loopStalls_;
  lastLoopStallAtMs_ = nowMs;
  lastLoopStallDurationUs_ = totalUs;
  lastLoopStallStage_ = MainLoopStage::Network;
  uint32_t longestStageUs = networkUs;
  if (applicationUs > longestStageUs) {
    longestStageUs = applicationUs;
    lastLoopStallStage_ = MainLoopStage::Application;
  }
  if (httpUs > longestStageUs) {
    longestStageUs = httpUs;
    lastLoopStallStage_ = MainLoopStage::Http;
  }
  if (telemetryUs > longestStageUs) {
    lastLoopStallStage_ = MainLoopStage::Telemetry;
  }
}

TelemetryStatus TelemetryService::status() const {
  portENTER_CRITICAL(&statusMux_);
  TelemetryStatus copy = status_;
  portEXIT_CRITICAL(&statusMux_);
  copy.freeHeapBytes = ESP.getFreeHeap();
  copy.minimumFreeHeapBytes = ESP.getMinFreeHeap();
  copy.maximumLoopDurationUs = maximumLoopDurationUs_;
  copy.maximumNetworkDurationUs = maximumNetworkDurationUs_;
  copy.maximumApplicationDurationUs = maximumApplicationDurationUs_;
  copy.maximumHttpDurationUs = maximumHttpDurationUs_;
  copy.maximumTelemetryDurationUs = maximumTelemetryDurationUs_;
  copy.loopStalls = loopStalls_;
  copy.lastLoopStallAtMs = lastLoopStallAtMs_;
  copy.lastLoopStallDurationUs = lastLoopStallDurationUs_;
  copy.lastLoopStallStage = loopStageName(lastLoopStallStage_);
  copy.previousResetReason = previousResetReason_;
  copy.previousResetStage = loopStageName(previousResetStage_);
  return copy;
}

const char* TelemetryService::loopStageName(const MainLoopStage stage) {
  switch (stage) {
    case MainLoopStage::Idle:
      return "idle";
    case MainLoopStage::Setup:
      return "setup";
    case MainLoopStage::Network:
      return "network";
    case MainLoopStage::Application:
      return "application";
    case MainLoopStage::Http:
      return "http";
    case MainLoopStage::Telemetry:
      return "telemetry";
  }
  return "unknown";
}

void TelemetryService::begin(const uint32_t nowMs) {
  queue_ = xQueueCreate(1, sizeof(Snapshot));
  if (queue_ == nullptr) {
    log_.println("[telemetry] failed to allocate export queue");
    return;
  }

  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(instanceId_, sizeof(instanceId_), "%04X%08X",
           static_cast<unsigned int>(chipId >> 32U),
           static_cast<unsigned int>(chipId));
  configTime(0, 0, config::telemetry::kNtpServer);
  lastQueuedAtMs_ = nowMs - config::telemetry::kExportIntervalMs;

  if (xTaskCreatePinnedToCore(taskEntry, "otlp-export",
                              config::telemetry::kTaskStackBytes, this,
                              config::telemetry::kTaskPriority, nullptr,
                              config::telemetry::kTaskCore) != pdPASS) {
    vQueueDelete(queue_);
    queue_ = nullptr;
    log_.println("[telemetry] failed to start export task");
    return;
  }

  portENTER_CRITICAL(&statusMux_);
  status_.started = true;
  portEXIT_CRITICAL(&statusMux_);
  log_.printf("[telemetry] NTP server=%s OTLP endpoint=%s\n",
              config::telemetry::kNtpServer,
              config::telemetry::kOtlpMetricsEndpoint);
}

bool TelemetryService::captureSnapshot(const uint32_t nowMs,
                                       Snapshot& snapshot) {
  timeval currentTime{};
  gettimeofday(&currentTime, nullptr);
  if (currentTime.tv_sec < kMinimumValidUnixTime) {
    return false;
  }

  const uint64_t observedAtUnixNano =
      static_cast<uint64_t>(currentTime.tv_sec) * 1000000000ULL +
      static_cast<uint64_t>(currentTime.tv_usec) * 1000ULL;
  if (startedAtUnixNano_ == 0) {
    startedAtUnixNano_ =
        observedAtUnixNano - static_cast<uint64_t>(nowMs) * 1000000ULL;
    portENTER_CRITICAL(&statusMux_);
    status_.timeSynchronized = true;
    portEXIT_CRITICAL(&statusMux_);
    log_.println("[telemetry] NTP time synchronized");
  }

  if (nowMs - lastQueuedAtMs_ < config::telemetry::kExportIntervalMs) {
    return false;
  }
  lastQueuedAtMs_ = nowMs;

  const ApplicationStatus applicationStatus = application_.status(nowMs);
  const FroniusBatteryReading& battery = application_.batteryReading();
  const ModbusSnifferStats& modbus = application_.modbusSnifferStats();
  const TelemetryStatus telemetryStatus = status();

  snapshot.observedAtUnixNano = observedAtUnixNano;
  snapshot.startedAtUnixNano = startedAtUnixNano_;
  snapshot.uptimeMs = nowMs;
  snapshot.freeHeapBytes = telemetryStatus.freeHeapBytes;
  snapshot.minimumFreeHeapBytes = telemetryStatus.minimumFreeHeapBytes;
  snapshot.intervalMaximumLoopDurationUs = intervalMaximumLoopDurationUs_;
  snapshot.intervalMaximumNetworkDurationUs =
      intervalMaximumNetworkDurationUs_;
  snapshot.intervalMaximumApplicationDurationUs =
      intervalMaximumApplicationDurationUs_;
  snapshot.intervalMaximumHttpDurationUs = intervalMaximumHttpDurationUs_;
  snapshot.intervalMaximumTelemetryDurationUs =
      intervalMaximumTelemetryDurationUs_;
  snapshot.loopStalls = loopStalls_;
  snapshot.exportSuccesses = telemetryStatus.exportSuccesses;
  snapshot.exportFailures = telemetryStatus.exportFailures;
  snapshot.modbusCrcErrors = modbus.crcErrors;
  snapshot.modbusOverflows = modbus.overflows;
  snapshot.smartMeterTimeouts = application_.smartMeterTimeouts();
  snapshot.batteryTimeouts = application_.batteryTimeouts();
  snapshot.surplusW = applicationStatus.surplusW;
  snapshot.batteryPowerW = battery.powerW;
  snapshot.estimatedHeaterEnergyWh =
      applicationStatus.estimatedHeaterEnergyWh;
  snapshot.controlTemperatureC = applicationStatus.temperatureC;
  snapshot.batteryStateOfChargePercent =
      battery.stateOfChargeHundredths / 100.0F;
  snapshot.heaterPhases = applicationStatus.outputs.heaterPhases;
  snapshot.pump = applicationStatus.outputs.circulationPump;
  snapshot.measurementsValid = applicationStatus.measurementsValid;
  snapshot.temperatureValid = applicationStatus.temperatureValid;
  snapshot.batteryValid = isFroniusBatteryFresh(
      battery, nowMs, config::modbus::kBatteryStaleMs);
  snapshot.surplusValid =
      application_.simulatedSurplusEnabled() ||
      isFroniusSmartMeterSummaryFresh(application_.smartMeterReading(), nowMs,
                                       config::modbus::kSmartMeterStaleMs);
  snapshot.outputDriverHealthy = applicationStatus.outputDriverHealthy;

  portENTER_CRITICAL(&statusMux_);
  status_.lastIntervalMaximumLoopDurationUs =
      intervalMaximumLoopDurationUs_;
  portEXIT_CRITICAL(&statusMux_);
  intervalMaximumLoopDurationUs_ = 0;
  intervalMaximumNetworkDurationUs_ = 0;
  intervalMaximumApplicationDurationUs_ = 0;
  intervalMaximumHttpDurationUs_ = 0;
  intervalMaximumTelemetryDurationUs_ = 0;
  return true;
}

void TelemetryService::taskEntry(void* context) {
  static_cast<TelemetryService*>(context)->taskLoop();
}

void TelemetryService::taskLoop() {
  Snapshot snapshot;
  while (true) {
    if (xQueueReceive(queue_, &snapshot, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    int httpStatus = 0;
    const bool success = exportSnapshot(snapshot, httpStatus);
    reportTaskResult(success, httpStatus, millis());
  }
}

bool TelemetryService::exportSnapshot(const Snapshot& snapshot,
                                      int& httpStatus) {
  String payload = buildPayload(snapshot);
  NetworkClient client;
  HTTPClient http;
  http.setConnectTimeout(config::telemetry::kHttpTimeoutMs);
  http.setTimeout(config::telemetry::kHttpTimeoutMs);
  if (!http.begin(client, config::telemetry::kOtlpMetricsEndpoint)) {
    httpStatus = -1;
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  httpStatus =
      http.POST(reinterpret_cast<uint8_t*>(payload.begin()), payload.length());
  http.end();
  return httpStatus == HTTP_CODE_OK;
}

String TelemetryService::buildPayload(const Snapshot& snapshot) const {
  char observedAt[24];
  char startedAt[24];
  snprintf(observedAt, sizeof(observedAt), "%llu",
           static_cast<unsigned long long>(snapshot.observedAtUnixNano));
  snprintf(startedAt, sizeof(startedAt), "%llu",
           static_cast<unsigned long long>(snapshot.startedAtUnixNano));

  JsonDocument document;
  JsonObject resourceMetrics =
      document["resourceMetrics"].to<JsonArray>().add<JsonObject>();
  JsonArray attributes =
      resourceMetrics["resource"]["attributes"].to<JsonArray>();
  addResourceAttribute(attributes, "service.name",
                       config::telemetry::kServiceName);
  addResourceAttribute(attributes, "service.instance.id", instanceId_);
  addResourceAttribute(attributes, "device.type", "ESP32-S3");

  JsonObject scopeMetrics =
      resourceMetrics["scopeMetrics"].to<JsonArray>().add<JsonObject>();
  scopeMetrics["scope"]["name"] = "ltw8.heat-pilot";
  JsonArray metrics = scopeMetrics["metrics"].to<JsonArray>();

  addGauge(metrics, "heat_pilot.up", "1", "Device heartbeat", observedAt, 1);
  addGauge(metrics, "heat_pilot.uptime", "s", "Seconds since startup",
           observedAt, snapshot.uptimeMs / 1000.0);
  addGauge(metrics, "heat_pilot.measurements.valid", "1",
           "All measurements required for automatic control are valid",
           observedAt, snapshot.measurementsValid ? 1 : 0);
  addGauge(metrics, "heat_pilot.outputs.healthy", "1",
           "Output driver health", observedAt,
           snapshot.outputDriverHealthy ? 1 : 0);
  addGauge(metrics, "heat_pilot.heater.active_phases", "1",
           "Active heater phases", observedAt, snapshot.heaterPhases);
  addGauge(metrics, "heat_pilot.heater.estimated_power", "W",
           "Estimated heater power", observedAt,
           snapshot.heaterPhases * config::control::kHeaterPhasePowerW);
  addSum(metrics, "heat_pilot.heater.estimated_energy", "Wh",
         "Estimated heater energy since startup", startedAt, observedAt,
         snapshot.estimatedHeaterEnergyWh);
  addGauge(metrics, "heat_pilot.pump.active", "1",
           "Circulation pump output", observedAt, snapshot.pump ? 1 : 0);
  if (snapshot.surplusValid) {
    addGauge(metrics, "heat_pilot.pv.surplus", "W",
             "Observed PV surplus", observedAt, snapshot.surplusW);
  }
  if (snapshot.temperatureValid) {
    addGauge(metrics, "heat_pilot.temperature.control", "Cel",
             "Highest valid control temperature", observedAt,
             snapshot.controlTemperatureC);
  }
  if (snapshot.batteryValid) {
    addGauge(metrics, "heat_pilot.battery.state_of_charge", "%",
             "Battery state of charge", observedAt,
             snapshot.batteryStateOfChargePercent);
    addGauge(metrics, "heat_pilot.battery.power", "W",
             "Battery discharge power", observedAt, snapshot.batteryPowerW);
  }
  addGauge(metrics, "heat_pilot.memory.heap.free", "By", "Free heap",
           observedAt, snapshot.freeHeapBytes);
  addGauge(metrics, "heat_pilot.memory.heap.minimum_free", "By",
           "Minimum free heap since startup", observedAt,
           snapshot.minimumFreeHeapBytes);
  addGauge(metrics, "heat_pilot.loop.duration.max", "us",
           "Maximum main-loop duration in the export interval", observedAt,
           snapshot.intervalMaximumLoopDurationUs);
  addGauge(metrics, "heat_pilot.loop.network.duration.max", "us",
           "Maximum network-service duration in the export interval",
           observedAt, snapshot.intervalMaximumNetworkDurationUs);
  addGauge(metrics, "heat_pilot.loop.application.duration.max", "us",
           "Maximum application duration in the export interval", observedAt,
           snapshot.intervalMaximumApplicationDurationUs);
  addGauge(metrics, "heat_pilot.loop.http.duration.max", "us",
           "Maximum HTTP-server duration in the export interval", observedAt,
           snapshot.intervalMaximumHttpDurationUs);
  addGauge(metrics, "heat_pilot.loop.telemetry.duration.max", "us",
           "Maximum telemetry-service duration in the export interval",
           observedAt, snapshot.intervalMaximumTelemetryDurationUs);
  addSum(metrics, "heat_pilot.loop.stalls", "{stall}",
         "Main-loop iterations exceeding 100 milliseconds", startedAt,
         observedAt, snapshot.loopStalls);
  addSum(metrics, "heat_pilot.modbus.crc_errors", "{error}",
         "Modbus frames with invalid CRC", startedAt, observedAt,
         snapshot.modbusCrcErrors);
  addSum(metrics, "heat_pilot.modbus.overflows", "{error}",
         "Modbus receive buffer overflows", startedAt, observedAt,
         snapshot.modbusOverflows);
  addSum(metrics, "heat_pilot.smart_meter.timeouts", "{timeout}",
         "Smart Meter measurement timeouts", startedAt, observedAt,
         snapshot.smartMeterTimeouts);
  addSum(metrics, "heat_pilot.battery.timeouts", "{timeout}",
         "Battery measurement timeouts", startedAt, observedAt,
         snapshot.batteryTimeouts);
  addSum(metrics, "heat_pilot.telemetry.export.successes", "{export}",
         "Successful OTLP exports", startedAt, observedAt,
         snapshot.exportSuccesses);
  addSum(metrics, "heat_pilot.telemetry.export.failures", "{export}",
         "Failed OTLP exports", startedAt, observedAt,
         snapshot.exportFailures);

  String payload;
  payload.reserve(8192);
  serializeJson(document, payload);
  return payload;
}

void TelemetryService::reportTaskResult(const bool success,
                                        const int httpStatus,
                                        const uint32_t nowMs) {
  portENTER_CRITICAL(&statusMux_);
  status_.lastHttpStatus = httpStatus;
  status_.lastAttemptAtMs = nowMs;
  if (success) {
    ++status_.exportSuccesses;
    status_.lastSuccessAtMs = nowMs;
  } else {
    ++status_.exportFailures;
  }
  ++resultSequence_;
  portEXIT_CRITICAL(&statusMux_);
}

void TelemetryService::logNewResult() {
  uint32_t sequence;
  uint32_t successes;
  uint32_t failures;
  int httpStatus;
  portENTER_CRITICAL(&statusMux_);
  sequence = resultSequence_;
  successes = status_.exportSuccesses;
  failures = status_.exportFailures;
  httpStatus = status_.lastHttpStatus;
  portEXIT_CRITICAL(&statusMux_);

  if (sequence == loggedResultSequence_) {
    return;
  }
  loggedResultSequence_ = sequence;
  if (httpStatus == HTTP_CODE_OK) {
    if (!loggedFirstSuccess_) {
      loggedFirstSuccess_ = true;
      log_.println("[telemetry] first OTLP export succeeded");
    }
    return;
  }
  log_.printf("[telemetry] OTLP export failed status=%d successes=%lu "
              "failures=%lu\n",
              httpStatus, static_cast<unsigned long>(successes),
              static_cast<unsigned long>(failures));
}
