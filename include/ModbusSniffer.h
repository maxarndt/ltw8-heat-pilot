#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "Config.h"
#include "FroniusBattery.h"
#include "FroniusSmartMeter.h"

struct CapturedModbusFrame {
  uint32_t capturedAtMs = 0;
  uint16_t length = 0;
  bool crcValid = false;
  uint8_t data[config::modbus::kMaximumFrameLength]{};
};

struct ModbusSnifferStats {
  uint32_t frames = 0;
  uint32_t validFrames = 0;
  uint32_t crcErrors = 0;
  uint32_t overflows = 0;
};

class ModbusSniffer {
 public:
  explicit ModbusSniffer(Print& log) : log_(log) {}

  void begin();
  void update(uint32_t nowMs);

  const ModbusSnifferStats& stats() const { return stats_; }
  const FroniusSmartMeterReading& smartMeterReading() const {
    return smartMeterDecoder_.reading();
  }
  const FroniusBatteryReading& batteryReading() const {
    return batteryDecoder_.reading();
  }
  uint8_t recentFrameCount() const { return recentFrameCount_; }
  const CapturedModbusFrame& recentFrame(uint8_t newestFirstIndex) const;
  bool hasLastInvalidFrame() const { return hasLastInvalidFrame_; }
  const CapturedModbusFrame& lastInvalidFrame() const {
    return lastInvalidFrame_;
  }
  static void formatHex(const uint8_t* data, size_t length, char* target,
                        size_t targetSize);

 private:
  void extractCompleteFrames(uint32_t nowMs);
  void finishFrame(uint32_t nowMs);
  void storeFrame(uint32_t nowMs, bool crcValid);
  void printSummary(uint32_t nowMs);

  Print& log_;
  HardwareSerial serial_{1};
  ModbusSnifferStats stats_{};
  FroniusSmartMeterDecoder smartMeterDecoder_{};
  FroniusBatteryDecoder batteryDecoder_{};
  CapturedModbusFrame recentFrames_[config::modbus::kRecentFrameCount]{};
  CapturedModbusFrame lastInvalidFrame_{};
  uint8_t receiveBuffer_[config::modbus::kMaximumFrameLength]{};
  uint16_t receiveLength_ = 0;
  uint8_t recentFrameCount_ = 0;
  uint8_t nextFrameIndex_ = 0;
  uint32_t lastByteAtUs_ = 0;
  uint32_t lastLogAtMs_ = 0;
  bool receiving_ = false;
  bool currentFrameOverflow_ = false;
  bool hasLastInvalidFrame_ = false;
};
