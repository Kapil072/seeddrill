#include "machine_ctl.h"

namespace sd {

static uint32_t abs32(int32_t v) { return v < 0 ? -v : v; }

void MachineCtl::configure(const MachineProfile& p, const DriveConfig& d, bool sim) {
  prof_ = p;
  drv_ = d;
  sim_ = sim;
  limit_ = compute(prof_, drv_, kin_);
  if (state_ == RunState::Ready || state_ == RunState::Checking) hz_target_ = kin_.hz;
}

bool MachineCtl::permissivesOk() const {
  return in_.estop_ok && in_.guard_ok && in_.local_enable && t_.drive_online && t_.drive_ready && limit_ == Limit::Ok;
}

void MachineCtl::beginTest(bool remote, uint32_t now_unix) {
  memset(&hdr_, 0, sizeof(hdr_));
  hdr_.magic = TEST_MAGIC;
  hdr_.version = 1;
  hdr_.id = next_id_++;
  hdr_.start_unix = now_unix;
  hdr_.rpm_target_x10 = (uint16_t)(kin_.out_rpm * 10.0f + 0.5f);
  hdr_.hz_target_x100 = (uint16_t)(kin_.hz * 100.0f + 0.5f);
  hdr_.sim = sim_;
  hdr_.measured = drv_.ppr ? 1 : 0;
  hdr_.profile = prof_;
  hdr_.drive = drv_;

  n_ = samples_ = 0;
  sum_rpm_ = sum_err_ = sum_amps_ = 0;
  min_rpm_ = 0xFFFF; max_rpm_ = 0; peak_amps_ = 0;
  dist_m_ = 0; run_ms_ = 0; next_sample_ms_ = 0;
  oc_ms_ = dev_ms_ = stall_ms_ = link_ms_ = 0;
  hz_ramp_ = 0; hz_target_ = kin_.hz;
  steady_ = false;
  remote_run_ = remote;
  warn_left_ms_ = remote ? REMOTE_WARN_MS : 0;
  started_motion_ = !remote;
  end_ = EndReason::None;
  state_ = RunState::Running;
  ev_.onTestStart(hdr_);
}

void MachineCtl::requestStop(EndReason why) {
  if (state_ != RunState::Running) return;
  end_ = why;
  state_ = RunState::Stopping;
}

// Every fault takes the drive command to zero. cut_permit drops the permit relay
// at once for conditions where a controlled ramp cannot be trusted.
void MachineCtl::raise(Fault f, bool cut_permit) {
  if (state_ == RunState::Fault) return;
  fault_ = f;
  bool in_test = state_ == RunState::Running || state_ == RunState::Stopping;
  if (in_test) {
    end_ = EndReason::Faulted;
    finish();
  }
  state_ = RunState::Fault;
  confirmed_ = false;
  hz_ramp_ = 0;
  if (cut_permit) permit_ = false;
  ev_.onFault(f, fb_.fault_code);
}

void MachineCtl::finish() {
  hdr_.duration_s = run_ms_ / 1000;
  hdr_.dist_dm = (uint32_t)(dist_m_ * 10.0 + 0.5);
  hdr_.samples = samples_;
  hdr_.end = end_;
  hdr_.fault = fault_;
  hdr_.vfd_fault = fb_.fault_code;
  hdr_.amps_peak_x100 = peak_amps_;
  if (n_) {
    hdr_.rpm_avg_x10 = (uint16_t)(sum_rpm_ / n_ + 0.5);
    hdr_.amps_avg_x100 = (uint16_t)(sum_amps_ / n_ + 0.5);
    hdr_.rpm_min_x10 = min_rpm_;
    hdr_.rpm_max_x10 = max_rpm_;
    double err = sum_err_ / n_;
    hdr_.accuracy_x10 = err >= 100.0 ? 0 : (uint16_t)((100.0 - err) * 10.0 + 0.5);
  }
  ev_.onTestEnd(hdr_);
  t_.last_end = end_;
  remote_run_ = false;
}

void MachineCtl::logSample() {
  Sample s;
  s.t_s = (uint16_t)(run_ms_ / 1000);
  s.rpm_target_x10 = t_.rpm_target_x10;
  s.rpm_x10 = t_.rpm_x10;
  s.hz_x100 = fb_.hz_x100;
  s.amps_x100 = fb_.amps_x100;
  s.v_sim_x10 = t_.v_sim_x10;
  s.dist_m = (uint16_t)(dist_m_ > 65535.0 ? 65535 : dist_m_);
  s.state = (uint8_t)state_;
  s.fault = (uint8_t)fault_;
  ev_.onSample(s);
  samples_++;

  if (fb_.amps_x100 > peak_amps_) peak_amps_ = fb_.amps_x100;
  if (steady_ && state_ == RunState::Running && t_.rpm_target_x10) {
    n_++;
    sum_rpm_ += t_.rpm_x10;
    sum_amps_ += fb_.amps_x100;
    sum_err_ += 100.0 * abs32((int32_t)t_.rpm_x10 - t_.rpm_target_x10) / t_.rpm_target_x10;
    if (t_.rpm_x10 < min_rpm_) min_rpm_ = t_.rpm_x10;
    if (t_.rpm_x10 > max_rpm_) max_rpm_ = t_.rpm_x10;
  }
}

void MachineCtl::handle(const Cmd& c, uint32_t now_unix) {
  switch (c.type) {
    case CmdType::Arm:
      if (state_ == RunState::Ready) { state_ = RunState::Checking; confirmed_ = false; }
      break;
    case CmdType::Disarm:
      if (state_ == RunState::Checking) { state_ = RunState::Ready; confirmed_ = false; }
      break;
    case CmdType::ConfirmSecured:
      if (state_ == RunState::Checking) confirmed_ = c.flag != 0;
      break;
    case CmdType::Start:
      if (state_ == RunState::Checking && confirmed_ && permissivesOk()) beginTest(false, now_unix);
      break;
    case CmdType::Stop:
      requestStop(EndReason::OperatorStop);
      if (state_ == RunState::Checking) { state_ = RunState::Ready; confirmed_ = false; }
      break;
    case CmdType::RemoteStart:
      // Needs the key switch and the operator's on-screen permission; both are local.
      if ((state_ == RunState::Ready || state_ == RunState::Checking || state_ == RunState::Complete) && remote_allowed_ && permissivesOk())
        beginTest(true, now_unix);
      break;
    case CmdType::RemoteStop:
      requestStop(EndReason::RemoteStop);
      break;
    case CmdType::RemoteSetpoint:
      if (remote_allowed_ && state_ != RunState::Stopping && state_ != RunState::Fault) {
        MachineProfile trial = prof_;
        if (trial.mode == MODE_FIELD) trial.speed_x10 = c.arg; else trial.rpm_x10 = c.arg;
        Kinematics k;
        if (compute(trial, drv_, k) == Limit::Ok) {   // out-of-range requests change nothing
          prof_ = trial;
          kin_ = k;
          limit_ = Limit::Ok;
          hz_target_ = k.hz;
        }
      }
      break;
    case CmdType::SetRemoteAllowed:
      remote_allowed_ = c.flag != 0;
      break;
    case CmdType::ResetFault:
      if (state_ == RunState::Fault) {
        reset_pulse_ = true;
        // Holds until the cause is gone, so a reset can never lead to motion on its own.
        if (in_.estop_ok && in_.guard_ok && !fb_.fault_code) {
          state_ = RunState::Ready;
          fault_ = Fault::None;
        }
      }
      break;
    case CmdType::Acknowledge:
      if (state_ == RunState::Complete) state_ = RunState::Ready;
      break;
    default:
      break;
  }
}

void MachineCtl::tick(uint32_t now_ms, uint32_t now_unix, const Inputs& in, const DriveFeedback& fb, const Links& links) {
  uint32_t dt = last_ms_ ? now_ms - last_ms_ : 0;
  last_ms_ = now_ms;
  in_ = in;
  fb_ = fb;
  (void)now_unix;

  t_.drive_online = fb.online && fb.age_ms < COMMS_TIMEOUT_MS;
  t_.drive_ready = t_.drive_online && fb.fault_code == 0 && in.drive_relay_ok;
  comms_ms_ = t_.drive_online ? 0 : comms_ms_ + dt;

  // Panel buttons act on the press edge; a button held through a reset or power-up
  // does nothing until released and pressed again.
  bool start_edge = in.start_pb && !prev_start_;
  bool stop_edge = in.stop_pb && !prev_stop_;
  prev_start_ = in.start_pb;
  prev_stop_ = in.stop_pb;
  if (stop_edge) handle({CmdType::Stop, 0, 0}, now_unix);
  if (start_edge) handle({CmdType::Start, 0, 0}, now_unix);

  bool active = state_ == RunState::Running || state_ == RunState::Stopping;

  // Interlocks: highest priority first, each one ends the test.
  if (active) {
    if (!in.estop_ok) raise(Fault::EStop, true);
    else if (!in.guard_ok) raise(Fault::Guard, true);
    else if (!in.local_enable) raise(Fault::LocalEnableLost, true);
    else if (comms_ms_ >= COMMS_TIMEOUT_MS) raise(Fault::DriveComms, true);
    else if (fb.fault_code) raise(Fault::DriveFault, true);
    else if (!in.drive_relay_ok) raise(Fault::DriveRelay, true);
    else if (!links.hmi_ok) raise(Fault::HmiLost, false);
  } else if (state_ != RunState::Fault) {
    if (fb.online && fb.fault_code) raise(Fault::DriveFault, true);
  }

  active = state_ == RunState::Running || state_ == RunState::Stopping;
  if (active && state_ == RunState::Running && started_motion_) {
    oc_ms_ = fb.amps_x100 > drv_.amps_max_x10 * 10u ? oc_ms_ + dt : 0;
    if (oc_ms_ >= OVERCURRENT_MS) raise(Fault::OverCurrent, false);
  }

  // Remote run depends on the link that started it.
  if (state_ == RunState::Running && remote_run_) {
    link_ms_ = links.cloud_ok ? 0 : link_ms_ + dt;
    if (link_ms_ >= REMOTE_LINK_MS) requestStop(EndReason::RemoteLinkLost);
  }

  if (state_ == RunState::Running && warn_left_ms_) {
    warn_left_ms_ = warn_left_ms_ > dt ? warn_left_ms_ - dt : 0;
    if (!warn_left_ms_) started_motion_ = true;
  }
  warn_ = warn_left_ms_ > 0;

  // Speed ramp, in Hz per ms from the configured 0-to-rated times.
  active = state_ == RunState::Running || state_ == RunState::Stopping;
  if (active) {
    run_ms_ += started_motion_ ? dt : 0;
    float rated = drv_.rated_hz_x10 / 10.0f;
    if (state_ == RunState::Running && started_motion_) {
      float step = rated / (drv_.acc_x10 ? drv_.acc_x10 * 100.0f : 1000.0f) * dt;
      float dn = rated / (drv_.dec_x10 ? drv_.dec_x10 * 100.0f : 1000.0f) * dt;
      if (hz_ramp_ < hz_target_) { hz_ramp_ += step; if (hz_ramp_ > hz_target_) hz_ramp_ = hz_target_; }
      else if (hz_ramp_ > hz_target_) { hz_ramp_ -= dn; if (hz_ramp_ < hz_target_) hz_ramp_ = hz_target_; }
    } else if (state_ == RunState::Stopping) {
      float dn = rated / (drv_.dec_x10 ? drv_.dec_x10 * 100.0f : 1000.0f) * dt;
      hz_ramp_ = hz_ramp_ > dn ? hz_ramp_ - dn : 0;
    }
  }

  // Live values: measured when a sensor is fitted, otherwise estimated from the drive.
  float rpm = drv_.ppr ? in.sensor_rpm_x10 / 10.0f : outRpmFromHz(fb.hz_x100 / 100.0f, drv_);
  float kmh = speedFromOutRpm(rpm, prof_);
  if (active) dist_m_ += (double)(kmh / 3.6f) * dt / 1000.0;

  if (state_ == RunState::Running && started_motion_) {
    float tgt = hz_target_ * 100.0f;
    if (!steady_ && tgt > 0 && hz_ramp_ >= hz_target_ && fb.hz_x100 >= tgt * 0.98f) steady_ = true;

    // Drive not following its command.
    if (!steady_ && hz_ramp_ > 2.0f && fb.hz_x100 < hz_ramp_ * 100.0f * 0.2f) stall_ms_ += dt; else stall_ms_ = 0;
    if (stall_ms_ >= STALL_MS) raise(Fault::Stall, false);

    if (steady_ && drv_.dev_x10) {
      float target_rpm = outRpmFromHz(hz_target_, drv_);
      float err = target_rpm > 0 ? (rpm > target_rpm ? rpm - target_rpm : target_rpm - rpm) / target_rpm * 1000.0f : 0;
      dev_ms_ = err > drv_.dev_x10 ? dev_ms_ + dt : 0;
      if (dev_ms_ >= DEVIATION_MS) raise(Fault::SpeedDeviation, false);
    }
  }

  // Test end
  uint32_t remaining = 0;
  uint8_t progress = 0;
  if (state_ == RunState::Running || state_ == RunState::Stopping) {
    if (prof_.stop_by == STOP_TIME) {
      uint32_t goal = prof_.length_x10 * 6u;   // minutes x10 to seconds
      uint32_t el = run_ms_ / 1000;
      remaining = goal > el ? goal - el : 0;
      progress = goal ? (uint8_t)(el * 100u / goal > 100 ? 100 : el * 100u / goal) : 0;
      if (el >= goal) requestStop(EndReason::Completed);
    } else {
      double goal = prof_.length_x10 / 10.0;
      double left = goal - dist_m_;
      remaining = left > 0 && kmh > 0.05f ? (uint32_t)(left / (kmh / 3.6f)) : 0;
      progress = goal > 0 ? (uint8_t)(dist_m_ >= goal ? 100 : dist_m_ * 100.0 / goal) : 0;
      if (dist_m_ >= goal) requestStop(EndReason::Completed);
    }
  }

  active = state_ == RunState::Running || state_ == RunState::Stopping;
  if (state_ == RunState::Stopping && hz_ramp_ <= 0 && fb.hz_x100 < STOPPED_HZ_X100) {
    finish();
    state_ = RunState::Complete;
    active = false;
  }

  // Outputs
  if (active) {
    out_.run = started_motion_ ? 1 : 0;
    out_.hz_x100 = (uint16_t)(hz_ramp_ * 100.0f + 0.5f);
    permit_ = true;
  } else {
    out_.run = 0;
    out_.hz_x100 = 0;
    if (fb.hz_x100 < STOPPED_HZ_X100) permit_ = false;
  }
  out_.reset = reset_pulse_ ? 1 : 0;
  reset_pulse_ = false;

  // Published view
  t_.seq++;
  t_.test_id = hdr_.id;
  t_.elapsed_s = run_ms_ / 1000;
  t_.remaining_s = remaining;
  t_.dist_dm = (uint32_t)(dist_m_ * 10.0 + 0.5);
  t_.rpm_target_x10 = (uint16_t)(outRpmFromHz(hz_target_, drv_) * 10.0f + 0.5f);
  t_.rpm_x10 = (uint16_t)(rpm * 10.0f + 0.5f);
  t_.v_sim_x10 = (uint16_t)(kmh * 10.0f + 0.5f);
  t_.hz_cmd_x100 = out_.hz_x100;
  t_.hz_out_x100 = fb.hz_x100;
  t_.amps_x100 = fb.amps_x100;
  t_.vbus_x10 = fb.vbus_x10;
  t_.torque_x10 = fb.torque_x10;
  t_.vfd_fault = fb.fault_code;
  t_.vfd_warn = fb.warn_code;
  t_.progress = progress;
  t_.state = state_;
  t_.fault = fault_;
  t_.estop_ok = in.estop_ok;
  t_.guard_ok = in.guard_ok;
  t_.local_enable = in.local_enable;
  t_.target_ok = limit_ == Limit::Ok;
  t_.confirmed = confirmed_;
  t_.remote_allowed = remote_allowed_;
  t_.remote_run = remote_run_;
  t_.sim_drive = sim_;
  t_.measured = drv_.ppr ? 1 : 0;

  // 1 Hz log
  if (active && started_motion_ && run_ms_ >= next_sample_ms_) {
    logSample();
    next_sample_ms_ += 1000;
  }
}

}  // namespace sd
