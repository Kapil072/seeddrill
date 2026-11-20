#include "cloud_msgs.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "report.h"

namespace sd {

const char* stateName(RunState s) {
  switch (s) {
    case RunState::Ready: return "ready";
    case RunState::Checking: return "checking";
    case RunState::Running: return "running";
    case RunState::Stopping: return "stopping";
    case RunState::Complete: return "complete";
    case RunState::Fault: return "fault";
  }
  return "unknown";
}

namespace {

class Out {
 public:
  Out(char* p, size_t cap) : p_(p), cap_(cap) {}
  void f(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (bad_) return;
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(p_ + len_, cap_ - len_, fmt, a);
    va_end(a);
    if (n < 0 || (size_t)n >= cap_ - len_) bad_ = true;
    else len_ += n;
  }
  // Profile names are operator input: keep the JSON valid whatever was typed.
  void str(const char* s) {
    f("\"");
    for (; *s && !bad_; s++) {
      if (*s == '"' || *s == '\\') f("\\%c", *s);
      else if ((unsigned char)*s >= 0x20 && (unsigned char)*s < 0x7F) f("%c", *s);
    }
    f("\"");
  }
  size_t done() const { return bad_ ? 0 : len_; }

 private:
  char* p_;
  size_t cap_, len_ = 0;
  bool bad_ = false;
};

const char* field(const char* p, size_t n, const char* key) {
  size_t k = strlen(key);
  for (size_t i = 0; i + k + 3 < n; i++) {
    if (p[i] == '"' && !memcmp(p + i + 1, key, k) && p[i + 1 + k] == '"') {
      size_t j = i + k + 2;
      while (j < n && (p[j] == ' ' || p[j] == ':')) j++;
      return j < n ? p + j : nullptr;
    }
  }
  return nullptr;
}

bool strField(const char* p, size_t n, const char* key, char* out, size_t cap) {
  const char* v = field(p, n, key);
  if (!v || *v != '"') return false;
  v++;
  size_t i = 0;
  const char* end = p + n;
  while (v < end && *v != '"' && i + 1 < cap) out[i++] = *v++;
  out[i] = 0;
  return v < end && *v == '"';
}

bool numField(const char* p, size_t n, const char* key, uint32_t& out) {
  const char* v = field(p, n, key);
  if (!v || *v < '0' || *v > '9') return false;
  uint32_t r = 0;
  const char* end = p + n;
  while (v < end && *v >= '0' && *v <= '9') r = r * 10 + (*v++ - '0');
  out = r;
  return true;
}

}  // namespace

size_t jsonTelemetry(char* out, size_t cap, const Telemetry& t, uint32_t now, uint32_t heap_free) {
  Out o(out, cap);
  o.f("{\"ts\":%lu,\"st\":\"%s\",\"test\":%lu,\"rpm\":%u,\"rpmT\":%u,\"kmh\":%u,\"hz\":%u,\"hzC\":%u,\"a\":%u,\"v\":%u,\"tq\":%d,",
      (unsigned long)now, stateName(t.state), (unsigned long)t.test_id, t.rpm_x10, t.rpm_target_x10, t.v_sim_x10, t.hz_out_x100,
      t.hz_cmd_x100, t.amps_x100, t.vbus_x10, t.torque_x10);
  o.f("\"dist\":%lu,\"el\":%lu,\"left\":%lu,\"prog\":%u,\"fault\":%u,\"vfd\":%u,\"warn\":%u,", (unsigned long)t.dist_dm,
      (unsigned long)t.elapsed_s, (unsigned long)t.remaining_s, t.progress, (unsigned)t.fault, t.vfd_fault, t.vfd_warn);
  o.f("\"est\":%u,\"grd\":%u,\"en\":%u,\"drv\":%u,\"rem\":%u,\"remRun\":%u,\"sim\":%u,\"meas\":%u,\"heap\":%lu}", t.estop_ok, t.guard_ok,
      t.local_enable, t.drive_online, t.remote_allowed, t.remote_run, t.sim_drive, t.measured, (unsigned long)heap_free);
  return o.done();
}

size_t jsonStatus(char* out, size_t cap, bool online, const char* fw) {
  Out o(out, cap);
  o.f("{\"online\":%s,\"fw\":\"%s\"}", online ? "true" : "false", fw);
  return o.done();
}

size_t jsonEvent(char* out, size_t cap, uint32_t now, Fault f, uint16_t vfd_code, uint32_t test_id) {
  Out o(out, cap);
  o.f("{\"ts\":%lu,\"type\":\"fault\",\"fault\":%u,\"text\":", (unsigned long)now, (unsigned)f);
  o.str(faultText(f));
  o.f(",\"vfd\":%u,\"test\":%lu}", vfd_code, (unsigned long)test_id);
  return o.done();
}

size_t jsonReport(char* out, size_t cap, const TestHeader& h) {
  const MachineProfile& m = h.profile;
  Out o(out, cap);
  o.f("{\"id\":%lu,\"start\":%lu,\"dur\":%lu,\"dist\":%lu,\"samples\":%lu,", (unsigned long)h.id, (unsigned long)h.start_unix,
      (unsigned long)h.duration_s, (unsigned long)h.dist_dm, (unsigned long)h.samples);
  o.f("\"rpmAvg\":%u,\"rpmMin\":%u,\"rpmMax\":%u,\"rpmT\":%u,\"hzT\":%u,\"aPeak\":%u,\"aAvg\":%u,\"acc\":%u,", h.rpm_avg_x10, h.rpm_min_x10,
      h.rpm_max_x10, h.rpm_target_x10, h.hz_target_x100, h.amps_peak_x100, h.amps_avg_x100, h.accuracy_x10);
  o.f("\"seed\":%lu,\"fert\":%lu,\"end\":", (unsigned long)h.seed_dg, (unsigned long)h.fert_dg);
  o.str(endText(h.end));
  o.f(",\"fault\":%u,\"vfd\":%u,\"sim\":%u,\"meas\":%u,\"profile\":{\"name\":", (unsigned)h.fault, h.vfd_fault, h.sim, h.measured);
  o.str(m.name);
  o.f(",\"wheelMm\":%u,\"speed\":%u,\"rpm\":%u,\"mode\":%u,\"stopBy\":%u,\"length\":%lu,\"rows\":%u,\"rowSp\":%u,\"spacing\":%u,\"cells\":%u,",
      m.wheel_mm, m.speed_x10, m.rpm_x10, m.mode, m.stop_by, (unsigned long)m.length_x10, m.rows, m.row_sp_x10, m.spacing_x10, m.cells);
  o.f("\"s1\":[%u,%u],\"s2\":[%u,%u],\"rig\":[%u,%u],\"couple\":%u,\"gear\":%u}}", m.s1_driver, m.s1_driven, m.s2_driver, m.s2_driven,
      m.rig_driver, m.rig_driven, m.coupling, h.drive.gear_x100);
  return o.done();
}

size_t jsonSamples(char* out, size_t cap, uint32_t test_id, uint32_t first, const Sample* s, uint32_t n) {
  Out o(out, cap);
  o.f("{\"id\":%lu,\"first\":%lu,\"d\":[", (unsigned long)test_id, (unsigned long)first);
  for (uint32_t i = 0; i < n; i++) {
    o.f("%s[%u,%u,%u,%u,%u,%u,%u,%u,%u]", i ? "," : "", s[i].t_s, s[i].rpm_target_x10, s[i].rpm_x10, s[i].hz_x100, s[i].amps_x100,
        s[i].v_sim_x10, s[i].dist_m, s[i].state, s[i].fault);
  }
  o.f("]}");
  return o.done();
}

size_t jsonAck(char* out, size_t cap, const char* id, bool ok, const char* reason) {
  Out o(out, cap);
  o.f("{\"id\":");
  o.str(id);
  o.f(",\"ok\":%s,\"reason\":", ok ? "true" : "false");
  o.str(reason);
  o.f("}");
  return o.done();
}

bool parseCommand(const char* p, size_t n, CloudCmd& c) {
  memset(&c, 0, sizeof(c));
  char cmd[16];
  uint32_t value = 0;
  if (!strField(p, n, "cmd", cmd, sizeof(cmd)) || !strField(p, n, "id", c.id, sizeof(c.id))) return false;
  if (!numField(p, n, "exp", c.expires)) return false;
  numField(p, n, "value", value);
  if (value > 0xFFFF) return false;
  c.value = (uint16_t)value;
  if (!strcmp(cmd, "start")) c.kind = CloudCmd::Start;
  else if (!strcmp(cmd, "stop")) c.kind = CloudCmd::Stop;
  else if (!strcmp(cmd, "setpoint")) c.kind = CloudCmd::Setpoint;
  else if (!strcmp(cmd, "ping")) c.kind = CloudCmd::Ping;
  else return false;
  return true;
}

}  // namespace sd
