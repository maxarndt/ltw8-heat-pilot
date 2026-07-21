#pragma once

#include <stddef.h>
#include <stdint.h>

// Returns the length of the first complete, CRC-valid RTU frame in the buffer.
// Zero means that more bytes are required or the buffered prefix is invalid.
size_t completeModbusRtuFrameLength(const uint8_t* data, size_t length);
