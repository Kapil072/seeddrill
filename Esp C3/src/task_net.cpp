// Wi-Fi, MQTT to AWS IoT Core, SNTP and report upload. Core 0.
// ESP-IDF APIs directly: the Arduino WiFi class builds Strings on the heap, and the
// only heap traffic wanted at run time is what TLS and lwIP do internally.
#include <LittleFS.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <fcntl.h>
#include <mqtt_client.h>
#include <nvs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "cloud_msgs.h"

namespace app {

namespace {

constexpr uint32_t STACK_WORDS = 6144 / sizeof(StackType_t);
StackType_t stack[STACK_WORDS];
StaticTask_t tcb;

constexpr uint8_t SCAN_MAX = sd::WIFI_MAX_APS;
constexpr uint32_t SAMPLES_PER_CHUNK = 120;
constexpr uint32_t PUBACK_TIMEOUT_MS = 8000;
constexpr uint32_t MIN_HEAP_FOR_UPLOAD = 40 * 1024;

// Requests from the UI task
enum class Req : uint8_t { None, Scan, Connect, Forget };
std::atomic<Req> req{Req::None};
char req_ssid[33], req_pass[64];
std::atomic<uint8_t> req_use_saved{0};

// Published status; a seqlock so the UI reads it without a lock.
sd::SeqLock<sd::NetStatus> status;
sd::NetStatus st{};

struct ApList {
  uint8_t count;
  sd::WifiAp aps[SCAN_MAX];
};
sd::SeqLock<ApList> ap_list;
ApList scan_out;
wifi_ap_record_t scan_raw[SCAN_MAX];

char saved_ssid[33], saved_pass[64];
std::atomic<uint8_t> connecting_fresh{0};   // true while joining with a newly typed password
std::atomic<uint8_t> scan_done{0};
std::atomic<uint8_t> got_ip{0}, lost_link{0}, auth_failed{0};
uint8_t retry_count = 0;
uint32_t next_retry_ms = 0, rejoin_ms = 0;

// Cloud
fs::LittleFSFS certs_fs;
char ca_pem[2048], crt_pem[2048], key_pem[2048];
char endpoint[96], thing[48];
char t_tele[80], t_status[80], t_event[80], t_report[80], t_samples[80], t_cmd[80], t_ack[80];
char lwt_msg[40];
esp_mqtt_client_handle_t mqtt = nullptr;
std::atomic<uint8_t> mqtt_up{0};
std::atomic<int> acked_msg{-1};
bool cloud_configured = false;

// Message buffers: one set, used only by this task. Command replies use their own.
char msg[512];
char big[8192];

std::atomic<uint32_t> ntp_pending{0};

uint32_t ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

void publishStatus() { status.write(st); }

void nvsLoadWifi() {
  nvs_handle_t h;
  if (nvs_open("sd", NVS_READONLY, &h) != ESP_OK) return;
  size_t a = sizeof(saved_ssid), b = sizeof(saved_pass);
  nvs_get_str(h, "ssid", saved_ssid, &a);
  nvs_get_str(h, "pass", saved_pass, &b);
  nvs_close(h);
}

void nvsSaveWifi() {
  nvs_handle_t h;
  if (nvs_open("sd", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "ssid", saved_ssid);
  nvs_set_str(h, "pass", saved_pass);
  nvs_commit(h);
  nvs_close(h);
}

void onWifi(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT) {
    if (id == WIFI_EVENT_SCAN_DONE) {
      scan_done.store(1);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
      auto* d = (wifi_event_sta_disconnected_t*)data;
      if (d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
          d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) auth_failed.store(1);
      lost_link.store(1);
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* e = (ip_event_got_ip_t*)data;
    snprintf(st.ip, sizeof(st.ip), IPSTR, IP2STR(&e->ip_info.ip));
    got_ip.store(1);
  }
}

void wifiStart() {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&c);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);   // credentials live in our NVS keys only
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifi, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifi, nullptr);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
}

