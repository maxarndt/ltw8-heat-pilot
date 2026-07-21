#pragma once

#include <Arduino.h>
#include <WebServer.h>

class Application;
class TelemetryService;

class HttpApi {
 public:
  HttpApi(Application& application, TelemetryService& telemetry, Print& log)
      : application_(application), telemetry_(telemetry), log_(log) {}

  void update(bool networkOnline);

 private:
  void begin();
  void handleWebUi();
  void handleStatus();
  void handleManualOutput();
  void handleMode();
  void handleSimulation();
  void sendError(int statusCode, const char* code, const char* message);

  Application& application_;
  TelemetryService& telemetry_;
  Print& log_;
  WebServer server_{80};
  bool started_ = false;
};
