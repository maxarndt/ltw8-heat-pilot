#include "HttpApi.h"

#include <ArduinoJson.h>

#include "ApiValidation.h"
#include "Application.h"
#include "Config.h"

void HttpApi::update(const bool networkOnline) {
  if (networkOnline && !started_) {
    begin();
  }

  if (started_) {
    server_.handleClient();
  }
}

void HttpApi::begin() {
  server_.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/v1/manual-output", HTTP_PUT,
             [this]() { handleManualOutput(); });
  server_.on("/api/v1/mode", HTTP_PUT, [this]() { handleMode(); });
  server_.on("/api/v1/simulation", HTTP_PUT,
             [this]() { handleSimulation(); });
  server_.onNotFound([this]() {
    sendError(404, "not_found", "Endpoint not found");
  });
  server_.begin();
  started_ = true;
  log_.println("[http] REST API ready on port 80");
}

void HttpApi::handleStatus() {
  const ApplicationStatus status = application_.status(millis());

  JsonDocument response;
  response["uptime_ms"] = status.uptimeMs;
  response["mode"] = status.mode;
  response["state"] = status.state;
  response["heater_phases"] = status.outputs.heaterPhases;
  response["pump"] = status.outputs.circulationPump;
  response["output_driver_healthy"] = status.outputDriverHealthy;
  response["manual_timeout_remaining_ms"] = status.manualTimeoutRemainingMs;
  response["pump_overrun_remaining_ms"] = status.pumpOverrunRemainingMs;
  response["phase_change_remaining_ms"] = status.phaseChangeRemainingMs;
  response["measurements_valid"] = status.measurementsValid;
  response["temperature_valid"] = status.temperatureValid;
  response["temperature_fault"] = status.temperatureFault;
  response["surplus_w"] = status.surplusW;
  const FroniusSmartMeterReading& meter = application_.smartMeterReading();
  const bool smartMeterFresh = isFroniusSmartMeterSummaryFresh(
      meter, status.uptimeMs, config::modbus::kSmartMeterStaleMs);
  response["surplus_source"] =
      application_.simulatedSurplusEnabled()
          ? "simulation"
          : (smartMeterFresh ? "smart_meter" : "unavailable");
  response["temperature_c"] = status.temperatureC;

  JsonArray temperatureSensors =
      response["temperature_sensors"].to<JsonArray>();
  for (uint8_t index = 0; index < application_.temperatureSensorCount();
       ++index) {
    const TemperatureSensorReading& reading =
        application_.temperatureSensor(index);
    JsonObject sensor = temperatureSensors.add<JsonObject>();
    char address[17];
    TemperatureService::formatAddress(reading.address, address,
                                      sizeof(address));
    sensor["address"] = address;
    sensor["valid"] = reading.valid;
    sensor["measured_at_ms"] = reading.measuredAtMs;
    if (reading.valid) {
      sensor["temperature_c"] = reading.temperatureC;
    } else {
      sensor["temperature_c"] = nullptr;
    }
  }

  const ModbusSnifferStats& snifferStats = application_.modbusSnifferStats();
  JsonObject sniffer = response["modbus_sniffer"].to<JsonObject>();
  sniffer["mode"] = "receive_only";
  sniffer["baud"] = config::modbus::kBaudRate;
  sniffer["frames"] = snifferStats.frames;
  sniffer["valid_frames"] = snifferStats.validFrames;
  sniffer["crc_errors"] = snifferStats.crcErrors;
  sniffer["overflows"] = snifferStats.overflows;
  sniffer["smart_meter_timeouts"] = application_.smartMeterTimeouts();
  sniffer["battery_timeouts"] = application_.batteryTimeouts();
  if (application_.hasLastInvalidModbusFrame()) {
    const CapturedModbusFrame& frame = application_.lastInvalidModbusFrame();
    JsonObject invalid = sniffer["last_invalid_frame"].to<JsonObject>();
    invalid["captured_at_ms"] = frame.capturedAtMs;
    invalid["length"] = frame.length;
    if (frame.length >= 2U) {
      invalid["unit_id"] = frame.data[0];
      invalid["function"] = frame.data[1];
    }
    char hex[config::modbus::kMaximumFrameLength * 2U + 1U];
    ModbusSniffer::formatHex(frame.data, frame.length, hex, sizeof(hex));
    invalid["hex"] = hex;
  }

  JsonObject smartMeter = response["smart_meter"].to<JsonObject>();
  smartMeter["unit_id"] = 1;
  smartMeter["summary_valid"] = meter.summaryValid;
  smartMeter["phases_valid"] = meter.phasesValid;
  smartMeter["fresh"] = smartMeterFresh;
  if (meter.summaryValid) {
    smartMeter["measured_at_ms"] = meter.summaryMeasuredAtMs;
    smartMeter["voltage_v"] = meter.voltageDecivolts / 10.0F;
    smartMeter["voltage_phase_to_phase_v"] =
        meter.voltagePhaseToPhaseDecivolts / 10.0F;
    smartMeter["grid_power_w"] = meter.realPowerDeciwatts / 10.0F;
    smartMeter["observed_surplus_w"] =
        -meter.realPowerDeciwatts / 10.0F;
    smartMeter["apparent_power_va"] =
        meter.apparentPowerDecivoltAmps / 10.0F;
    smartMeter["reactive_power_var"] =
        meter.reactivePowerDecivars / 10.0F;
    smartMeter["power_factor"] = meter.powerFactorMilli / 1000.0F;
    smartMeter["frequency_hz"] = meter.frequencyDecihertz / 10.0F;
  }
  JsonArray phases = smartMeter["phases"].to<JsonArray>();
  if (meter.phasesValid) {
    for (uint8_t index = 0; index < 3; ++index) {
      const FroniusSmartMeterPhaseReading& reading = meter.phases[index];
      JsonObject phase = phases.add<JsonObject>();
      phase["phase"] = index + 1;
      phase["voltage_v"] = reading.voltageDecivolts / 10.0F;
      phase["voltage_phase_to_phase_v"] =
          reading.voltagePhaseToPhaseDecivolts / 10.0F;
      phase["current_a"] = reading.currentMilliamps / 1000.0F;
      phase["real_power_w"] = reading.realPowerDeciwatts / 10.0F;
      phase["apparent_power_va"] =
          reading.apparentPowerDecivoltAmps / 10.0F;
      phase["reactive_power_var"] =
          reading.reactivePowerDecivars / 10.0F;
      phase["power_factor"] = reading.powerFactorMilli / 1000.0F;
    }
  }

  const FroniusBatteryReading& battery = application_.batteryReading();
  JsonObject batteryJson = response["battery"].to<JsonObject>();
  const bool batteryFresh = isFroniusBatteryFresh(
      battery, status.uptimeMs, config::modbus::kBatteryStaleMs);
  batteryJson["unit_id"] = 21;
  batteryJson["valid"] = battery.valid;
  batteryJson["fresh"] = batteryFresh;
  if (battery.valid) {
    batteryJson["measured_at_ms"] = battery.measuredAtMs;
    batteryJson["status"] = battery.status;
    batteryJson["mode"] = battery.mode;
    batteryJson["state_of_charge_percent"] =
        battery.stateOfChargeHundredths / 100.0F;
    batteryJson["power_w"] = battery.powerW;
    batteryJson["internal_power_w"] = battery.internalPowerW;
    batteryJson["voltage_v"] = battery.voltageDecivolts / 10.0F;
    batteryJson["total_capacity_wh"] = battery.totalCapacityWh;
  }
  JsonArray recentFrames = sniffer["recent_frames"].to<JsonArray>();
  for (uint8_t index = 0; index < application_.recentModbusFrameCount();
       ++index) {
    const CapturedModbusFrame& frame = application_.recentModbusFrame(index);
    JsonObject item = recentFrames.add<JsonObject>();
    item["captured_at_ms"] = frame.capturedAtMs;
    item["length"] = frame.length;
    item["crc_valid"] = frame.crcValid;
    if (frame.length >= 2) {
      item["unit_id"] = frame.data[0];
      item["function"] = frame.data[1];
    }
    char hex[config::modbus::kMaximumFrameLength * 2U + 1U];
    ModbusSniffer::formatHex(frame.data, frame.length, hex, sizeof(hex));
    item["hex"] = hex;
  }

  String body;
  serializeJson(response, body);
  server_.send(200, "application/json", body);
}

