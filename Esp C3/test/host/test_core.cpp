// Host tests for the portable core. Build: see test/host/run.sh
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "cloud_msgs.h"
#include "dgus.h"
#include "drive_sim.h"
#include "kinematics.h"
#include "machine_ctl.h"
#include "modbus.h"
#include "report.h"
#include "sd_lockfree.h"

using namespace sd;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { double _a = (a), _b = (b); if (fabs(_a - _b) <= (tol)) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s = %.4f, want %.4f\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

// ---- kinematics: numbers taken straight from the project documents ----------
static void testKinematics() {
  puts("kinematics");
  DriveConfig d; defaultDrive(d);
  MachineProfile p; defaultProfile(p);
  d.slip_x10 = 0;
  Kinematics k;

  // Hardware Details s4: 100 RPM output = 1500 motor RPM; 30 RPM = 450 motor RPM.
  p.mode = MODE_DIRECT; p.rpm_x10 = 1000;
  CHECK(compute(p, d, k) == Limit::Ok);
  NEAR(k.motor_rpm, 1500, 0.01);
  NEAR(k.hz, 50.0, 0.001);
  p.rpm_x10 = 300;
  CHECK(compute(p, d, k) == Limit::Ok);
  NEAR(k.motor_rpm, 450, 0.01);

  // 4 poles means P = 2 pole pairs: 1500 RPM must be 50 Hz, not 100.
  NEAR(outRpmFromHz(50.0f, d), 100.0, 0.01);

  // Slip per TECH-SPEC 3.2: f x (1 + S). Nameplate 1440/1500 = 4 %.
  d.slip_x10 = 40; p.rpm_x10 = 1000;
  CHECK(compute(p, d, k) == Limit::Ok);
  NEAR(k.hz, 52.0, 0.001);
  NEAR(outRpmFromHz(k.hz, d), 100.0, 0.01);

  // Summary s4: n_wheel = (v m/s x 60) / (pi x D)
  d.slip_x10 = 0;
  p.mode = MODE_FIELD; p.speed_x10 = 65; p.wheel_mm = 400; d.rpm_min_x10 = 0; d.rpm_max_x10 = 3000; d.hz_max_x10 = 9999;
  compute(p, d, k);
  NEAR(k.wheel_rpm, (6.5 / 3.6 * 60) / (M_PI * 0.4), 0.01);

  // One chain stage: n_driven = n_driver x Z_driver / Z_driven
  p.s1_driver = 15; p.s1_driven = 30; p.s2_driver = 0; p.s2_driven = 0;
  compute(p, d, k);
  NEAR(k.ratio, 0.5, 1e-5);
  NEAR(k.shaft_rpm, k.wheel_rpm * 0.5, 1e-3);
  // Two stages multiply
  p.s2_driver = 40; p.s2_driven = 20;
  compute(p, d, k);
  NEAR(k.ratio, 1.0, 1e-5);

  // Coupling to the metering shaft moves the target past the chain stages.
  p.s2_driver = 0; p.s2_driven = 0;
  p.coupling = COUPLE_WHEEL; compute(p, d, k); float at_axle = k.out_rpm;
  p.coupling = COUPLE_SHAFT; compute(p, d, k);
  NEAR(k.out_rpm, at_axle * 0.5, 1e-3);

  // Rig adapter: small rig sprocket driving a big one means the gearbox runs faster.
  p.coupling = COUPLE_WHEEL; p.rig_driver = 10; p.rig_driven = 20;
  compute(p, d, k);
  NEAR(k.out_rpm, at_axle * 2.0, 1e-3);
  p.rig_driver = 20; p.rig_driven = 20;

  // Round trip: output RPM back to ground speed.
  compute(p, d, k);
  NEAR(speedFromOutRpm(k.out_rpm, p), 6.5, 0.01);

  // Limits reject, per Hardware Details s6.
  defaultDrive(d);
  p.mode = MODE_DIRECT; p.rpm_x10 = 200; CHECK(compute(p, d, k) == Limit::BelowMin);
  p.rpm_x10 = 1100; CHECK(compute(p, d, k) == Limit::AboveMax);
  d.hz_max_x10 = 500; p.rpm_x10 = 1000; CHECK(compute(p, d, k) == Limit::OverFrequency);
  p.wheel_mm = 0; CHECK(compute(p, d, k) == Limit::BadInput);

  // TECH-SPEC table 4 at v = 1.8 m/s: spacing cm, cells, drop/s, interval ms, RPM.
  struct { uint16_t sp, cells; double hz, ms, rpm; } T[] = {
      {200, 24, 9.0, 111.1, 22.5}, {50, 30, 36.0, 27.8, 72.0}, {100, 12, 18.0, 55.6, 90.0}, {30, 60, 60.0, 16.7, 60.0}};
  for (auto& t : T) {
    p.spacing_x10 = t.sp; p.cells = t.cells;
    SeedRate s = seedRate(1.8f * 3.6f, p);
    NEAR(s.drop_hz, t.hz, 0.01); NEAR(s.interval_ms, t.ms, 0.05); NEAR(s.disc_rpm, t.rpm, 0.01);
  }

  // 1 kg over 1000 m at 2 m width = 2000 m2 = 0.2 ha -> 5 kg/ha
  p.rows = 10; p.row_sp_x10 = 200;
  NEAR(ratePerHa(1000, 1000, p), 5.0, 1e-3);
}

// ---- protocols -------------------------------------------------------------
struct Buf : ByteSink {
  std::vector<uint8_t> b;
  void write(const uint8_t* p, size_t n) override { b.insert(b.end(), p, p + n); }
};

static void testDgus() {
  puts("dgus");
  Buf o; DgusTx tx(o);
  uint16_t w = 0x04D2;
  tx.writeWords(0x1000, &w, 1);   // guide example: 5A A5 05 82 1000 04D2
  const uint8_t e1[] = {0x5A, 0xA5, 0x05, 0x82, 0x10, 0x00, 0x04, 0xD2};
  CHECK(o.b.size() == 8 && !memcmp(o.b.data(), e1, 8));
  o.b.clear(); tx.setPage(1);     // guide: 5A A5 07 82 0084 5A01 0001
  const uint8_t e2[] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x01};
  CHECK(o.b.size() == 10 && !memcmp(o.b.data(), e2, 10));
  o.b.clear(); tx.beep(0x7D);     // guide: 5A A5 05 82 00A0 007D
  const uint8_t e3[] = {0x5A, 0xA5, 0x05, 0x82, 0x00, 0xA0, 0x00, 0x7D};
  CHECK(!memcmp(o.b.data(), e3, 8));
  o.b.clear(); tx.read(0x000F, 1);  // guide: 5A A5 04 83 000F 01
  const uint8_t e4[] = {0x5A, 0xA5, 0x04, 0x83, 0x00, 0x0F, 0x01};
  CHECK(!memcmp(o.b.data(), e4, 7));
  o.b.clear(); tx.systemConfig();   // guide: 5A A5 07 82 0080 5A 00 00 38
  const uint8_t e5[] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x80, 0x5A, 0x00, 0x00, 0x38};
  CHECK(!memcmp(o.b.data(), e5, 10));

  struct Got { uint16_t vp, v; int n; } got{0, 0, 0};
  DgusRx rx;
  rx.setHandler([](void* c, uint16_t vp, const uint16_t* w, uint8_t) { auto* g = (Got*)c; g->vp = vp; g->v = w[0]; g->n++; }, &got);
  const uint8_t touch[] = {0x5A, 0xA5, 0x06, 0x83, 0x54, 0x36, 0x01, 0x00, 0x01};   // guide example
  rx.feed(touch, sizeof(touch));
  CHECK(got.n == 1 && got.vp == 0x5436 && got.v == 1);
  // Noise, a split frame, and an ack in between must not lose the next frame.
  const uint8_t noise[] = {0x00, 0x5A, 0x12, 0xFF, 0x5A, 0xA5, 0x03, 0x82, 0x4F, 0x4B, 0x5A};
  rx.feed(noise, sizeof(noise));
  rx.feed(touch + 1, 4); rx.feed(touch + 5, 4);
  CHECK(got.n == 2);
}

