#!/bin/sh
# Builds and runs the host tests. Needs: pip install ziglang (any C++14 compiler works: set CXX).
cd "$(dirname "$0")/../.." || exit 1
CXX=${CXX:-python -m ziglang c++}
mkdir -p test/host/out
$CXX -std=gnu++14 -O1 -g -Wall -Wextra -Wno-unused-parameter -Ilib/core -Ilib/hmi \
  lib/core/kinematics.cpp lib/core/dgus.cpp lib/core/modbus.cpp lib/core/machine_ctl.cpp lib/core/report.cpp lib/core/cloud_msgs.cpp \
  test/host/test_core.cpp -o test/host/out/test_core.exe || exit 1
./test/host/out/test_core.exe test/host/out
