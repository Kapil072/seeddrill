// Screen logic for the DWIN display. The display holds only pictures and variables;
// every decision is made here, so the whole UI runs unchanged in the host emulator.
// All state is in this object: no heap, one instance, one task.
#pragma once
#include "dgus.h"
#include "kinematics.h"
#include "report.h"
#include "sd_types.h"

namespace sd {

constexpr uint8_t WIFI_MAX_APS = 24;
constexpr uint8_t PASS_MAX = 63;

struct WifiAp {
  char ssid[33];
  int8_t rssi;
  uint8_t secure;
  uint8_t known;
};

enum class WifiState : uint8_t { NotConnected, Connecting, Connected, Scanning, WrongPassword };

struct NetStatus {
  WifiState wifi;
  uint8_t bars;      // 0-4
  uint8_t cloud;     // 0 off, 1 connecting, 2 online
  char ssid[33];
  char ip[16];
};

struct ReportRow {
  uint32_t id, start_unix, duration_s;
  EndReason end;
  uint8_t sync;      // 0 local, 1 uploading, 2 in cloud
  uint8_t sim;
  char name[NAME_LEN];
};

// Everything the UI needs from the rest of the system. Implemented by the firmware
// and by the host emulator. None of these may block for long.
class UiServices {
 public:
  virtual ~UiServices() {}
  virtual bool telemetry(Telemetry& t) = 0;
  virtual void command(const Cmd& c) = 0;
  virtual void applyMachine(const MachineProfile& p, const AppConfig& a) = 0;
  virtual void saveProfile(uint8_t slot, const MachineProfile& p) = 0;
  virtual void saveConfig(const AppConfig& a) = 0;
  virtual void wifiScan() = 0;
  virtual uint8_t wifiList(WifiAp* out, uint8_t max) = 0;
  virtual void wifiConnect(const char* ssid, const char* pass) = 0;   // pass null: use saved
  virtual void wifiForget() = 0;
  virtual void netStatus(NetStatus& s) = 0;
  virtual uint16_t reportCount() = 0;
  virtual uint8_t reportRows(uint16_t offset, ReportRow* out, uint8_t n) = 0;   // newest first
  virtual bool loadReport(uint32_t id, TestHeader& h) = 0;
  virtual void saveMeasured(uint32_t id, uint32_t seed_dg, uint32_t fert_dg) = 0;
  virtual uint8_t reportSync(uint32_t id) = 0;
  virtual uint32_t nowUnix() = 0;
  virtual void diagLine(uint8_t i, char* out, size_t len) = 0;
  // The panel's RTC keeps time across power cycles when there is no Wi-Fi.
  virtual void rtcRead(uint32_t unix_s) = 0;
  virtual bool rtcPending(uint32_t& unix_s) = 0;   // true: write this time to the panel
};

class Ui {
 public:
  Ui(ByteSink& out, UiServices& s);

  // Profiles live here and are mirrored to storage.
  void begin(const AppConfig& a, const MachineProfile* profiles, uint8_t count);
  void feed(const uint8_t* p, size_t n) { rx_.feed(p, n); }
  void tick(uint32_t now_ms);
  bool linkOk(uint32_t now_ms) const { return now_ms - last_rx_ms_ < LINK_TIMEOUT_MS; }
  uint16_t page() const { return page_; }

  static constexpr uint32_t LINK_TIMEOUT_MS = 6000;

 private:
  enum class Kb : uint8_t { Password, ProfileName };
  enum class Ask : uint8_t { None, ForgetWifi, DeleteProfile, OverwriteProfile };

  static void onVp(void* self, uint16_t vp, const uint16_t* w, uint8_t n);
  void handleVp(uint16_t vp, const uint16_t* w, uint8_t n);
  void onKey(uint16_t code);
  void onChar(char c);
  void goTo(uint16_t page);
  void push(uint16_t page);
  void pop();
  void render(bool force);
  void renderHeader();
  void renderHome();
  void renderSetup();
  void renderTrans();
  void renderCrop();
  void renderPre();
  void renderRun();
  void renderReport();
  void renderProfiles();
  void renderReports();
  void renderSettings();
  void renderWifi();
  void renderDrive();
  void renderDiag();
  void renderFault();
  void enterReport(const TestHeader& h);
  void changed();
  bool startEdit(uint16_t key);
  int32_t fieldGet(uint8_t f);
  void fieldSet(uint8_t f, int32_t v);
  void npCommit();
  void npRender();
  void kbOpen(Kb mode, const char* title, const char* initial, uint8_t max);
  void kbRender();
  void kbCommit();
  void confirm(Ask what, const char* title, const char* m1, const char* m2);
  void setDrive(bool sim);
  void rtcReply(const uint16_t* w);
  void rtcWrite(uint32_t unix_s);

  // Writes through a shadow copy of display memory: only changed words go on the wire.
  void put(uint16_t vp, const uint16_t* w, uint8_t n);
  void state(uint16_t vp, uint16_t v) { put(vp, &v, 1); }
  void digits(uint16_t vp, uint8_t len, uint8_t dec, int32_t scaled);
  void dashes(uint16_t vp, uint8_t len);
  void clock(uint16_t vp, uint32_t seconds);
  void text(uint16_t vp, uint8_t maxlen, const char* s);
  void textf(uint16_t vp, uint8_t maxlen, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

  DgusTx tx_;
  DgusRx rx_;
  UiServices& s_;

  static constexpr uint16_t SHADOW_BASE = 0x5000;
  static constexpr uint16_t SHADOW_WORDS = 0x0A00;
  uint16_t shadow_[SHADOW_WORDS];

  uint16_t page_ = 0;
  uint16_t stack_[8];
  uint8_t depth_ = 0;
  uint32_t now_ = 0, last_render_ = 0, last_slow_ = 0, last_poll_ = 0, last_rx_ms_ = 0, boot_ms_ = 0, last_curve_ = 0;
  bool booted_ = false, link_seen_ = false;

  AppConfig cfg_{};
  MachineProfile prof_[PROFILE_SLOTS];
  MachineProfile cur_{};
  uint8_t slot_ = 0;
  Kinematics kin_{};
  Limit limit_ = Limit::BadInput;
  Telemetry t_{};
  RunState prev_state_ = RunState::Ready;

  // Numeric entry
  uint8_t np_field_ = 0;
  char np_buf_[24] = {0};
  uint8_t np_len_ = 0;
  bool np_fresh_ = true;
  char np_err_[40] = {0};
  bool np_pin_ = false;

  // Keyboard
  Kb kb_mode_ = Kb::Password;
  char kb_buf_[PASS_MAX + 1] = {0};
  uint8_t kb_len_ = 0, kb_max_ = 0;
  bool kb_show_ = false;
  char kb_title_[20] = {0};
  uint16_t kb_page_ = 0;

  // Wi-Fi
  WifiAp aps_[WIFI_MAX_APS];
  uint8_t ap_count_ = 0, ap_page_ = 0;
  char pending_ssid_[33] = {0};

  // Lists
  uint8_t pf_page_ = 0, pf_sel_ = 0;
  uint16_t rp_page_ = 0;
  ReportRow rows_[6];
  uint8_t row_count_ = 0;

  // Current report
  TestHeader rep_{};
  bool rep_valid_ = false, rep_from_list_ = false;
  uint32_t rep_wait_ = 0;

  Ask ask_ = Ask::None;
};

}  // namespace sd
