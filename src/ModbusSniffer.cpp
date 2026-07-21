#include "ModbusSniffer.h"

#include <cstdio>
#include <cstring>

#include "ModbusRtu.h"
#include "ModbusRtuFraming.h"

void ModbusSniffer::begin() {
  // Keep the transceiver permanently in receive mode. TX is deliberately not
  // assigned to the UART, so this firmware cannot drive the shared bus.
  pinMode(config::modbus::kDirectionPin, OUTPUT);
  digitalWrite(config::modbus::kDirectionPin, LOW);
  pinMode(config::modbus::kTransmitPin, INPUT);
  serial_.begin(config::modbus::kBaudRate, SERIAL_8N1,
                config::modbus::kReceivePin, -1);
  log_.printf("[modbus-sniffer] RX-only GPIO=%d baud=%lu 8N1\n",
              config::modbus::kReceivePin,
              static_cast<unsigned long>(config::modbus::kBaudRate));
}

void ModbusSniffer::update(const uint32_t nowMs) {
  while (serial_.available() > 0) {
    const int value = serial_.read();
    if (value < 0) {
      break;
    }
    receiving_ = true;
    lastByteAtUs_ = micros();
    if (receiveLength_ < sizeof(receiveBuffer_)) {
      receiveBuffer_[receiveLength_++] = static_cast<uint8_t>(value);
      extractCompleteFrames(nowMs);
    } else {
      currentFrameOverflow_ = true;
    }
  }

  if (receiving_ &&
      micros() - lastByteAtUs_ >= config::modbus::kFrameGapUs) {
    finishFrame(nowMs);
  }

  if (nowMs - lastLogAtMs_ >= config::modbus::kLogIntervalMs) {
    lastLogAtMs_ = nowMs;
    printSummary(nowMs);
  }
}

void ModbusSniffer::extractCompleteFrames(const uint32_t nowMs) {
  const size_t frameLength =
      completeModbusRtuFrameLength(receiveBuffer_, receiveLength_);
  if (frameLength == 0U) {
    return;
  }

  ++stats_.frames;
  ++stats_.validFrames;
  receiveLength_ = static_cast<uint16_t>(frameLength);
  smartMeterDecoder_.processFrame(receiveBuffer_, receiveLength_, nowMs);
  batteryDecoder_.processFrame(receiveBuffer_, receiveLength_, nowMs);
  storeFrame(nowMs, true);
  receiveLength_ = 0;
  receiving_ = false;
  currentFrameOverflow_ = false;
}

const CapturedModbusFrame& ModbusSniffer::recentFrame(
    const uint8_t newestFirstIndex) const {
  const uint8_t index = static_cast<uint8_t>(
      (nextFrameIndex_ + config::modbus::kRecentFrameCount - 1U -
       newestFirstIndex) %
      config::modbus::kRecentFrameCount);
  return recentFrames_[index];
}

void ModbusSniffer::formatHex(const uint8_t* data, const size_t length,
                             char* target, const size_t targetSize) {
  if (targetSize == 0) {
    return;
  }
  const size_t bytesToWrite =
      length < (targetSize - 1U) / 2U ? length : (targetSize - 1U) / 2U;
  for (size_t index = 0; index < bytesToWrite; ++index) {
    std::snprintf(target + index * 2U, targetSize - index * 2U, "%02X",
                  data[index]);
  }
  target[bytesToWrite * 2U] = '\0';
}

void ModbusSniffer::finishFrame(const uint32_t nowMs) {
  if (receiveLength_ == 0U && !currentFrameOverflow_) {
    receiving_ = false;
    return;
  }
  ++stats_.frames;
  if (currentFrameOverflow_) {
    ++stats_.overflows;
  }

  const bool crcValid = !currentFrameOverflow_ &&
                        hasValidModbusRtuCrc(receiveBuffer_, receiveLength_);
  if (crcValid) {
    ++stats_.validFrames;
    smartMeterDecoder_.processFrame(receiveBuffer_, receiveLength_, nowMs);
    batteryDecoder_.processFrame(receiveBuffer_, receiveLength_, nowMs);
  } else {
    ++stats_.crcErrors;
  }
  storeFrame(nowMs, crcValid);

  receiveLength_ = 0;
  receiving_ = false;
  currentFrameOverflow_ = false;
}

void ModbusSniffer::storeFrame(const uint32_t nowMs, const bool crcValid) {
  CapturedModbusFrame& frame = recentFrames_[nextFrameIndex_];
  frame.capturedAtMs = nowMs;
  frame.length = receiveLength_;
  frame.crcValid = crcValid;
  std::memcpy(frame.data, receiveBuffer_, receiveLength_);
  if (!crcValid) {
    lastInvalidFrame_ = frame;
    hasLastInvalidFrame_ = true;
  }

  nextFrameIndex_ =
      static_cast<uint8_t>((nextFrameIndex_ + 1U) %
                           config::modbus::kRecentFrameCount);
  if (recentFrameCount_ < config::modbus::kRecentFrameCount) {
    ++recentFrameCount_;
  }
}

void ModbusSniffer::printSummary(const uint32_t nowMs) {
  (void)nowMs;
  log_.printf(
      "[modbus-sniffer] frames=%lu valid=%lu crc_errors=%lu overflows=%lu",
      static_cast<unsigned long>(stats_.frames),
      static_cast<unsigned long>(stats_.validFrames),
      static_cast<unsigned long>(stats_.crcErrors),
      static_cast<unsigned long>(stats_.overflows));

  if (recentFrameCount_ > 0) {
    const CapturedModbusFrame& latest = recentFrame(0);
    char hex[config::modbus::kMaximumFrameLength * 2U + 1U];
    formatHex(latest.data, latest.length, hex, sizeof(hex));
    log_.printf(" last_crc=%u last=%s", latest.crcValid, hex);
  }
  const FroniusSmartMeterReading& meter = smartMeterDecoder_.reading();
  if (meter.summaryValid) {
    log_.printf(" grid_power_w=%.1f observed_surplus_w=%.1f",
                meter.realPowerDeciwatts / 10.0F,
                -meter.realPowerDeciwatts / 10.0F);
  }
  const FroniusBatteryReading& battery = batteryDecoder_.reading();
  if (battery.valid) {
    log_.printf(" battery_soc=%.2f battery_power_w=%ld",
                battery.stateOfChargeHundredths / 100.0F,
                static_cast<long>(battery.powerW));
  }
  log_.println();
}