static void testModbus() {
  puts("modbus");
  uint8_t f[32];
  // Manual example: read 2 regs from 2102H at address 1
  size_t n = mbReadHolding(f, 1, 0x2102, 2);
  CHECK(n == 8 && f[6] == 0x6F && f[7] == 0xF7);
  n = mbWriteSingle(f, 1, 0x0100, 0x1770);
  CHECK(n == 8);
  uint16_t v[2] = {ms300::CTRL_RUN_FWD, 2500};
  n = mbWriteMultiple(f, 1, ms300::REG_CONTROL, v, 2);
  CHECK(n == 13 && f[1] == 0x10 && f[2] == 0x20 && f[3] == 0x00 && f[7] == 0x00 && f[8] == 0x12);
  CHECK(modbusCrc(f, n) == 0);

  // Reply decode; 18 = 0x12 = RUN+FWD as in the manual's bit table.
  CHECK(ms300::CTRL_RUN_FWD == 18 && ms300::CTRL_STOP == 1);
  uint8_t r[64]; r[0] = 1; r[1] = 3; r[2] = 26;
  uint16_t regs[13] = {0x0302, 3, 5000, 4998, 345, 3201, 2300, 0, 0, 0, 0, 455, 1438};
  for (int i = 0; i < 13; i++) { r[3 + 2 * i] = regs[i] >> 8; r[4 + 2 * i] = regs[i]; }
  uint16_t c = modbusCrc(r, 29); r[29] = c; r[30] = c >> 8;
  uint16_t w[13];
  CHECK(mbParseRead(r, 31, 1, w, 13) == MbResult::Ok);
  DriveFeedback fb{}; ms300Decode(w, fb);
  CHECK(fb.fault_code == 2 && fb.warn_code == 3 && fb.status_bits == 3 && fb.hz_x100 == 4998 && fb.amps_x100 == 345 && fb.torque_x10 == 455 && fb.motor_rpm == 1438);
  CHECK(mbParseRead(r, 10, 1, w, 13) == MbResult::Incomplete);
  r[10] ^= 1;
  CHECK(mbParseRead(r, 31, 1, w, 13) == MbResult::BadCrc);
  uint8_t ex[5] = {1, 0x83, 2, 0, 0}; uint16_t cc = modbusCrc(ex, 3); ex[3] = cc; ex[4] = cc >> 8;
  CHECK(mbParseRead(ex, 5, 1, w, 13) == MbResult::Exception);
}

