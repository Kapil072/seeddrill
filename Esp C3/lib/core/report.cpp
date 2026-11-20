#include "report.h"
#include <stdarg.h>
#include <stdio.h>
#include "kinematics.h"
#include "modbus.h"

namespace sd {

static constexpr int32_t TZ_OFFSET_S = 19800;   // UTC+05:30

void formatDateTime(uint32_t unix_s, char* out, size_t len) {
  if (unix_s < 1000000000u) { snprintf(out, len, "time not set"); return; }
  int64_t t = (int64_t)unix_s + TZ_OFFSET_S;
  int32_t days = (int32_t)(t / 86400), rem = (int32_t)(t % 86400);
  // Civil-from-days (Howard Hinnant)
  int32_t z = days + 719468, era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  snprintf(out, len, "%02u-%02u-%04d %02d:%02d", (unsigned)d, (unsigned)m, (int)y, (int)(rem / 3600), (int)(rem % 3600 / 60));
}

const char* endText(EndReason e) {
  switch (e) {
    case EndReason::Completed: return "Completed";
    case EndReason::OperatorStop: return "Stopped by operator";
    case EndReason::RemoteStop: return "Stopped remotely";
    case EndReason::RemoteLinkLost: return "Stopped: remote link lost";
    case EndReason::Faulted: return "Stopped by fault";
    case EndReason::Interrupted: return "Interrupted by power loss";
    default: return "In progress";
  }
}

const char* faultText(Fault f) {
  switch (f) {
    case Fault::None: return "None";
    case Fault::EStop: return "Emergency stop pressed";
    case Fault::Guard: return "Guard opened";
    case Fault::LocalEnableLost: return "Local enable switched off";
    case Fault::DriveComms: return "Lost communication with VFD";
    case Fault::DriveFault: return "VFD fault";
    case Fault::DriveRelay: return "VFD fault relay open";
    case Fault::OverCurrent: return "Motor current over limit";
    case Fault::SpeedDeviation: return "Speed outside tolerance";
    case Fault::Stall: return "Drive not reaching speed";
    case Fault::HmiLost: return "Display connection lost";
  }
  return "Unknown";
}

namespace {

// printf into a small stack buffer, then out. No heap, bounded stack.
class Printer {
 public:
  explicit Printer(OutStream& o) : out_(o) {}
  bool put(const char* p, size_t n) {
    if (!ok_) return false;
    ok_ = out_.write(p, n);
    pos_ += n;
    return ok_;
  }
  bool f(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[200];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    if (n < 0) return false;
    return put(b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1);
  }
  uint32_t pos() const { return pos_; }
  bool ok() const { return ok_; }

 private:
  OutStream& out_;
  uint32_t pos_ = 0;
  bool ok_ = true;
};

constexpr uint32_t BLOCK = 32;   // samples per read, 512 bytes on the stack

}  // namespace

bool writeCsv(const TestHeader& h, SampleSource& src, OutStream& out) {
  Printer p(out);
  char when[24];
  formatDateTime(h.start_unix, when, sizeof(when));
  p.f("# Seed Drill Field Simulator test report\r\n# Test,%lu\r\n# Started,%s\r\n", (unsigned long)h.id, when);
  p.f("# Profile,%s\r\n# Result,%s\r\n# Simulated drive,%s\r\n", h.profile.name, endText(h.end), h.sim ? "yes" : "no");
  p.f("# RPM source,%s\r\n", h.measured ? "measured sensor" : "estimated from VFD");
  p.f("t_s,target_rpm,output_rpm,drive_hz,current_a,sim_kmh,distance_m,state,fault\r\n");
  Sample b[BLOCK];
  uint32_t total = src.count();
  for (uint32_t i = 0; i < total && p.ok();) {
    uint32_t n = src.read(i, b, BLOCK);
    if (!n) break;
    for (uint32_t k = 0; k < n; k++) {
      const Sample& s = b[k];
      p.f("%u,%u.%u,%u.%u,%u.%02u,%u.%02u,%u.%u,%u,%u,%u\r\n", s.t_s, s.rpm_target_x10 / 10, s.rpm_target_x10 % 10,
          s.rpm_x10 / 10, s.rpm_x10 % 10, s.hz_x100 / 100, s.hz_x100 % 100, s.amps_x100 / 100, s.amps_x100 % 100,
          s.v_sim_x10 / 10, s.v_sim_x10 % 10, s.dist_m, s.state, s.fault);
    }
    i += n;
  }
  return p.ok();
}

// ---- PDF -----------------------------------------------------------------
// PDF 1.4, base-14 fonts (nothing embedded), uncompressed content. Objects are
// written in order and only their byte offsets are remembered for the xref table.
// Stream lengths are indirect objects emitted after each stream, which is what
// lets the body be produced in one forward pass.
namespace {

constexpr int PAGE_W = 595, PAGE_H = 842;
constexpr int ROWS_PER_PAGE = 50, MAX_DATA_PAGES = 5;
constexpr int MAX_OBJ = 4 + (1 + MAX_DATA_PAGES) * 3 + 1;
constexpr int CHART_POINTS = 240;

class Pdf {
 public:
  explicit Pdf(OutStream& o) : p_(o) {}
  Printer& p() { return p_; }

