#pragma once

#include <Arduino.h>
#include <Network.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

class NetworkService {
 public:
  void begin();
  void update();
  size_t write(const uint8_t* buffer, size_t size);
  bool online() const { return ethernetOnline_; }

 private:
  void handleEvent(arduino_event_id_t event, arduino_event_info_t info);
  void startNetworkServices();
  static void logTaskEntry(void* context);
  void logTaskLoop();
  void acceptLogClient();

  static constexpr size_t kLogBufferSize = 4096;
  static constexpr uint32_t kLogTaskStackBytes = 4096;
  NetworkServer logServer_{23};
  NetworkClient logClient_{};
  StreamBufferHandle_t logBuffer_ = nullptr;
  volatile bool ethernetOnline_ = false;
  volatile bool servicesStarted_ = false;
  volatile bool logClientConnected_ = false;
  volatile uint32_t droppedLogBytes_ = 0;
};

class LogOutput : public Print {
 public:
  explicit LogOutput(NetworkService& network) : network_(network) {}

  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  NetworkService& network_;
};
