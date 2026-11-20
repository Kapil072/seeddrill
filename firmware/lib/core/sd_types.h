// Plain data shared by every module. Everything is fixed size and trivially
// copyable: no pointers, no heap, safe for SeqLock, NVS blobs and file records.
// Values are scaled integers so stored data is exact and portable.
#pragma once
#include <stdint.h>
#include <string.h>

namespace sd {

constexpr uint8_t PROFILE_SLOTS = 12;
constexpr uint8_t NAME_LEN = 24;

enum Mode : uint8_t { MODE_FIELD = 0, MODE_DIRECT = 1 };
enum StopBy : uint8_t { STOP_TIME = 0, STOP_DISTANCE = 1 };
enum Coupling : uint8_t { COUPLE_WHEEL = 0, COUPLE_SHAFT = 1 };

struct MachineProfile {
  char name[NAME_LEN];
  uint16_t wheel_mm;        // ground wheel diameter
  uint16_t speed_x10;       // km/h, field mode
  uint16_t rpm_x10;         // gearbox output RPM, direct mode
  uint32_t length_x10;      // minutes or metres, per stop_by
  uint16_t spacing_x10;     // cm
  uint16_t cells;           // metering disc cells
  uint16_t row_sp_x10;      // cm
  uint8_t rows;
  uint8_t s1_driver, s1_driven, s2_driver, s2_driven;   // teeth; 0 = stage unused
  uint8_t rig_driver, rig_driven;
  uint8_t mode, stop_by, coupling, crop;
  uint8_t used;
};

struct DriveConfig {
  uint16_t rated_rpm;       // nameplate, e.g. 1440
  uint16_t rated_hz_x10;
  uint16_t slip_x10;        // %
  uint16_t gear_x100;       // reduction, e.g. 1500 = 15.00:1
  uint16_t rpm_min_x10, rpm_max_x10;   // true gearbox output limits
  uint16_t hz_max_x10;
  uint16_t amps_max_x10;
  uint16_t dev_x10;         // %
  uint16_t acc_x10, dec_x10;   // seconds, 0 to rated frequency
  uint16_t ppr;             // RPM sensor pulses per rev; 0 = no sensor
  uint8_t poles;
  uint8_t vfd_id;
};

struct AppConfig {
  uint32_t magic;
  DriveConfig drive;
  uint8_t brightness;
  uint8_t sim_drive;        // 1 = run without a VFD
  uint8_t active_profile;
  uint8_t reserved;
};

inline void defaultDrive(DriveConfig& d) {
  d.rated_rpm = 1440; d.rated_hz_x10 = 500; d.slip_x10 = 40; d.gear_x100 = 1500;
  d.rpm_min_x10 = 300; d.rpm_max_x10 = 1000; d.hz_max_x10 = 550; d.amps_max_x10 = 70;
  d.dev_x10 = 50; d.acc_x10 = 50; d.dec_x10 = 50; d.ppr = 0; d.poles = 4; d.vfd_id = 1;
}
inline void defaultProfile(MachineProfile& p) {
  memset(&p, 0, sizeof(p));
  strncpy(p.name, "Multi-seed drill", NAME_LEN - 1);
  p.wheel_mm = 500; p.speed_x10 = 50; p.rpm_x10 = 500; p.length_x10 = 50;
  p.spacing_x10 = 200; p.cells = 24; p.row_sp_x10 = 225; p.rows = 11;
  p.s1_driver = 20; p.s1_driven = 20; p.rig_driver = 20; p.rig_driven = 20;
  p.used = 1;
}

enum class RunState : uint8_t { Ready, Checking, Running, Stopping, Complete, Fault };

enum class Fault : uint8_t {
  None = 0, EStop, Guard, LocalEnableLost, DriveComms, DriveFault, DriveRelay,
  OverCurrent, SpeedDeviation, Stall, HmiLost,
};

enum class EndReason : uint8_t { None = 0, Completed, OperatorStop, RemoteStop, RemoteLinkLost, Faulted, Interrupted };

enum class CmdType : uint8_t {
  None = 0, Arm, Disarm, ConfirmSecured, Start, Stop, ResetFault, Acknowledge,
  RemoteStart, RemoteStop, RemoteSetpoint, SetRemoteAllowed, ApplyConfig,
};

struct Cmd {
  CmdType type;
  uint8_t flag;
  uint16_t arg;
};

// Live drive data, written by the vfd task.
struct DriveFeedback {
  uint32_t age_ms;
  uint16_t hz_x100, cmd_hz_x100;
  uint16_t amps_x100;
  uint16_t vbus_x10;
  int16_t torque_x10;
  uint16_t motor_rpm;
  uint16_t fault_code, warn_code;
  uint8_t status_bits;      // 2101H bits 1-0
  uint8_t online;
};

struct DriveCommand {
  uint16_t hz_x100;
  uint8_t run;
  uint8_t reset;
};

struct Inputs {
  uint8_t estop_ok, guard_ok, local_enable, start_pb, stop_pb, drive_relay_ok;
  uint16_t sensor_rpm_x10;  // valid when config ppr > 0
};

// One published view of the machine; read by HMI, net and web tasks.
struct Telemetry {
  uint32_t seq;
  uint32_t test_id;
  uint32_t elapsed_s, remaining_s;
  uint32_t dist_dm;         // simulated distance, 0.1 m
  uint16_t rpm_target_x10, rpm_x10;
  uint16_t v_sim_x10;       // km/h
  uint16_t hz_cmd_x100, hz_out_x100;
  uint16_t amps_x100;
  uint16_t vbus_x10;
  int16_t torque_x10;
  uint16_t vfd_fault, vfd_warn;
  uint8_t progress;         // 0-100
  RunState state;
  Fault fault;
  uint8_t estop_ok, guard_ok, local_enable, drive_online, drive_ready, target_ok, confirmed;
  uint8_t remote_allowed, remote_run, sim_drive, measured;
  EndReason last_end;
};

// 1 Hz log record. 16 bytes: a one hour test is 56 KB.
struct Sample {
  uint16_t t_s;
  uint16_t rpm_target_x10, rpm_x10;
  uint16_t hz_x100;
  uint16_t amps_x100;
  uint16_t v_sim_x10;
  uint16_t dist_m;
  uint8_t state;
  uint8_t fault;
};
static_assert(sizeof(Sample) == 16, "Sample is a file format");

// Start of every test file; rewritten once when the test ends.
struct TestHeader {
  uint32_t magic, version;
  uint32_t id;
  uint32_t start_unix;
  uint32_t duration_s;
  uint32_t dist_dm;
  uint32_t samples;
  uint16_t rpm_avg_x10, rpm_min_x10, rpm_max_x10, rpm_target_x10;
  uint16_t amps_peak_x100, amps_avg_x100;
  uint16_t hz_target_x100;
  uint16_t accuracy_x10;    // % of target held, 0-1000
  uint32_t seed_dg, fert_dg;   // operator-measured mass, 0.1 g
  uint16_t vfd_fault;
  EndReason end;
  Fault fault;
  uint8_t sim, measured, uploaded, reserved;
  MachineProfile profile;
  DriveConfig drive;
};
constexpr uint32_t TEST_MAGIC = 0x53445431;   // "SDT1"

}  // namespace sd
