#pragma once

#include <stdint.h>

enum class RetainedActivity : uint8_t {
  Idle = 0,
  Heating = 1,
  PumpOverrun = 2,
};

struct RetainedOperationalState {
  uint32_t magic;
  uint8_t version;
  uint8_t activity;
  uint16_t checksum;
};

static_assert(sizeof(RetainedOperationalState) == 8,
              "Retained operational state must remain compact");

void writeRetainedActivity(RetainedOperationalState& state,
                           RetainedActivity activity);
bool retainedOperationalStateValid(const RetainedOperationalState& state);
RetainedActivity readRetainedActivity(const RetainedOperationalState& state);
bool shouldRecoverPumpOverrun(const RetainedOperationalState& state);
