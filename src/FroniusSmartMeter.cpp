#include "FroniusSmartMeter.h"

#include "ModbusRtu.h"

namespace {

constexpr uint8_t kSmartMeterUnitId = 1;
constexpr uint8_t kReadHoldingRegisters = 3;
constexpr uint16_t kSummaryStartAddress = 0x0102;
constexpr uint16_t kSummaryRegisterCount = 16;
constexpr uint16_t kPhasesStartAddress = 0x011E;
constexpr uint16_t kPhasesRegisterCount = 42;

uint16_t decodeBigEndian16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                               data[1]);
}

}  // namespace

bool isFroniusSmartMeterSummaryFresh(
    const FroniusSmartMeterReading& reading, const uint32_t nowMs,
    const uint32_t maximumAgeMs) {
  return reading.summaryValid &&
         nowMs - reading.summaryMeasuredAtMs <= maximumAgeMs;
}

int32_t froniusGridPowerToSurplusWatts(
    const int32_t gridPowerDeciwatts) {
  const int32_t surplusDeciwatts = -gridPowerDeciwatts;
  return surplusDeciwatts >= 0 ? (surplusDeciwatts + 5) / 10
                              : (surplusDeciwatts - 5) / 10;
}

bool FroniusSmartMeterDecoder::processFrame(const uint8_t* frame,
                                            const size_t length,
                                            const uint32_t nowMs) {
  if (!hasValidModbusRtuCrc(frame, length) || length < 5U ||
      frame[0] != kSmartMeterUnitId || frame[1] != kReadHoldingRegisters) {
    return false;
  }

  if (length == 8U) {
    const uint16_t startAddress = decodeBigEndian16(frame + 2);
    const uint16_t registerCount = decodeBigEndian16(frame + 4);
    if (startAddress == kSummaryStartAddress &&
        registerCount == kSummaryRegisterCount) {
      pendingRequest_ = PendingRequest::Summary;
      return false;
    }
    if (startAddress == kPhasesStartAddress &&
        registerCount == kPhasesRegisterCount) {
      pendingRequest_ = PendingRequest::Phases;
      return false;
    }
    pendingRequest_ = PendingRequest::None;
    return false;
  }

  bool decoded = false;
  if (pendingRequest_ == PendingRequest::Summary) {
    decoded = decodeSummary(frame, length, nowMs);
  } else if (pendingRequest_ == PendingRequest::Phases) {
    decoded = decodePhases(frame, length, nowMs);
  }
  pendingRequest_ = PendingRequest::None;
  return decoded;
}

int32_t FroniusSmartMeterDecoder::decodeSignedLowWordFirst(
    const uint8_t* data) {
  const uint32_t lowWord = decodeBigEndian16(data);
  const uint32_t highWord = decodeBigEndian16(data + 2);
  return static_cast<int32_t>((highWord << 16U) | lowWord);
}

bool FroniusSmartMeterDecoder::decodeSummary(const uint8_t* frame,
                                             const size_t length,
                                             const uint32_t nowMs) {
  constexpr size_t kDataBytes = kSummaryRegisterCount * 2U;
  if (length != kDataBytes + 5U || frame[2] != kDataBytes) {
    return false;
  }

  const uint8_t* data = frame + 3;
  int32_t values[8];
  for (size_t index = 0; index < 8U; ++index) {
    values[index] = decodeSignedLowWordFirst(data + index * 4U);
  }

  reading_.voltageDecivolts = values[0];
  reading_.voltagePhaseToPhaseDecivolts = values[1];
  reading_.realPowerDeciwatts = values[2];
  reading_.apparentPowerDecivoltAmps = values[3];
  reading_.reactivePowerDecivars = values[4];
  reading_.powerFactorMilli = values[5];
  reading_.unknownSum = values[6];
  reading_.frequencyDecihertz = values[7];
  reading_.summaryMeasuredAtMs = nowMs;
  reading_.summaryValid = true;
  return true;
}

bool FroniusSmartMeterDecoder::decodePhases(const uint8_t* frame,
                                            const size_t length,
                                            const uint32_t nowMs) {
  constexpr size_t kDataBytes = kPhasesRegisterCount * 2U;
  if (length != kDataBytes + 5U || frame[2] != kDataBytes) {
    return false;
  }

  const uint8_t* data = frame + 3;
  for (size_t phase = 0; phase < 3U; ++phase) {
    FroniusSmartMeterPhaseReading& target = reading_.phases[phase];
    const uint8_t* source = data + phase * 28U;
    target.voltagePhaseToPhaseDecivolts =
        decodeSignedLowWordFirst(source + 0U);
    target.voltageDecivolts = decodeSignedLowWordFirst(source + 4U);
    target.currentMilliamps = decodeSignedLowWordFirst(source + 8U);
    target.realPowerDeciwatts = decodeSignedLowWordFirst(source + 12U);
    target.apparentPowerDecivoltAmps =
        decodeSignedLowWordFirst(source + 16U);
    target.reactivePowerDecivars = decodeSignedLowWordFirst(source + 20U);
    target.powerFactorMilli = decodeSignedLowWordFirst(source + 24U);
  }

  reading_.phasesMeasuredAtMs = nowMs;
  reading_.phasesValid = true;
  return true;
}
