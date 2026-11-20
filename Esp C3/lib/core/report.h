// Test report output. CSV and PDF are both generated as a stream: the file is
// never held in RAM, so a 10 hour test needs the same memory as a 10 second one.
#pragma once
#include <stddef.h>
#include "sd_types.h"

namespace sd {

class OutStream {
 public:
  virtual ~OutStream() {}
  virtual bool write(const char* p, size_t n) = 0;   // false aborts the report
};

class SampleSource {
 public:
  virtual ~SampleSource() {}
  virtual uint32_t count() = 0;
  virtual uint32_t read(uint32_t index, Sample* dst, uint32_t n) = 0;
};

// Civil time at the workshop (IST), from Unix seconds.
void formatDateTime(uint32_t unix_s, char* out, size_t len);
const char* endText(EndReason e);
const char* faultText(Fault f);

bool writeCsv(const TestHeader& h, SampleSource& src, OutStream& out);
bool writePdf(const TestHeader& h, SampleSource& src, OutStream& out);

}  // namespace sd