  void beginObj(int n) {
    off_[n] = p_.pos();
    p_.f("%d 0 obj\n", n);
  }
  void endObj() { p_.put("endobj\n", 7); }

  void beginStream(int n) {
    beginObj(n);
    p_.f("<< /Length %d 0 R >>\nstream\n", n + 1);
    start_ = p_.pos();
  }
  void endStream(int n) {
    uint32_t len = p_.pos() - start_;
    p_.put("\nendstream\n", 11);
    endObj();
    beginObj(n + 1);
    p_.f("%lu\n", (unsigned long)len);
    endObj();
  }

  void xref(int count) {
    uint32_t at = p_.pos();
    p_.f("xref\n0 %d\n0000000000 65535 f \n", count);
    for (int i = 1; i < count; i++) p_.f("%010lu 00000 n \n", (unsigned long)off_[i]);
    p_.f("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", count, (unsigned long)at);
  }

  // Drawing helpers
  void rgb(float r, float g, float b, bool stroke = false) { p_.f("%.3f %.3f %.3f %s\n", r, g, b, stroke ? "RG" : "rg"); }
  void rect(float x, float y, float w, float h) { p_.f("%.1f %.1f %.1f %.1f re f\n", x, y, w, h); }
  void line(float x0, float y0, float x1, float y1) { p_.f("%.1f %.1f m %.1f %.1f l S\n", x0, y0, x1, y1); }
  void text(float x, float y, float size, bool bold, const char* s) {
    p_.f("BT /F%d %.1f Tf %.1f %.1f Td (", bold ? 2 : 1, size, x, y);
    for (; *s; s++) {
      char c = *s;
      if (c == '(' || c == ')' || c == '\\') p_.put("\\", 1);
      if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) p_.put(&c, 1);
    }
    p_.f(") Tj ET\n");
  }

