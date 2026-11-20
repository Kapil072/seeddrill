// Field-speed to motor-frequency conversion (TECH-SPEC 3.2, Hardware Details 6).
#pragma once
#include "sd_types.h"

namespace sd {

struct Kinematics {
  float wheel_rpm;      // equivalent ground wheel
  float ratio;          // wheel to driven shaft, product of driver/driven stages
  float shaft_rpm;      // metering shaft
  float rig_ratio;      // gearbox output to machine input
  float out_rpm;        // true gearbox output
  float motor_rpm;
  float hz;             // VFD command
  float speed_kmh;      // simulated ground speed
};

enum class Limit : uint8_t { Ok, BadInput, BelowMin, AboveMax, OverFrequency };

float stageRatio(const MachineProfile& p);
float rigRatio(const MachineProfile& p);
float slipFactor(const DriveConfig& d);

// Solves the whole chain from the profile, in either mode.
Limit compute(const MachineProfile& p, const DriveConfig& d, Kinematics& k);
const char* limitText(Limit l);

// Actual drive frequency back to machine values, for live readings.
float outRpmFromHz(float hz, const DriveConfig& d);
float speedFromOutRpm(float out_rpm, const MachineProfile& p);

struct SeedRate {
  float drop_hz, interval_ms, disc_rpm;
};
SeedRate seedRate(float speed_kmh, const MachineProfile& p);

// kg/ha from grams collected over the simulated distance.
float ratePerHa(float grams, float dist_m, const MachineProfile& p);

}  // namespace sd
