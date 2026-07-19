#include "HttpApi.h"

#include <ArduinoJson.h>

#include "ApiValidation.h"
#include "Application.h"

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
  response["surplus_w"] = status.surplusW;
  response["temperature_c"] = status.temperatureC;

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
  if (!request["surplus_w"].is<int>() ||
      !request["temperature_c"].is<float>()) {
    sendError(400, "invalid_request",
              "surplus_w must be an integer and temperature_c a number");
    return;
  }

  const int32_t surplusW = request["surplus_w"].as<int32_t>();
  const float temperatureC = request["temperature_c"].as<float>();
  if (!isValidTemperature(temperatureC)) {
    sendError(422, "invalid_temperature",
              "temperature_c must be between -55 and 125");
    return;
  }

  application_.setSimulatedMeasurements(surplusW, temperatureC);
  log_.printf("[simulation] surplus_w=%ld temperature_c=%.1f\n",
              static_cast<long>(surplusW), temperatureC);
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
