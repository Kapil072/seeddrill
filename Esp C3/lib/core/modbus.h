// Modbus RTU framing and the Delta MS300 register map. Addresses, scaling and
// bit meanings are from the MS300 User Manual, Pr.09 address list.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sd_types.h"

namespace sd {

uint16_t modbusCrc(const uint8_t* p, size_t n);

namespace ms300 {
constexpr uint16_t REG_CONTROL = 0x2000;      // bits 1-0: 01 stop, 10 run; bits 5-4: 01 FWD
constexpr uint16_t REG_FREQ = 0x2001;         // 0.01 Hz
constexpr uint16_t REG_FAULT_CTRL = 0x2002;   // bit 1: reset
constexpr uint16_t REG_STATUS_BASE = 0x2100;  // 2100H .. 210CH read as one block
constexpr uint8_t STATUS_WORDS = 13;
constexpr uint16_t CTRL_STOP = 0x0001;
constexpr uint16_t CTRL_RUN_FWD = 0x0012;
constexpr uint16_t FAULT_RESET = 0x0002;
const char* faultName(uint8_t code);
}  // namespace ms300

// All builders return the frame length, CRC included.
size_t mbReadHolding(uint8_t* f, uint8_t id, uint16_t reg, uint16_t count);
size_t mbWriteSingle(uint8_t* f, uint8_t id, uint16_t reg, uint16_t value);
size_t mbWriteMultiple(uint8_t* f, uint8_t id, uint16_t reg, const uint16_t* v, uint8_t count);

enum class MbResult : uint8_t { Ok, Incomplete, BadCrc, Exception, BadReply };

// Checks a reply against the request id/function. On a read, words get the data.
MbResult mbParseRead(const uint8_t* f, size_t n, uint8_t id, uint16_t* words, uint8_t count);
MbResult mbParseWrite(const uint8_t* f, size_t n, uint8_t id, uint8_t fn);
size_t mbExpectedRead(uint8_t count);

// Maps the 2100H.. block onto DriveFeedback fields.
void ms300Decode(const uint16_t* w, DriveFeedback& fb);

}  // namespace sd
