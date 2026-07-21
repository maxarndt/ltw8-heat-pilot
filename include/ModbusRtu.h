#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t modbusRtuCrc16(const uint8_t* data, size_t length);
bool hasValidModbusRtuCrc(const uint8_t* frame, size_t length);
