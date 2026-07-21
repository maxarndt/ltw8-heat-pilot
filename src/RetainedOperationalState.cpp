#include "RetainedOperationalState.h"

namespace {
constexpr uint32_t kMagic = 0x48504F53U;  // "HPOS"
constexpr uint8_t kVersion = 1;

uint16_t checksum(const uint32_t magic, const uint8_t version,
                  const uint8_t activity) {
  uint16_t value = 0xA5C3U;
  value ^= static_cast<uint16_t>(magic);
  value ^= static_cast<uint16_t>(magic >> 16U);
  value ^= static_cast<uint16_t>(version) << 8U;
  value ^= activity;
  return value;
}

bool activityValid(const uint8_t activity) {
  return activity <= static_cast<uint8_t>(RetainedActivity::PumpOverrun);
}
}  // namespace

void writeRetainedActivity(RetainedOperationalState& state,
                           const RetainedActivity activity) {
  state.magic = kMagic;
  state.version = kVersion;
  state.activity = static_cast<uint8_t>(activity);
  state.checksum = checksum(state.magic, state.version, state.activity);
}

bool retainedOperationalStateValid(const RetainedOperationalState& state) {
  return state.magic == kMagic && state.version == kVersion &&
         activityValid(state.activity) &&
         state.checksum == checksum(state.magic, state.version, state.activity);
}

RetainedActivity readRetainedActivity(const RetainedOperationalState& state) {
  return retainedOperationalStateValid(state)
             ? static_cast<RetainedActivity>(state.activity)
             : RetainedActivity::Idle;
}

bool shouldRecoverPumpOverrun(const RetainedOperationalState& state) {
  const RetainedActivity activity = readRetainedActivity(state);
  return activity == RetainedActivity::Heating ||
         activity == RetainedActivity::PumpOverrun;
}
