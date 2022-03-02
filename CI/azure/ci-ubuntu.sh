#!/bin/bash
set -x
uname -a
DEBIAN_FRONTEND=noninteractive apt update
DEBIAN_FRONTEND=noninteractive apt -y upgrade
DEBIAN_FRONTEND=noninteractive apt install -y bison flex cmake git build-essential libxml2-dev doxygen
DEBIAN_FRONTEND=noninteractive apt install -y python3 python3-sphinx python3-setuptools
echo "$PWD"
mkdir build && cd build
cmake .. -DWITH_HWMON=ON -DWITH_SERIAL_BACKEND=ON -DWITH_EXAMPLES=ON -DPYTHON_BINDINGS=ON -DENABLE_PACKAGING=ON -DCPACK_SYSTEM_NAME="{ARTIFACTNAME}"
make
make package
