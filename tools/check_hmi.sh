#!/bin/sh
# Rebuild the display files, compile the host emulator, run the full operator scenario.
# Usage: PY=python tools/check_hmi.sh [shots_dir]
cd "$(dirname "$0")/.." || exit 1
PY=${PY:-python}
(cd hmi && node src/capture.mjs >/dev/null && "$PY" src/pack.py | tail -1) || exit 1
(cd firmware && "$PY" -m ziglang c++ -std=gnu++14 -O1 -g -Wall -Wextra -Wno-unused-parameter -Ilib/core -Ilib/hmi \
  lib/core/kinematics.cpp lib/core/dgus.cpp lib/core/modbus.cpp lib/core/machine_ctl.cpp lib/core/report.cpp lib/core/ui.cpp \
  test/host/emu_host.cpp -o test/host/out/emu_host.exe) || exit 1
exec "$PY" tools/hmi_emulator.py ${1:+--shots "$1"}
