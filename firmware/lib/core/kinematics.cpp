#include "kinematics.h"

namespace sd {

static constexpr float PI_F = 3.14159265f;

float stageRatio(const MachineProfile& p) {
  float r = 1.0f;
  if (p.s1_driver && p.s1_driven) r *= (float)p.s1_driver / p.s1_driven;
  if (p.s2_driver && p.s2_driven) r *= (float)p.s2_driver / p.s2_driven;
  return r;
}

float rigRatio(const MachineProfile& p) {
  return (p.rig_driver && p.rig_driven) ? (float)p.rig_driver / p.rig_driven : 1.0f;
}

// Spec: f = ((N_shaft * R_gear * P) / 60) * (1 + S), P being pole PAIRS.
float slipFactor(const DriveConfig& d) { return 1.0f + d.slip_x10 / 1000.0f; }

static float hzFromOutRpm(float out_rpm, const DriveConfig& d) {
  float motor = out_rpm * d.gear_x100 / 100.0f;
  return motor * (d.poles / 2.0f) / 60.0f * slipFactor(d);
}

float outRpmFromHz(float hz, const DriveConfig& d) {
  if (!d.gear_x100 || !d.poles) return 0;
  float motor = hz / slipFactor(d) * 60.0f / (d.poles / 2.0f);
  return motor / (d.gear_x100 / 100.0f);
}

// Machine input shaft speed that corresponds to a ground speed depends on where
// the rig is coupled: straight onto the wheel axle, or past the chain stages.
static float machinePerWheel(const MachineProfile& p) {
  return p.coupling == COUPLE_SHAFT ? stageRatio(p) : 1.0f;
}

float speedFromOutRpm(float out_rpm, const MachineProfile& p) {
  float machine = out_rpm * rigRatio(p);
  float wheel = machine / machinePerWheel(p);
  return wheel * 60.0f * PI_F * (p.wheel_mm / 1000.0f) / 1000.0f;
}

Limit compute(const MachineProfile& p, const DriveConfig& d, Kinematics& k) {
  memset(&k, 0, sizeof(k));
  if (!p.wheel_mm || !d.gear_x100 || d.poles < 2) return Limit::BadInput;
  k.ratio = stageRatio(p);
  k.rig_ratio = rigRatio(p);
  float D = p.wheel_mm / 1000.0f;

  if (p.mode == MODE_FIELD) {
    if (!p.speed_x10) return Limit::BadInput;
    k.speed_kmh = p.speed_x10 / 10.0f;
    k.wheel_rpm = (1000.0f * k.speed_kmh) / (60.0f * PI_F * D);
    k.out_rpm = k.wheel_rpm * machinePerWheel(p) / k.rig_ratio;
  } else {
    if (!p.rpm_x10) return Limit::BadInput;
    k.out_rpm = p.rpm_x10 / 10.0f;
    k.speed_kmh = speedFromOutRpm(k.out_rpm, p);
    k.wheel_rpm = k.out_rpm * k.rig_ratio / machinePerWheel(p);
  }
  k.shaft_rpm = k.wheel_rpm * k.ratio;
  k.motor_rpm = k.out_rpm * d.gear_x100 / 100.0f;
  k.hz = hzFromOutRpm(k.out_rpm, d);

  if (k.out_rpm * 10.0f < d.rpm_min_x10 - 0.5f) return Limit::BelowMin;
  if (k.out_rpm * 10.0f > d.rpm_max_x10 + 0.5f) return Limit::AboveMax;
  if (k.hz * 10.0f > d.hz_max_x10 + 0.5f) return Limit::OverFrequency;
  return Limit::Ok;
}

const char* limitText(Limit l) {
  switch (l) {
    case Limit::Ok: return "";
    case Limit::BadInput: return "Enter speed and wheel diameter";
    case Limit::BelowMin: return "Output too slow: below minimum RPM";
    case Limit::AboveMax: return "Output too fast: above maximum RPM";
    case Limit::OverFrequency: return "Needs more than max drive frequency";
  }
  return "";
}

SeedRate seedRate(float speed_kmh, const MachineProfile& p) {
  SeedRate s{0, 0, 0};
  float d = p.spacing_x10 / 1000.0f;   // cm x10 to m
  if (d <= 0 || speed_kmh <= 0) return s;
  float v = speed_kmh / 3.6f;
  s.drop_hz = v / d;
  s.interval_ms = 1000.0f / s.drop_hz;
  if (p.cells) s.disc_rpm = s.drop_hz / p.cells * 60.0f;
  return s;
}

float ratePerHa(float grams, float dist_m, const MachineProfile& p) {
  float width = p.rows * (p.row_sp_x10 / 1000.0f);
  float area = dist_m * width;
  if (area <= 0) return 0;
  return (grams / 1000.0f) / (area / 10000.0f);
}

}  // namespace sd
