#include <Arduino.h>

#include "Application.h"
#include "NetworkService.h"

namespace {
NetworkService network;
LogOutput logOutput(network);
Application application(logOutput);
}

void setup() {
  Serial.begin(115200);

  // Native USB CDC can take a moment to become available. Do not wait for a
  // host indefinitely because the controller must also run headless.
  const uint32_t waitStartedAt = millis();
  while (!Serial && millis() - waitStartedAt < 1500) {
    delay(10);
  }

  network.begin();
  application.begin();
}

void loop() {
  network.update();
  application.update(millis());
  delay(1);
}
