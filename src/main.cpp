#include <Arduino.h>
#include <esp_system.h>

#include "Application.h"
#include "HttpApi.h"
#include "ModbusSniffer.h"
#include "NetworkService.h"
#include "OutputController.h"
#include "RetainedOperationalState.h"
#include "TemperatureService.h"
#include "TelemetryService.h"

namespace {
constexpr uint32_t kRetainedStageMagic = 0x48505354;
RTC_DATA_ATTR uint32_t retainedStageMagic;
RTC_DATA_ATTR uint8_t retainedStage;
RTC_DATA_ATTR RetainedOperationalState retainedOperationalState;

NetworkService network;
LogOutput logOutput(network);
OutputController outputs;
TemperatureService temperatures(logOutput);
ModbusSniffer modbusSniffer(logOutput);
Application application(logOutput, outputs, temperatures, modbusSniffer,
                        retainedOperationalState);
TelemetryService telemetry(application, logOutput);
HttpApi httpApi(application, telemetry, logOutput);

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
      return "task_watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep_sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    case ESP_RST_UNKNOWN:
    default:
      return "unknown";
  }
}

bool isUnexpectedReset(const esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

void enterStage(const MainLoopStage stage) {
  retainedStageMagic = kRetainedStageMagic;
  retainedStage = static_cast<uint8_t>(stage);
}
}

void setup() {
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const bool recoverPumpOverrun =
      isUnexpectedReset(resetReason) &&
      shouldRecoverPumpOverrun(retainedOperationalState);
  const MainLoopStage previousStage =
      retainedStageMagic == kRetainedStageMagic
          ? static_cast<MainLoopStage>(retainedStage)
          : MainLoopStage::Idle;
  telemetry.setResetDiagnostics(resetReasonName(resetReason),
                                previousStage);
  enterStage(MainLoopStage::Setup);
  Serial.begin(115200);
  application.begin(recoverPumpOverrun);
  enableLoopWDT();
  network.begin();
  enterStage(MainLoopStage::Idle);
}

void loop() {
  const uint32_t loopStartedAtUs = micros();
  const uint32_t nowMs = millis();

  enterStage(MainLoopStage::Network);
  const uint32_t networkStartedAtUs = micros();
  network.update();
  const uint32_t networkDurationUs = micros() - networkStartedAtUs;

  enterStage(MainLoopStage::Application);
  const uint32_t applicationStartedAtUs = micros();
  application.update(nowMs);
  const uint32_t applicationDurationUs = micros() - applicationStartedAtUs;

  enterStage(MainLoopStage::Http);
  const uint32_t httpStartedAtUs = micros();
  httpApi.update(network.online());
  const uint32_t httpDurationUs = micros() - httpStartedAtUs;

  enterStage(MainLoopStage::Telemetry);
  const uint32_t telemetryStartedAtUs = micros();
  telemetry.update(network.online(), nowMs);
  const uint32_t telemetryDurationUs = micros() - telemetryStartedAtUs;

  telemetry.observeLoopDurations(
      nowMs, micros() - loopStartedAtUs, networkDurationUs,
      applicationDurationUs, httpDurationUs, telemetryDurationUs);
  enterStage(MainLoopStage::Idle);
  delay(1);
}
