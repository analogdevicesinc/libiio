#!/bin/bash
set -x
uname -a
echo "$PWD"
mkdir build && cd build
cmake .. -DWITH_HWMON=ON -DWITH_SERIAL_BACKEND=ON -DWITH_EXAMPLES=ON -DPYTHON_BINDINGS=ON -DENABLE_PACKAGING=ON -DCPACK_SYSTEM_NAME="${ARTIFACTNAME}"
make
make package
