#include "ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "hmi_map.h"
#include "modbus.h"

namespace sd {
using namespace hmi;

static_assert(VP_TOP - 0x5000 <= 0x0A00, "shadow memory too small for the HMI map");

namespace {

constexpr int32_t SERVICE_PIN = 2580;
constexpr uint8_t ROWS = 6;
constexpr uint8_t BEEP_SHORT = 5, BEEP_ERROR = 40;

// One entry per numeric field: scaled limits and how to show them.
enum Field : uint8_t {
  FD_SPEED, FD_WHEEL, FD_LENGTH, FD_S1A, FD_S1B, FD_S2A, FD_S2B, FD_RA, FD_RB, FD_ROWS, FD_ROWSP, FD_SPACING,
  FD_CELLS, FD_SEED, FD_FERT, FD_POLES, FD_NRATED, FD_FRATED, FD_SLIP, FD_GEAR, FD_RPMMIN, FD_RPMMAX, FD_HZMAX,
  FD_AMAX, FD_DEV, FD_ACC, FD_DEC, FD_PPR, FD_VFDID, FD_PIN,
};
struct FieldDef {
  uint16_t key;
  const char* title;
  const char* unit;
  int32_t min, max;
  uint8_t dec;
};
const FieldDef FIELDS[] = {
    {KEY_F_SPEED, "Travel speed", "km/h", 1, 200, 1},
    {KEY_F_WHEEL, "Ground wheel diameter", "m", 100, 2500, 3},
    {KEY_F_LENGTH, "Test duration", "min", 1, 99999, 1},
    {KEY_F_S1A, "Stage 1 driver teeth", "T", 1, 200, 0},
    {KEY_F_S1B, "Stage 1 driven teeth", "T", 1, 200, 0},
    {KEY_F_S2A, "Stage 2 driver teeth", "T", 0, 200, 0},
    {KEY_F_S2B, "Stage 2 driven teeth", "T", 0, 200, 0},
    {KEY_F_RA, "Rig sprocket teeth", "T", 1, 200, 0},
    {KEY_F_RB, "Machine sprocket teeth", "T", 1, 200, 0},
    {KEY_F_ROWS, "Rows / shovels", "", 1, 40, 0},
    {KEY_F_ROWSP, "Row spacing", "cm", 50, 1000, 1},
    {KEY_F_SPACING, "Seed spacing", "cm", 5, 1000, 1},
    {KEY_F_CELLS, "Metering disc cells", "", 1, 200, 0},
    {KEY_F_SEED, "Seed collected", "g", 0, 999999, 1},
    {KEY_F_FERT, "Fertilizer collected", "g", 0, 999999, 1},
    {KEY_F_POLES, "Motor poles", "", 2, 12, 0},
    {KEY_F_NRATED, "Motor rated speed", "RPM", 300, 3600, 0},
    {KEY_F_FRATED, "Motor rated frequency", "Hz", 250, 600, 1},
    {KEY_F_SLIP, "Slip compensation", "%", 0, 150, 1},
    {KEY_F_GEAR, "Gearbox ratio", ": 1", 100, 99900, 2},
    {KEY_F_RPMMIN, "Min output speed", "RPM", 10, 2000, 1},
    {KEY_F_RPMMAX, "Max output speed", "RPM", 10, 3000, 1},
    {KEY_F_HZMAX, "Max drive frequency", "Hz", 100, 800, 1},
    {KEY_F_AMAX, "Motor current limit", "A", 5, 500, 1},
    {KEY_F_DEV, "Max speed deviation", "%", 0, 500, 1},
    {KEY_F_ACC, "Acceleration 0 to rated", "s", 5, 6000, 1},
    {KEY_F_DEC, "Deceleration rated to 0", "s", 5, 6000, 1},
    {KEY_F_PPR, "RPM sensor pulses per rev", "/rev", 0, 999, 0},
    {KEY_F_VFDID, "VFD Modbus address", "", 1, 254, 0},
    {0, "Service PIN", "", 0, 9999, 0},
};

struct CropPreset { const char* name; uint16_t spacing_x10, cells; };
const CropPreset CROPS[] = {
    {"Maize / Corn", 200, 24}, {"Soybean", 50, 30}, {"Chickpea (Gram)", 100, 12}, {"Mustard", 30, 60}, {"Custom crop", 0, 0}};

const uint16_t CROP_SEL[] = {S_CROP0_SEL, S_CROP1_SEL, S_CROP2_SEL, S_CROP3_SEL, S_CROP4_SEL};
const uint16_t PF_ON[] = {S_PF0_ON, S_PF1_ON, S_PF2_ON, S_PF3_ON, S_PF4_ON, S_PF5_ON};
const uint16_t PF_T[] = {T_PF0_T, T_PF1_T, T_PF2_T, T_PF3_T, T_PF4_T, T_PF5_T};
const uint16_t RP_T[] = {T_RP0_T, T_RP1_T, T_RP2_T, T_RP3_T, T_RP4_T, T_RP5_T};
const uint16_t RP_UP[] = {S_RP0_UP, S_RP1_UP, S_RP2_UP, S_RP3_UP, S_RP4_UP, S_RP5_UP};
const uint16_t WF_T[] = {T_WF0_T, T_WF1_T, T_WF2_T, T_WF3_T, T_WF4_T, T_WF5_T};
const uint16_t WF_SIG[] = {S_WF0_SIG, S_WF1_SIG, S_WF2_SIG, S_WF3_SIG, S_WF4_SIG, S_WF5_SIG};
const uint16_t WF_ON[] = {S_WF0_ON, S_WF1_ON, S_WF2_ON, S_WF3_ON, S_WF4_ON, S_WF5_ON};
const uint16_t WF_L[] = {S_WF0_L, S_WF1_L, S_WF2_L, S_WF3_L, S_WF4_L, S_WF5_L};
const uint16_t DG[] = {T_DG0, T_DG1, T_DG2, T_DG3, T_DG4, T_DG5, T_DG6, T_DG7, T_DG8, T_DG9, T_DG10,
                       T_DG11, T_DG12, T_DG13, T_DG14, T_DG15, T_DG16, T_DG17, T_DG18, T_DG19, T_DG20,
                       T_DG21, T_DG22, T_DG23, T_DG24, T_DG25, T_DG26, T_DG27, T_DG28, T_DG29, T_DG30, T_DG31};

const char* faultAdvice(Fault f) {
  switch (f) {
    case Fault::EStop: return "Release the emergency stop after checking the machine";
    case Fault::Guard: return "Close and latch the chain guard";
    case Fault::LocalEnableLost: return "Turn the local enable key back on";
    case Fault::DriveComms: return "Check the RS485 cable and VFD power";
    case Fault::DriveRelay: return "Check the VFD display for the fault";
    case Fault::OverCurrent: return "Check for a jammed metering drive or tight chain";
    case Fault::SpeedDeviation: return "Check chain, coupling and the RPM sensor";
    case Fault::Stall: return "Motor is not following the command: check the load";
    case Fault::HmiLost: return "Check the display cable";
    default: return "";
  }
}

}  // namespace

Ui::Ui(ByteSink& out, UiServices& s) : tx_(out), s_(s) {
  rx_.setHandler(&Ui::onVp, this);
  memset(shadow_, 0xA5, sizeof(shadow_));
}

void Ui::begin(const AppConfig& a, const MachineProfile* profiles, uint8_t count) {
  cfg_ = a;
  memset(prof_, 0, sizeof(prof_));
  for (uint8_t i = 0; i < count && i < PROFILE_SLOTS; i++) prof_[i] = profiles[i];
  slot_ = a.active_profile < PROFILE_SLOTS ? a.active_profile : 0;
  if (!prof_[slot_].used) defaultProfile(prof_[slot_]);
  cur_ = prof_[slot_];
  pf_sel_ = slot_;
  pf_page_ = slot_ / ROWS;
  limit_ = compute(cur_, cfg_.drive, kin_);
  s_.applyMachine(cur_, cfg_);
}

// ---- wire layer -------------------------------------------------------------
void Ui::put(uint16_t vp, const uint16_t* w, uint8_t n) {
  uint16_t off = vp - SHADOW_BASE;
  if (off + n > SHADOW_WORDS) return;
  if (!memcmp(shadow_ + off, w, n * 2u)) return;
  memcpy(shadow_ + off, w, n * 2u);
  tx_.writeWords(vp, w, n);
}

void Ui::digits(uint16_t vp, uint8_t len, uint8_t dec, int32_t v) {
  uint16_t d[8];
  bool neg = v < 0;
  if (neg) v = -v;
  for (int i = len - 1; i >= 0; i--) {
    d[i] = v % 10;
    v /= 10;
  }
  if (v) { for (uint8_t i = 0; i < len; i++) d[i] = 9; }   // overflow shows all nines
  // Leading zeros blank, but keep one digit before the decimal point.
  for (int i = 0; i < len - dec - 1 && d[i] == 0; i++) d[i] = DIGIT_BLANK;
  put(vp, d, len);
}

void Ui::dashes(uint16_t vp, uint8_t len) {
  uint16_t d[8];
  for (uint8_t i = 0; i < len; i++) d[i] = DIGIT_DASH;
  put(vp, d, len);
}

void Ui::clock(uint16_t vp, uint32_t seconds) {
  uint32_t m = seconds / 60, s = seconds % 60;
  if (m > 99) { m = 99; s = 59; }
  uint16_t d[4] = {(uint16_t)(m / 10), (uint16_t)(m % 10), (uint16_t)(s / 10), (uint16_t)(s % 10)};
  put(vp, d, 4);
}

void Ui::text(uint16_t vp, uint8_t maxlen, const char* s) {
  uint16_t w[80];
  uint8_t len = 0;
  uint8_t bytes[160];
  while (s[len] && len < maxlen && len < 156) {
    uint8_t c = (uint8_t)s[len];
    bytes[len++] = (c < 0x20 || c > 0x7E) ? '?' : c;
  }
  bytes[len] = 0xFF;
  bytes[len + 1] = 0xFF;
  uint8_t words = (len + 3) / 2;
  if (len & 1) bytes[len + 2] = 0xFF;
  for (uint8_t i = 0; i < words; i++) w[i] = (uint16_t)bytes[2 * i] << 8 | bytes[2 * i + 1];
  put(vp, w, words);
}

void Ui::textf(uint16_t vp, uint8_t maxlen, const char* fmt, ...) {
  char b[80];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  text(vp, maxlen, b);
}

// ---- navigation -------------------------------------------------------------
void Ui::goTo(uint16_t p) {
  page_ = p;
  tx_.setPage(p);
  render(true);
}

void Ui::push(uint16_t p) {
  if (depth_ < 8) stack_[depth_++] = page_;
  goTo(p);
}

void Ui::pop() {
  goTo(depth_ ? stack_[--depth_] : (uint16_t)PAGE_HOME);
}

void Ui::onVp(void* self, uint16_t vp, const uint16_t* w, uint8_t n) { ((Ui*)self)->handleVp(vp, w, n); }

void Ui::handleVp(uint16_t vp, const uint16_t* w, uint8_t n) {
  last_rx_ms_ = now_;
  if (!link_seen_) {
    // First reply from the display: it has just booted, so nothing we wrote survives.
    link_seen_ = true;
    tx_.systemConfig();
    tx_.brightness(cfg_.brightness);
    memset(shadow_, 0xA5, sizeof(shadow_));
    if (!booted_) { boot_ms_ = now_; page_ = PAGE_BOOT; }
    tx_.setPage(page_);
    tx_.read(0x0010, 4);
    render(true);
  }
  if (vp == VP_KEY && n) { onKey(w[0]); return; }
  if (vp == 0x0010 && n >= 4) { rtcReply(w); return; }
  if (vp == DGUS_PIC_NOW && n && w[0] != page_) {
    // Pressing a key flashes the pressed image but the page id stays; a mismatch
    // means the display restarted behind our back.
    if (w[0] == PAGE_BOOT) { link_seen_ = false; return; }
    tx_.setPage(page_);
  }
}

// RTC words: [YY MM] [DD week] [hh mm] [ss 00], local time.
void Ui::rtcReply(const uint16_t* w) {
  int y = 2000 + (w[0] >> 8), m = w[0] & 0xFF, d = w[1] >> 8;
  int hh = w[2] >> 8, mm = w[2] & 0xFF, ss = w[3] >> 8;
  if (y < 2024 || m < 1 || m > 12 || d < 1 || d > 31) return;   // never set, or no RTC fitted
  // Days from civil (Howard Hinnant)
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t)era * 146097 + doe - 719468;
  s_.rtcRead((uint32_t)(days * 86400 + hh * 3600 + mm * 60 + ss - 19800));
}

