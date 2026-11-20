// Local report download. A phone on the same Wi-Fi scans the QR code on the report
// screen and gets the PDF straight from the controller; no internet required.
// The PDF is produced while it is being sent, through one fixed 1 KB buffer.
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "cloud_msgs.h"

namespace app {

extern TaskHandle_t h_ctl, h_vfd, h_hmi, h_net, h_store;

namespace {

// httpd runs handlers one at a time on its single task, so one buffer is enough.
class ChunkOut : public sd::OutStream {
 public:
  explicit ChunkOut(httpd_req_t* r) : r_(r) {}
  bool write(const char* p, size_t n) override {
    while (n) {
      size_t room = sizeof(buf_) - len_;
      size_t c = n < room ? n : room;
      memcpy(buf_ + len_, p, c);
      len_ += c; p += c; n -= c;
      if (len_ == sizeof(buf_) && !flush()) return false;
    }
    return true;
  }
  bool flush() {
    bool ok = !len_ || httpd_resp_send_chunk(r_, buf_, len_) == ESP_OK;
    len_ = 0;
    return ok;
  }

 private:
  httpd_req_t* r_;
  static char buf_[1024];
  size_t len_ = 0;
};
char ChunkOut::buf_[1024];

sd::TestHeader header;
TestReader reader;

esp_err_t sendReport(httpd_req_t* r) {
  // /r/123.pdf or /r/123.csv
  const char* name = r->uri + 3;
  char* dot = nullptr;
  uint32_t id = strtoul(name, &dot, 10);
  bool pdf = dot && !strcmp(dot, ".pdf"), csv = dot && !strcmp(dot, ".csv");
  if (!id || (!pdf && !csv) || !storeLoadReport(id, header) || !reader.open(id)) {
    httpd_resp_send_404(r);
    return ESP_OK;
  }
  char disp[64];
  snprintf(disp, sizeof(disp), "attachment; filename=\"seeddrill-test-%lu.%s\"", (unsigned long)id, pdf ? "pdf" : "csv");
  httpd_resp_set_type(r, pdf ? "application/pdf" : "text/csv");
  httpd_resp_set_hdr(r, "Content-Disposition", disp);

  ChunkOut out(r);
  bool ok = pdf ? sd::writePdf(header, reader, out) : sd::writeCsv(header, reader, out);
  ok = ok && out.flush();
  reader.close();
  httpd_resp_send_chunk(r, nullptr, 0);
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t sendIndex(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/html");
  ChunkOut o(r);
  static const char head[] =
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Seed Drill reports</title>"
      "<style>body{font:16px system-ui;margin:0;background:#0c1311;color:#eef4f0}h1{font-size:20px;padding:16px;margin:0;background:#17221e}"
      "a{color:#9ae34f}li{list-style:none;padding:14px 16px;border-bottom:1px solid #2b3b35}ul{margin:0;padding:0}</style><h1>Test reports</h1><ul>";
  o.write(head, sizeof(head) - 1);
  sd::ReportRow rows[8];
  char line[160], when[24];
  uint16_t total = storeReportCount();
  for (uint16_t off = 0; off < total; off += 8) {
    uint8_t n = storeReportRows(off, rows, 8);
    for (uint8_t i = 0; i < n; i++) {
      sd::formatDateTime(rows[i].start_unix, when, sizeof(when));
      int len = snprintf(line, sizeof(line), "<li>#%lu %s %s &nbsp; <a href=/r/%lu.pdf>PDF</a> <a href=/r/%lu.csv>CSV</a></li>", (unsigned long)rows[i].id,
                         when, sd::endText(rows[i].end), (unsigned long)rows[i].id, (unsigned long)rows[i].id);
      o.write(line, len);
    }
  }
  o.write("</ul>", 5);
  o.flush();
  httpd_resp_send_chunk(r, nullptr, 0);
  return ESP_OK;
}

}  // namespace

void startWeb() {
  static httpd_handle_t server = nullptr;
  if (server) return;
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.core_id = 0;
  c.task_priority = 4;
  c.stack_size = 6144;
  c.max_open_sockets = 3;
  c.max_uri_handlers = 3;
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.lru_purge_enable = true;
  if (httpd_start(&server, &c) != ESP_OK) return;
  httpd_uri_t report{"/r/*", HTTP_GET, sendReport, nullptr};
  httpd_uri_t index{"/", HTTP_GET, sendIndex, nullptr};
  httpd_register_uri_handler(server, &report);
  httpd_register_uri_handler(server, &index);
}

uint32_t stackFree(uint8_t i) {
  TaskHandle_t t[] = {h_ctl, h_vfd, h_hmi, h_net, h_store};
  return i < 5 && t[i] ? uxTaskGetStackHighWaterMark(t[i]) * sizeof(StackType_t) : 0;
}

void diagLine(uint8_t i, char* o, size_t n) {
  static sd::NetStatus s;
  o[0] = 0;
  uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
  switch (i) {
    case 0: snprintf(o, n, "Firmware %s", FW_VERSION); break;
    case 1: snprintf(o, n, "Uptime %02lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60)); break;
    case 2: snprintf(o, n, "Heap free %u KB, lowest %u KB", (unsigned)(esp_get_free_heap_size() >> 10), (unsigned)(esp_get_minimum_free_heap_size() >> 10)); break;
    case 3: snprintf(o, n, "Largest block %u KB", (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >> 10)); break;
    case 4: snprintf(o, n, "Control loop worst %lu us", (unsigned long)g.ctl_max_us.load()); break;
    case 5: snprintf(o, n, "Modbus ok %lu, errors %lu", (unsigned long)g.mb_ok.load(), (unsigned long)g.mb_err.load()); break;
    case 6: snprintf(o, n, "Samples dropped %lu", (unsigned long)g.samples.dropped()); break;
    case 7: snprintf(o, n, "Tests stored %u", storeReportCount()); break;
    case 16: snprintf(o, n, "Stack free: ctl %lu  vfd %lu", (unsigned long)stackFree(0), (unsigned long)stackFree(1)); break;
    case 17: snprintf(o, n, "Stack free: hmi %lu  net %lu", (unsigned long)stackFree(2), (unsigned long)stackFree(3)); break;
    case 18: snprintf(o, n, "Stack free: store %lu", (unsigned long)stackFree(4)); break;
    case 19: netStatus(s); snprintf(o, n, "Wi-Fi %s", s.ssid[0] ? s.ssid : "not connected"); break;
    case 20: netStatus(s); snprintf(o, n, "IP %s", s.ip[0] ? s.ip : "-"); break;
    case 21: netStatus(s); snprintf(o, n, "Cloud %s", s.cloud == 2 ? "online" : s.cloud == 1 ? "connecting" : "off"); break;
    default: break;
  }
}

}  // namespace app