 private:
  Printer p_;
  uint32_t off_[MAX_OBJ + 1] = {0};
  uint32_t start_ = 0;
};

const float GREEN[3] = {0.11f, 0.30f, 0.20f};
const float LIME[3] = {0.42f, 0.71f, 0.18f};
const float INK[3] = {0.10f, 0.13f, 0.12f};
const float GREY[3] = {0.45f, 0.50f, 0.48f};
const float LINE[3] = {0.85f, 0.88f, 0.86f};

void kv(Pdf& d, float x, float y, float w, const char* k, const char* v) {
  d.rgb(GREY[0], GREY[1], GREY[2]);
  d.text(x, y, 9, false, k);
  d.rgb(INK[0], INK[1], INK[2]);
  d.text(x + w, y, 9.5f, true, v);
}

void heading(Pdf& d, float x, float y, const char* s) {
  d.rgb(GREEN[0], GREEN[1], GREEN[2]);
  d.text(x, y, 11, true, s);
  d.rgb(LINE[0], LINE[1], LINE[2], true);
  d.p().f("0.8 w\n");
  d.line(x, y - 5, x + 245, y - 5);
}

// Decimated polyline straight from the source; two passes, nothing retained.
void chart(Pdf& d, const TestHeader& h, SampleSource& src, float x, float y, float w, float ht) {
  uint32_t total = src.count();
  float ymax = 120;
  uint16_t top = h.rpm_target_x10 > h.rpm_max_x10 ? h.rpm_target_x10 : h.rpm_max_x10;
  if (top) { ymax = top / 10.0f * 1.2f; ymax = ((int)(ymax / 10) + 1) * 10.0f; }

  d.p().f("0.5 w\n");
  d.rgb(LINE[0], LINE[1], LINE[2], true);
  char lab[16];
  for (int i = 0; i <= 4; i++) {
    float gy = y + ht * i / 4;
    d.line(x, gy, x + w, gy);
    snprintf(lab, sizeof(lab), "%d", (int)(ymax * i / 4));
    d.rgb(GREY[0], GREY[1], GREY[2]);
    d.text(x - 22, gy - 3, 8, false, lab);
  }
  if (total < 2) return;

  uint32_t step = total > CHART_POINTS ? total / CHART_POINTS + 1 : 1;
  uint32_t t_end = 1;
  Sample s;
  if (src.read(total - 1, &s, 1)) t_end = s.t_s ? s.t_s : 1;

  for (int series = 0; series < 2; series++) {
    if (series == 0) { d.rgb(GREY[0], GREY[1], GREY[2], true); d.p().f("0.9 w [3 2] 0 d\n"); }
    else { d.rgb(LIME[0], LIME[1], LIME[2], true); d.p().f("1.4 w [] 0 d\n"); }
    bool first = true;
    for (uint32_t i = 0; i < total; i += step) {
      if (!src.read(i, &s, 1)) break;
      float v = (series ? s.rpm_x10 : s.rpm_target_x10) / 10.0f;
      float px = x + w * s.t_s / t_end, py = y + ht * (v > ymax ? ymax : v) / ymax;
      d.p().f("%.1f %.1f %s\n", px, py, first ? "m" : "l");
      first = false;
    }
    d.p().f("S\n");
  }
  d.p().f("[] 0 d\n");
  d.rgb(GREY[0], GREY[1], GREY[2]);
  d.text(x, y - 12, 8, false, "0 s");
  snprintf(lab, sizeof(lab), "%lu s", (unsigned long)t_end);
  d.text(x + w - 24, y - 12, 8, false, lab);
}

void summaryPage(Pdf& d, const TestHeader& h, SampleSource& src) {
  const MachineProfile& m = h.profile;
  char b[64], when[24];
  formatDateTime(h.start_unix, when, sizeof(when));

  d.rgb(GREEN[0], GREEN[1], GREEN[2]);
  d.rect(0, PAGE_H - 84, PAGE_W, 84);
  d.rgb(1, 1, 1);
  d.text(40, PAGE_H - 44, 19, true, "Seed Drill Field Simulation Report");
  d.text(40, PAGE_H - 65, 10, false, "SPK Engineers  |  Workshop test record");
  snprintf(b, sizeof(b), "Test #%lu", (unsigned long)h.id);
  d.text(470, PAGE_H - 44, 12, true, b);
  d.text(470, PAGE_H - 62, 9, false, when);

  float y = PAGE_H - 116;
  if (h.sim) {
    d.rgb(0.96f, 0.72f, 0.24f);
    d.rect(40, y - 6, 515, 20);
    d.rgb(INK[0], INK[1], INK[2]);
    d.text(48, y, 10, true, "SIMULATED DRIVE: no motor was run. Not a machine calibration record.");
    y -= 30;
  }

  float L = 40, R = 310, c = 118;
  heading(d, L, y, "Machine and settings");
  heading(d, R, y, "Result");
  y -= 22;
  float yl = y, yr = y;
  auto left = [&](const char* k, const char* v) { kv(d, L, yl, c, k, v); yl -= 16; };
  auto right = [&](const char* k, const char* v) { kv(d, R, yr, c, k, v); yr -= 16; };

  left("Profile", m.name);
  left("Mode", m.mode == MODE_FIELD ? "Field emulation" : "Direct RPM");
  snprintf(b, sizeof(b), "%u.%u km/h", m.speed_x10 / 10, m.speed_x10 % 10);
  if (m.mode == MODE_FIELD) left("Simulated speed", b);
  snprintf(b, sizeof(b), "%u.%03u m", m.wheel_mm / 1000, m.wheel_mm % 1000);
  left("Wheel diameter", b);
  snprintf(b, sizeof(b), "%.3f : 1", stageRatio(m));
  left("Wheel to shaft ratio", b);
  left("Rig coupled to", m.coupling == COUPLE_SHAFT ? "Metering shaft" : "Wheel axle");
  snprintf(b, sizeof(b), "%u.%02u : 1", h.drive.gear_x100 / 100, h.drive.gear_x100 % 100);
  left("Gearbox", b);
  snprintf(b, sizeof(b), "%u.%u RPM", h.rpm_target_x10 / 10, h.rpm_target_x10 % 10);
  left("Target output", b);
  snprintf(b, sizeof(b), "%u.%02u Hz", h.hz_target_x100 / 100, h.hz_target_x100 % 100);
  left("Drive frequency", b);

  right("Outcome", endText(h.end));
  if (h.fault != Fault::None) right("Fault", faultText(h.fault));
  if (h.vfd_fault) right("VFD code", ms300::faultName((uint8_t)h.vfd_fault));
  snprintf(b, sizeof(b), "%lu:%02lu min", (unsigned long)(h.duration_s / 60), (unsigned long)(h.duration_s % 60));
  right("Duration", b);
  snprintf(b, sizeof(b), "%lu.%lu m (calculated)", (unsigned long)(h.dist_dm / 10), (unsigned long)(h.dist_dm % 10));
  right("Distance", b);
  snprintf(b, sizeof(b), "%u.%u RPM (%s)", h.rpm_avg_x10 / 10, h.rpm_avg_x10 % 10, h.measured ? "measured" : "estimated");
  right("Average output", b);
  snprintf(b, sizeof(b), "%u.%u to %u.%u RPM", h.rpm_min_x10 / 10, h.rpm_min_x10 % 10, h.rpm_max_x10 / 10, h.rpm_max_x10 % 10);
  right("Range", b);
  snprintf(b, sizeof(b), "%u.%u %%", h.accuracy_x10 / 10, h.accuracy_x10 % 10);
  right("Speed accuracy", b);
  snprintf(b, sizeof(b), "%u.%02u A peak, %u.%02u A avg", h.amps_peak_x100 / 100, h.amps_peak_x100 % 100,
           h.amps_avg_x100 / 100, h.amps_avg_x100 % 100);
  right("Motor current", b);

  y = (yl < yr ? yl : yr) - 10;
  heading(d, L, y, "Output speed over time (RPM)");
  d.rgb(LIME[0], LIME[1], LIME[2]);
  d.rect(R, y + 1, 12, 3);
  d.rgb(GREY[0], GREY[1], GREY[2]);
  d.text(R + 16, y, 8.5f, false, "Actual");
  d.text(R + 62, y, 8.5f, false, "- - Target");
  float ch = 160;
  y -= ch + 16;
  chart(d, h, src, L + 22, y, 493, ch);

  y -= 40;
  heading(d, L, y, "Seed and fertilizer output");
  heading(d, R, y, "Sign-off");
  y -= 22;
  float dist = h.dist_dm / 10.0f;
  float yc = y;
  if (h.seed_dg || h.fert_dg) {
    snprintf(b, sizeof(b), "%lu.%lu g", (unsigned long)(h.seed_dg / 10), (unsigned long)(h.seed_dg % 10));
    kv(d, L, yc, c, "Seed collected", b); yc -= 16;
    snprintf(b, sizeof(b), "%.1f kg/ha", ratePerHa(h.seed_dg / 10.0f, dist, m));
    kv(d, L, yc, c, "Seed rate", b); yc -= 16;
    snprintf(b, sizeof(b), "%lu.%lu g", (unsigned long)(h.fert_dg / 10), (unsigned long)(h.fert_dg % 10));
    kv(d, L, yc, c, "Fertilizer collected", b); yc -= 16;
    snprintf(b, sizeof(b), "%.1f kg/ha", ratePerHa(h.fert_dg / 10.0f, dist, m));
    kv(d, L, yc, c, "Fertilizer rate", b);
  } else {
    d.rgb(GREY[0], GREY[1], GREY[2]);
    d.text(L, yc, 9, false, "No measured output entered.");
  }
  d.rgb(GREY[0], GREY[1], GREY[2]);
  d.text(R, y, 9, false, "Operator:");
  d.text(R, y - 30, 9, false, "Checked by:");
  d.rgb(LINE[0], LINE[1], LINE[2], true);
  d.line(R + 62, y - 2, R + 245, y - 2);
  d.line(R + 62, y - 32, R + 245, y - 32);

  d.rgb(GREY[0], GREY[1], GREY[2]);
  d.text(L, 34, 7.5f, false, h.measured ? "RPM from independent sensor. Distance is calculated by integrating simulated speed over time."
                                        : "RPM is estimated from VFD frequency, not measured at the shaft. Distance is calculated. Verify with a tachometer.");
}

void dataPage(Pdf& d, const TestHeader& h, SampleSource& src, int page, int pages, uint32_t step) {
  char b[96];
  d.rgb(GREEN[0], GREEN[1], GREEN[2]);
  snprintf(b, sizeof(b), "Test #%lu data log", (unsigned long)h.id);
  d.text(40, PAGE_H - 50, 14, true, b);
  d.rgb(GREY[0], GREY[1], GREY[2]);
  snprintf(b, sizeof(b), "Every %lu s  |  page %d of %d", (unsigned long)step, page + 2, pages + 1);
  d.text(40, PAGE_H - 66, 9, false, b);

  static const char* const COLS[] = {"Time (s)", "Target RPM", "Output RPM", "Drive Hz", "Current A", "km/h", "Dist m"};
  float y = PAGE_H - 92;
  d.rgb(GREEN[0], GREEN[1], GREEN[2]);
  d.rect(40, y - 5, 515, 17);
  d.rgb(1, 1, 1);
  for (int i = 0; i < 7; i++) d.text(48 + i * 73, y, 8.5f, true, COLS[i]);
  y -= 14.5f;

  uint32_t total = src.count();
  for (int r = 0; r < ROWS_PER_PAGE; r++) {
    uint32_t idx = (uint32_t)(page * ROWS_PER_PAGE + r) * step;
    if (idx >= total) break;
    Sample s;
    if (!src.read(idx, &s, 1)) break;
    if (r & 1) { d.rgb(0.95f, 0.97f, 0.96f); d.rect(40, y - 4, 515, 14.5f); }
    d.rgb(INK[0], INK[1], INK[2]);
    float x = 48;
    snprintf(b, sizeof(b), "%u", s.t_s); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u.%u", s.rpm_target_x10 / 10, s.rpm_target_x10 % 10); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u.%u", s.rpm_x10 / 10, s.rpm_x10 % 10); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u.%02u", s.hz_x100 / 100, s.hz_x100 % 100); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u.%02u", s.amps_x100 / 100, s.amps_x100 % 100); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u.%u", s.v_sim_x10 / 10, s.v_sim_x10 % 10); d.text(x, y, 8.5f, false, b); x += 73;
    snprintf(b, sizeof(b), "%u", s.dist_m); d.text(x, y, 8.5f, false, b);
    y -= 14.5f;
  }
}

}  // namespace

