// Stand-in for the VFD: bench tests and operator training without a motor.
#pragma once
#include "sd_types.h"

namespace sd {

class DriveSim {
 public:
  void step(uint32_t dt_ms, const DriveCommand& c, DriveFeedback& fb) {
    float target = c.run ? c.hz_x100 / 100.0f : 0;
    float rate = 25.0f * dt_ms / 1000.0f;   // Hz/s; the drive's own ramp is faster than ours
    if (hz_ < target) { hz_ += rate; if (hz_ > target) hz_ = target; }
    else { hz_ -= rate; if (hz_ < target) hz_ = target; }
    if (hz_ < 0) hz_ = 0;

    // Small repeatable ripple so traces look like a real drive
    phase_ = phase_ * 1103515245u + 12345u;
    float ripple = ((phase_ >> 16) & 0xFF) / 255.0f - 0.5f;

    fb.online = 1;
    fb.age_ms = 0;
    fb.hz_x100 = (uint16_t)(hz_ * 100.0f + (hz_ > 1 ? ripple * 6.0f : 0) + 0.5f);
    fb.cmd_hz_x100 = c.hz_x100;
    fb.amps_x100 = hz_ > 0.2f ? (uint16_t)(180.0f + hz_ * 6.5f + ripple * 8.0f) : 0;
    fb.vbus_x10 = (uint16_t)(3200 + ripple * 10.0f);
    fb.torque_x10 = hz_ > 0.2f ? (int16_t)(420 + ripple * 30.0f) : 0;
    fb.motor_rpm = (uint16_t)(hz_ * 28.8f);
    fb.status_bits = hz_ > 0.1f ? 3 : 0;
    fb.fault_code = 0;
    fb.warn_code = 0;
  }

 private:
  float hz_ = 0;
  uint32_t phase_ = 1;
};

}  // namespace sd
