#pragma once

#include <stddef.h>
#include <stdint.h>

struct FroniusSmartMeterPhaseReading {
  int32_t voltagePhaseToPhaseDecivolts = 0;
  int32_t voltageDecivolts = 0;
  int32_t currentMilliamps = 0;
  int32_t realPowerDeciwatts = 0;
  int32_t apparentPowerDecivoltAmps = 0;
  int32_t reactivePowerDecivars = 0;
  int32_t powerFactorMilli = 0;
};

struct FroniusSmartMeterReading {
  bool summaryValid = false;
  bool phasesValid = false;
  uint32_t summaryMeasuredAtMs = 0;
  uint32_t phasesMeasuredAtMs = 0;
  int32_t voltageDecivolts = 0;
  int32_t voltagePhaseToPhaseDecivolts = 0;
  int32_t realPowerDeciwatts = 0;
  int32_t apparentPowerDecivoltAmps = 0;
  int32_t reactivePowerDecivars = 0;
  int32_t powerFactorMilli = 0;
  int32_t unknownSum = 0;
  int32_t frequencyDecihertz = 0;
  FroniusSmartMeterPhaseReading phases[3]{};
};

bool isFroniusSmartMeterSummaryFresh(
    const FroniusSmartMeterReading& reading, uint32_t nowMs,
    uint32_t maximumAgeMs);
int32_t froniusGridPowerToSurplusWatts(int32_t gridPowerDeciwatts);

class FroniusSmartMeterDecoder {
 public:
  bool processFrame(const uint8_t* frame, size_t length, uint32_t nowMs);
  const FroniusSmartMeterReading& reading() const { return reading_; }

 private:
  enum class PendingRequest : uint8_t { None, Summary, Phases };

  static int32_t decodeSignedLowWordFirst(const uint8_t* data);
  bool decodeSummary(const uint8_t* frame, size_t length, uint32_t nowMs);
  bool decodePhases(const uint8_t* frame, size_t length, uint32_t nowMs);

  PendingRequest pendingRequest_ = PendingRequest::None;
  FroniusSmartMeterReading reading_{};
};
