// Flash owner. Core 0. A flash write suspends the instruction cache on BOTH cores, so
// the rule here is: as few writes as possible, and never on behalf of the control loop
// directly. Samples collect in RAM and reach flash in 1 KB bursts, about once a minute.
#include <Arduino.h>
#include <LittleFS.h>
#include <dirent.h>
#include <fcntl.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"

namespace app {

namespace {

constexpr const char* DIR_PATH = "/logs/t";
constexpr uint16_t MAX_TESTS = 160;
constexpr size_t LOG_FREE_MARGIN = 256 * 1024;
constexpr uint32_t FLUSH_SAMPLES = 64;

constexpr uint32_t STACK_WORDS = 4096 / sizeof(StackType_t);
StackType_t stack[STACK_WORDS];
StaticTask_t tcb;

fs::LittleFSFS logs_fs;

// RAM index of every test on flash, so the UI list never touches the file system.
struct Entry {
  uint32_t id, start, duration;
  sd::EndReason end;
  uint8_t sim, uploaded;
  char name[sd::NAME_LEN];
};
Entry idx[MAX_TESTS];
uint16_t idx_count = 0;
SemaphoreHandle_t idx_lock;
StaticSemaphore_t idx_lock_mem;

struct Guard {
  Guard() { xSemaphoreTake(idx_lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(idx_lock); }
};

// Requests from other tasks. Fixed-size, in a static queue.
struct Req {
  enum Kind : uint8_t { Config, Profile, Measured, Uploaded } kind;
  uint8_t slot;
  uint32_t id, a, b;
  union {
    sd::AppConfig config;
    sd::MachineProfile profile;
  };
};
QueueHandle_t reqs;
StaticQueue_t reqs_ctrl;
uint8_t reqs_store[8 * sizeof(Req)];

void pathOf(uint32_t id, char* out, size_t n) { snprintf(out, n, "%s/%06lu.bin", DIR_PATH, (unsigned long)id); }

void nvsPut(const char* key, const void* p, size_t n) {
  nvs_handle_t h;
  if (nvs_open("sd", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, key, p, n);
  nvs_commit(h);
  nvs_close(h);
}

bool nvsGet(const char* key, void* p, size_t n) {
  nvs_handle_t h;
  if (nvs_open("sd", NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = n;
  bool ok = nvs_get_blob(h, key, p, &len) == ESP_OK && len == n;
  nvs_close(h);
  return ok;
}

bool readHeader(const char* path, sd::TestHeader& h) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  ssize_t n = read(fd, &h, sizeof(h));
  close(fd);
  return n == (ssize_t)sizeof(h) && h.magic == sd::TEST_MAGIC;
}

bool writeHeader(uint32_t id, const sd::TestHeader& h) {
  char path[32];
  pathOf(id, path, sizeof(path));
  int fd = open(path, O_WRONLY);
  if (fd < 0) return false;
  bool ok = write(fd, &h, sizeof(h)) == (ssize_t)sizeof(h);
  close(fd);
  return ok;
}

void indexAdd(const sd::TestHeader& h) {
  Guard l;
  for (uint16_t i = 0; i < idx_count; i++) {
    if (idx[i].id == h.id) {
      idx[i].duration = h.duration_s;
      idx[i].end = h.end;
      idx[i].uploaded = h.uploaded;
      return;
    }
  }
  if (idx_count == MAX_TESTS) {
    memmove(idx, idx + 1, sizeof(Entry) * (MAX_TESTS - 1));
    idx_count--;
  }
  Entry& e = idx[idx_count++];
  e.id = h.id; e.start = h.start_unix; e.duration = h.duration_s; e.end = h.end;
  e.sim = h.sim; e.uploaded = h.uploaded;
  strncpy(e.name, h.profile.name, sd::NAME_LEN - 1);
  e.name[sd::NAME_LEN - 1] = 0;
}

// Keeps space for the next test: deletes the oldest records, uploaded ones first.
void pruneIfNeeded() {
  while (logs_fs.totalBytes() - logs_fs.usedBytes() < LOG_FREE_MARGIN && idx_count > 1) {
    int victim = -1;
    for (uint16_t i = 0; i < idx_count && victim < 0; i++) if (idx[i].uploaded) victim = i;
    if (victim < 0) victim = 0;
    char path[32];
    pathOf(idx[victim].id, path, sizeof(path));
    unlink(path);
    Guard l;
    memmove(idx + victim, idx + victim + 1, sizeof(Entry) * (idx_count - victim - 1));
    idx_count--;
  }
}

// Boot scan: rebuild the index, and close out any test a power cut left open.
void scan() {
  mkdir(DIR_PATH, 0775);
  DIR* d = opendir(DIR_PATH);
  if (!d) return;
  uint32_t top = 0;
  static sd::TestHeader h;
  static uint32_t ids[MAX_TESTS];
  uint16_t n = 0;
  while (struct dirent* e = readdir(d)) {
    uint32_t id = strtoul(e->d_name, nullptr, 10);
    if (!id) continue;
    if (id > top) top = id;
    if (n < MAX_TESTS) ids[n++] = id;
  }
  closedir(d);
  // Sorted oldest first; a small insertion sort over a bounded list.
  for (uint16_t i = 1; i < n; i++) {
    uint32_t v = ids[i];
    int j = i - 1;
    while (j >= 0 && ids[j] > v) { ids[j + 1] = ids[j]; j--; }
    ids[j + 1] = v;
  }
  for (uint16_t i = 0; i < n; i++) {
    char path[32];
    pathOf(ids[i], path, sizeof(path));
    if (!readHeader(path, h)) continue;
    if (h.end == sd::EndReason::None) {
      struct stat st;
      if (!stat(path, &st)) h.samples = (st.st_size - sizeof(h)) / sizeof(sd::Sample);
      h.duration_s = h.samples;
      h.end = sd::EndReason::Interrupted;
      writeHeader(h.id, h);
    }
    indexAdd(h);
  }
  g.next_test_id.store(top + 1);
}

// ---- log writer -------------------------------------------------------------
int log_fd = -1;
uint32_t log_id = 0, log_written = 0, log_expected = 0;
bool log_ending = false;
sd::Sample wbuf[FLUSH_SAMPLES];
uint32_t wcount = 0;
sd::TestHeader end_header;

void flushSamples() {
  if (log_fd >= 0 && wcount) {
    write(log_fd, wbuf, wcount * sizeof(sd::Sample));
    fsync(log_fd);   // survives a power cut with at most one batch lost
  }
  wcount = 0;
}

void logOpen(const sd::TestHeader& h) {
  pruneIfNeeded();
  char path[32];
  pathOf(h.id, path, sizeof(path));
  log_fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0664);
  log_id = h.id;
  log_written = 0;
  log_ending = false;
  wcount = 0;
  if (log_fd >= 0) {
    write(log_fd, &h, sizeof(h));
    fsync(log_fd);
  }
  indexAdd(h);
}

void logClose() {
  flushSamples();
  if (log_fd >= 0) { close(log_fd); log_fd = -1; }
  writeHeader(end_header.id, end_header);
  indexAdd(end_header);
  log_ending = false;
}

void handleReq(const Req& r) {
  static sd::TestHeader h;
  char path[32];
  switch (r.kind) {
    case Req::Config:
      nvsPut("cfg", &r.config, sizeof(r.config));
      break;
    case Req::Profile: {
      char key[8];
      snprintf(key, sizeof(key), "p%u", r.slot);
      nvsPut(key, &r.profile, sizeof(r.profile));
      break;
    }
    case Req::Measured:
    case Req::Uploaded:
      pathOf(r.id, path, sizeof(path));
      if (!readHeader(path, h)) break;
      if (r.kind == Req::Measured) {
        h.seed_dg = r.a;
        h.fert_dg = r.b;
        h.uploaded = 0;   // the cloud copy is now out of date, so send it again
      } else {
        h.uploaded = 1;
      }
      writeHeader(r.id, h);
      indexAdd(h);
      break;
  }
}

void run(void*) {
  static LogHeader lh;
  static Req r;
  uint32_t idle = 0;
  for (;;) {
    bool busy = false;
    if (g.log_headers.pop(lh)) {
      busy = true;
      if (lh.kind == LogHeader::Start) logOpen(lh.h);
      else {
        end_header = lh.h;
        log_expected = lh.h.samples;
        log_ending = true;
      }
    }
    sd::Sample s;
    // Stops at the end of this test so a following test's samples are not absorbed.
    while ((!log_ending || log_written < log_expected) && g.samples.pop(s)) {
      busy = true;
      wbuf[wcount++] = s;
      log_written++;
      if (wcount == FLUSH_SAMPLES) flushSamples();
    }
    if (log_ending && log_written >= log_expected) logClose();

    if (xQueueReceive(reqs, &r, pdMS_TO_TICKS(busy ? 0 : 100))) handleReq(r);
    idle = busy ? 0 : idle + 1;
  }
}

}  // namespace

void storeInit(Persist& out) {
  idx_lock = xSemaphoreCreateMutexStatic(&idx_lock_mem);
  reqs = xQueueCreateStatic(8, sizeof(Req), reqs_store, &reqs_ctrl);

  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  memset(&out, 0, sizeof(out));
  if (!nvsGet("cfg", &out.config, sizeof(out.config)) || out.config.magic != 0x53444346) {
    out.config.magic = 0x53444346;
    sd::defaultDrive(out.config.drive);
    out.config.brightness = 80;
#ifdef SD_BENCH
    out.config.sim_drive = 1;
#endif
  }
  for (uint8_t i = 0; i < sd::PROFILE_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%u", i);
    nvsGet(key, &out.profiles[i], sizeof(out.profiles[i]));
  }
  if (!out.profiles[0].used) sd::defaultProfile(out.profiles[0]);

  // Mount logs partition - if it fails, continue anyway (just won't save test logs)
  if (!logs_fs.begin(true, "/logs", 4, "logs")) {
    // Format failed or partition corrupt - continue without logging
    Serial.println("[WARN] Failed to mount /logs partition - test logging disabled");
  } else {
    scan();
  }
}

void storeSaveConfig(const sd::AppConfig& a) {
  static Req r;
  r.kind = Req::Config;
  r.config = a;
  xQueueSend(reqs, &r, 0);
}

void storeSaveProfile(uint8_t slot, const sd::MachineProfile& p) {
  static Req r;
  r.kind = Req::Profile;
  r.slot = slot;
  r.profile = p;
  xQueueSend(reqs, &r, 0);
}

void storeSaveMeasured(uint32_t id, uint32_t seed, uint32_t fert) {
  static Req r;
  r.kind = Req::Measured;
  r.id = id; r.a = seed; r.b = fert;
  xQueueSend(reqs, &r, 0);
}

void storeMarkUploaded(uint32_t id) {
  static Req r;
  r.kind = Req::Uploaded;
  r.id = id;
  xQueueSend(reqs, &r, 0);
}

uint16_t storeReportCount() {
  Guard l;
  return idx_count;
}

uint8_t storeReportRows(uint16_t offset, sd::ReportRow* out, uint8_t n) {
  Guard l;
  uint8_t k = 0;
  for (int i = (int)idx_count - 1 - offset; i >= 0 && k < n; i--, k++) {
    const Entry& e = idx[i];
    sd::ReportRow& r = out[k];
    r.id = e.id; r.start_unix = e.start; r.duration_s = e.duration; r.end = e.end;
    r.sync = e.uploaded ? 2 : 0;
    r.sim = e.sim;
    memcpy(r.name, e.name, sd::NAME_LEN);
  }
  return k;
}

bool storeLoadReport(uint32_t id, sd::TestHeader& h) {
  char path[32];
  pathOf(id, path, sizeof(path));
  return readHeader(path, h);
}

bool storeNextUpload(sd::TestHeader& h) {
  uint32_t id = 0;
  {
    Guard l;
    for (uint16_t i = 0; i < idx_count; i++) {
      if (!idx[i].uploaded && idx[i].end != sd::EndReason::None) { id = idx[i].id; break; }
    }
  }
  return id && storeLoadReport(id, h);
}

uint8_t storeSync(uint32_t id) {
  Guard l;
  for (uint16_t i = 0; i < idx_count; i++) if (idx[i].id == id) return idx[i].uploaded ? 2 : 0;
  return 0;
}

bool TestReader::open(uint32_t id) {
  close();
  char path[32];
  pathOf(id, path, sizeof(path));
  fd_ = ::open(path, O_RDONLY);
  if (fd_ < 0) return false;
  struct stat st;
  if (fstat(fd_, &st)) { close(); return false; }
  count_ = st.st_size > (off_t)sizeof(sd::TestHeader) ? (st.st_size - sizeof(sd::TestHeader)) / sizeof(sd::Sample) : 0;
  valid_ = 0;
  return true;
}

void TestReader::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  count_ = valid_ = 0;
}

uint32_t TestReader::read(uint32_t index, sd::Sample* dst, uint32_t n) {
  uint32_t done = 0;
  while (done < n && index < count_) {
    if (index < first_ || index >= first_ + valid_) {
      first_ = index / CACHE * CACHE;
      lseek(fd_, sizeof(sd::TestHeader) + first_ * sizeof(sd::Sample), SEEK_SET);
      ssize_t got = ::read(fd_, cache_, sizeof(cache_));
      valid_ = got > 0 ? got / sizeof(sd::Sample) : 0;
      if (!valid_) break;
    }
    dst[done++] = cache_[index - first_];
    index++;
  }
  return done;
}

TaskHandle_t h_store;

void startStore() {
  h_store = xTaskCreateStaticPinnedToCore(run, "store", STACK_WORDS, nullptr, 5, stack, &tcb, 0);
}

}  // namespace app
