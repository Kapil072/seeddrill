// Runs the real Ui + MachineCtl + DriveSim on the PC. tools/hmi_emulator.py drives
// it over stdin/stdout and plays the part of the DWIN panel.
//   rx <hex>        bytes from the panel
//   run <ms>        advance simulated time
//   in <name> <v>   set a panel input
//   wifi <state>    simulate network state changes
// Replies: "tx <hex>" for every frame sent to the panel, then "ok".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "drive_sim.h"
#include "machine_ctl.h"
#include "report.h"
#include "ui.h"

using namespace sd;

struct HexOut : ByteSink {
  void write(const uint8_t* p, size_t n) override {
    printf("tx ");
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
  }
};

struct Stored { TestHeader h; std::vector<Sample> s; };

struct World : CtlEvents, UiServices {
  std::map<uint32_t, Stored> tests;
  uint32_t open_id = 0;
  MachineCtl m{*this};
  AppConfig cfg{};
  std::vector<WifiAp> aps;
  NetStatus net{};
  std::string pending_pass, saved_ssid = "", saved_pass = "";
  uint32_t wifi_timer = 0, unix_time = 1762300000u;

  // CtlEvents
  void onTestStart(const TestHeader& h) override { open_id = h.id; tests[h.id].h = h; }
  void onSample(const Sample& s) override { tests[open_id].s.push_back(s); }
  void onTestEnd(const TestHeader& h) override { tests[h.id].h = h; }
  void onFault(Fault, uint16_t) override {}

  // UiServices
  bool telemetry(Telemetry& t) override { t = m.telemetry(); return true; }
  void command(const Cmd& c) override { m.handle(c, unix_time); }
  void applyMachine(const MachineProfile& p, const AppConfig& a) override { cfg = a; m.configure(p, a.drive, a.sim_drive); }
  void saveProfile(uint8_t, const MachineProfile&) override {}
  void saveConfig(const AppConfig& a) override { cfg = a; }
  void wifiScan() override { net.wifi = WifiState::Scanning; wifi_timer = 300; }
  uint8_t wifiList(WifiAp* out, uint8_t max) override {
    uint8_t n = 0;
    for (auto& a : aps) { if (n < max) out[n++] = a; }
    return n;
  }
  void wifiConnect(const char* ssid, const char* pass) override {
    strncpy(net.ssid, ssid, 32);
    pending_pass = pass ? pass : saved_pass;
    net.wifi = WifiState::Connecting;
    wifi_timer = 500;
  }
  void wifiForget() override { saved_ssid = ""; saved_pass = ""; net.wifi = WifiState::NotConnected; net.ssid[0] = 0; net.cloud = 0; }
  void netStatus(NetStatus& s) override { s = net; }
  uint16_t reportCount() override { return tests.size(); }
  uint8_t reportRows(uint16_t off, ReportRow* out, uint8_t n) override {
    uint8_t k = 0; uint16_t skip = off;
    for (auto it = tests.rbegin(); it != tests.rend() && k < n; ++it) {
      if (skip) { skip--; continue; }
      const TestHeader& h = it->second.h;
      ReportRow r{}; r.id = h.id; r.start_unix = h.start_unix; r.duration_s = h.duration_s; r.end = h.end; r.sync = h.uploaded ? 2 : 0; r.sim = h.sim;
      strncpy(r.name, h.profile.name, NAME_LEN - 1);
      out[k++] = r;
    }
    return k;
  }
  bool loadReport(uint32_t id, TestHeader& h) override {
    auto it = tests.find(id); if (it == tests.end()) return false; h = it->second.h; return true;
  }
  void saveMeasured(uint32_t id, uint32_t seed, uint32_t fert) override { tests[id].h.seed_dg = seed; tests[id].h.fert_dg = fert; }
  uint8_t reportSync(uint32_t id) override { return tests[id].h.uploaded ? 2 : 0; }
  uint32_t nowUnix() override { return unix_time; }
  void diagLine(uint8_t i, char* o, size_t n) override {
    static const char* const L[] = {"Firmware 1.0.0  host emulator", "Uptime 00:01:23", "Core 1: ctl hmi vfd   Core 0: net store web",
                                    "Heap free 214 KB  min 198 KB  largest 110 KB", "ctl stack free 2104 B", "hmi stack free 3312 B"};
    if (i < 6) snprintf(o, n, "%s", L[i]);
  }

