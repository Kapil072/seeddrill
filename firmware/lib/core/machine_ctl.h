// Test sequencing and interlocks. Runs at 100 Hz on the real-time core; pure logic,
// no I/O and no allocation, so every safety rule is exercised by the host tests.
//
// This is software supervision only. The emergency stop is hardwired and acts on
// the drive without passing through this code (Project Summary section 7).
#pragma once
#include "kinematics.h"
#include "sd_types.h"

namespace sd {

class CtlEvents {
 public:
  virtual ~CtlEvents() {}
  virtual void onTestStart(const TestHeader& h) = 0;
  virtual void onSample(const Sample& s) = 0;
  virtual void onTestEnd(const TestHeader& h) = 0;
  virtual void onFault(Fault f, uint16_t vfd_code) = 0;
};

struct Links {
  bool hmi_ok;
  bool cloud_ok;
};

class MachineCtl {
 public:
  static constexpr uint32_t COMMS_TIMEOUT_MS = 500;
  static constexpr uint32_t OVERCURRENT_MS = 2000;
  static constexpr uint32_t DEVIATION_MS = 3000;
  static constexpr uint32_t STALL_MS = 8000;
  static constexpr uint32_t REMOTE_LINK_MS = 10000;
  static constexpr uint32_t REMOTE_WARN_MS = 3000;   // horn before remote motion
  static constexpr uint16_t STOPPED_HZ_X100 = 50;

  explicit MachineCtl(CtlEvents& ev) : ev_(ev) {}

  void configure(const MachineProfile& p, const DriveConfig& d, bool sim);
  void handle(const Cmd& c, uint32_t now_unix);
  void tick(uint32_t now_ms, uint32_t now_unix, const Inputs& in, const DriveFeedback& fb, const Links& links);

  const DriveCommand& driveCommand() const { return out_; }
  bool runPermit() const { return permit_; }
  bool warning() const { return warn_; }
  const Telemetry& telemetry() const { return t_; }
  const TestHeader& lastTest() const { return hdr_; }
  void setNextTestId(uint32_t id) { next_id_ = id; }

 private:
  bool permissivesOk() const;
  void beginTest(bool remote, uint32_t now_unix);
  void requestStop(EndReason why);
  void raise(Fault f, bool cut_permit);
  void finish();
  void logSample();
  void updateTarget();

  CtlEvents& ev_;
  MachineProfile prof_{};
  DriveConfig drv_{};
  Kinematics kin_{};
  Limit limit_ = Limit::BadInput;

  Telemetry t_{};
  TestHeader hdr_{};
  DriveCommand out_{};
  Inputs in_{};
  DriveFeedback fb_{};

  RunState state_ = RunState::Ready;
  Fault fault_ = Fault::None;
  EndReason end_ = EndReason::None;
  bool permit_ = false, warn_ = false, confirmed_ = false, remote_allowed_ = false, remote_run_ = false;
  bool sim_ = false, reset_pulse_ = false, prev_start_ = true, prev_stop_ = true, steady_ = false;
  bool started_motion_ = false;

  float hz_target_ = 0, hz_ramp_ = 0;
  double dist_m_ = 0;
  uint32_t last_ms_ = 0, run_ms_ = 0, warn_left_ms_ = 0, next_sample_ms_ = 0;
  uint32_t oc_ms_ = 0, dev_ms_ = 0, stall_ms_ = 0, link_ms_ = 0, comms_ms_ = 0;
  uint32_t next_id_ = 1;

  // Steady-state statistics
  uint32_t n_ = 0;
  double sum_rpm_ = 0, sum_err_ = 0, sum_amps_ = 0;
  uint16_t min_rpm_ = 0xFFFF, max_rpm_ = 0, peak_amps_ = 0;
  uint32_t samples_ = 0;
};

}  // namespace sd
