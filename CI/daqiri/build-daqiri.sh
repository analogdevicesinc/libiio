#!/bin/sh
# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT
#
# Build and install the pinned DAQIRI revision into a prefix.
# Usage: build-daqiri.sh <absolute-prefix> [<engine-list> [<dpdk-prefix>]]
#   engine-list  DAQIRI_ENGINE value, e.g. "dpdk ibverbs"; empty = sockets only
#   dpdk-prefix  prefix of build-dpdk.sh (needed when engine-list has dpdk)
# Env:   DAQIRI_CMAKE_EXTRA  extra space-separated CMake arguments
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <absolute-prefix> [<engine-list> [<dpdk-prefix>]]" >&2
	exit 2
fi
prefix=$1
engines=${2:-}
dpdk_prefix=${3:-}
case "$prefix" in
/*) ;;
*) echo "prefix must be an absolute path" >&2; exit 2 ;;
esac
case "$dpdk_prefix" in
"" | /*) ;;
*) echo "dpdk prefix must be an absolute path" >&2; exit 2 ;;
esac
if [ -n "$dpdk_prefix" ]; then
	PKG_CONFIG_PATH=$dpdk_prefix/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
	export PKG_CONFIG_PATH
fi

here=$(cd "$(dirname "$0")" && pwd)
ref=$(cat "$here/DAQIRI_REF")
src=${TMPDIR:-/tmp}/daqiri-src-$ref

if [ ! -d "$src/.git" ]; then
	git clone https://github.com/NVIDIA/daqiri "$src"
fi
git -C "$src" checkout "$ref"
# Idempotent: drop a previous run's patches, then apply ours.
git -C "$src" checkout -- .
for p in "$here"/patches/daqiri/*.patch; do
	git -C "$src" apply --check "$p"
	git -C "$src" apply "$p"
done
git -C "$src" submodule update --init --recursive

# shellcheck disable=SC2086
cmake -S "$src" -B "$src/build" \
	-DBUILD_SHARED_LIBS=ON \
	-DDAQIRI_BUILD_PYTHON=OFF \
	-DDAQIRI_BUILD_EXAMPLES=OFF \
	-DDAQIRI_ENGINE="$engines" \
	-DCMAKE_INSTALL_PREFIX="$prefix" \
	${DAQIRI_CMAKE_EXTRA:-}
cmake --build "$src/build" -j"$(nproc)"
cmake --install "$src/build"

# Raw-engine probe for the hardware/loopback tests.
mkdir -p "$prefix/bin"
# shellcheck disable=SC2046
pcpath=$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
# libdaqiri.so references the CUDA driver API (-lcuda; the toolkit stub is
# enough to link) and, with the DPDK engine, DPDK's shared libs (-rpath-link).
cuda_root=${CUDAToolkit_ROOT:-${CUDA_HOME:-/usr/local/cuda}}
rpl="-L$cuda_root/lib64/stubs -lcuda"
if [ -n "$dpdk_prefix" ]; then
	rpl="$rpl -Wl,-rpath-link,$dpdk_prefix/lib/x86_64-linux-gnu"
fi
g++ -std=c++17 "$here/probe-raw.cpp" \
	$(PKG_CONFIG_PATH=$pcpath pkg-config --cflags --libs daqiri) $rpl \
	-o "$prefix/bin/daqiri-probe-raw"

PKG_CONFIG_PATH=$prefix/lib/pkgconfig pkg-config --modversion daqiri