void Ui::rtcWrite(uint32_t unix_s) {
  char b[24];
  formatDateTime(unix_s, b, sizeof(b));   // dd-mm-yyyy hh:mm, already local
  int d, m, y, hh, mm;
  if (sscanf(b, "%d-%d-%d %d:%d", &d, &m, &y, &hh, &mm) != 5) return;
  uint16_t w[4] = {0x5AA5, (uint16_t)((y - 2000) << 8 | m), (uint16_t)(d << 8 | hh), (uint16_t)(mm << 8 | (unix_s % 60))};
  tx_.writeWords(DGUS_RTC_SET, w, 4);
}

// ---- keys -------------------------------------------------------------------
void Ui::changed() {
  limit_ = compute(cur_, cfg_.drive, kin_);
  cur_.used = 1;
  prof_[slot_] = cur_;
  s_.applyMachine(cur_, cfg_);
  s_.saveProfile(slot_, cur_);
}

void Ui::setDrive(bool sim) {
  if (t_.state != RunState::Ready) { tx_.beep(BEEP_ERROR); return; }
  cfg_.sim_drive = sim;
  s_.saveConfig(cfg_);
  s_.applyMachine(cur_, cfg_);
}

bool Ui::startEdit(uint16_t key) {
  for (uint8_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); i++) {
    if (FIELDS[i].key != key) continue;
    np_field_ = i;
    np_pin_ = false;
    int32_t v = fieldGet(i);
    uint8_t dec = FIELDS[i].dec;
    if (i == FD_SPEED && cur_.mode == MODE_DIRECT) dec = 1;
    int32_t div = dec == 3 ? 1000 : dec == 2 ? 100 : dec == 1 ? 10 : 1;
    if (dec) snprintf(np_buf_, sizeof(np_buf_), "%ld.%0*ld", (long)(v / div) % 1000000, (int)dec, (long)(v % div));
    else snprintf(np_buf_, sizeof(np_buf_), "%ld", (long)v);
    np_len_ = strlen(np_buf_);
    np_fresh_ = true;
    np_err_[0] = 0;
    push(PAGE_NUMPAD);
    return true;
  }
  return false;
}

