#!/bin/bash
set -x
uname -a
echo "$PWD"
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" -Werror=dev -DCOMPILE_WARNING_AS_ERROR=ON -DWITH_SERIAL_BACKEND=ON -DWITH_EXAMPLES=ON -DPYTHON_BINDINGS=ON -DCPP_BINDINGS=ON -DENABLE_PACKAGING=ON -DCPACK_SYSTEM_NAME="${ARTIFACTNAME}"
make
make package
make required2tar
