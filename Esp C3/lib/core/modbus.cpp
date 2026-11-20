#include "modbus.h"

namespace sd {

uint16_t modbusCrc(const uint8_t* p, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c >> 1) ^ ((c & 1) ? 0xA001 : 0);
  }
  return c;
}

static size_t finish(uint8_t* f, size_t n) {
  uint16_t c = modbusCrc(f, n);
  f[n] = c & 0xFF; f[n + 1] = c >> 8;
  return n + 2;
}

size_t mbReadHolding(uint8_t* f, uint8_t id, uint16_t reg, uint16_t count) {
  f[0] = id; f[1] = 0x03; f[2] = reg >> 8; f[3] = reg; f[4] = count >> 8; f[5] = count;
  return finish(f, 6);
}

size_t mbWriteSingle(uint8_t* f, uint8_t id, uint16_t reg, uint16_t value) {
  f[0] = id; f[1] = 0x06; f[2] = reg >> 8; f[3] = reg; f[4] = value >> 8; f[5] = value;
  return finish(f, 6);
}

size_t mbWriteMultiple(uint8_t* f, uint8_t id, uint16_t reg, const uint16_t* v, uint8_t count) {
  f[0] = id; f[1] = 0x10; f[2] = reg >> 8; f[3] = reg; f[4] = 0; f[5] = count; f[6] = count * 2;
  for (uint8_t i = 0; i < count; i++) { f[7 + 2 * i] = v[i] >> 8; f[8 + 2 * i] = v[i]; }
  return finish(f, 7 + count * 2);
}

static bool crcOk(const uint8_t* f, size_t n) {
  if (n < 4) return false;
  return modbusCrc(f, n - 2) == (uint16_t)(f[n - 2] | f[n - 1] << 8);
}

size_t mbExpectedRead(uint8_t count) { return 5 + count * 2u; }

MbResult mbParseRead(const uint8_t* f, size_t n, uint8_t id, uint16_t* words, uint8_t count) {
  if (n >= 5 && f[1] == 0x83) return crcOk(f, 5) ? MbResult::Exception : MbResult::BadCrc;
  if (n < mbExpectedRead(count)) return MbResult::Incomplete;
  if (!crcOk(f, mbExpectedRead(count))) return MbResult::BadCrc;
  if (f[0] != id || f[1] != 0x03 || f[2] != count * 2) return MbResult::BadReply;
  for (uint8_t i = 0; i < count; i++) words[i] = (uint16_t)f[3 + 2 * i] << 8 | f[4 + 2 * i];
  return MbResult::Ok;
}

MbResult mbParseWrite(const uint8_t* f, size_t n, uint8_t id, uint8_t fn) {
  if (n >= 5 && f[1] == (fn | 0x80)) return crcOk(f, 5) ? MbResult::Exception : MbResult::BadCrc;
  if (n < 8) return MbResult::Incomplete;
  if (!crcOk(f, 8)) return MbResult::BadCrc;
  return (f[0] == id && f[1] == fn) ? MbResult::Ok : MbResult::BadReply;
}

void ms300Decode(const uint16_t* w, DriveFeedback& fb) {
  fb.fault_code = w[0] & 0xFF;      // 2100H low byte: error, high byte: warning
  fb.warn_code = w[0] >> 8;
  fb.status_bits = w[1] & 0x03;     // 2101H: 00 stop, 01 decel, 10 standby, 11 operating
  fb.cmd_hz_x100 = w[2];            // 2102H
  fb.hz_x100 = w[3];                // 2103H
  fb.amps_x100 = w[4];              // 2104H
  fb.vbus_x10 = w[5];               // 2105H
  fb.torque_x10 = (int16_t)w[11];   // 210BH
  fb.motor_rpm = w[12];             // 210CH
}

namespace ms300 {
const char* faultName(uint8_t c) {
  switch (c) {
    case 0: return "None";
    case 1: return "ocA over-current in accel";
    case 2: return "ocd over-current in decel";
    case 3: return "ocn over-current at speed";
    case 4: return "GFF ground fault";
    case 6: return "ocS over-current at stop";
    case 7: return "ovA over-voltage in accel";
    case 8: return "ovd over-voltage in decel";
    case 9: return "ovn over-voltage at speed";
    case 10: return "ovS over-voltage at stop";
    case 11: return "LvA low voltage in accel";
    case 12: return "Lvd low voltage in decel";
    case 13: return "Lvn low voltage at speed";
    case 14: return "LvS low voltage at stop";
    case 15: return "OrP input phase loss";
    case 16: return "oH1 IGBT over-heat";
    case 21: return "oL drive overload";
    case 22: return "EoL1 motor overload";
    case 23: return "EoL2 motor overload";
    case 24: return "oH3 motor over-heat";
    case 26: return "ot1 over-torque";
    case 27: return "ot2 over-torque";
    case 49: return "EF external fault";
    case 50: return "EF1 emergency stop";
    case 51: return "bb base block";
    case 54: case 55: case 56: case 57: return "CE communication error";
    case 58: return "CE10 comms time-out";
    default: return "Drive fault";
  }
}
}  // namespace ms300

}  // namespace sd