void wifiJoin(const char* ssid, const char* pass) {
  wifi_config_t c{};
  strncpy((char*)c.sta.ssid, ssid, sizeof(c.sta.ssid) - 1);
  strncpy((char*)c.sta.password, pass, sizeof(c.sta.password) - 1);
  c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  c.sta.pmf_cfg.capable = true;
  esp_wifi_disconnect();
  esp_wifi_set_config(WIFI_IF_STA, &c);
  esp_wifi_connect();
  strncpy(st.ssid, ssid, sizeof(st.ssid) - 1);
  st.wifi = sd::WifiState::Connecting;
  publishStatus();
}

void finishScan() {
  uint16_t n = SCAN_MAX;
  esp_wifi_scan_get_ap_records(&n, scan_raw);
  uint8_t out = 0;
  for (uint16_t i = 0; i < n; i++) {
    const char* ssid = (const char*)scan_raw[i].ssid;
    if (!ssid[0]) continue;
    bool dup = false;   // one row per network name; records arrive strongest first
    for (uint8_t k = 0; k < out; k++) dup |= !strcmp(scan_out.aps[k].ssid, ssid);
    if (dup) continue;
    sd::WifiAp& a = scan_out.aps[out++];
    strncpy(a.ssid, ssid, 32);
    a.ssid[32] = 0;
    a.rssi = scan_raw[i].rssi;
    a.secure = scan_raw[i].authmode != WIFI_AUTH_OPEN;
    a.known = !strcmp(saved_ssid, ssid);
  }
  scan_out.count = out;
  ap_list.write(scan_out);
  if (st.wifi == sd::WifiState::Scanning) {
    st.wifi = st.ip[0] ? sd::WifiState::Connected : sd::WifiState::NotConnected;
    publishStatus();
  }
}

void updateBars() {
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    st.bars = ap.rssi > -55 ? 4 : ap.rssi > -65 ? 3 : ap.rssi > -75 ? 2 : 1;
    publishStatus();
  }
}

void startSntp() {
  static bool started = false;
  if (started) return;
  started = true;
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "time.google.com");
  sntp_setservername(1, "pool.ntp.org");
  sntp_set_time_sync_notification_cb([](struct timeval* tv) { ntp_pending.store((uint32_t)tv->tv_sec); });
  sntp_init();
}

bool loadFile(const char* path, char* buf, size_t cap) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  ssize_t n = read(fd, buf, cap - 1);
  close(fd);
  if (n <= 0) return false;
  buf[n] = 0;
  return true;
}

// device.cfg: line 1 endpoint, line 2 thing name
bool loadCloudConfig() {
  if (!certs_fs.begin(false, "/certs", 2, "spiffs")) return false;
  char cfg[160];
  if (!loadFile("/certs/device.cfg", cfg, sizeof(cfg))) return false;
  char* nl = strchr(cfg, '\n');
  if (!nl) return false;
  *nl = 0;
  strncpy(endpoint, cfg, sizeof(endpoint) - 1);
  char* end = strpbrk(nl + 1, "\r\n");
  if (end) *end = 0;
  strncpy(thing, nl + 1, sizeof(thing) - 1);
  if (endpoint[0] == 0 || thing[0] == 0) return false;
  if (!loadFile("/certs/root.pem", ca_pem, sizeof(ca_pem))) return false;
  if (!loadFile("/certs/device.crt", crt_pem, sizeof(crt_pem))) return false;
  if (!loadFile("/certs/device.key", key_pem, sizeof(key_pem))) return false;
  snprintf(t_tele, sizeof(t_tele), "seeddrill/%s/telemetry", thing);
  snprintf(t_status, sizeof(t_status), "seeddrill/%s/status", thing);
  snprintf(t_event, sizeof(t_event), "seeddrill/%s/event", thing);
  snprintf(t_report, sizeof(t_report), "seeddrill/%s/report", thing);
  snprintf(t_samples, sizeof(t_samples), "seeddrill/%s/samples", thing);
  snprintf(t_cmd, sizeof(t_cmd), "seeddrill/%s/cmd", thing);
  snprintf(t_ack, sizeof(t_ack), "seeddrill/%s/ack", thing);
  return true;
}