static void testLockfree() {
  puts("lockfree");
  SpscRing<int, 4> q; int v;
  CHECK(!q.pop(v));
  for (int i = 0; i < 4; i++) CHECK(q.push(i));
  CHECK(!q.push(9) && q.dropped() == 1);
  for (int i = 0; i < 4; i++) { CHECK(q.pop(v) && v == i); }
  // Wrap-around
  for (int i = 0; i < 100; i++) { q.push(i); CHECK(q.pop(v) && v == i); }
  SeqLock<Telemetry> s; Telemetry t{}, r{};
  CHECK(!s.read(r));               // nothing published yet
  t.seq = 7; s.write(t);
  CHECK(s.read(r) && r.seq == 7);
}

// ---- machine control --------------------------------------------------------
struct Ev : CtlEvents {
  int starts = 0, ends = 0, faults = 0; std::vector<Sample> samples; TestHeader last{}; Fault lastFault = Fault::None;
  void onTestStart(const TestHeader&) override { starts++; }
  void onSample(const Sample& s) override { samples.push_back(s); }
  void onTestEnd(const TestHeader& h) override { ends++; last = h; }
  void onFault(Fault f, uint16_t) override { faults++; lastFault = f; }
};

struct Rig {
  Ev ev; MachineCtl m{ev}; DriveSim sim; Inputs in{}; DriveFeedback fb{}; Links links{true, true};
  MachineProfile p; DriveConfig d; uint32_t ms = 1000;
  Rig() {
    defaultProfile(p); defaultDrive(d);
    p.mode = MODE_DIRECT; p.rpm_x10 = 600; p.stop_by = STOP_TIME; p.length_x10 = 5;   // 30 s
    in.estop_ok = in.guard_ok = in.local_enable = in.drive_relay_ok = 1;
    m.configure(p, d, true);
    run(100);
  }
  void run(uint32_t for_ms) {
    for (uint32_t t = 0; t < for_ms; t += 10) {
      ms += 10;
      sim.step(10, m.driveCommand(), fb);
      m.tick(ms, 1760000000u, in, fb, links);
    }
  }
  void cmd(CmdType t, uint8_t f = 0, uint16_t a = 0) { m.handle({t, f, a}, 1760000000u); }
  void startLocal() { cmd(CmdType::Arm); cmd(CmdType::ConfirmSecured, 1); cmd(CmdType::Start); }
  RunState st() { return m.telemetry().state; }
};

