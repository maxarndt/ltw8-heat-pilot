#include <Arduino.h>

#include "Application.h"
#include "HttpApi.h"
#include "ModbusSniffer.h"
#include "NetworkService.h"
#include "OutputController.h"
#include "TemperatureService.h"
#include "TelemetryService.h"

namespace {
NetworkService network;
LogOutput logOutput(network);
OutputController outputs;
TemperatureService temperatures(logOutput);
ModbusSniffer modbusSniffer(logOutput);
Application application(logOutput, outputs, temperatures, modbusSniffer);
TelemetryService telemetry(application, logOutput);
HttpApi httpApi(application, telemetry, logOutput);
}

void setup() {
  Serial.begin(115200);

  // Native USB CDC can take a moment to become available. Do not wait for a
  // host indefinitely because the controller must also run headless.
  const uint32_t waitStartedAt = millis();
  while (!Serial && millis() - waitStartedAt < 1500) {
    delay(10);
  }

  application.begin();
  network.begin();
}

void loop() {
  const uint32_t loopStartedAtUs = micros();
  const uint32_t nowMs = millis();
  network.update();
  application.update(nowMs);
  httpApi.update(network.online());
  telemetry.update(network.online(), nowMs);
  telemetry.observeLoopDuration(micros() - loopStartedAtUs);
  delay(1);
}
