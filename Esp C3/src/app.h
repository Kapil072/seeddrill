// Shared wiring of the firmware. Everything here is statically allocated.
//
//   Core 1 (real time)              Core 0 (communications)
//   ctl  prio 22  100 Hz            net    prio 6   Wi-Fi, MQTT, uploads
//   vfd  prio 20  Modbus, 20 Hz     store  prio 5   flash: logs, settings
//   hmi  prio 12  display, 50 Hz    httpd  prio 4   CSV and PDF download
//
// Tasks on different cores never share a lock on the control path. Data moves
// through SeqLock (latest value) and SpscRing (ordered stream), both wait-free for
// the writer, so nothing on core 0 can delay the 100 Hz loop.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "report.h"
#include "sd_lockfree.h"
#include "sd_types.h"
#include "ui.h"

namespace app {

constexpr const char* FW_VERSION = "1.0.0";

// ---- pins (ESP32-S3 DevKitC-1). The wiring diagram is generated from this table. ----
constexpr int PIN_HMI_TX = 17, PIN_HMI_RX = 18;     // UART1 to DWIN UART2
constexpr int PIN_RS485_TX = 15, PIN_RS485_RX = 16, PIN_RS485_DE = 7;   // UART2 to MAX3485
constexpr int PIN_ESTOP_OK = 4;     // opto input, high = healthy
constexpr int PIN_GUARD_OK = 5;     // opto input, high = closed
constexpr int PIN_ENABLE = 6;       // opto input, high = key on
constexpr int PIN_START_PB = 8;     // opto input, high = pressed
constexpr int PIN_STOP_PB = 9;      // opto input, high = HEALTHY (NC button, broken wire = stop)
constexpr int PIN_DRIVE_OK = 10;    // opto input, high = VFD fault relay healthy
constexpr int PIN_RPM = 11;         // opto input, proximity sensor pulses
constexpr int PIN_RUN_PERMIT = 12;  // relay driver, high = permit
constexpr int PIN_HORN = 13;        // relay driver, high = horn/beacon

// ---- shared state --------------------------------------------------------------
struct MachineCfg {
  sd::MachineProfile profile;
  sd::AppConfig config;
};

struct LogHeader {
  enum Kind : uint8_t { Start, End } kind;
  sd::TestHeader h;
};

struct NetEvent {
  sd::Fault fault;
  uint16_t vfd;
  uint32_t test;
  uint32_t when;
};

struct Shared {
  sd::SeqLock<sd::Telemetry> telemetry;       // ctl -> everyone
  sd::SeqLock<sd::DriveFeedback> feedback;    // vfd -> ctl
  sd::SeqLock<sd::DriveCommand> drive_cmd;    // ctl -> vfd
  sd::SeqLock<MachineCfg> machine;            // hmi -> ctl, vfd
  sd::SpscRing<sd::Sample, 128> samples;      // ctl -> store, two minutes of slack
  sd::SpscRing<LogHeader, 4> log_headers;     // ctl -> store
  sd::SpscRing<NetEvent, 16> net_events;      // ctl -> net
  std::atomic<uint8_t> hmi_ok{1};
  std::atomic<uint8_t> cloud_ok{0};
  std::atomic<uint32_t> next_test_id{1};
  std::atomic<uint32_t> ctl_max_us{0};        // worst loop time seen
  std::atomic<uint32_t> mb_ok{0}, mb_err{0};
};
extern Shared g;

// Commands reach ctl through one static queue: hmi and net are both producers.
extern QueueHandle_t g_cmds;
inline void sendCmd(const sd::Cmd& c) { xQueueSend(g_cmds, &c, 0); }

// ---- tasks ----------------------------------------------------------------------
void startCtl();
void startVfd();
void startHmi();
void startNet();
void startStore();
void startWeb();

// ---- storage API (store task owns the flash; these are safe from any task) -----------
struct Persist {
  sd::AppConfig config;
  sd::MachineProfile profiles[sd::PROFILE_SLOTS];
};
void storeInit(Persist& out);                      // called once before tasks start
void storeSaveConfig(const sd::AppConfig& a);
void storeSaveProfile(uint8_t slot, const sd::MachineProfile& p);
void storeSaveMeasured(uint32_t id, uint32_t seed_dg, uint32_t fert_dg);
void storeMarkUploaded(uint32_t id);
uint16_t storeReportCount();
uint8_t storeReportRows(uint16_t offset, sd::ReportRow* out, uint8_t n);
bool storeLoadReport(uint32_t id, sd::TestHeader& h);
bool storeNextUpload(sd::TestHeader& h);           // oldest finished test not yet in the cloud
uint8_t storeSync(uint32_t id);

// Reads samples of a stored test. Keeps its own file handle and a one-block cache,
// so report generation uses a fixed few hundred bytes of RAM at any test length.
class TestReader : public sd::SampleSource {
 public:
  ~TestReader() override { close(); }
  bool open(uint32_t id);
  void close();
  uint32_t count() override { return count_; }
  uint32_t read(uint32_t index, sd::Sample* dst, uint32_t n) override;

 private:
  static constexpr uint32_t CACHE = 32;
  int fd_ = -1;
  uint32_t count_ = 0, first_ = 0, valid_ = 0;
  sd::Sample cache_[CACHE];
};

// ---- network API --------------------------------------------------------------------
void netScan();
uint8_t netList(sd::WifiAp* out, uint8_t max);
void netConnect(const char* ssid, const char* pass);
void netForget();
void netStatus(sd::NetStatus& s);
uint32_t nowUnix();
void setClockFromRtc(uint32_t unix_s);
bool takeNtpSync(uint32_t& unix_s);

void diagLine(uint8_t i, char* out, size_t len);
uint32_t stackFree(uint8_t task_index);

}  // namespace app