// Commands are validated here, then applied by ctl, which re-checks every interlock.
void handleCommand(const char* p, size_t n) {
  static char ack[160];
  sd::CloudCmd c;
  if (!sd::parseCommand(p, n, c)) return;
  const char* err = nullptr;
  uint32_t now = nowUnix();
  if (now > 1000000000u && c.expires < now) err = "expired";
  else {
    sd::Telemetry t{};
    g.telemetry.read(t);
    switch (c.kind) {
      case sd::CloudCmd::Start:
        if (!t.remote_allowed) err = "remote not allowed on the panel";
        else if (!t.local_enable) err = "local enable off";
        else if (!t.estop_ok || !t.guard_ok) err = "interlock open";
        else sendCmd({sd::CmdType::RemoteStart, 0, 0});
        break;
      case sd::CloudCmd::Stop:
        sendCmd({sd::CmdType::RemoteStop, 0, 0});   // stop is always accepted
        break;
      case sd::CloudCmd::Setpoint:
        if (!t.remote_allowed) err = "remote not allowed on the panel";
        else sendCmd({sd::CmdType::RemoteSetpoint, 0, c.value});
        break;
      default:
        break;
    }
  }
  size_t len = sd::jsonAck(ack, sizeof(ack), c.id, !err, err ? err : "");
  if (len) esp_mqtt_client_publish(mqtt, t_ack, ack, len, 0, 0);
}

void onMqtt(void*, esp_event_base_t, int32_t id, void* data) {
  auto* e = (esp_mqtt_event_handle_t)data;
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
      mqtt_up.store(1);
      g.cloud_ok.store(1, std::memory_order_relaxed);
      esp_mqtt_client_subscribe(mqtt, t_cmd, 1);
      static char on[48];
      size_t n = sd::jsonStatus(on, sizeof(on), true, FW_VERSION);
      esp_mqtt_client_publish(mqtt, t_status, on, n, 1, 1);
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      mqtt_up.store(0);
      g.cloud_ok.store(0, std::memory_order_relaxed);
      break;
    case MQTT_EVENT_PUBLISHED:
      acked_msg.store(e->msg_id);
      break;
    case MQTT_EVENT_DATA:
      if (e->current_data_offset == 0 && e->data_len == e->total_data_len) handleCommand(e->data, e->data_len);
      break;
    default:
      break;
  }
}

void mqttStart() {
  if (mqtt || !cloud_configured) return;
  sd::jsonStatus(lwt_msg, sizeof(lwt_msg), false, FW_VERSION);
  esp_mqtt_client_config_t c{};
  c.host = endpoint;
  c.port = 8883;
  c.transport = MQTT_TRANSPORT_OVER_SSL;
  c.client_id = thing;
  c.cert_pem = ca_pem;
  c.client_cert_pem = crt_pem;
  c.client_key_pem = key_pem;
  c.keepalive = 60;
  c.lwt_topic = t_status;
  c.lwt_msg = lwt_msg;
  c.lwt_qos = 1;
  c.lwt_retain = 1;
  c.task_prio = 5;
  c.task_stack = 8192;   // mutual-TLS handshake runs on this stack
  c.buffer_size = 2048;
  c.out_buffer_size = 8704;
  c.reconnect_timeout_ms = 5000;
  c.network_timeout_ms = 10000;
  mqtt = esp_mqtt_client_init(&c);
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, onMqtt, nullptr);
  esp_mqtt_client_start(mqtt);
}