int32_t Ui::fieldGet(uint8_t f) {
  const DriveConfig& d = cfg_.drive;
  switch (f) {
    case FD_SPEED: return cur_.mode == MODE_FIELD ? cur_.speed_x10 : cur_.rpm_x10;
    case FD_WHEEL: return cur_.wheel_mm;
    case FD_LENGTH: return (int32_t)cur_.length_x10;
    case FD_S1A: return cur_.s1_driver;
    case FD_S1B: return cur_.s1_driven;
    case FD_S2A: return cur_.s2_driver;
    case FD_S2B: return cur_.s2_driven;
    case FD_RA: return cur_.rig_driver;
    case FD_RB: return cur_.rig_driven;
    case FD_ROWS: return cur_.rows;
    case FD_ROWSP: return cur_.row_sp_x10;
    case FD_SPACING: return cur_.spacing_x10;
    case FD_CELLS: return cur_.cells;
    case FD_SEED: return (int32_t)rep_.seed_dg;
    case FD_FERT: return (int32_t)rep_.fert_dg;
    case FD_POLES: return d.poles;
    case FD_NRATED: return d.rated_rpm;
    case FD_FRATED: return d.rated_hz_x10;
    case FD_SLIP: return d.slip_x10;
    case FD_GEAR: return d.gear_x100;
    case FD_RPMMIN: return d.rpm_min_x10;
    case FD_RPMMAX: return d.rpm_max_x10;
    case FD_HZMAX: return d.hz_max_x10;
    case FD_AMAX: return d.amps_max_x10;
    case FD_DEV: return d.dev_x10;
    case FD_ACC: return d.acc_x10;
    case FD_DEC: return d.dec_x10;
    case FD_PPR: return d.ppr;
    case FD_VFDID: return d.vfd_id;
  }
  return 0;
}

void Ui::fieldSet(uint8_t f, int32_t v) {
  DriveConfig& d = cfg_.drive;
  bool drive = false;
  switch (f) {
    case FD_SPEED: if (cur_.mode == MODE_FIELD) cur_.speed_x10 = v; else cur_.rpm_x10 = v; break;
    case FD_WHEEL: cur_.wheel_mm = v; break;
    case FD_LENGTH: cur_.length_x10 = v; break;
    case FD_S1A: cur_.s1_driver = v; break;
    case FD_S1B: cur_.s1_driven = v; break;
    case FD_S2A: cur_.s2_driver = v; break;
    case FD_S2B: cur_.s2_driven = v; break;
    case FD_RA: cur_.rig_driver = v; break;
    case FD_RB: cur_.rig_driven = v; break;
    case FD_ROWS: cur_.rows = v; break;
    case FD_ROWSP: cur_.row_sp_x10 = v; break;
    case FD_SPACING: cur_.spacing_x10 = v; cur_.crop = 4; break;
    case FD_CELLS: cur_.cells = v; cur_.crop = 4; break;
    case FD_SEED: rep_.seed_dg = v; s_.saveMeasured(rep_.id, rep_.seed_dg, rep_.fert_dg); return;
    case FD_FERT: rep_.fert_dg = v; s_.saveMeasured(rep_.id, rep_.seed_dg, rep_.fert_dg); return;
    case FD_POLES: d.poles = v; drive = true; break;
    case FD_NRATED: d.rated_rpm = v; drive = true; break;
    case FD_FRATED: d.rated_hz_x10 = v; drive = true; break;
    case FD_SLIP: d.slip_x10 = v; drive = true; break;
    case FD_GEAR: d.gear_x100 = v; drive = true; break;
    case FD_RPMMIN: d.rpm_min_x10 = v; drive = true; break;
    case FD_RPMMAX: d.rpm_max_x10 = v; drive = true; break;
    case FD_HZMAX: d.hz_max_x10 = v; drive = true; break;
    case FD_AMAX: d.amps_max_x10 = v; drive = true; break;
    case FD_DEV: d.dev_x10 = v; drive = true; break;
    case FD_ACC: d.acc_x10 = v; drive = true; break;
    case FD_DEC: d.dec_x10 = v; drive = true; break;
    case FD_PPR: d.ppr = v; drive = true; break;
    case FD_VFDID: d.vfd_id = v; drive = true; break;
    default: return;
  }
  if (drive) s_.saveConfig(cfg_);
  changed();
}

// Parses the typed text into the field's scaled integer without floating point, so
// "1.005" with 3 decimals is exactly 1005.
void Ui::npCommit() {
  const FieldDef& f = FIELDS[np_field_];
  uint8_t dec = f.dec;
  int32_t lo = f.min, hi = f.max;
  if (np_field_ == FD_SPEED && cur_.mode == MODE_DIRECT) { lo = cfg_.drive.rpm_min_x10; hi = cfg_.drive.rpm_max_x10; }
  int64_t v = 0;
  int frac = -1;
  for (uint8_t i = 0; i < np_len_; i++) {
    char c = np_buf_[i];
    if (c == '.') { if (frac >= 0) { frac = 99; break; } frac = 0; continue; }
    if (frac >= 0) { if (frac >= dec) continue; frac++; }
    v = v * 10 + (c - '0');
  }
  if (frac == 99 || !np_len_) { strcpy(np_err_, "Not a valid number"); tx_.beep(BEEP_ERROR); npRender(); return; }
  for (int i = frac < 0 ? 0 : frac; i < dec; i++) v *= 10;

  if (np_pin_) {
    if (v == SERVICE_PIN) { depth_ = depth_ ? depth_ - 1 : 0; page_ = stack_[depth_]; push(PAGE_DRIVE); return; }
    strcpy(np_err_, "Wrong PIN");
    np_len_ = 0; np_buf_[0] = 0;
    tx_.beep(BEEP_ERROR);
    npRender();
    return;
  }
  if (v < lo || v > hi) {
    strcpy(np_err_, "Outside the allowed range");
    tx_.beep(BEEP_ERROR);
    npRender();
    return;
  }
  fieldSet(np_field_, (int32_t)v);
  pop();
}

