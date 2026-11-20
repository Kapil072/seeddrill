// DGUS II serial protocol: 5A A5 <len> <cmd> <data>. Allocation-free.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace sd {

class ByteSink {
 public:
  virtual ~ByteSink() {}
  virtual void write(const uint8_t* p, size_t n) = 0;
};

constexpr uint16_t DGUS_SYS_CONFIG = 0x0080, DGUS_BRIGHT = 0x0082, DGUS_PIC_SET = 0x0084,
                   DGUS_PIC_NOW = 0x0014, DGUS_BEEP = 0x00A0, DGUS_RTC_SET = 0x009C,
                   DGUS_VERSION = 0x000F, DGUS_CURVE0_STATE = 0x0300;

class DgusTx {
 public:
  explicit DgusTx(ByteSink& out) : out_(out) {}
  void writeWords(uint16_t vp, const uint16_t* w, uint8_t n);
  void writeBytes(uint16_t vp, const uint8_t* b, uint8_t n);
  void read(uint16_t vp, uint8_t words);
  void setPage(uint16_t page);
  void beep(uint8_t x8ms);
  void brightness(uint8_t pct);
  // Forces touch auto-upload on. Not saved by the display, so sent at every link-up.
  void systemConfig();
  void curvePoint(uint8_t channel, uint16_t value);
  void curveClear(uint8_t channel);

 private:
  ByteSink& out_;
};

// Streaming frame parser. Handles 0x83 replies and touch uploads; 0x82 acks are
// consumed silently. Resynchronises by itself after noise.
class DgusRx {
 public:
  using Handler = void (*)(void* ctx, uint16_t vp, const uint16_t* words, uint8_t n);
  void setHandler(Handler h, void* ctx) { h_ = h; ctx_ = ctx; }
  void feed(const uint8_t* p, size_t n);
  uint32_t frames() const { return frames_; }
  uint32_t badFrames() const { return bad_; }

 private:
  void dispatch();
  Handler h_ = nullptr;
  void* ctx_ = nullptr;
  uint8_t buf_[256];
  uint16_t pos_ = 0, need_ = 0;
  uint8_t state_ = 0;
  uint32_t frames_ = 0, bad_ = 0;
};

}  // namespace sd
