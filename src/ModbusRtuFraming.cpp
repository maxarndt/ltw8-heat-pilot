#include "ModbusRtuFraming.h"

#include "ModbusRtu.h"

namespace {

bool validPrefix(const uint8_t* data, const size_t available,
                 const size_t candidateLength) {
  return available >= candidateLength &&
         hasValidModbusRtuCrc(data, candidateLength);
}

}  // namespace

size_t completeModbusRtuFrameLength(const uint8_t* data,
                                    const size_t length) {
  if (data == nullptr || length < 2U) {
    return 0;
  }

  const uint8_t function = data[1];
  if ((function & 0x80U) != 0U) {
    return validPrefix(data, length, 5U) ? 5U : 0U;
  }

  if (function == 0x03U || function == 0x04U) {
    // A read request always has eight bytes. Check its CRC before treating the
    // third byte as the byte count of a response.
    if (validPrefix(data, length, 8U)) {
      return 8U;
    }
    if (length < 3U || data[2] < 2U || (data[2] & 1U) != 0U) {
      return 0U;
    }
    const size_t responseLength = static_cast<size_t>(data[2]) + 5U;
    return validPrefix(data, length, responseLength) ? responseLength : 0U;
  }

  if (function == 0x10U) {
    // A successful write-multiple-registers response has eight bytes.
    if (validPrefix(data, length, 8U)) {
      return 8U;
    }
    if (length < 7U || data[6] == 0U) {
      return 0U;
    }
    const size_t requestLength = static_cast<size_t>(data[6]) + 9U;
    return validPrefix(data, length, requestLength) ? requestLength : 0U;
  }

  // The other currently observed fixed-size request/response functions use
  // eight-byte RTU frames. Unknown variable-length functions fall back to the
  // inter-frame gap and are retained as raw diagnostic frames.
  return validPrefix(data, length, 8U) ? 8U : 0U;
}