void HttpApi::handleManualOutput() {
  JsonDocument request;
  const DeserializationError error = deserializeJson(request, server_.arg("plain"));
  if (error) {
    sendError(400, "invalid_json", "Request body must be valid JSON");
    return;
  }

  if (!request["heater_phases"].is<int>() || !request["pump"].is<bool>()) {
    sendError(400, "invalid_request",
              "heater_phases must be an integer and pump a boolean");
    return;
  }

  const int heaterPhases = request["heater_phases"].as<int>();
  const bool pump = request["pump"].as<bool>();
  const ManualOutputValidation validation =
      validateManualOutput(heaterPhases, pump);
  if (validation == ManualOutputValidation::InvalidPhaseCount) {
    sendError(422, "invalid_heater_phases",
              "heater_phases must be between 0 and 3");
    return;
  }

  if (validation == ManualOutputValidation::PumpRequired) {
    sendError(422, "pump_interlock",
              "pump must be true while heater phases are active");
    return;
  }
  if (heaterPhases > 0 && !application_.status(millis()).temperatureValid) {
    sendError(409, "temperature_unavailable",
              "heater requires valid temperature measurements");
    return;
  }
  if (!application_.setManualOutput(static_cast<uint8_t>(heaterPhases), pump,
                                    millis())) {
    sendError(503, "output_driver_unavailable",
              "Output command could not be applied");
    return;
  }

  log_.printf("[http] manual output heater_phases=%d pump=%u\n", heaterPhases,
              pump);
  handleStatus();
}