static void testHappyPath() {
  puts("test run");
  Rig r;
  CHECK(r.st() == RunState::Ready);
  CHECK(!r.m.runPermit() && r.m.driveCommand().run == 0);
  r.startLocal();
  r.run(100);
  CHECK(r.st() == RunState::Running);
  CHECK(r.m.runPermit() && r.m.driveCommand().run == 1);

  // Ramp: command climbs, it does not step. 0 -> 50 Hz in 5 s, so about 1 Hz after 100 ms.
  CHECK(r.m.driveCommand().hz_x100 < 200);
  r.run(6000);
  NEAR(r.m.driveCommand().hz_x100, 3120, 2);   // 60 RPM -> 900 motor -> 30 Hz + 4 % slip
  NEAR(r.m.telemetry().rpm_x10, 600, 6);

  r.run(30000);
  CHECK(r.st() == RunState::Complete);
  CHECK(r.ev.ends == 1 && r.ev.last.end == EndReason::Completed);
  NEAR(r.ev.last.duration_s, 30, 3);
  NEAR(r.ev.last.rpm_avg_x10, 600, 3);
  CHECK(r.ev.last.accuracy_x10 > 980);
  CHECK(r.ev.samples.size() >= 29 && r.ev.samples.size() <= 35);
  CHECK(!r.m.runPermit() && r.m.driveCommand().run == 0);

  // Distance = integral of v/3.6 dt (Hardware Details s6)
  float v = speedFromOutRpm(60, r.p);
  NEAR(r.ev.last.dist_dm / 10.0, v / 3.6 * 30, v / 3.6 * 4);

  r.cmd(CmdType::Acknowledge);
  r.run(50);
  CHECK(r.st() == RunState::Ready);
}

static void testDistanceStop() {
  puts("stop by distance");
  Rig r;
  r.p.stop_by = STOP_DISTANCE; r.p.length_x10 = 200;   // 20 m
  r.m.configure(r.p, r.d, true);
  r.startLocal();
  r.run(120000);
  CHECK(r.st() == RunState::Complete && r.ev.last.end == EndReason::Completed);
  NEAR(r.ev.last.dist_dm / 10.0, 20.0, 4.0);
}

