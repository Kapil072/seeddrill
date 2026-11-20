// DWIN display link. Core 1, below ctl and vfd: the UI gets whatever time the control
// path leaves, and a burst of screen updates can never delay the drive.
#include <driver/uart.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "app.h"

namespace app {

const Persist& persisted();

namespace {

constexpr uart_port_t PORT = UART_NUM_1;
constexpr uint32_t STACK_WORDS = 6144 / sizeof(StackType_t);
StackType_t stack[STACK_WORDS];
StaticTask_t tcb;

class UartSink : public sd::ByteSink {
 public:
  void write(const uint8_t* p, size_t n) override { uart_write_bytes(PORT, p, n); }
};

class Services : public sd::UiServices {
 public:
  bool telemetry(sd::Telemetry& t) override { return g.telemetry.read(t); }
  void command(const sd::Cmd& c) override { sendCmd(c); }
  void applyMachine(const sd::MachineProfile& p, const sd::AppConfig& a) override {
    static MachineCfg m;
    m.profile = p;
    m.config = a;
    g.machine.write(m);
    sendCmd({sd::CmdType::ApplyConfig, 0, 0});
  }
  void saveProfile(uint8_t slot, const sd::MachineProfile& p) override { storeSaveProfile(slot, p); }
  void saveConfig(const sd::AppConfig& a) override { storeSaveConfig(a); }
  void wifiScan() override { netScan(); }
  uint8_t wifiList(sd::WifiAp* out, uint8_t max) override { return netList(out, max); }
  void wifiConnect(const char* ssid, const char* pass) override { netConnect(ssid, pass); }
  void wifiForget() override { netForget(); }
  void netStatus(sd::NetStatus& s) override { app::netStatus(s); }
  uint16_t reportCount() override { return storeReportCount(); }
  uint8_t reportRows(uint16_t off, sd::ReportRow* out, uint8_t n) override { return storeReportRows(off, out, n); }
  bool loadReport(uint32_t id, sd::TestHeader& h) override { return storeLoadReport(id, h); }
  void saveMeasured(uint32_t id, uint32_t seed, uint32_t fert) override { storeSaveMeasured(id, seed, fert); }
  uint8_t reportSync(uint32_t id) override { return storeSync(id); }
  uint32_t nowUnix() override { return app::nowUnix(); }
  void diagLine(uint8_t i, char* out, size_t len) override { app::diagLine(i, out, len); }
  void rtcRead(uint32_t unix_s) override { setClockFromRtc(unix_s); }
  bool rtcPending(uint32_t& unix_s) override { return takeNtpSync(unix_s); }
};

UartSink sink;
Services services;
sd::Ui ui(sink, services);

void run(void*) {
  esp_task_wdt_add(nullptr);

  uart_config_t c{};
  c.baud_rate = 115200;
  c.data_bits = UART_DATA_8_BITS;
  c.parity = UART_PARITY_DISABLE;
  c.stop_bits = UART_STOP_BITS_1;
  c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  c.source_clk = UART_SCLK_APB;
  // The TX ring means a screen update hands bytes to the ISR and returns at once.
  uart_driver_install(PORT, 512, 2048, 0, nullptr, 0);
  uart_param_config(PORT, &c);
  uart_set_pin(PORT, PIN_HMI_TX, PIN_HMI_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  const Persist& p = persisted();
  ui.begin(p.config, p.profiles, sd::PROFILE_SLOTS);

  TickType_t wake = xTaskGetTickCount();
  uint8_t rx[128];
  bool seen = false;
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
    esp_task_wdt_reset();
    int n = uart_read_bytes(PORT, rx, sizeof(rx), 0);
    if (n > 0) ui.feed(rx, n);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    ui.tick(now);
    bool ok = ui.linkOk(now);
    if (ok) seen = true;
    // Before the panel first answers there is nothing to lose; it may just be booting.
    g.hmi_ok.store(ok || !seen, std::memory_order_relaxed);
  }
}

}  // namespace

TaskHandle_t h_hmi;

void startHmi() {
  h_hmi = xTaskCreateStaticPinnedToCore(run, "hmi", STACK_WORDS, nullptr, 12, stack, &tcb, 1);
}

}  // namespace app
