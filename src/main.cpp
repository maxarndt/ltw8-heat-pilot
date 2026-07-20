#include <Arduino.h>

#include "Application.h"
#include "HttpApi.h"
#include "NetworkService.h"
#include "OutputController.h"
#include "TemperatureService.h"

namespace {
NetworkService network;
LogOutput logOutput(network);
OutputController outputs;
TemperatureService temperatures(logOutput);
Application application(logOutput, outputs, temperatures);
HttpApi httpApi(application, logOutput);
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
  network.update();
  application.update(millis());
  httpApi.update(network.online());
  delay(1);
}
