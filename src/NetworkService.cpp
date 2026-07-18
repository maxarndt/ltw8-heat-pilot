#include "NetworkService.h"

#include <ArduinoOTA.h>
#include <ETH.h>
#include <SPI.h>

#include "Config.h"

void NetworkService::begin() {
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
  acceptLogClient();

  if (logClient_ && !logClient_.connected()) {
    logClient_.stop();
  }
}

size_t NetworkService::write(const uint8_t* buffer, const size_t size) {
  if (!logClient_ || !logClient_.connected()) {
    return size;
  }

  logClient_.write(buffer, size);
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
      Serial.println("[network] ethernet lost IP address");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ethernetOnline_ = false;
      Serial.println("[network] ethernet link disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      ethernetOnline_ = false;
      servicesStarted_ = false;
      logClient_.stop();
      logServer_.end();
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

void NetworkService::acceptLogClient() {
  NetworkClient incoming = logServer_.accept();
  if (!incoming) {
    return;
  }

  if (logClient_) {
    logClient_.stop();
  }

  logClient_ = incoming;
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