bool publishAcked(const char* topic, const char* data, size_t n) {
  int id = esp_mqtt_client_publish(mqtt, topic, data, n, 1, 0);
  if (id < 0) return false;
  uint32_t t0 = ms();
  while (ms() - t0 < PUBACK_TIMEOUT_MS) {
    if (acked_msg.load() == id) return true;
    if (!mqtt_up.load()) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

// One chunk per call, so telemetry keeps flowing during a long upload. Only the
// confirmed end of a report marks it uploaded; a dropped link just restarts that test.
struct Upload {
  sd::TestHeader h;
  TestReader reader;
  uint32_t next = 0;
  bool active = false;
};
Upload up;

void uploadStep() {
  if (!mqtt_up.load() || esp_get_free_heap_size() < MIN_HEAP_FOR_UPLOAD) return;
  if (!up.active) {
    if (!storeNextUpload(up.h) || !up.reader.open(up.h.id)) return;
    up.next = 0;
    up.active = true;
  }
  static sd::Sample s[SAMPLES_PER_CHUNK];
  uint32_t n = up.reader.read(up.next, s, SAMPLES_PER_CHUNK);
  if (n) {
    size_t len = sd::jsonSamples(big, sizeof(big), up.h.id, up.next, s, n);
    if (!len || !publishAcked(t_samples, big, len)) { up.active = false; up.reader.close(); return; }
    up.next += n;
    return;
  }
  // Summary goes last: its arrival tells the cloud the record is complete.
  size_t len = sd::jsonReport(big, sizeof(big), up.h);
  if (len && publishAcked(t_report, big, len)) storeMarkUploaded(up.h.id);
  up.active = false;
  up.reader.close();
}

void run(void*) {
  wifiStart();
  nvsLoadWifi();
  cloud_configured = loadCloudConfig();
  publishStatus();
  if (saved_ssid[0]) wifiJoin(saved_ssid, saved_pass);

  uint32_t last_tele = 0, last_bars = 0, last_up = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));
    uint32_t now = ms();

    switch (req.exchange(Req::None)) {
      case Req::Scan: {
        wifi_scan_config_t c{};
        c.show_hidden = false;
        if (esp_wifi_scan_start(&c, false) == ESP_OK) {
          if (st.wifi != sd::WifiState::Connected) st.wifi = sd::WifiState::Scanning;
          publishStatus();
        }
        break;
      }
      case Req::Connect:
        if (req_use_saved.load()) { strcpy(req_ssid, saved_ssid); strcpy(req_pass, saved_pass); }
        strcpy(saved_ssid, req_ssid);
        strcpy(saved_pass, req_pass);
        connecting_fresh.store(1);
        retry_count = 0;
        st.ip[0] = 0;
        wifiJoin(req_ssid, req_pass);
        memset(req_pass, 0, sizeof(req_pass));
        break;
      case Req::Forget:
        saved_ssid[0] = saved_pass[0] = 0;
        nvsSaveWifi();
        esp_wifi_disconnect();
        st.wifi = sd::WifiState::NotConnected;
        st.ssid[0] = st.ip[0] = 0;
        st.bars = 0;
        publishStatus();
        break;
      default:
        break;
    }

    if (scan_done.exchange(0)) finishScan();

    if (got_ip.exchange(0)) {
      st.wifi = sd::WifiState::Connected;
      retry_count = 0;
      if (connecting_fresh.exchange(0)) nvsSaveWifi();   // saved only once proven good
      publishStatus();
      updateBars();
      startSntp();
      startWeb();
      mqttStart();
    }

    if (lost_link.exchange(0)) {
      st.ip[0] = 0;
      st.bars = 0;
      bool bad_pass = auth_failed.exchange(0) && connecting_fresh.load();
      if (bad_pass) {
        connecting_fresh.store(0);
        st.wifi = sd::WifiState::WrongPassword;
        st.ssid[0] = 0;
        // Forget the rejected entry and fall back to the last network that worked.
        saved_ssid[0] = saved_pass[0] = 0;
        nvsLoadWifi();
        if (saved_ssid[0]) rejoin_ms = now + 5000;   // leave the message up for a moment
      } else if (saved_ssid[0]) {
        st.wifi = sd::WifiState::Connecting;
        // Back off to one try a minute so a dead access point costs almost nothing.
        uint32_t wait = retry_count < 6 ? 2000u << retry_count : 60000u;
        if (retry_count < 6) retry_count++;
        next_retry_ms = now + wait;
      } else {
        st.wifi = sd::WifiState::NotConnected;
      }
      publishStatus();
    }
    if (rejoin_ms && (int32_t)(now - rejoin_ms) >= 0) {
      rejoin_ms = 0;
      wifiJoin(saved_ssid, saved_pass);
    }
    if (next_retry_ms && (int32_t)(now - next_retry_ms) >= 0) {
      next_retry_ms = 0;
      esp_wifi_connect();
    }

    if (now - last_bars > 5000 && st.wifi == sd::WifiState::Connected) { last_bars = now; updateBars(); }

    uint8_t cloud = !cloud_configured || st.wifi != sd::WifiState::Connected ? 0 : mqtt_up.load() ? 2 : 1;
    if (cloud != st.cloud) { st.cloud = cloud; publishStatus(); }

    if (!mqtt_up.load()) continue;

    NetEvent ev;
    while (g.net_events.pop(ev)) {
      size_t n = sd::jsonEvent(msg, sizeof(msg), ev.when, ev.fault, ev.vfd, ev.test);
      if (n) esp_mqtt_client_publish(mqtt, t_event, msg, n, 1, 0);
    }

    sd::Telemetry t{};
    if (g.telemetry.read(t)) {
      bool active = t.state == sd::RunState::Running || t.state == sd::RunState::Stopping;
      if (now - last_tele >= (active ? 1000u : 10000u)) {
        last_tele = now;
        size_t n = sd::jsonTelemetry(msg, sizeof(msg), t, nowUnix(), esp_get_free_heap_size());
        if (n) esp_mqtt_client_publish(mqtt, t_tele, msg, n, 0, 0);
      }
      // Uploads wait for idle: the link and heap belong to live data during a run.
      if (!active && now - last_up >= 200) { last_up = now; uploadStep(); }
    }
  }
}

}  // namespace

