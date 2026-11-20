// Local report download and real-time dashboard.
// The dashboard (React/Vite) is served from LittleFS as pre-gzipped flat files.
// All API endpoints return JSON with CORS headers so the GitHub Pages build can
// also reach the device — but mixed-content rules prevent that from a browser;
// the preferred path is opening http://<device-ip>/app/ directly.
#include <LittleFS.h>
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

// ---- CORS helper -------------------------------------------------------------------
static void addCors(httpd_req_t* r) {
  httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(r, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(r, "Access-Control-Allow-Headers", "Content-Type");
}

// ---- OPTIONS pre-flight ------------------------------------------------------------
static esp_err_t handleOptions(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_status(r, "204 No Content");
  httpd_resp_send(r, nullptr, 0);
  return ESP_OK;
}

// ---- GET /api/status ---------------------------------------------------------------
// Returns live telemetry from the SeqLock as JSON.
// Field names match the existing cloud schema used by Live.jsx.
static esp_err_t apiStatus(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_type(r, "application/json");

  sd::Telemetry t{};
  g.telemetry.read(t);
  sd::NetStatus ns{};
  netStatus(ns);

  // RunState: Ready=0 Checking=1 Running=2 Stopping=3 Complete=4 Fault=5
  static const char* st_names[] = {"ready", "checking", "running", "stopping", "complete", "fault"};
  uint8_t si = (uint8_t)t.state;
  const char* st_str = (si < 6) ? st_names[si] : "idle";

  char buf[512];
  int n = snprintf(buf, sizeof(buf),
    "{"
    "\"online\":true,"
    "\"fw\":\"%s\","
    "\"telemetry\":{"
      "\"ts\":%lu,"
      "\"st\":\"%s\","
      "\"rpm\":%u,"
      "\"rpmT\":%u,"
      "\"kmh\":%u,"
      "\"dist\":%lu,"
      "\"a\":%u,"
      "\"hz\":%u,"
      "\"prog\":%u,"
      "\"el\":%lu,"
      "\"left\":%lu,"
      "\"est\":%u,"
      "\"grd\":%u,"
      "\"en\":%u,"
      "\"drv\":%u,"
      "\"rem\":%u,"
      "\"meas\":%u,"
      "\"sim\":%u,"
      "\"fault\":%u"
    "}"
    "}",
    FW_VERSION,
    (unsigned long)(esp_timer_get_time() / 1000000),
    st_str,
    t.rpm_x10, t.rpm_target_x10,
    t.v_sim_x10, (unsigned long)t.dist_dm,
    t.amps_x100, t.hz_cmd_x100,
    t.progress,
    (unsigned long)t.elapsed_s, (unsigned long)t.remaining_s,
    t.estop_ok, t.guard_ok, t.local_enable, t.drive_online,
    t.remote_allowed, t.measured, t.sim_drive,
    (unsigned)t.fault
  );
  httpd_resp_send(r, buf, n);
  return ESP_OK;
}

// ---- GET /api/reports --------------------------------------------------------------
// Returns the report index as JSON for the Reports tab in the dashboard.
static esp_err_t apiReports(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_type(r, "application/json");

  ChunkOut o(r);
  o.write("{\"tests\":[", 10);
  sd::ReportRow rows[8];
  char line[256];
  uint16_t total = storeReportCount();
  bool first = true;
  static const char* end_names[] = {"None","Completed","OperatorStop","RemoteStop","RemoteLinkLost","Faulted","Interrupted"};
  for (uint16_t off = 0; off < total; off += 8) {
    uint8_t n = storeReportRows(off, rows, 8);
    for (uint8_t i = 0; i < n; i++) {
      uint8_t ei = (uint8_t)rows[i].end;
      const char* end_str = (ei < 7) ? end_names[ei] : "Interrupted";
      // Map EndReason to a UI-friendly string matching what Live.jsx expects
      const char* end_ui = (ei == 1) ? "Completed" :
                           (ei == 2 || ei == 3 || ei == 4) ? "Stopped" : "Faulted";
      int len = snprintf(line, sizeof(line),
        "%s{\"id\":%lu,\"start\":%lu,\"end\":\"%s\",\"dur\":%lu,"
        "\"rpmAvg\":0,\"rpmT\":0,\"acc\":0,\"dist\":0,"
        "\"aPeak\":0,\"sim\":%u,"
        "\"profile\":{\"name\":\"%s\"}}",
        first ? "" : ",",
        (unsigned long)rows[i].id,
        (unsigned long)rows[i].start_unix,
        end_ui,
        (unsigned long)rows[i].duration_s,
        rows[i].sim,
        rows[i].name
      );
      (void)end_str;
      o.write(line, len);
      first = false;
    }
  }
  o.write("],\"cursor\":null}", 16);
  o.flush();
  httpd_resp_send_chunk(r, nullptr, 0);
  return ESP_OK;
}

// ---- GET /api/tests/:id ------------------------------------------------------------
// Returns one full test with all samples for TestDetail.jsx
static esp_err_t apiTestDetail(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_type(r, "application/json");

  const char* path = r->uri + 11;   // skip "/api/tests/"
  uint32_t id = (uint32_t)strtoul(path, nullptr, 10);

  sd::TestHeader h{};
  if (!id || !storeLoadReport(id, h)) {
    httpd_resp_send_404(r);
    return ESP_OK;
  }

  TestReader rd;
  if (!rd.open(id)) {
    httpd_resp_send_404(r);
    return ESP_OK;
  }

  // Map EndReason to UI string
  uint8_t ei = (uint8_t)h.end;
  const char* end_ui = (ei == 1) ? "Completed" :
                       (ei == 2 || ei == 3 || ei == 4) ? "Stopped" : "Faulted";

  ChunkOut o(r);
  char hdr[512];
  int n = snprintf(hdr, sizeof(hdr),
    "{\"test\":{"
      "\"id\":%lu,\"start\":%lu,\"end\":\"%s\",\"dur\":%lu,"
      "\"rpmAvg\":%u,\"rpmT\":%u,\"acc\":%u,\"dist\":%lu,"
      "\"aPeak\":%u,\"sim\":%u,"
      "\"profile\":{\"name\":\"%s\",\"rows\":%u,\"rowSp\":%u}"
    "},\"samples\":[",
    (unsigned long)h.id,
    (unsigned long)h.start_unix, end_ui,
    (unsigned long)h.duration_s,
    h.rpm_avg_x10, h.rpm_target_x10, h.accuracy_x10,
    (unsigned long)h.dist_dm,
    h.amps_peak_x100, h.sim,
    h.profile.name, h.profile.rows, h.profile.row_sp_x10
  );
  o.write(hdr, n);

  // Samples: [ts_s, rpm_target_x10, rpm_x10, v_sim_x10, amps_x100]
  sd::Sample s[16];
  uint32_t sidx = 0, cnt = rd.count();
  bool sf = true;
  while (sidx < cnt) {
    uint32_t got = rd.read(sidx, s, 16);
    for (uint32_t i = 0; i < got; i++) {
      char row[64];
      int rn = snprintf(row, sizeof(row), "%s[%u,%u,%u,%u,%u]",
        sf ? "" : ",",
        s[i].t_s,
        s[i].rpm_target_x10, s[i].rpm_x10, s[i].v_sim_x10, s[i].amps_x100
      );
      o.write(row, rn);
      sf = false;
    }
    sidx += got;
    if (!got) break;
  }
  rd.close();
  o.write("]}", 2);
  o.flush();
  httpd_resp_send_chunk(r, nullptr, 0);
  return ESP_OK;
}

// ---- POST /api/command -------------------------------------------------------------
// Accepts: {"cmd":"start"}, {"cmd":"stop"}, {"cmd":"setpoint","value":NNN}
static esp_err_t apiCommand(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_type(r, "application/json");

  char buf[128];
  int len = r->content_len < (int)sizeof(buf) - 1 ? r->content_len : (int)sizeof(buf) - 1;
  if (len <= 0 || httpd_req_recv(r, buf, len) <= 0) {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad body");
    return ESP_OK;
  }
  buf[len] = '\0';

  sd::Cmd cmd{};
  if (strstr(buf, "\"start\""))        cmd.type = sd::CmdType::RemoteStart;
  else if (strstr(buf, "\"stop\""))    cmd.type = sd::CmdType::RemoteStop;
  else if (strstr(buf, "\"setpoint\"")) {
    cmd.type = sd::CmdType::RemoteSetpoint;
    const char* vp = strstr(buf, "\"value\"");
    if (vp) {
      vp = strchr(vp, ':');
      if (vp) cmd.arg = (uint16_t)strtoul(vp + 1, nullptr, 10);
    }
  } else {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Unknown cmd");
    return ESP_OK;
  }

  sendCmd(cmd);
  httpd_resp_send(r, "{\"ok\":true}", 11);
  return ESP_OK;
}

// ---- GET /api/events ---------------------------------------------------------------
// Exposes fault-ending tests as fault events for the Events tab.
static esp_err_t apiEvents(httpd_req_t* r) {
  addCors(r);
  httpd_resp_set_type(r, "application/json");
  sd::ReportRow rows[16];
  ChunkOut o(r);
  o.write("{\"events\":[", 11);
  uint16_t total = storeReportCount();
  bool first = true;
  for (uint16_t off = 0; off < total; off += 16) {
    uint8_t n = storeReportRows(off, rows, 16);
    for (uint8_t i = 0; i < n; i++) {
      uint8_t ei = (uint8_t)rows[i].end;
      // Skip clean stops (Completed=1, OperatorStop=2, RemoteStop=3)
      if (ei <= 3) continue;
      static const char* end_txt[] = {"None","Completed","Stopped","RemoteStop","LinkLost","Faulted","Interrupted"};
      const char* txt = (ei < 7) ? end_txt[ei] : "Faulted";
      char line[160];
      int ln = snprintf(line, sizeof(line),
        "%s{\"ts\":%lu,\"fault\":1,\"text\":\"%s\",\"test\":%lu,\"vfd\":0}",
        first ? "" : ",",
        (unsigned long)(rows[i].start_unix + rows[i].duration_s),
        txt,
        (unsigned long)rows[i].id
      );
      o.write(line, ln);
      first = false;
    }
  }
  o.write("]}", 2);
  o.flush();
  httpd_resp_send_chunk(r, nullptr, 0);
  return ESP_OK;
}

// ---- GET /app/* — serve pre-gzipped dashboard from LittleFS -------------------
// Files live at /www/<flat-name>.gz, e.g. /www/assets_index-xxx.js.gz
// index.html is served for any path under /app/ (SPA fallback).
static esp_err_t serveApp(httpd_req_t* r) {
  // Strip /app prefix; empty → "/"
  const char* sub = r->uri + 4;   // skip "/app"
  if (!sub[0] || (sub[0] == '/' && !sub[1])) sub = "/index.html";

  // Flat-map: /assets/foo.js → assets_foo.js
  char flat[64] = {};
  const char* p = sub[0] == '/' ? sub + 1 : sub;
  size_t fi = 0;
  while (*p && fi < sizeof(flat) - 1) {
    flat[fi++] = (*p == '/') ? '_' : *p;
    p++;
  }
  flat[fi] = '\0';

  char path[80];
  snprintf(path, sizeof(path), "/www/%s.gz", flat);

  // Try LittleFS
  FILE* f = fopen(path, "rb");
  if (!f) {
    // SPA fallback: any unknown path → index.html
    f = fopen("/www/index.html.gz", "rb");
    snprintf(path, sizeof(path), "/www/index.html.gz");
  }
  if (!f) { httpd_resp_send_404(r); return ESP_OK; }

  // Content-Type by extension
  const char* ct = "application/octet-stream";
  if (strstr(flat, ".html"))    ct = "text/html";
  else if (strstr(flat, ".js")) ct = "application/javascript";
  else if (strstr(flat, ".css"))ct = "text/css";
  else if (strstr(flat, ".svg"))ct = "image/svg+xml";
  else if (strstr(flat, ".webmanifest")) ct = "application/manifest+json";
  else if (strstr(flat, ".ico"))ct = "image/x-icon";

  httpd_resp_set_type(r, ct);
  httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(r, "Cache-Control", "public, max-age=3600");

  static char fbuf[1024];
  size_t n;
  while ((n = fread(fbuf, 1, sizeof(fbuf), f)) > 0)
    httpd_resp_send_chunk(r, fbuf, n);
  fclose(f);
  httpd_resp_send_chunk(r, nullptr, 0);
  return ESP_OK;
}

void startWeb() {
  static httpd_handle_t server = nullptr;
  if (server) return;

  // Mount the small 'spiffs' partition that holds the pre-gzipped dashboard files.
  // This is separate from the 'logs' partition used by task_store.
  static bool www_mounted = false;
  if (!www_mounted) {
    static fs::LittleFSFS www_fs;
    if (www_fs.begin(true, "/www", 4, "spiffs")) {
      www_mounted = true;
    } else {
      ESP_LOGW("web", "Could not mount /www (spiffs) — dashboard unavailable");
    }
  }
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.core_id = 0;
  c.task_priority = 4;
  c.stack_size = 8192;   // bumped: JSON serialisation needs more stack
  c.max_open_sockets = 4;
  c.max_uri_handlers = 14;
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.lru_purge_enable = true;
  if (httpd_start(&server, &c) != ESP_OK) return;

  // Legacy report download endpoints
  httpd_uri_t h_report   {"/r/*",            HTTP_GET,    sendReport,    nullptr};
  httpd_uri_t h_index    {"/",               HTTP_GET,    sendIndex,     nullptr};
  // Real-time API
  httpd_uri_t h_status   {"/api/status",     HTTP_GET,    apiStatus,     nullptr};
  httpd_uri_t h_reports  {"/api/reports",    HTTP_GET,    apiReports,    nullptr};
  httpd_uri_t h_tests    {"/api/tests/*",    HTTP_GET,    apiTestDetail, nullptr};
  httpd_uri_t h_command  {"/api/command",    HTTP_POST,   apiCommand,    nullptr};
  httpd_uri_t h_events   {"/api/events",     HTTP_GET,    apiEvents,     nullptr};
  // CORS pre-flight for all API routes
  httpd_uri_t h_options  {"/api/*",          HTTP_OPTIONS, handleOptions, nullptr};

  // Dashboard SPA
  httpd_uri_t h_app      {"/app/*",           HTTP_GET,    serveApp,      nullptr};
  // Redirect bare /app to /app/
  httpd_uri_t h_app_root {"/app",             HTTP_GET,    [](httpd_req_t* r) -> esp_err_t {
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "/app/");
    httpd_resp_send(r, nullptr, 0);
    return ESP_OK;
  }, nullptr};

  httpd_register_uri_handler(server, &h_report);
  httpd_register_uri_handler(server, &h_index);
  httpd_register_uri_handler(server, &h_app);
  httpd_register_uri_handler(server, &h_app_root);
  httpd_register_uri_handler(server, &h_status);
  httpd_register_uri_handler(server, &h_reports);
  httpd_register_uri_handler(server, &h_tests);
  httpd_register_uri_handler(server, &h_command);
  httpd_register_uri_handler(server, &h_events);
  httpd_register_uri_handler(server, &h_options);
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
