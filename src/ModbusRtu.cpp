#include "ModbusRtu.h"

uint16_t modbusRtuCrc16(const uint8_t* data, const size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001U) != 0) {
        crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
      } else {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

bool hasValidModbusRtuCrc(const uint8_t* frame, const size_t length) {
  if (length < 4) {
    return false;
  }
  const uint16_t expected = modbusRtuCrc16(frame, length - 2);
  const uint16_t received = static_cast<uint16_t>(frame[length - 2]) |
                            static_cast<uint16_t>(frame[length - 1]) << 8U;
  return expected == received;
}