bool writePdf(const TestHeader& h, SampleSource& src, OutStream& out) {
  Pdf d(out);
  Printer& p = d.p();
  uint32_t total = src.count();

  // Fit the log into at most MAX_DATA_PAGES by thinning it evenly.
  uint32_t capacity = ROWS_PER_PAGE * MAX_DATA_PAGES;
  uint32_t step = total > capacity ? (total + capacity - 1) / capacity : 1;
  uint32_t rows = (total + step - 1) / step;
  int data_pages = (int)((rows + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
  int pages = 1 + data_pages;

  p.f("%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
  d.beginObj(1); p.f("<< /Type /Catalog /Pages 2 0 R >>\n"); d.endObj();
  d.beginObj(2);
  p.f("<< /Type /Pages /Count %d /Kids [", pages);
  for (int i = 0; i < pages; i++) p.f("%d 0 R ", 5 + i * 3);
  p.f("] >>\n");
  d.endObj();
  d.beginObj(3); p.f("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n"); d.endObj();
  d.beginObj(4); p.f("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\n"); d.endObj();

  for (int i = 0; i < pages; i++) {
    int obj = 5 + i * 3;
    d.beginObj(obj);
    p.f("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents %d 0 R "
        "/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> >>\n", PAGE_W, PAGE_H, obj + 1);
    d.endObj();
    d.beginStream(obj + 1);
    if (i == 0) summaryPage(d, h, src);
    else dataPage(d, h, src, i - 1, data_pages, step);
    d.endStream(obj + 1);
    if (!p.ok()) return false;
  }
  d.xref(5 + pages * 3);
  return p.ok();
}

}  // namespace sd