static void testStartBlocked() {
  puts("start interlocks");
  auto blocked = [](void (*breakIt)(Rig&)) {
    Rig r; breakIt(r); r.run(600);
    r.startLocal(); r.run(100);
    return r.st() != RunState::Running && r.ev.starts == 0 && !r.m.runPermit();
  };
  CHECK(blocked([](Rig& r) { r.in.estop_ok = 0; }));
  CHECK(blocked([](Rig& r) { r.in.guard_ok = 0; }));
  CHECK(blocked([](Rig& r) { r.in.local_enable = 0; }));
  CHECK(blocked([](Rig& r) { r.in.drive_relay_ok = 0; }));
  CHECK(blocked([](Rig& r) { r.p.rpm_x10 = 2000; r.m.configure(r.p, r.d, true); }));

  // Operator confirmation is required.
  Rig r; r.cmd(CmdType::Arm); r.cmd(CmdType::Start); r.run(100);
  CHECK(r.st() == RunState::Checking && r.ev.starts == 0);
  // Start without arming does nothing.
  Rig r2; r2.cmd(CmdType::Start); r2.run(100);
  CHECK(r2.st() == RunState::Ready && r2.ev.starts == 0);
}

static void testInterlockTrips() {
  puts("run interlocks");
  struct { void (*act)(Rig&); Fault want; bool cut; } C[] = {
      {[](Rig& r) { r.in.estop_ok = 0; }, Fault::EStop, true},
      {[](Rig& r) { r.in.guard_ok = 0; }, Fault::Guard, true},
      {[](Rig& r) { r.in.local_enable = 0; }, Fault::LocalEnableLost, true},
      {[](Rig& r) { r.in.drive_relay_ok = 0; }, Fault::DriveRelay, true},
  };
  for (auto& c : C) {
    Rig r; r.startLocal(); r.run(8000);
    CHECK(r.st() == RunState::Running);
    c.act(r);
    r.run(20);
    CHECK(r.st() == RunState::Fault && r.m.telemetry().fault == c.want);
    CHECK(r.m.driveCommand().run == 0 && r.m.driveCommand().hz_x100 == 0);
    CHECK(!r.m.runPermit());          // cut within one tick
    CHECK(r.ev.ends == 1 && r.ev.last.end == EndReason::Faulted && r.ev.last.fault == c.want);
  }
}

static void testNoAutoRestart() {
  puts("no automatic restart");
  Rig r; r.startLocal(); r.run(8000);
  r.in.guard_ok = 0; r.run(50);
  CHECK(r.st() == RunState::Fault);
  r.cmd(CmdType::ResetFault);       // cause still present: reset refused
  r.run(50);
  CHECK(r.st() == RunState::Fault);
  r.in.guard_ok = 1; r.run(2000);   // cause gone: still latched
  CHECK(r.st() == RunState::Fault && r.m.driveCommand().run == 0);
  r.cmd(CmdType::ResetFault); r.run(2000);
  CHECK(r.st() == RunState::Ready);
  CHECK(r.m.driveCommand().run == 0 && !r.m.runPermit());   // reset alone never moves the machine
  CHECK(r.ev.starts == 1);

  // Start button held down across a reset must not start anything.
  r.cmd(CmdType::Arm); r.cmd(CmdType::ConfirmSecured, 1);
  Rig h; h.in.start_pb = 1; h.run(100);
  h.cmd(CmdType::Arm); h.cmd(CmdType::ConfirmSecured, 1); h.run(500);
  CHECK(h.st() == RunState::Checking);
  h.in.start_pb = 0; h.run(100); h.in.start_pb = 1; h.run(100);   // a fresh press does
  CHECK(h.st() == RunState::Running);
}

static void testCommsLoss() {
  puts("drive comms loss");
  Rig r; r.startLocal(); r.run(8000);
  // Feedback goes stale: drive stopped answering.
  for (int i = 0; i < 80 && r.st() == RunState::Running; i++) {
    r.ms += 10; r.fb.online = 0; r.fb.age_ms += 10;
    r.m.tick(r.ms, 0, r.in, r.fb, r.links);
  }
  CHECK(r.st() == RunState::Fault && r.m.telemetry().fault == Fault::DriveComms);
  CHECK(!r.m.runPermit());

  Rig f; f.startLocal(); f.run(8000);
  f.ms += 10; f.fb.fault_code = 16;
  f.m.tick(f.ms, 0, f.in, f.fb, f.links);
  CHECK(f.st() == RunState::Fault && f.m.telemetry().fault == Fault::DriveFault && !f.m.runPermit());
  CHECK(f.ev.last.vfd_fault == 16);
}

