// Modbus master for the MS300, or the drive simulator. Core 1.
// One cycle every 50 ms: write control word + frequency in a single frame, then read
// the status block. Writing every cycle also feeds the drive's own comms watchdog
// (Pr.09-03), so a hung controller stops the motor without any help from this code.
#include <driver/uart.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "app.h"
#include "drive_sim.h"
#include "modbus.h"

namespace app {

namespace {

constexpr uart_port_t PORT = UART_NUM_2;
constexpr uint32_t BAUD = 19200;
constexpr uint32_t CYCLE_MS = 50;
constexpr uint32_t REPLY_TIMEOUT_MS = 40;
constexpr uint8_t MAX_FAILS = 3;

constexpr uint32_t STACK_WORDS = 3072 / sizeof(StackType_t);
StackType_t stack[STACK_WORDS];
StaticTask_t tcb;

sd::DriveSim sim;

void setupUart() {
  uart_config_t c{};
  c.baud_rate = BAUD;
  c.data_bits = UART_DATA_8_BITS;
  c.parity = UART_PARITY_EVEN;     // Pr.09-04 = 14: 8E1 RTU
  c.stop_bits = UART_STOP_BITS_1;
  c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  // UART_SCLK_APB was renamed to UART_SCLK_DEFAULT in IDF v5; support both.
#if defined(UART_SCLK_DEFAULT)
  c.source_clk = UART_SCLK_DEFAULT;
#else
  c.source_clk = UART_SCLK_APB;
#endif
  uart_driver_install(PORT, 256, 0, 0, nullptr, 0);
  uart_param_config(PORT, &c);
  // DE follows transmission in hardware: no timing code, no bus contention.
  uart_set_pin(PORT, PIN_RS485_TX, PIN_RS485_RX, PIN_RS485_DE, UART_PIN_NO_CHANGE);
  uart_set_mode(PORT, UART_MODE_RS485_HALF_DUPLEX);
}

// Reads until the expected length or timeout. Returns bytes received.
size_t receive(uint8_t* f, size_t want) {
  size_t got = 0;
  uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
  while (got < want) {
    int n = uart_read_bytes(PORT, f + got, want - got, pdMS_TO_TICKS(5));
    if (n > 0) {
      got += n;
      // An exception reply is only 5 bytes
      if (got >= 5 && (f[1] & 0x80)) break;
    }
    if ((uint32_t)(esp_timer_get_time() / 1000) - t0 > REPLY_TIMEOUT_MS) break;
  }
  return got;
}

bool transact(const sd::DriveCommand& c, uint8_t id, sd::DriveFeedback& fb) {
  uint8_t f[48];
  uart_flush_input(PORT);
  if (c.reset) {
    size_t n = sd::mbWriteSingle(f, id, sd::ms300::REG_FAULT_CTRL, sd::ms300::FAULT_RESET);
    uart_write_bytes(PORT, f, n);
    receive(f, 8);
    uart_flush_input(PORT);
  }

  uint16_t w[2] = {c.run ? sd::ms300::CTRL_RUN_FWD : sd::ms300::CTRL_STOP, c.hz_x100};
  size_t n = sd::mbWriteMultiple(f, id, sd::ms300::REG_CONTROL, w, 2);
  uart_write_bytes(PORT, f, n);
  size_t got = receive(f, 8);
  if (sd::mbParseWrite(f, got, id, 0x10) != sd::MbResult::Ok) return false;

  n = sd::mbReadHolding(f, id, sd::ms300::REG_STATUS_BASE, sd::ms300::STATUS_WORDS);
  uart_write_bytes(PORT, f, n);
  got = receive(f, sd::mbExpectedRead(sd::ms300::STATUS_WORDS));
  uint16_t regs[sd::ms300::STATUS_WORDS];
  if (sd::mbParseRead(f, got, id, regs, sd::ms300::STATUS_WORDS) != sd::MbResult::Ok) return false;
  sd::ms300Decode(regs, fb);
  return true;
}

void run(void*) {
  // esp_task_wdt_add(nullptr);  // Watchdog disabled for debugging
  setupUart();

  TickType_t wake = xTaskGetTickCount();
  sd::DriveFeedback fb{};
  uint8_t fails = MAX_FAILS;
  uint32_t last_good_ms = 0;
  MachineCfg cfg{};

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CYCLE_MS));
    // esp_task_wdt_reset();  // Watchdog disabled
    g.machine.read(cfg);

    sd::DriveCommand c{};
    g.drive_cmd.read(c);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (cfg.config.sim_drive) {
      sim.step(CYCLE_MS, c, fb);
      last_good_ms = now;
      fails = 0;
    } else if (transact(c, cfg.config.drive.vfd_id, fb)) {
      g.mb_ok.fetch_add(1, std::memory_order_relaxed);
      last_good_ms = now;
      fails = 0;
      fb.online = 1;
    } else {
      g.mb_err.fetch_add(1, std::memory_order_relaxed);
      if (fails < MAX_FAILS) fails++;
      if (fails >= MAX_FAILS) fb.online = 0;
    }
    fb.age_ms = now - last_good_ms;
    g.feedback.write(fb);
  }
}

}  // namespace

TaskHandle_t h_vfd;

void startVfd() {
  h_vfd = xTaskCreateStaticPinnedToCore(run, "vfd", STACK_WORDS, nullptr, 20, stack, &tcb, 1);
}

}  // namespace app
