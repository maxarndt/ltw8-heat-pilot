#include "NetworkService.h"

#include <ArduinoOTA.h>
#include <ETH.h>
#include <SPI.h>

#include "Config.h"

void NetworkService::begin() {
  logBuffer_ = xStreamBufferCreate(kLogBufferSize, 1);
  if (logBuffer_ == nullptr ||
      xTaskCreatePinnedToCore(logTaskEntry, "network-log",
                              kLogTaskStackBytes, this, 1, nullptr, 0) !=
          pdPASS) {
    Serial.println("[network] failed to start asynchronous log task");
  }

  Network.onEvent([this](const arduino_event_id_t event,
                         const arduino_event_info_t info) {
    handleEvent(event, info);
  });

  SPI.begin(config::ethernet::kClockPin, config::ethernet::kMisoPin,
            config::ethernet::kMosiPin);

  if (!ETH.begin(ETH_PHY_W5500, config::ethernet::kPhyAddress,
                 config::ethernet::kChipSelectPin,
                 config::ethernet::kInterruptPin,
                 config::ethernet::kResetPin, SPI)) {
    Serial.println("[network] failed to start W5500");
  }
}

void NetworkService::update() {
  if (ethernetOnline_ && !servicesStarted_) {
    startNetworkServices();
  }

  if (!servicesStarted_) {
    return;
  }

  ArduinoOTA.handle();
}

size_t NetworkService::write(const uint8_t* buffer, const size_t size) {
  if (!logClientConnected_ || logBuffer_ == nullptr) {
    return size;
  }
  const size_t queued = xStreamBufferSend(logBuffer_, buffer, size, 0);
  droppedLogBytes_ += static_cast<uint32_t>(size - queued);
  return size;
}

void NetworkService::handleEvent(const arduino_event_id_t event,
                                 const arduino_event_info_t info) {
  (void)info;

  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(config::kHostname);
      Serial.println("[network] ethernet started; waiting for DHCP");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[network] ethernet link connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethernetOnline_ = true;
      Serial.printf("[network] DHCP address=%s hostname=%s.local\n",
                    ETH.localIP().toString().c_str(), config::kHostname);
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      ethernetOnline_ = false;
      servicesStarted_ = false;
      Serial.println("[network] ethernet lost IP address");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ethernetOnline_ = false;
      servicesStarted_ = false;
      Serial.println("[network] ethernet link disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      ethernetOnline_ = false;
      servicesStarted_ = false;
      Serial.println("[network] ethernet stopped");
      break;
    default:
      break;
  }
}

void NetworkService::startNetworkServices() {
  logServer_.begin();

  ArduinoOTA.setHostname(config::kHostname);
  ArduinoOTA.setPassword(config::kOtaPassword);
  ArduinoOTA.onStart([]() { Serial.println("[ota] update started"); });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] update complete"); });
  ArduinoOTA.onError([](const ota_error_t error) {
    Serial.printf("[ota] update failed error=%u\n", error);
  });
  ArduinoOTA.begin();

  servicesStarted_ = true;
  Serial.printf("[network] OTA ready; TCP log port=%u\n", config::kLogPort);
}

void NetworkService::logTaskEntry(void* context) {
  static_cast<NetworkService*>(context)->logTaskLoop();
}

void NetworkService::logTaskLoop() {
  uint8_t buffer[512];
  while (true) {
    if (!servicesStarted_) {
      logClientConnected_ = false;
      if (logClient_) {
        logClient_.stop();
      }
      if (logBuffer_ != nullptr) {
        xStreamBufferReset(logBuffer_);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    acceptLogClient();
    if (!logClient_ || !logClient_.connected()) {
      logClientConnected_ = false;
      if (logClient_) {
        logClient_.stop();
      }
      if (logBuffer_ != nullptr) {
        xStreamBufferReset(logBuffer_);
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    logClientConnected_ = true;
    const size_t received = xStreamBufferReceive(
        logBuffer_, buffer, sizeof(buffer), pdMS_TO_TICKS(20));
    if (received > 0 && logClient_.write(buffer, received) != received) {
      logClientConnected_ = false;
      logClient_.stop();
    }
  }
}

void NetworkService::acceptLogClient() {
  NetworkClient incoming = logServer_.accept();
  if (!incoming) {
    return;
  }

  if (logClient_) {
    logClientConnected_ = false;
    logClient_.stop();
  }

  logClient_ = incoming;
  logClientConnected_ = true;
  logClient_.printf("LTW8 Heat Pilot network log\r\n");
  logClient_.printf("IP: %s\r\n", ETH.localIP().toString().c_str());
}

size_t LogOutput::write(const uint8_t value) {
  return write(&value, 1);
}

size_t LogOutput::write(const uint8_t* buffer, const size_t size) {
  Serial.write(buffer, size);
  network_.write(buffer, size);
  return size;
}
