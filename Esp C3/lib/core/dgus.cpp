#include "dgus.h"
#include <string.h>

namespace sd {

static constexpr uint8_t MAX_PAYLOAD = 240;

void DgusTx::writeBytes(uint16_t vp, const uint8_t* b, uint8_t n) {
  while (n) {
    uint8_t chunk = n > MAX_PAYLOAD ? MAX_PAYLOAD : n;
    uint8_t f[6 + MAX_PAYLOAD];
    f[0] = 0x5A; f[1] = 0xA5; f[2] = 3 + chunk; f[3] = 0x82;
    f[4] = vp >> 8; f[5] = vp & 0xFF;
    memcpy(f + 6, b, chunk);
    out_.write(f, 6 + chunk);
    b += chunk; n -= chunk; vp += chunk / 2;
  }
}

void DgusTx::writeWords(uint16_t vp, const uint16_t* w, uint8_t n) {
  uint8_t tmp[MAX_PAYLOAD];
  while (n) {
    uint8_t chunk = n > MAX_PAYLOAD / 2 ? MAX_PAYLOAD / 2 : n;
    for (uint8_t i = 0; i < chunk; i++) { tmp[2 * i] = w[i] >> 8; tmp[2 * i + 1] = w[i] & 0xFF; }
    writeBytes(vp, tmp, chunk * 2);
    w += chunk; n -= chunk; vp += chunk;
  }
}

void DgusTx::read(uint16_t vp, uint8_t words) {
  uint8_t f[7] = {0x5A, 0xA5, 0x04, 0x83, (uint8_t)(vp >> 8), (uint8_t)vp, words};
  out_.write(f, 7);
}

void DgusTx::setPage(uint16_t page) {
  uint16_t w[2] = {0x5A01, page};
  writeWords(DGUS_PIC_SET, w, 2);
}

void DgusTx::beep(uint8_t x8ms) {
  uint16_t w = x8ms;
  writeWords(DGUS_BEEP, &w, 1);
}

void DgusTx::brightness(uint8_t pct) {
  uint16_t w = (uint16_t)pct << 8 | pct;
  writeWords(DGUS_BRIGHT, &w, 1);
}

void DgusTx::systemConfig() {
  // D3=5A apply, D0=0x38: auto upload on, touch sound on, no touch standby, 0 degrees
  uint16_t w[2] = {0x5A00, 0x0038};
  writeWords(DGUS_SYS_CONFIG, w, 2);
}

void DgusTx::curvePoint(uint8_t channel, uint16_t value) {
  uint8_t f[7] = {0x5A, 0xA5, 0x04, 0x84, (uint8_t)(1u << channel), (uint8_t)(value >> 8), (uint8_t)value};
  out_.write(f, 7);
}

void DgusTx::curveClear(uint8_t channel) {
  uint16_t w = 0;
  writeWords(DGUS_CURVE0_STATE + channel * 2 + 1, &w, 1);
}

void DgusRx::feed(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t b = p[i];
    switch (state_) {
      case 0: if (b == 0x5A) state_ = 1; break;
      case 1: state_ = b == 0xA5 ? 2 : (b == 0x5A ? 1 : 0); break;
      case 2:
        if (b < 1 || b > 250) { bad_++; state_ = 0; break; }
        need_ = b; pos_ = 0; state_ = 3;
        break;
      default:
        buf_[pos_++] = b;
        if (pos_ == need_) { dispatch(); state_ = 0; }
    }
  }
}

void DgusRx::dispatch() {
  if (buf_[0] == 0x82) { frames_++; return; }          // write ack: 82 4F 4B
  if (buf_[0] != 0x83 || need_ < 4) { bad_++; return; }
  uint8_t words = buf_[3];
  if (need_ < 4u + words * 2u) { bad_++; return; }
  uint16_t out[120];
  if (words > 120) { bad_++; return; }
  for (uint8_t i = 0; i < words; i++) out[i] = (uint16_t)buf_[4 + 2 * i] << 8 | buf_[5 + 2 * i];
  frames_++;
  if (h_) h_(ctx_, (uint16_t)buf_[1] << 8 | buf_[2], out, words);
}

}  // namespace sd