void Ui::npRender() {
  const FieldDef& f = FIELDS[np_field_];
  uint8_t dec = f.dec;
  int32_t lo = f.min, hi = f.max;
  const char* unit = f.unit;
  const char* title = f.title;
  if (np_field_ == FD_SPEED && cur_.mode == MODE_DIRECT) { lo = cfg_.drive.rpm_min_x10; hi = cfg_.drive.rpm_max_x10; unit = "RPM"; title = "Output shaft speed"; }
  if (np_field_ == FD_LENGTH && cur_.stop_by == STOP_DISTANCE) { unit = "m"; title = "Simulated distance"; }
  text(T_NP_TITLE, T_NP_TITLE_LEN, title);
  if (np_pin_) {
    char m[14] = {0};
    memset(m, '*', np_len_);
    text(T_NP_VALUE, T_NP_VALUE_LEN, m);
    text(T_NP_RANGE, T_NP_RANGE_LEN, "");
  } else {
    text(T_NP_VALUE, T_NP_VALUE_LEN, np_buf_);
    int32_t div = dec == 3 ? 1000 : dec == 2 ? 100 : dec == 1 ? 10 : 1;
    if (dec) textf(T_NP_RANGE, T_NP_RANGE_LEN, "Range %ld.%0*ld to %ld.%0*ld", (long)(lo / div), dec, (long)(lo % div), (long)(hi / div), dec, (long)(hi % div));
    else textf(T_NP_RANGE, T_NP_RANGE_LEN, "Range %ld to %ld", (long)lo, (long)hi);
  }
  text(T_NP_UNIT, T_NP_UNIT_LEN, unit);
  text(T_NP_ERR, T_NP_ERR_LEN, np_err_);
}

void Ui::kbOpen(Kb mode, const char* title, const char* initial, uint8_t max) {
  kb_mode_ = mode;
  kb_max_ = max > PASS_MAX ? PASS_MAX : max;
  strncpy(kb_buf_, initial, PASS_MAX);
  kb_buf_[PASS_MAX] = 0;
  kb_len_ = strlen(kb_buf_);
  kb_show_ = mode != Kb::Password;
  strncpy(kb_title_, title, sizeof(kb_title_) - 1);
  kb_title_[sizeof(kb_title_) - 1] = 0;
  kb_page_ = PAGE_KB_LOW;
  push(PAGE_KB_LOW);
}

void Ui::kbRender() {
  text(T_KB_TITLE, T_KB_TITLE_LEN, kb_title_);
  char v[PASS_MAX + 2];
  if (kb_show_) memcpy(v, kb_buf_, kb_len_ + 1);
  else { memset(v, '*', kb_len_); v[kb_len_] = 0; }
  const char* shown = v;
  if (kb_len_ > T_KB_VALUE_LEN - 1) shown = v + kb_len_ - (T_KB_VALUE_LEN - 1);
  char line[T_KB_VALUE_LEN + 2];
  snprintf(line, sizeof(line), "%s_", shown);
  text(T_KB_VALUE, T_KB_VALUE_LEN, line);
}

void Ui::kbCommit() {
  if (kb_mode_ == Kb::Password) {
    if (kb_len_ < 8) {   // WPA2 minimum
      tx_.beep(BEEP_ERROR);
      return;
    }
    s_.wifiConnect(pending_ssid_, kb_buf_);
    memset(kb_buf_, 0, sizeof(kb_buf_));
    kb_len_ = 0;
    pop();
    return;
  }
  uint8_t slot = pf_sel_;
  if (!prof_[slot].used) { tx_.beep(BEEP_ERROR); pop(); return; }
  strncpy(prof_[slot].name, kb_buf_, NAME_LEN - 1);
  prof_[slot].name[NAME_LEN - 1] = 0;
  s_.saveProfile(slot, prof_[slot]);
  if (slot == slot_) { memcpy(cur_.name, prof_[slot].name, NAME_LEN); s_.applyMachine(cur_, cfg_); }
  pop();
}

void Ui::confirm(Ask what, const char* title, const char* m1, const char* m2) {
  ask_ = what;
  text(T_CF_TITLE, T_CF_TITLE_LEN, title);
  text(T_CF_MSG1, T_CF_MSG1_LEN, m1);
  text(T_CF_MSG2, T_CF_MSG2_LEN, m2);
  push(PAGE_CONFIRM);
}

void Ui::onChar(char c) {
  if (page_ == PAGE_NUMPAD) {
    if (np_fresh_) { np_len_ = 0; np_buf_[0] = 0; np_fresh_ = false; }
    if ((c >= '0' && c <= '9') || (c == '.' && !np_pin_ && FIELDS[np_field_].dec)) {
      if (np_len_ < sizeof(np_buf_) - 1 && np_len_ < 11) { np_buf_[np_len_++] = c; np_buf_[np_len_] = 0; }
    }
    np_err_[0] = 0;
    npRender();
  } else if (page_ == PAGE_KB_LOW || page_ == PAGE_KB_UP || page_ == PAGE_KB_SYM) {
    if (kb_len_ < kb_max_) { kb_buf_[kb_len_++] = c; kb_buf_[kb_len_] = 0; }
    kbRender();
    // One-shot shift, as on a phone
    if (page_ == PAGE_KB_UP) { page_ = PAGE_KB_LOW; tx_.setPage(page_); }
  }
}

