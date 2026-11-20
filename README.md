# Seed Drill Field Simulator

Workshop test rig that drives a seed drill's metering transmission with a VFD-controlled motor, in place of the ground wheel. The operator enters field conditions on a 7-inch touchscreen; the controller works out the motor speed, runs the test, logs it and produces a report. Results reach a phone dashboard through AWS.

```
DWIN 7" HMI ──UART── ESP32-S3 ──RS485/Modbus── Delta MS300 VFD ── motor ── 15:1 gearbox ── chain ── seed drill
                        │
                        └─ Wi-Fi ── MQTT/TLS ── AWS IoT Core ── Lambda ── DynamoDB ── API ── React phone dashboard
```

| Folder | What it is |
|---|---|
| `firmware/` | ESP32-S3 controller (PlatformIO). `lib/core` is portable logic, `src` is the tasks and drivers |
| `hmi/` | Display UI source. `hmi/DWIN_SET/` is ready to copy to an SD card |
| `cloud/` | AWS template, Lambda code, device provisioning, and a local stand-in for testing |
| `dashboard/` | React mobile dashboard (installable PWA) |
| `tools/` | Display emulator, end-to-end tests, circuit diagram generator |
| `docs/` | [Wiring diagram](docs/circuit_diagram.png), [commissioning guide](docs/COMMISSIONING.md), [sample report](docs/sample_report.pdf), screenshots |

## Quick start on the bench

No VFD or machine needed. This proves the display, Wi-Fi and reports.

1. **Load the display.** Format a microSD card (32 GB or less) as FAT32 with 4096-byte clusters: `format X: /FS:FAT32 /A:4096 /Q`. Copy `hmi/DWIN_SET` to the root of the card. With the display off, insert the card, power on, wait for the blue screen to finish, power off, remove the card.
2. **Wire it.** Follow [the diagram](docs/circuit_diagram.png). Check the display's serial level first (see below).
3. **Flash the bench build.** `cd firmware`, then `pio run -e esp32s3_bench -t upload`. This build forces the interlocks healthy, so never use it on a machine.
4. On the display: Settings, Wi-Fi, pick your network, enter the password. Then run a test.
5. A finished test shows a QR code. Scan it on a phone on the same Wi-Fi to download the PDF.

> **Check the serial level before connecting.** This display can be RS232 or TTL, set by a jumper. Power it alone and measure TX to GND. About -6 V means RS232: use the MAX3232, because those levels damage an ESP32 pin. About +3.3 V means TTL: wire directly.

For a real machine use `pio run -e esp32s3 -t upload` and follow [COMMISSIONING.md](docs/COMMISSIONING.md).

## Firmware design

Control and communications run on separate cores and never share a lock.

| Core | Task | Priority | Job |
|---|---|---|---|
| 1 | `ctl` | 22 | 100 Hz interlocks, test sequence, speed ramp |
| 1 | `vfd` | 20 | Modbus to the drive, 20 Hz |
| 1 | `hmi` | 12 | Display link and screen logic |
| 0 | `net` | 6 | Wi-Fi, MQTT, report upload |
| 0 | `store` | 5 | All flash writes |
| 0 | `httpd` | 4 | Local CSV and PDF download |

**Memory**
- No heap on the control path. The compiled `ctl`, `vfd`, `hmi` and `lib/core` objects contain no reference to `malloc`, `new`, `free` or `String`; check with `nm -u`.
- Task stacks, queues and buffers are static, so running out of memory shows up at link time rather than in the field.
- ESP-IDF drivers are used directly, because the Arduino `String`, `File` and `HardwareSerial` classes allocate.
- Static RAM is 36 %, leaving about 200 KB of heap for Wi-Fi and TLS.
- Settings > Diagnostics shows free heap, the lowest it has been, largest block, stack headroom per task and the worst control loop time.

**Between cores**
- `SeqLock`: latest-value data. The writer never waits, so a slow reader on core 0 cannot delay the control loop.
- `SpscRing`: ordered streams such as log samples. Lock-free, bounded, counts anything dropped.

**Flash** A flash write pauses the instruction cache on both cores, so samples are collected in RAM and written about once a minute (at most 64 s of log lost in a power cut). The drive keeps running through a pause and has its own timeout as a backstop.

**PDF** Reports are generated while they are being sent, through one 1 KB buffer. A 3-hour test uses the same RAM as a 10-second one, and long logs are thinned to fit 6 pages.

## Safety model

The emergency stop is hardwired and acts on the mains contactor without passing through any software. The firmware adds supervision on top:

- Starting needs: E-stop released, guard closed, key on, drive ready, target in range, and the operator's confirmation.
- Losing any of these during a test stops the machine.
- Nothing restarts by itself after power loss, a fault reset or a network reconnect.
- Remote start needs the key on and permission given on the panel, which clears at every power-up. It sounds the horn for 3 s first.
- A remotely started test stops if the cloud link drops for 10 s. A locally started test ignores the cloud.
- If the controller hangs, the watchdog resets it and the drive stops on its own comms timeout (needs Pr.09-03 set: it defaults to off).

This is a reference design. A qualified person must validate the electrical and mechanical safety before commissioning.

## Calculations

```
wheel RPM   = 1000 v / (60 pi D)                 v in km/h, D in m
stage ratio = product of Z_driver / Z_driven
gearbox RPM = machine shaft RPM x Z_rig_driven / Z_rig_driver
VFD Hz      = gearbox RPM x gear ratio x pole pairs / 60 x (1 + slip)
distance    = integral of v / 3.6 dt
seed drop   = v / spacing,  disc RPM = drop rate / cells x 60
kg/ha       = collected kg / (distance x rows x row spacing / 10000)
```

RPM is labelled *estimated* unless a sensor is configured, as the hardware brief requires.

## Tests

```sh
sh firmware/test/host/run.sh            # 268 checks: maths, protocols, interlocks, reports, JSON
sh tools/check_hmi.sh                   # rebuilds DWIN_SET, then 81 steps through the real UI and generated files
node --test cloud/test/cloud.test.mjs   # 8 suites, fed by the firmware's real JSON output
node tools/dashboard_e2e.mjs            # 20 checks in a phone-sized browser (see the header of that file)
```

Host tests need `pip install ziglang pillow` (or set `CXX`). Run the dashboard locally with no AWS account: `npm run local` and `node local/device.mjs rig-001 --seed` in `cloud/`, then `npm run dev` in `dashboard/`. Sign in as `operator@spk.local` / `demo1234`.

## Changing the display

Screens are authored once in `hmi/src/pages.mjs`. `npm run build` in `hmi/` renders them, packs `DWIN_SET` and regenerates `firmware/lib/hmi/hmi_map.h`, so the display and firmware always agree. No DGUS tool is needed.

## Deploying to AWS

```powershell
cd cloud
.\deploy.ps1 -Region ap-south-1
.\provision-device.ps1 -Thing rig-001
cd ..\firmware; pio run -t uploadfs
```

Records are kept for 400 days through DynamoDB TTL. Users are created in Cognito; only the `operator` group can send commands.
