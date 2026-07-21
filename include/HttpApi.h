#pragma once

#include <Arduino.h>
#include <WebServer.h>

class Application;

class HttpApi {
 public:
  HttpApi(Application& application, Print& log)
      : application_(application), log_(log) {}

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
  Print& log_;
  WebServer server_{80};
  bool started_ = false;
};
