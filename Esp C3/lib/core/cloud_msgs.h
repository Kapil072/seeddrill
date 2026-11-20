// MQTT payloads. Formatted into caller-owned buffers: no JSON library, no heap.
// The schema is mirrored by cloud/lambda and checked by the end-to-end test.
#pragma once
#include <stddef.h>
#include "sd_types.h"

namespace sd {

constexpr size_t MSG_ID_LEN = 40;

struct CloudCmd {
  enum Kind : uint8_t { Invalid, Start, Stop, Setpoint, Ping } kind;
  char id[MSG_ID_LEN];
  uint32_t expires;
  uint16_t value;
};

const char* stateName(RunState s);

// Each returns the length written, or 0 if the buffer was too small.
size_t jsonTelemetry(char* out, size_t cap, const Telemetry& t, uint32_t now, uint32_t heap_free);
size_t jsonStatus(char* out, size_t cap, bool online, const char* fw);
size_t jsonEvent(char* out, size_t cap, uint32_t now, Fault f, uint16_t vfd_code, uint32_t test_id);
size_t jsonReport(char* out, size_t cap, const TestHeader& h);
// Writes samples [first, first+n) as compact rows of scaled integers.
size_t jsonSamples(char* out, size_t cap, uint32_t test_id, uint32_t first, const Sample* s, uint32_t n);
size_t jsonAck(char* out, size_t cap, const char* id, bool ok, const char* reason);

// Parses {"cmd":"start","id":"...","exp":123,"value":65}. Unknown input yields Invalid.
bool parseCommand(const char* p, size_t n, CloudCmd& c);

}  // namespace sd