void Ui::onKey(uint16_t code) {
  if (code < 0x80) { onChar((char)code); return; }

  switch (code) {
    case KEY_BACK:
      if (page_ == PAGE_PRE) s_.command({CmdType::Disarm, 0, 0});
      pop();
      return;
    case KEY_GO_PRE:
      if (limit_ != Limit::Ok) { tx_.beep(BEEP_ERROR); if (page_ != PAGE_SETUP) push(PAGE_SETUP); return; }
      s_.command({CmdType::Arm, 0, 0});
      push(PAGE_PRE);
      return;
    case KEY_GO_SETUP: push(PAGE_SETUP); return;
    case KEY_GO_PROFILES: case KEY_SAVE_PROFILE: pf_sel_ = slot_; pf_page_ = slot_ / ROWS; push(PAGE_PROFILES); return;
    case KEY_GO_CROP: push(PAGE_CROP); return;
    case KEY_GO_TRANS: push(PAGE_TRANS); return;
    case KEY_GO_REPORTS: rp_page_ = 0; push(PAGE_REPORTS); return;
    case KEY_GO_SETTINGS: push(PAGE_SETTINGS); return;
    case KEY_GO_WIFI: ap_page_ = 0; s_.wifiScan(); push(PAGE_WIFI); return;
    case KEY_GO_DIAG: case KEY_FT_DIAG: push(PAGE_DIAG); return;
    case KEY_GO_DRIVE:
      if (t_.state != RunState::Ready) { tx_.beep(BEEP_ERROR); return; }
      np_field_ = FD_PIN; np_pin_ = true; np_len_ = 0; np_buf_[0] = 0; np_fresh_ = false; np_err_[0] = 0;
      push(PAGE_NUMPAD);
      return;

    case KEY_MODE_FIELD: cur_.mode = MODE_FIELD; changed(); render(true); return;
    case KEY_MODE_RPM: cur_.mode = MODE_DIRECT; changed(); render(true); return;
    case KEY_STOP_TIME: cur_.stop_by = STOP_TIME; changed(); render(true); return;
    case KEY_STOP_DIST: cur_.stop_by = STOP_DISTANCE; changed(); render(true); return;
    case KEY_CPL_WHEEL: cur_.coupling = COUPLE_WHEEL; changed(); render(true); return;
    case KEY_CPL_SHAFT: cur_.coupling = COUPLE_SHAFT; changed(); render(true); return;
    case KEY_CROP0: case KEY_CROP1: case KEY_CROP2: case KEY_CROP3: case KEY_CROP4: {
      uint8_t i = code - KEY_CROP0;
      cur_.crop = i;
      if (CROPS[i].cells) { cur_.spacing_x10 = CROPS[i].spacing_x10; cur_.cells = CROPS[i].cells; }
      changed();
      render(true);
      return;
    }

    case KEY_KB_BKSP:
      if (page_ == PAGE_NUMPAD) {
        np_fresh_ = false;
        if (np_len_) np_buf_[--np_len_] = 0;
        np_err_[0] = 0;
        npRender();
      } else {
        if (kb_len_) kb_buf_[--kb_len_] = 0;
        kbRender();
      }
      return;
    case KEY_NP_CLEAR: np_len_ = 0; np_buf_[0] = 0; np_fresh_ = false; np_err_[0] = 0; npRender(); return;
    case KEY_NP_CANCEL: pop(); return;
    case KEY_NP_OK: npCommit(); return;

    case KEY_KB_UPPER: page_ = PAGE_KB_UP; tx_.setPage(page_); return;
    case KEY_KB_LOWER: page_ = PAGE_KB_LOW; tx_.setPage(page_); return;
    case KEY_KB_SYM: page_ = PAGE_KB_SYM; tx_.setPage(page_); return;
    case KEY_KB_SHOW: kb_show_ = !kb_show_; kbRender(); return;
    case KEY_KB_CANCEL: memset(kb_buf_, 0, sizeof(kb_buf_)); kb_len_ = 0; pop(); return;
    case KEY_KB_OK: kbCommit(); return;

    case KEY_PRE_CONFIRM: s_.command({CmdType::ConfirmSecured, (uint8_t)!t_.confirmed, 0}); return;
    case KEY_RUN_START:
      if (t_.confirmed && t_.estop_ok && t_.guard_ok && t_.local_enable && t_.drive_ready && t_.target_ok) s_.command({CmdType::Start, 0, 0});
      else tx_.beep(BEEP_ERROR);
      return;
    case KEY_RUN_STOP: s_.command({CmdType::Stop, 0, 0}); return;

    case KEY_R_SAVE: if (rep_valid_) s_.saveMeasured(rep_.id, rep_.seed_dg, rep_.fert_dg); tx_.beep(BEEP_SHORT); return;
    case KEY_R_HOME:
      if (t_.state == RunState::Complete) s_.command({CmdType::Acknowledge, 0, 0});
      if (rep_from_list_) pop(); else { depth_ = 0; goTo(PAGE_HOME); }
      return;

    case KEY_PF0: case KEY_PF1: case KEY_PF2: case KEY_PF3: case KEY_PF4: case KEY_PF5:
      pf_sel_ = pf_page_ * ROWS + (code - KEY_PF0);
      render(true);
      return;
    case KEY_PF_PREV: if (pf_page_) { pf_page_--; render(true); } return;
    case KEY_PF_NEXT: if ((pf_page_ + 1) * ROWS < PROFILE_SLOTS) { pf_page_++; render(true); } return;
    case KEY_PF_LOAD:
      if (!prof_[pf_sel_].used || t_.state != RunState::Ready) { tx_.beep(BEEP_ERROR); return; }
      slot_ = pf_sel_;
      cur_ = prof_[slot_];
      cfg_.active_profile = slot_;
      s_.saveConfig(cfg_);
      changed();
      render(true);
      tx_.beep(BEEP_SHORT);
      return;
    case KEY_PF_SAVE:
      if (prof_[pf_sel_].used && pf_sel_ != slot_) { confirm(Ask::OverwriteProfile, "Replace this profile?", prof_[pf_sel_].name, "Its settings will be overwritten."); return; }
      prof_[pf_sel_] = cur_;
      prof_[pf_sel_].used = 1;
      s_.saveProfile(pf_sel_, prof_[pf_sel_]);
      render(true);
      return;
    case KEY_PF_RENAME:
      if (!prof_[pf_sel_].used) { tx_.beep(BEEP_ERROR); return; }
      kbOpen(Kb::ProfileName, "Profile name", prof_[pf_sel_].name, NAME_LEN - 1);
      return;
    case KEY_PF_DELETE:
      if (!prof_[pf_sel_].used || pf_sel_ == slot_) { tx_.beep(BEEP_ERROR); return; }
      confirm(Ask::DeleteProfile, "Delete this profile?", prof_[pf_sel_].name, "This cannot be undone.");
      return;

    case KEY_RP0: case KEY_RP1: case KEY_RP2: case KEY_RP3: case KEY_RP4: case KEY_RP5: {
      uint8_t i = code - KEY_RP0;
      if (i >= row_count_) return;
      TestHeader h;
      if (s_.loadReport(rows_[i].id, h)) { rep_from_list_ = true; enterReport(h); push(PAGE_DONE); }
      else tx_.beep(BEEP_ERROR);
      return;
    }
    case KEY_RP_PREV: if (rp_page_) { rp_page_--; render(true); } return;
    case KEY_RP_NEXT: if ((rp_page_ + 1u) * ROWS < s_.reportCount()) { rp_page_++; render(true); } return;

    case KEY_REMOTE_OFF: s_.command({CmdType::SetRemoteAllowed, 0, 0}); return;
    case KEY_REMOTE_ON:
      if (!t_.local_enable) { tx_.beep(BEEP_ERROR); return; }
      s_.command({CmdType::SetRemoteAllowed, 1, 0});
      return;
    case KEY_SIM_OFF: setDrive(false); render(true); return;
    case KEY_SIM_ON: setDrive(true); render(true); return;
    case KEY_BR_DOWN: case KEY_BR_UP: {
      int b = cfg_.brightness + (code == KEY_BR_UP ? 10 : -10);
      cfg_.brightness = b < 10 ? 10 : b > 100 ? 100 : b;
      tx_.brightness(cfg_.brightness);
      s_.saveConfig(cfg_);
      render(true);
      return;
    }

    case KEY_WF0: case KEY_WF1: case KEY_WF2: case KEY_WF3: case KEY_WF4: case KEY_WF5: {
      uint8_t i = ap_page_ * ROWS + (code - KEY_WF0);
      if (i >= ap_count_) return;
      strncpy(pending_ssid_, aps_[i].ssid, 32);
      pending_ssid_[32] = 0;
      if (aps_[i].secure && !aps_[i].known) kbOpen(Kb::Password, "Password", "", PASS_MAX);
      else s_.wifiConnect(pending_ssid_, aps_[i].secure ? nullptr : "");
      return;
    }
    case KEY_WF_PREV: if (ap_page_) { ap_page_--; render(true); } return;
    case KEY_WF_NEXT: if ((ap_page_ + 1u) * ROWS < ap_count_) { ap_page_++; render(true); } return;
    case KEY_WF_SCAN: ap_page_ = 0; s_.wifiScan(); return;
    case KEY_WF_FORGET: confirm(Ask::ForgetWifi, "Forget this network?", "The device will disconnect and clear", "the saved password."); return;

    case KEY_FT_RESET: s_.command({CmdType::ResetFault, 0, 0}); return;

    case KEY_CF_NO: ask_ = Ask::None; pop(); return;
    case KEY_CF_YES: {
      Ask a = ask_;
      ask_ = Ask::None;
      if (a == Ask::ForgetWifi) s_.wifiForget();
      if (a == Ask::DeleteProfile) { prof_[pf_sel_].used = 0; s_.saveProfile(pf_sel_, prof_[pf_sel_]); }
      if (a == Ask::OverwriteProfile) { prof_[pf_sel_] = cur_; prof_[pf_sel_].used = 1; s_.saveProfile(pf_sel_, prof_[pf_sel_]); }
      pop();
      return;
    }
    default:
      if (startEdit(code)) return;
  }
}

