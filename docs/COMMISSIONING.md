# Commissioning guide

Do these in order, with the seed drill uncoupled until step 5. A qualified electrician must verify the panel first.

## Status of this design

| Part | State |
|---|---|
| Firmware logic, UI, reports, cloud code | Tested on a PC against simulators |
| Display files | Layouts match real DGUS output; not yet loaded on a physical panel |
| Modbus register map | From the Delta MS300 manual; not yet run against a physical drive |
| AWS stack | Template passes lint; not yet deployed |

Expect to find and fix small issues during steps 1 to 4.

## Parts list

| Ref | Part | Note |
|---|---|---|
| | ESP32-S3 DevKitC-1 | 8 MB flash or more |
| | DWIN DMG10600C070_03WTR | 5 V, 1 A. FFC cable and breakout |
| U1 | MAX3232 module | Only if the display is at RS232 level |
| U2 | MAX3485 module, 3.3 V | With DE/RE broken out |
| | 7 x PC817 + 4.7 k + 10 k | Or a 24 V opto input board |
| U3 | ULN2003 | 10 k pull-downs on both inputs |
| K1 | Contactor, 24 V coil | Rated for the VFD input |
| K2 | Relay, 24 V coil | Dry contact to MI1 |
| PSU1 | 24 V DC 60 W | |
| | Buck 24 V to 5 V, 3 A | |
| S1 | E-stop, twin NC | Safety rated |
| | Guard switch, key switch, START (NO), STOP (NC), horn | |
| | Inductive proximity sensor | Optional, recommended for true RPM |

## 1. Display

1. Measure TX to GND with the display powered alone. See the README. Decide whether U1 is needed.
2. Load `hmi/DWIN_SET` from the SD card. The blue screen should list the files and finish with END. Delete any older `.icl` or `.bin` files from the card first: leftovers can overwrite the new ones.
3. Connect to the ESP32. The boot screen should appear and move to the home screen.
4. Tap around. Check that the trend curve (Run page) and QR code (Report page) appear; these two are the only controls not cross-checked against a real DGUS file. If touch does nothing, confirm RX/TX are crossed.

## 2. Drive parameters

Set on the MS300 keypad, motor disconnected from the load:

| Parameter | Value | Meaning |
|---|---|---|
| 05-01 to 05-04 | Motor nameplate | Full-load current, power, speed, poles |
| 01-00 | 60 Hz | Max frequency: 100 RPM needs about 52 Hz |
| 00-20 | 1 | Frequency from RS485 |
| 00-21 | 2 | Run command from RS485 |
| 09-00 | 1 | Modbus address |
| 09-01 | 19.2 | Baud rate |
| 09-04 | 14 | 8E1 RTU |
| 09-02 | 1 | On comms loss: warn and ramp to stop |
| **09-03** | **1.0** | **Comms time-out. Defaults to 0, meaning disabled.** |
| 02-01 | 49 | MI1 = drive enable, wired to K2 |
| 02-13 | 11 | Relay = fault |
| 01-12, 01-13 | 2 s | Keep short: the controller sets the real ramp |

Run the drive's motor auto-tune as the manual describes.

## 3. Interlocks

Flash the normal build: `pio run -e esp32s3 -t upload`. In Settings > Drive mode choose Real drive.

Open Pre-start Check and confirm each row follows its switch. Confirm in Diagnostics that Modbus errors stay at zero.

With the motor uncoupled, start a test and confirm each of these stops it:

- [ ] Press E-stop: K1 drops, motor coasts, fault page shows
- [ ] Open the guard
- [ ] Turn the key off
- [ ] Press panel STOP
- [ ] Unplug RS485: fault within about 1 s
- [ ] Unplug the display: controlled stop within about 6 s
- [ ] Reset the ESP32 during a run: K2 drops at once and the drive stops
- [ ] After each one, confirm nothing restarts until a new start is given

## 4. Speed calibration

The firmware applies slip as specified: `f x (1 + S)`, default 4 %. If the drive's own slip compensation is active the motor will run about 4 % fast. Measure with a tachometer:

1. Direct RPM mode, 60 RPM. Measure the gearbox output.
2. Adjust Settings > Drive & limits > Slip compensation until it reads 60. Often this means setting it near 0.
3. Confirm at 30 and 100 RPM.

The service PIN for this page is `2580`. Change `SERVICE_PIN` in `firmware/lib/core/ui.cpp`.

## 5. Mechanical

1. Count the teeth on every sprocket and enter them on the Transmission page.
2. Confirm which shaft the rig drives and set it there. **Check the ratio direction:** the two project documents disagree. The firmware uses Hardware Details 6: `n_driven = n_driver x Z_driver / Z_driven`.
3. Verify with a real measurement: mark the wheel, run a short test, and count metering shaft turns against the calculated value.
4. Secure the drill and the rig, fit all guards, then couple.

## 6. Acceptance tests (TECH-SPEC 5.3)

- [ ] 60 minutes at 30 RPM under full load
- [ ] 30 minutes at 100 RPM
- [ ] Overload response: 150 % for 60 s
- [ ] Remote stop works from the dashboard
- [ ] Physical E-stop overrides everything, including a remote run
- [ ] Speed regulation measured by tachometer against the +/-1 % target

The torque figures in the specification (100 Nm continuous, 150 Nm peak) need checking against the gearbox actually bought. At 30 RPM the motor runs near 15 Hz: check its cooling for long tests.

## 7. Cloud

1. `cloud\deploy.ps1`, then `cloud\provision-device.ps1 -Thing rig-001`.
2. `pio run -t uploadfs` writes the certificates. Test records live in a separate partition and are not touched.
3. The cloud icon on the display turns green, and the machine appears on the dashboard.
4. Allow remote control on the panel, then test start and stop from a phone.

## Troubleshooting

| Symptom | Check |
|---|---|
| Display stays on the boot screen | Serial wiring, RX/TX crossed, 115200 baud |
| Screens do not change after SD load | Old files on the card; check the file count on the blue screen |
| Wrong colours or garbled text | Factory ASCII font missing: the panel needs its stock `0#` font |
| Drive ready stays red | RS485 polarity, Pr.09 settings, address, fault relay wiring |
| Cloud icon stays amber | Wi-Fi works, TLS fails: check the clock, certificates, and `device.cfg` |
| Reports show "time not set" | No Wi-Fi yet and the panel RTC is unset. Connect once to sync |