static void testSupervision() {
  puts("current and speed supervision");
  Rig r; r.startLocal(); r.run(8000);
  r.d.amps_max_x10 = 10; r.m.configure(r.p, r.d, true);   // 1 A limit: sim draws more
  r.run(1000);
  CHECK(r.st() == RunState::Running);   // brief overload tolerated
  r.run(1500);
  CHECK(r.st() == RunState::Fault && r.m.telemetry().fault == Fault::OverCurrent);

  // Sensor fitted and reading low: deviation trips after its window.
  Rig s; s.d.ppr = 4; s.m.configure(s.p, s.d, true);
  s.startLocal(); s.in.sensor_rpm_x10 = 600; s.run(9000);
  CHECK(s.st() == RunState::Running && s.m.telemetry().measured == 1);
  s.in.sensor_rpm_x10 = 400; s.run(2000);
  CHECK(s.st() == RunState::Running);
  s.run(1500);
  CHECK(s.st() == RunState::Fault && s.m.telemetry().fault == Fault::SpeedDeviation);
}

static void testRemote() {
  puts("remote control");
  // Refused until the operator allows it locally.
  Rig r; r.cmd(CmdType::RemoteStart); r.run(100);
  CHECK(r.st() == RunState::Ready && r.ev.starts == 0);
  // Refused without the key switch even when allowed.
  r.cmd(CmdType::SetRemoteAllowed, 1); r.in.local_enable = 0; r.run(100);
  r.cmd(CmdType::RemoteStart); r.run(100);
  CHECK(r.ev.starts == 0);
  r.in.local_enable = 1; r.run(100);

  r.cmd(CmdType::RemoteStart); r.run(100);
  CHECK(r.st() == RunState::Running && r.m.telemetry().remote_run == 1);
  // Horn sounds first and nothing moves until it finishes.
  CHECK(r.m.warning() && r.m.driveCommand().run == 0);
  r.run(3100);
  CHECK(!r.m.warning() && r.m.driveCommand().run == 1);

  // Out-of-range setpoint is ignored; in-range is applied.
  r.run(6000);
  uint16_t before = r.m.driveCommand().hz_x100;
  r.cmd(CmdType::RemoteSetpoint, 0, 5000); r.run(500);
  CHECK(r.m.driveCommand().hz_x100 == before);
  r.cmd(CmdType::RemoteSetpoint, 0, 800); r.run(4000);
  NEAR(r.m.driveCommand().hz_x100, 4160, 2);

  // Link loss stops a remote run after the window, as a normal ramped stop.
  r.links.cloud_ok = false; r.run(9000);
  CHECK(r.st() == RunState::Running);
  r.run(1500);
  CHECK(r.st() == RunState::Stopping);
  r.run(8000);
  CHECK(r.st() == RunState::Complete && r.ev.last.end == EndReason::RemoteLinkLost);

  // Reconnecting never resumes motion.
  r.links.cloud_ok = true; r.run(3000);
  CHECK(r.m.driveCommand().run == 0 && r.ev.starts == 1);

  // A locally started run ignores cloud loss completely.
  Rig l; l.links.cloud_ok = false; l.startLocal(); l.run(20000);
  CHECK(l.st() == RunState::Running);
}

static void testHmiLoss() {
  puts("display loss");
  Rig r; r.startLocal(); r.run(8000);
  r.links.hmi_ok = false; r.run(30);
  CHECK(r.st() == RunState::Fault && r.m.telemetry().fault == Fault::HmiLost);
  CHECK(r.m.driveCommand().run == 0);
  CHECK(r.m.runPermit());     // controlled stop: the drive ramps down first
  r.run(8000);
  CHECK(!r.m.runPermit());
}

