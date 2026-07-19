#pragma once

#include <Arduino.h>
#include <Network.h>

class NetworkService {
 public:
  void begin();
  void update();
  size_t write(const uint8_t* buffer, size_t size);
  bool online() const { return ethernetOnline_; }

 private:
  void handleEvent(arduino_event_id_t event, arduino_event_info_t info);
  void startNetworkServices();
  void acceptLogClient();

  NetworkServer logServer_{23};
  NetworkClient logClient_{};
  bool ethernetOnline_ = false;
  bool servicesStarted_ = false;
};

class LogOutput : public Print {
 public:
  explicit LogOutput(NetworkService& network) : network_(network) {}

  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  NetworkService& network_;
};