  void rtcRead(uint32_t) override {}
  bool rtcPending(uint32_t&) override { return false; }

  void stepWifi(uint32_t dt) {
    if (!wifi_timer) return;
    wifi_timer = wifi_timer > dt ? wifi_timer - dt : 0;
    if (wifi_timer) return;
    if (net.wifi == WifiState::Scanning) {
      net.wifi = saved_ssid.empty() ? WifiState::NotConnected : WifiState::Connected;
      aps.clear();
      struct { const char* s; int r; int sec; } L[] = {{"SPK-Workshop", -48, 1}, {"SPK-Guest", -60, 0}, {"Airtel_Fiber_5G", -66, 1}, {"JioFiber-Office", -71, 1},
                                                      {"TP-Link_2F40", -74, 1}, {"Redmi Note 12", -78, 1}, {"Machine-Shop-AP", -81, 1}, {"vivo T2x", -88, 1}};
      for (auto& l : L) { WifiAp a{}; strncpy(a.ssid, l.s, 32); a.rssi = l.r; a.secure = l.sec; a.known = saved_ssid == l.s; aps.push_back(a); }
    } else if (net.wifi == WifiState::Connecting) {
      if (pending_pass == "correcthorse" || pending_pass.empty()) {
        net.wifi = WifiState::Connected; net.bars = 4; net.cloud = 2;
        strcpy(net.ip, "192.168.1.57"); saved_ssid = net.ssid; saved_pass = pending_pass;
      } else {
        net.wifi = WifiState::WrongPassword; net.ssid[0] = 0;
      }
    }
  }
};

int main() {
  setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
  static World w;
  static HexOut hx;
  static Ui ui(hx, w);
  static DriveSim sim;
  static Inputs in{};
  in.estop_ok = in.guard_ok = in.local_enable = in.drive_relay_ok = 1;
  static DriveFeedback fb{};

  AppConfig a{}; a.magic = 1; defaultDrive(a.drive); a.brightness = 80; a.sim_drive = 1;
  static MachineProfile prof[PROFILE_SLOTS];
  memset(prof, 0, sizeof(prof));
  defaultProfile(prof[0]);
  strcpy(prof[0].name, "Vishwakarma 11 row");
  defaultProfile(prof[1]);
  strcpy(prof[1].name, "Demo 15 shovel");
  ui.begin(a, prof, PROFILE_SLOTS);
  fflush(stdout);

  uint32_t ms = 1000;
  Links links{true, true};
  char line[4096];
  while (fgets(line, sizeof(line), stdin)) {
    char cmd[16] = {0};
    sscanf(line, "%15s", cmd);
    if (!strcmp(cmd, "rx")) {
      const char* h = line + 3;
      uint8_t b[1024]; size_t n = 0;
      while (h[0] && h[1] && h[0] != '\n' && h[0] != '\r' && n < sizeof(b)) { unsigned v; sscanf(h, "%2x", &v); b[n++] = v; h += 2; }
      ui.feed(b, n);
    } else if (!strcmp(cmd, "run")) {
      unsigned d = 0; sscanf(line + 4, "%u", &d);
      for (unsigned t = 0; t < d; t += 10) {
        ms += 10;
        if (t % 1000 == 0) w.unix_time++;
        sim.step(10, w.m.driveCommand(), fb);
        links.hmi_ok = true;
        w.m.tick(ms, w.unix_time, in, fb, links);
        w.stepWifi(10);
        if (t % 20 == 0) ui.tick(ms);
      }
    } else if (!strcmp(cmd, "in")) {
      char n[16]; int v; sscanf(line + 3, "%15s %d", n, &v);
      if (!strcmp(n, "estop")) in.estop_ok = v;
      if (!strcmp(n, "guard")) in.guard_ok = v;
      if (!strcmp(n, "enable")) in.local_enable = v;
      if (!strcmp(n, "stoppb")) in.stop_pb = v;
    } else if (!strcmp(cmd, "quit")) {
      break;
    }
    printf("ok\n");
    fflush(stdout);
  }
  return 0;
}