// ---- reports -----------------------------------------------------------------
struct Vec : SampleSource {
  std::vector<Sample> s;
  uint32_t count() override { return s.size(); }
  uint32_t read(uint32_t i, Sample* d, uint32_t n) override {
    uint32_t k = 0;
    for (; k < n && i + k < s.size(); k++) d[k] = s[i + k];
    return k;
  }
};
struct Str : OutStream {
  std::string b; size_t maxChunk = 0;
  bool write(const char* p, size_t n) override { b.append(p, n); if (n > maxChunk) maxChunk = n; return true; }
};
struct Fail : OutStream {
  size_t left;
  explicit Fail(size_t n) : left(n) {}
  bool write(const char*, size_t n) override { if (n > left) return false; left -= n; return true; }
};

static TestHeader sampleReport(Vec& v, uint32_t seconds) {
  Rig r; r.p.length_x10 = seconds / 6 + 1;
  strcpy(r.p.name, "Vishwakarma (11 row)");
  r.m.configure(r.p, r.d, true);
  r.startLocal(); r.run(seconds * 1000 + 25000);
  TestHeader h = r.ev.last;
  h.start_unix = 1762300000u;
  h.seed_dg = 12345; h.fert_dg = 23456;
  v.s = r.ev.samples;
  return h;
}

static void testReports(const char* outdir) {
  puts("reports");
  char b[32];
  formatDateTime(1762300000u, b, sizeof(b));
  CHECK(!strcmp(b, "05-11-2025 05:16"));   // 2025-11-04 23:46:40 UTC + 5:30
  formatDateTime(0, b, sizeof(b));
  CHECK(!strcmp(b, "time not set"));

  Vec v; TestHeader h = sampleReport(v, 60);
  Str csv; CHECK(writeCsv(h, v, csv));
  CHECK(csv.b.find("t_s,target_rpm,output_rpm") != std::string::npos);
  size_t lines = 0; for (char c : csv.b) lines += c == '\n';
  CHECK(lines == v.s.size() + 8);   // 7 comment lines + column header

  Str pdf; CHECK(writePdf(h, v, pdf));
  CHECK(pdf.b.compare(0, 8, "%PDF-1.4") == 0);
  CHECK(pdf.b.find("%%EOF") != std::string::npos);
  CHECK(pdf.maxChunk <= 200);          // streaming: no large buffer anywhere

  // Multi-page, thinned: a 3 hour log still produces a bounded document.
  Vec big; TestHeader hb = h;
  for (uint32_t i = 0; i < 10800; i++) {
    Sample s{}; s.t_s = i; s.rpm_target_x10 = 600; s.rpm_x10 = 595 + (i * 7) % 11; s.hz_x100 = 3120; s.amps_x100 = 380;
    s.v_sim_x10 = 56; s.dist_m = i * 3 / 2; big.s.push_back(s);
  }
  hb.rpm_max_x10 = 606; hb.rpm_min_x10 = 595; hb.samples = 10800; hb.duration_s = 10800;
  Str pb; CHECK(writePdf(hb, big, pb));
  CHECK(pb.b.size() < 250000);

  // Client disconnects half way: stops cleanly.
  Fail f(1000); CHECK(!writePdf(h, v, f));

  // Empty test (stopped before the first sample) must still be valid.
  Vec none; TestHeader he = h; he.seed_dg = he.fert_dg = 0;
  Str pe; CHECK(writePdf(he, none, pe));

  if (outdir) {
    std::string d(outdir);
    FILE* o = fopen((d + "/report_short.pdf").c_str(), "wb"); fwrite(pdf.b.data(), 1, pdf.b.size(), o); fclose(o);
    o = fopen((d + "/report_long.pdf").c_str(), "wb"); fwrite(pb.b.data(), 1, pb.b.size(), o); fclose(o);
    o = fopen((d + "/report_empty.pdf").c_str(), "wb"); fwrite(pe.b.data(), 1, pe.b.size(), o); fclose(o);
    o = fopen((d + "/report.csv").c_str(), "wb"); fwrite(csv.b.data(), 1, csv.b.size(), o); fclose(o);
  }
}

