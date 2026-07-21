#include "FroniusBattery.h"

#include "ModbusRtu.h"

namespace {

constexpr uint8_t kBatteryUnitId = 21;
constexpr uint8_t kReadHoldingRegisters = 3;
constexpr uint16_t kStatusStartAddress = 0x0190;
constexpr uint16_t kStatusRegisterCount = 30;

}  // namespace

bool isFroniusBatteryFresh(const FroniusBatteryReading& reading,
                           const uint32_t nowMs,
                           const uint32_t maximumAgeMs) {
  return reading.valid && nowMs - reading.measuredAtMs <= maximumAgeMs;
}

bool FroniusBatteryDecoder::processFrame(const uint8_t* frame,
                                         const size_t length,
                                         const uint32_t nowMs) {
  if (!hasValidModbusRtuCrc(frame, length) || length < 5U ||
      frame[0] != kBatteryUnitId || frame[1] != kReadHoldingRegisters) {
    return false;
  }

  if (length == 8U) {
    const uint16_t startAddress = decodeUnsigned16(frame + 2);
    const uint16_t registerCount = decodeUnsigned16(frame + 4);
    waitingForStatusResponse_ = startAddress == kStatusStartAddress &&
                                registerCount == kStatusRegisterCount;
    return false;
  }

  constexpr size_t kDataBytes = kStatusRegisterCount * 2U;
  if (!waitingForStatusResponse_ || length != kDataBytes + 5U ||
      frame[2] != kDataBytes) {
    waitingForStatusResponse_ = false;
    return false;
  }
  waitingForStatusResponse_ = false;

  const uint8_t* data = frame + 3;
  reading_.status = decodeUnsigned16(data + 0U * 2U);
  reading_.mode = decodeUnsigned16(data + 2U * 2U);
  reading_.stateOfChargeHundredths = decodeUnsigned16(data + 3U * 2U);
  reading_.totalCapacityWh = decodeUnsigned32(data + 4U * 2U);
  reading_.voltageDecivolts = decodeUnsigned16(data + 12U * 2U);
  reading_.powerW = decodeSigned32(data + 13U * 2U);
  reading_.internalPowerW = decodeSigned32(data + 16U * 2U);
  reading_.measuredAtMs = nowMs;
  reading_.valid = true;
  return true;
}

uint16_t FroniusBatteryDecoder::decodeUnsigned16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                               data[1]);
}

uint32_t FroniusBatteryDecoder::decodeUnsigned32(const uint8_t* data) {
  return (static_cast<uint32_t>(decodeUnsigned16(data)) << 16U) |
         decodeUnsigned16(data + 2);
}

int32_t FroniusBatteryDecoder::decodeSigned32(const uint8_t* data) {
  return static_cast<int32_t>(decodeUnsigned32(data));
}