void netScan() { req.store(Req::Scan); }

uint8_t netList(sd::WifiAp* out, uint8_t max) {
  static ApList l;
  if (!ap_list.read(l)) return 0;
  uint8_t n = l.count < max ? l.count : max;
  memcpy(out, l.aps, n * sizeof(sd::WifiAp));
  return n;
}

void netConnect(const char* ssid, const char* pass) {
  strncpy(req_ssid, ssid, sizeof(req_ssid) - 1);
  req_use_saved.store(pass == nullptr);
  if (pass) strncpy(req_pass, pass, sizeof(req_pass) - 1);
  req.store(Req::Connect);
}

void netForget() { req.store(Req::Forget); }

void netStatus(sd::NetStatus& s) {
  if (!status.read(s)) memset(&s, 0, sizeof(s));
}

uint32_t nowUnix() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint32_t)tv.tv_sec;
}

void setClockFromRtc(uint32_t unix_s) {
  if (nowUnix() > 1000000000u) return;   // NTP already set the time
  struct timeval tv = {(time_t)unix_s, 0};
  settimeofday(&tv, nullptr);
}

bool takeNtpSync(uint32_t& unix_s) {
  unix_s = ntp_pending.exchange(0);
  return unix_s != 0;
}

TaskHandle_t h_net;

void startNet() {
  h_net = xTaskCreateStaticPinnedToCore(run, "net", STACK_WORDS, nullptr, 6, stack, &tcb, 0);
}

}  // namespace app