static void testCloud(const char* outdir) {
  puts("cloud messages");
  CloudCmd c;
  const char* ok = "{\"cmd\":\"setpoint\",\"id\":\"abc-1\",\"exp\":1762300100,\"value\":65}";
  CHECK(parseCommand(ok, strlen(ok), c) && c.kind == CloudCmd::Setpoint && !strcmp(c.id, "abc-1") && c.expires == 1762300100u && c.value == 65);
  const char* bad[] = {"", "{}", "garbage", "{\"cmd\":\"start\"}", "{\"cmd\":\"explode\",\"id\":\"x\",\"exp\":5}",
                       "{\"cmd\":\"start\",\"id\":\"x\",\"exp\":\"soon\"}", "{\"cmd\":\"setpoint\",\"id\":\"x\",\"exp\":5,\"value\":99999}"};
  for (const char* b : bad) CHECK(!parseCommand(b, strlen(b), c));
  // Truncated message must not read past its end
  char cut[12]; memcpy(cut, ok, 12);
  CHECK(!parseCommand(cut, 12, c));

  Vec v; TestHeader h = sampleReport(v, 30);
  strcpy(h.profile.name, "Quote\" and \\ slash");
  char big[8192];
  size_t n = jsonReport(big, sizeof(big), h);
  CHECK(n > 0 && n < 900);
  CHECK(strstr(big, "Quote\\\" and \\\\ slash") != nullptr);   // escaped, stays valid JSON
  CHECK(jsonReport(big, 40, h) == 0);                                // too small reports failure, no overrun
  Telemetry t{}; t.state = RunState::Running; t.rpm_x10 = 530;
  char tm[512];
  CHECK(jsonTelemetry(tm, sizeof(tm), t, 1762300000u, 200000) > 0);
  CHECK(strstr(tm, "\"st\":\"running\"") != nullptr);
  // 120 worst-case samples fit the upload buffer.
  std::vector<Sample> worst(120);
  for (auto& s : worst) { s.t_s = 65535; s.rpm_target_x10 = s.rpm_x10 = s.hz_x100 = s.amps_x100 = s.v_sim_x10 = s.dist_m = 65535; s.state = 5; s.fault = 11; }
  CHECK(jsonSamples(big, sizeof(big), 1, 0, worst.data(), 120) > 0);

  if (outdir) {
    std::string d(outdir);
    auto save = [&](const char* name, const char* p, size_t len) { FILE* o = fopen((d + "/" + name).c_str(), "wb"); fwrite(p, 1, len, o); fclose(o); };
    save("msg_telemetry.json", tm, strlen(tm));
    n = jsonReport(big, sizeof(big), h); save("msg_report.json", big, n);
    n = jsonSamples(big, sizeof(big), h.id, 0, v.s.data(), v.s.size()); save("msg_samples.json", big, n);
    n = jsonEvent(big, sizeof(big), 1762300000u, Fault::Guard, 0, h.id); save("msg_event.json", big, n);
    n = jsonAck(big, sizeof(big), "abc-1", false, "interlock open"); save("msg_ack.json", big, n);
  }
}

int main(int argc, char** argv) {
  testKinematics();
  testDgus();
  testModbus();
  testLockfree();
  testHappyPath();
  testDistanceStop();
  testStartBlocked();
  testInterlockTrips();
  testNoAutoRestart();
  testCommsLoss();
  testSupervision();
  testRemote();
  testHmiLoss();
  testReports(argc > 1 ? argv[1] : nullptr);
  testCloud(argc > 1 ? argv[1] : nullptr);
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
