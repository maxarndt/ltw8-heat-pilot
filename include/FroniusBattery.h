#pragma once

#include <stddef.h>
#include <stdint.h>

struct FroniusBatteryReading {
  bool valid = false;
  uint32_t measuredAtMs = 0;
  uint16_t status = 0;
  uint16_t mode = 0;
  uint16_t stateOfChargeHundredths = 0;
  uint32_t totalCapacityWh = 0;
  int32_t powerW = 0;
  int32_t internalPowerW = 0;
  uint16_t voltageDecivolts = 0;
};

bool isFroniusBatteryFresh(const FroniusBatteryReading& reading,
                           uint32_t nowMs, uint32_t maximumAgeMs);

class FroniusBatteryDecoder {
 public:
  bool processFrame(const uint8_t* frame, size_t length, uint32_t nowMs);
  const FroniusBatteryReading& reading() const { return reading_; }

 private:
  static uint16_t decodeUnsigned16(const uint8_t* data);
  static uint32_t decodeUnsigned32(const uint8_t* data);
  static int32_t decodeSigned32(const uint8_t* data);

  bool waitingForStatusResponse_ = false;
  FroniusBatteryReading reading_{};
};
