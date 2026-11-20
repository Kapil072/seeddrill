#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "app.h"

namespace app {

Shared g;
QueueHandle_t g_cmds;
static StaticQueue_t cmd_q_ctrl;
static uint8_t cmd_q_store[16 * sizeof(sd::Cmd)];

static Persist persist;
const Persist& persisted() { return persist; }

}  // namespace app

void setup() {
  using namespace app;
  Serial.begin(115200);

  // A reset with the relay outputs floating must read as "no permit": pins are driven
  // low before anything else happens, and the board has pull-downs for the reset itself.
  pinMode(PIN_RUN_PERMIT, OUTPUT);
  digitalWrite(PIN_RUN_PERMIT, LOW);
  pinMode(PIN_HORN, OUTPUT);
  digitalWrite(PIN_HORN, LOW);

  g_cmds = xQueueCreateStatic(16, sizeof(sd::Cmd), cmd_q_store, &cmd_q_ctrl);

  storeInit(persist);

  // 3 s watchdog on the real-time tasks. A hang resets the chip; the permit relay
  // drops and the VFD stops on its own communication timeout (Pr.09-03).
  esp_task_wdt_init(3, true);

  startStore();
  startVfd();
  startCtl();
  startHmi();
  startNet();

  Serial.printf("SeedDrill %s, reset reason %d, free heap %u\n", FW_VERSION, (int)esp_reset_reason(), (unsigned)esp_get_free_heap_size());

  // The Arduino loop task has nothing to do; returning its 8 KB stack to the heap
  // leaves more room for TLS.
  vTaskDelete(nullptr);
}

void loop() {}
