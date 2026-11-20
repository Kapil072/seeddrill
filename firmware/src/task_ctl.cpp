// 100 Hz control loop. Core 1, highest application priority. No heap, no flash, no
// network, nothing that can block: all it touches is GPIO and lock-free structures.
#include <driver/gpio.h>
#include <driver/pcnt.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "app.h"
#include "machine_ctl.h"

namespace app {

namespace {

constexpr uint32_t STACK_WORDS = 4096 / sizeof(StackType_t);
StackType_t stack[STACK_WORDS];
StaticTask_t tcb;

// Ring and queue pushes only: never blocks.
class Events : public sd::CtlEvents {
 public:
  void onTestStart(const sd::TestHeader& h) override { test_ = h.id; push(LogHeader::Start, h); }
  void onTestEnd(const sd::TestHeader& h) override { push(LogHeader::End, h); }
  void onSample(const sd::Sample& s) override { g.samples.push(s); }
  void onFault(sd::Fault f, uint16_t vfd) override {
    g.net_events.push({f, vfd, test_, nowUnix()});
  }

 private:
  uint32_t test_ = 0;
  void push(LogHeader::Kind k, const sd::TestHeader& h) {
    static LogHeader m;   // too big for this task's stack budget; only ctl uses it
    m.kind = k;
    m.h = h;
    g.log_headers.push(m);
  }
};

Events events;
sd::MachineCtl* machine_ptr = nullptr;

// Two-sample debounce on the 10 ms tick
struct Debounce {
  uint8_t stable, last;
  uint8_t update(uint8_t raw) {
    if (raw == last) stable = raw;
    last = raw;
    return stable;
  }
};
#ifndef SD_BENCH
Debounce d_estop, d_guard, d_enable, d_start, d_stop, d_drive;
#endif

inline uint8_t in(int pin) { return gpio_get_level((gpio_num_t)pin); }

void setupInput(int pin) {
  gpio_config_t c{};
  c.pin_bit_mask = 1ULL << pin;
  c.mode = GPIO_MODE_INPUT;
  c.pull_up_en = GPIO_PULLUP_DISABLE;
  c.pull_down_en = GPIO_PULLDOWN_ENABLE;   // open wire reads unsafe
  gpio_config(&c);
}

void setupPulseCounter() {
  pcnt_config_t c{};
  c.pulse_gpio_num = PIN_RPM;
  c.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  c.channel = PCNT_CHANNEL_0;
  c.unit = PCNT_UNIT_0;
  c.pos_mode = PCNT_COUNT_INC;
  c.neg_mode = PCNT_COUNT_DIS;
  c.lctrl_mode = PCNT_MODE_KEEP;
  c.hctrl_mode = PCNT_MODE_KEEP;
  c.counter_h_lim = 32000;
  pcnt_unit_config(&c);
  pcnt_set_filter_value(PCNT_UNIT_0, 1000);   // 12.5 us glitch filter
  pcnt_filter_enable(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_resume(PCNT_UNIT_0);
}

void run(void*) {
  // esp_task_wdt_add(nullptr);  // Watchdog disabled for debugging

  // Create machine controller on heap
  machine_ptr = new sd::MachineCtl(events);

  static const int inputs[] = {PIN_ESTOP_OK, PIN_GUARD_OK, PIN_ENABLE, PIN_START_PB, PIN_STOP_PB, PIN_DRIVE_OK};
  for (int p : inputs) setupInput(p);
  setupPulseCounter();

  MachineCfg cfg;
  while (!g.machine.read(cfg)) {   // hmi publishes the first config at start-up
    // esp_task_wdt_reset();  // Watchdog disabled
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  machine_ptr->configure(cfg.profile, cfg.config.drive, cfg.config.sim_drive);
  machine_ptr->setNextTestId(g.next_test_id.load());

  TickType_t wake = xTaskGetTickCount();
  uint32_t rpm_window_ms = 0;
  uint16_t sensor_rpm_x10 = 0;

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    int64_t t0 = esp_timer_get_time();
    // esp_task_wdt_reset();  // Watchdog disabled

    sd::Cmd c;
    while (xQueueReceive(g_cmds, &c, 0)) {
      if (c.type == sd::CmdType::ApplyConfig) {
        if (g.machine.read(cfg)) machine_ptr->configure(cfg.profile, cfg.config.drive, cfg.config.sim_drive);
      } else {
        machine_ptr->handle(c, nowUnix());
      }
    }

    // RPM from pulses in a 500 ms window; zero when the shaft stops.
    rpm_window_ms += 10;
    if (rpm_window_ms >= 500) {
      int16_t pulses = 0;
      pcnt_get_counter_value(PCNT_UNIT_0, &pulses);
      pcnt_counter_clear(PCNT_UNIT_0);
      uint16_t ppr = cfg.config.drive.ppr;
      sensor_rpm_x10 = ppr ? (uint16_t)((uint32_t)pulses * 1200u / ppr) : 0;
      rpm_window_ms = 0;
    }

    sd::Inputs i{};
#ifdef SD_BENCH
    i.estop_ok = i.guard_ok = i.local_enable = i.drive_relay_ok = 1;
#else
    i.estop_ok = d_estop.update(in(PIN_ESTOP_OK));
    i.guard_ok = d_guard.update(in(PIN_GUARD_OK));
    i.local_enable = d_enable.update(in(PIN_ENABLE));
    i.start_pb = d_start.update(in(PIN_START_PB));
    i.stop_pb = !d_stop.update(in(PIN_STOP_PB));   // NC button: low input means pressed
    i.drive_relay_ok = cfg.config.sim_drive ? 1 : d_drive.update(in(PIN_DRIVE_OK));
#endif
    i.sensor_rpm_x10 = sensor_rpm_x10;

    sd::DriveFeedback fb{};
    g.feedback.read(fb);

    sd::Links links{g.hmi_ok.load(std::memory_order_relaxed) != 0, g.cloud_ok.load(std::memory_order_relaxed) != 0};
    uint32_t now_ms = (uint32_t)(t0 / 1000);
    machine_ptr->tick(now_ms, nowUnix(), i, fb, links);

    g.drive_cmd.write(machine_ptr->driveCommand());
    g.telemetry.write(machine_ptr->telemetry());
    gpio_set_level((gpio_num_t)PIN_RUN_PERMIT, machine_ptr->runPermit());
    gpio_set_level((gpio_num_t)PIN_HORN, machine_ptr->warning());

    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    if (us > g.ctl_max_us.load(std::memory_order_relaxed)) g.ctl_max_us.store(us, std::memory_order_relaxed);
  }
}

}  // namespace

TaskHandle_t h_ctl;

void startCtl() {
  h_ctl = xTaskCreateStaticPinnedToCore(run, "ctl", STACK_WORDS, nullptr, 22, stack, &tcb, 1);
}

}  // namespace app