// ---- pages ------------------------------------------------------------------
void Ui::renderHeader() {
  NetStatus n;
  s_.netStatus(n);
  state(S_H_SIM, cfg_.sim_drive ? 1 : 0);
  state(S_H_REMOTE, t_.remote_allowed ? 1 : 0);
  state(S_H_WIFI, n.wifi == WifiState::Connected ? n.bars : 0);
  state(S_H_CLOUD, n.cloud);
  uint32_t u = s_.nowUnix();
  if (u > 1000000000u) {
    u += 19800;   // IST
    textf(T_H_CLOCK, T_H_CLOCK_LEN, "%02u:%02u", (unsigned)(u / 3600 % 24), (unsigned)(u / 60 % 60));
  } else {
    text(T_H_CLOCK, T_H_CLOCK_LEN, "--:--");
  }
}

void Ui::renderHome() {
  text(T_PROFILE, T_PROFILE_LEN, cur_.name);
  textf(T_CROP, T_CROP_LEN, "%s  |  %u rows  |  wheel %u.%03u m", CROPS[cur_.crop < 5 ? cur_.crop : 4].name, cur_.rows, cur_.wheel_mm / 1000, cur_.wheel_mm % 1000);
  state(S_STATE, (uint16_t)t_.state);
  digits(N_SPEED, N_SPEED_LEN, 1, (int32_t)(kin_.speed_kmh * 10.0f + 0.5f));
  digits(N_OUT_RPM, N_OUT_RPM_LEN, 1, (int32_t)(kin_.out_rpm * 10.0f + 0.5f));
  digits(N_HZ_CMD, N_HZ_CMD_LEN, 2, (int32_t)(kin_.hz * 100.0f + 0.5f));
  digits(N_LENGTH, N_LENGTH_LEN, 1, (int32_t)cur_.length_x10);
  state(S_LENGTH_U, cur_.stop_by);
}

void Ui::renderSetup() {
  state(S_MODE, cur_.mode);
  state(S_SPEED_L, cur_.mode);
  state(S_SPEED_U, cur_.mode);
  digits(N_F_SPEED_V, N_F_SPEED_V_LEN, 1, cur_.mode == MODE_FIELD ? cur_.speed_x10 : cur_.rpm_x10);
  digits(N_F_WHEEL_V, N_F_WHEEL_V_LEN, 3, cur_.wheel_mm);
  state(S_STOPBY, cur_.stop_by);
  state(S_LEN_L, cur_.stop_by);
  state(S_LEN_U, cur_.stop_by);
  digits(N_F_LENGTH_V, N_F_LENGTH_V_LEN, 1, (int32_t)cur_.length_x10);
  bool ok = limit_ == Limit::Ok;
  state(S_VALID, ok ? 0 : 1);
  text(T_VALID_MSG, T_VALID_MSG_LEN, limitText(limit_));
  if (limit_ == Limit::BadInput) {
    dashes(N_C_WHEEL, N_C_WHEEL_LEN); dashes(N_C_SHAFT, N_C_SHAFT_LEN); dashes(N_C_OUT, N_C_OUT_LEN);
    dashes(N_C_MOTOR, N_C_MOTOR_LEN); dashes(N_C_HZ, N_C_HZ_LEN);
    return;
  }
  digits(N_C_WHEEL, N_C_WHEEL_LEN, 1, (int32_t)(kin_.wheel_rpm * 10.0f + 0.5f));
  digits(N_C_SHAFT, N_C_SHAFT_LEN, 1, (int32_t)(kin_.shaft_rpm * 10.0f + 0.5f));
  digits(N_C_OUT, N_C_OUT_LEN, 1, (int32_t)(kin_.out_rpm * 10.0f + 0.5f));
  digits(N_C_MOTOR, N_C_MOTOR_LEN, 0, (int32_t)(kin_.motor_rpm + 0.5f));
  digits(N_C_HZ, N_C_HZ_LEN, 2, (int32_t)(kin_.hz * 100.0f + 0.5f));
}

void Ui::renderTrans() {
  state(S_COUPLE, cur_.coupling);
  digits(N_F_S1A_V, 3, 0, cur_.s1_driver);
  digits(N_F_S1B_V, 3, 0, cur_.s1_driven);
  digits(N_F_S2A_V, 3, 0, cur_.s2_driver);
  digits(N_F_S2B_V, 3, 0, cur_.s2_driven);
  digits(N_F_RA_V, 3, 0, cur_.rig_driver);
  digits(N_F_RB_V, 3, 0, cur_.rig_driven);
  digits(N_C_RATIO, N_C_RATIO_LEN, 3, (int32_t)(stageRatio(cur_) * 1000.0f + 0.5f));
  digits(N_C_RIG, N_C_RIG_LEN, 3, (int32_t)(rigRatio(cur_) * 1000.0f + 0.5f));
  digits(N_F_ROWS_V, 2, 0, cur_.rows);
  digits(N_F_ROWSP_V, N_F_ROWSP_V_LEN, 1, cur_.row_sp_x10);
}

void Ui::renderCrop() {
  for (uint8_t i = 0; i < 5; i++) state(CROP_SEL[i], cur_.crop == i ? 1 : 0);
  digits(N_F_SPACING_V, N_F_SPACING_V_LEN, 1, cur_.spacing_x10);
  digits(N_F_CELLS_V, N_F_CELLS_V_LEN, 0, cur_.cells);
  SeedRate s = seedRate(kin_.speed_kmh, cur_);
  digits(N_K_FREQ, N_K_FREQ_LEN, 1, (int32_t)(s.drop_hz * 10.0f + 0.5f));
  digits(N_K_INT, N_K_INT_LEN, 1, (int32_t)(s.interval_ms * 10.0f + 0.5f));
  digits(N_K_DISC, N_K_DISC_LEN, 1, (int32_t)(s.disc_rpm * 10.0f + 0.5f));
}