void HttpApi::handleMode() {
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) ||
      !request["mode"].is<const char*>()) {
    sendError(400, "invalid_request", "mode must be a string");
    return;
  }

  const String mode = request["mode"].as<String>();
  OperatingMode requestedMode;
  if (mode == "disabled") {
    requestedMode = OperatingMode::Disabled;
  } else if (mode == "automatic") {
    requestedMode = OperatingMode::Automatic;
  } else {
    sendError(422, "invalid_mode", "mode must be disabled or automatic");
    return;
  }

  if (!application_.setOperatingMode(requestedMode, millis())) {
    sendError(503, "mode_change_failed", "Mode could not be changed");
    return;
  }
  log_.printf("[http] operating mode=%s\n", mode.c_str());
  handleStatus();
}

void HttpApi::handleSimulation() {
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain"))) {
    sendError(400, "invalid_json", "Request body must be valid JSON");
    return;
  }
  if (request["enabled"].is<bool>() && !request["enabled"].as<bool>()) {
    application_.disableSimulatedSurplus(millis());
    log_.println("[simulation] disabled; using smart meter");
    handleStatus();
    return;
  }
  if (!request["surplus_w"].is<int>()) {
    sendError(400, "invalid_request", "surplus_w must be an integer");
    return;
  }

  const int32_t surplusW = request["surplus_w"].as<int32_t>();
  application_.setSimulatedSurplus(surplusW);
  log_.printf("[simulation] surplus_w=%ld\n", static_cast<long>(surplusW));
  handleStatus();
}

void HttpApi::sendError(const int statusCode, const char* code,
                        const char* message) {
  JsonDocument response;
  response["error"] = code;
  response["message"] = message;

  String body;
  serializeJson(response, body);
  server_.send(statusCode, "application/json", body);
}