void Ui::renderPre() {
  state(S_PRE0, t_.estop_ok);
  state(S_PRE1, t_.guard_ok);
  state(S_PRE2, t_.local_enable);
  state(S_PRE3, t_.drive_ready);
  state(S_PRE4, t_.target_ok);
  state(S_PRE5, t_.confirmed);
  state(S_PRE_GO, t_.estop_ok && t_.guard_ok && t_.local_enable && t_.drive_ready && t_.target_ok && t_.confirmed);
  text(T_PROFILE, T_PROFILE_LEN, cur_.name);
  digits(N_OUT_RPM, N_OUT_RPM_LEN, 1, (int32_t)(kin_.out_rpm * 10.0f + 0.5f));
  digits(N_HZ_CMD, N_HZ_CMD_LEN, 2, (int32_t)(kin_.hz * 100.0f + 0.5f));
}

void Ui::renderRun() {
  uint16_t g = t_.rpm_x10 / 20;
  state(S_GAUGE, g > S_GAUGE_N - 1 ? S_GAUGE_N - 1 : g);
  state(S_RPM_SRC, t_.measured);
  digits(N_RPM, N_RPM_LEN, 1, t_.rpm_x10);
  digits(N_RPM_T, N_RPM_T_LEN, 1, t_.rpm_target_x10);
  digits(N_V_SIM, N_V_SIM_LEN, 1, t_.v_sim_x10);
  digits(N_DIST, N_DIST_LEN + 0, 1, (int32_t)t_.dist_dm);
  clock(N_T_ELAPSED, t_.elapsed_s);
  clock(N_T_LEFT, t_.remaining_s);
  state(S_PROG, t_.progress / 2);
  digits(N_AMPS, N_AMPS_LEN, 1, t_.amps_x100 / 10);
  digits(N_HZ_OUT, N_HZ_OUT_LEN, 2, t_.hz_out_x100);
  digits(N_TORQUE, N_TORQUE_LEN, 0, t_.torque_x10 < 0 ? 0 : t_.torque_x10 / 10);
  digits(N_VBUS, N_VBUS_LEN, 0, t_.vbus_x10 / 10);
  if (now_ - last_curve_ >= 1000) {
    last_curve_ = now_;
    tx_.curvePoint(0, t_.rpm_x10 / 10);
  }
}

void Ui::enterReport(const TestHeader& h) {
  rep_ = h;
  rep_valid_ = true;
}

void Ui::renderReport() {
  if (!rep_valid_) return;
  const TestHeader& h = rep_;
  textf(T_R_TITLE, T_R_TITLE_LEN, "Test #%lu%s", (unsigned long)h.id, h.sim ? " (simulated)" : "");
  char when[24];
  formatDateTime(h.start_unix, when, sizeof(when));
  textf(T_R_SUB, T_R_SUB_LEN, "%s  |  %s", h.profile.name, when);
  uint16_t st = h.end == EndReason::Completed ? 0 : h.end == EndReason::Faulted ? 2 : h.end == EndReason::Interrupted ? 3 : 1;
  state(S_R_STATE, st);
  digits(N_R_RPM, N_R_RPM_LEN, 1, h.rpm_avg_x10);
  digits(N_R_ACC, N_R_ACC_LEN, 1, h.accuracy_x10);
  clock(N_R_TIME, h.duration_s);
  digits(N_R_DIST, N_R_DIST_LEN, 1, (int32_t)h.dist_dm);
  digits(N_R_AMPS, N_R_AMPS_LEN, 1, h.amps_peak_x100 / 10);
  state(S_R_SYNC, s_.reportSync(h.id));
  digits(N_F_SEED_V, N_F_SEED_V_LEN, 1, (int32_t)h.seed_dg);
  digits(N_F_FERT_V, N_F_FERT_V_LEN, 1, (int32_t)h.fert_dg);
  float dist = h.dist_dm / 10.0f;
  digits(N_R_SEED, N_R_SEED_LEN, 1, (int32_t)(ratePerHa(h.seed_dg / 10.0f, dist, h.profile) * 10.0f + 0.5f));
  digits(N_R_FERT, N_R_FERT_LEN, 1, (int32_t)(ratePerHa(h.fert_dg / 10.0f, dist, h.profile) * 10.0f + 0.5f));
  NetStatus n;
  s_.netStatus(n);
  if (n.wifi == WifiState::Connected) textf(T_R_QR, 60, "http://%s/r/%lu.pdf", n.ip, (unsigned long)h.id);
  else text(T_R_QR, 60, "");
}

void Ui::renderProfiles() {
  for (uint8_t i = 0; i < ROWS; i++) {
    uint8_t s = pf_page_ * ROWS + i;
    if (s >= PROFILE_SLOTS) { state(PF_ON[i], 0); text(PF_T[i], 66, ""); continue; }
    state(PF_ON[i], s == slot_ ? 1 : 0);
    const MachineProfile& p = prof_[s];
    const char* mark = s == pf_sel_ ? ">" : " ";
    if (!p.used) textf(PF_T[i], 66, "%s %02u  (empty)", mark, s + 1);
    else textf(PF_T[i], 66, "%s %02u  %-24s  wheel %u.%03u m  %u.%u km/h", mark, s + 1, p.name, p.wheel_mm / 1000, p.wheel_mm % 1000, p.speed_x10 / 10, p.speed_x10 % 10);
  }
}

void Ui::renderReports() {
  uint16_t total = s_.reportCount();
  row_count_ = s_.reportRows(rp_page_ * ROWS, rows_, ROWS);
  for (uint8_t i = 0; i < ROWS; i++) {
    if (i >= row_count_) { text(RP_T[i], 70, ""); state(RP_UP[i], 0); continue; }
    const ReportRow& r = rows_[i];
    char when[24];
    formatDateTime(r.start_unix, when, sizeof(when));
    textf(RP_T[i], 70, "#%-5lu %-16s %-18.18s %3lu:%02lu %s%s", (unsigned long)r.id, when, r.name, (unsigned long)(r.duration_s / 60),
          (unsigned long)(r.duration_s % 60), r.end == EndReason::Completed ? "OK" : r.end == EndReason::Faulted ? "FAULT" : "STOP", r.sim ? " SIM" : "");
    state(RP_UP[i], r.sync + 1);
  }
  uint16_t pages = (total + ROWS - 1) / ROWS;
  textf(T_RP_PAGE, T_RP_PAGE_LEN, "%u / %u", pages ? rp_page_ + 1 : 0, pages);
  textf(T_RP_MSG, T_RP_MSG_LEN, "%u tests on device", total);
}

void Ui::renderSettings() {
  state(S_REMOTE, t_.remote_allowed);
  state(S_SIM, cfg_.sim_drive);
  digits(N_BRIGHT, N_BRIGHT_LEN, 0, cfg_.brightness);
}

void Ui::renderWifi() {
  NetStatus n;
  s_.netStatus(n);
  ap_count_ = s_.wifiList(aps_, WIFI_MAX_APS);
  state(S_W_STATE, (uint16_t)n.wifi);
  if (n.wifi == WifiState::Connected) textf(T_W_STATUS, T_W_STATUS_LEN, "%s  |  %s", n.ssid, n.ip);
  else if (n.wifi == WifiState::WrongPassword) textf(T_W_STATUS, T_W_STATUS_LEN, "Could not join %s", pending_ssid_);
  else textf(T_W_STATUS, T_W_STATUS_LEN, "%u networks found", ap_count_);
  for (uint8_t i = 0; i < ROWS; i++) {
    uint8_t k = ap_page_ * ROWS + i;
    if (k >= ap_count_) {
      text(WF_T[i], 32, ""); state(WF_SIG[i], 0); state(WF_ON[i], 0); state(WF_L[i], 0);
      continue;
    }
    const WifiAp& a = aps_[k];
    text(WF_T[i], 32, a.ssid);
    state(WF_SIG[i], a.rssi > -55 ? 4 : a.rssi > -65 ? 3 : a.rssi > -75 ? 2 : 1);
    state(WF_ON[i], n.wifi == WifiState::Connected && !strcmp(n.ssid, a.ssid));
    state(WF_L[i], a.secure ? 1 : 0);
  }
  uint16_t pages = (ap_count_ + ROWS - 1) / ROWS;
  textf(T_WF_PAGE, T_WF_PAGE_LEN, "%u / %u", pages ? ap_page_ + 1 : 0, pages);
}

void Ui::renderDrive() {
  const DriveConfig& d = cfg_.drive;
  digits(N_F_POLES_V, 2, 0, d.poles);
  digits(N_F_NRATED_V, 4, 0, d.rated_rpm);
  digits(N_F_FRATED_V, 3, 1, d.rated_hz_x10);
  digits(N_F_SLIP_V, 3, 1, d.slip_x10);
  digits(N_F_GEAR_V, 5, 2, d.gear_x100);
  digits(N_F_RPMMIN_V, 4, 1, d.rpm_min_x10);
  digits(N_F_RPMMAX_V, 4, 1, d.rpm_max_x10);
  digits(N_F_HZMAX_V, 3, 1, d.hz_max_x10);
  digits(N_F_AMAX_V, 3, 1, d.amps_max_x10);
  digits(N_F_DEV_V, 3, 1, d.dev_x10);
  digits(N_F_ACC_V, 4, 1, d.acc_x10);
  digits(N_F_DEC_V, 4, 1, d.dec_x10);
  digits(N_F_PPR_V, 3, 0, d.ppr);
  digits(N_F_VFDID_V, 3, 0, d.vfd_id);
}

void Ui::renderDiag() {
  char b[48];
  for (uint8_t i = 0; i < 32; i++) {
    b[0] = 0;
    s_.diagLine(i, b, sizeof(b));
    text(DG[i], 46, b);
  }
}

void Ui::renderFault() {
  text(T_FT_TITLE, T_FT_TITLE_LEN, faultText(t_.fault));
  if (t_.fault == Fault::DriveFault) text(T_FT_DETAIL, T_FT_DETAIL_LEN, ms300::faultName((uint8_t)t_.vfd_fault));
  else text(T_FT_DETAIL, T_FT_DETAIL_LEN, faultAdvice(t_.fault));
  if (t_.vfd_fault) textf(T_FT_CODE, T_FT_CODE_LEN, "VFD code %u", t_.vfd_fault);
  else if (t_.test_id) textf(T_FT_CODE, T_FT_CODE_LEN, "Test #%lu ended at %lu s", (unsigned long)t_.test_id, (unsigned long)t_.elapsed_s);
  else text(T_FT_CODE, T_FT_CODE_LEN, "");
}

void Ui::render(bool force) {
  if (!s_.telemetry(t_)) return;
  renderHeader();
  switch (page_) {
    case PAGE_HOME: renderHome(); break;
    case PAGE_SETUP: renderSetup(); break;
    case PAGE_TRANS: renderTrans(); break;
    case PAGE_CROP: renderCrop(); break;
    case PAGE_NUMPAD: npRender(); break;
    case PAGE_PRE: renderPre(); break;
    case PAGE_RUN: renderRun(); break;
    case PAGE_DONE: renderReport(); break;
    case PAGE_PROFILES: renderProfiles(); break;
    case PAGE_REPORTS: renderReports(); break;
    case PAGE_SETTINGS: renderSettings(); break;
    case PAGE_WIFI: renderWifi(); break;
    case PAGE_KB_LOW: case PAGE_KB_UP: case PAGE_KB_SYM: kbRender(); break;
    case PAGE_DRIVE: renderDrive(); break;
    case PAGE_FAULT: renderFault(); break;
    case PAGE_DIAG: if (force || now_ - last_slow_ >= 1000) { last_slow_ = now_; renderDiag(); } break;
    default: break;
  }
}

void Ui::tick(uint32_t now_ms) {
  now_ = now_ms;

  // Display presence: poll the current page; the reply also feeds the link watchdog.
  if (now_ - last_poll_ >= (link_seen_ ? 2000u : 500u)) {
    last_poll_ = now_;
    tx_.read(DGUS_PIC_NOW, 1);
  }
  if (link_seen_ && !linkOk(now_)) link_seen_ = false;

  if (!booted_) {
    if (!link_seen_) return;
    uint32_t e = now_ - boot_ms_;
    state(S_BOOT_BAR, e >= 2000 ? 10 : e / 200);
    text(T_BOOT_MSG, T_BOOT_MSG_LEN, e < 1000 ? "Starting controller" : "Checking drive");
    if (e < 2000) return;
    booted_ = true;
    depth_ = 0;
    goTo(PAGE_HOME);
    return;
  }

  if (now_ - last_render_ < 200) return;
  last_render_ = now_;

  uint32_t sync;
  if (s_.rtcPending(sync)) rtcWrite(sync);

  Telemetry t;
  if (!s_.telemetry(t)) return;
  if (t.state != prev_state_) {
    RunState old = prev_state_;
    prev_state_ = t.state;
    t_ = t;
    if (t.state == RunState::Running && old != RunState::Stopping) {
      tx_.curveClear(0);
      depth_ = 0;
      stack_[depth_++] = PAGE_HOME;
      goTo(PAGE_RUN);
    } else if (t.state == RunState::Complete) {
      rep_wait_ = t.test_id;
      rep_valid_ = false;
      rep_from_list_ = false;
    } else if (t.state == RunState::Fault) {
      if (t.test_id && (old == RunState::Running || old == RunState::Stopping)) { rep_wait_ = t.test_id; rep_valid_ = false; rep_from_list_ = false; }
      depth_ = 0;
      stack_[depth_++] = PAGE_HOME;
      goTo(PAGE_FAULT);
    } else if (old == RunState::Fault && t.state == RunState::Ready) {
      depth_ = 0;
      goTo(PAGE_HOME);
    }
  }

  // The store task writes the header a moment after the test ends.
  if (rep_wait_) {
    TestHeader h;
    if (s_.loadReport(rep_wait_, h) && h.end != EndReason::None) {
      rep_wait_ = 0;
      enterReport(h);
      if (t.state == RunState::Complete) { depth_ = 0; goTo(PAGE_DONE); }
    }
  }
  render(false);
}

}  // namespace sd
