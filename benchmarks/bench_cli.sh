#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Times how long the iio_info and iio_attr CLI tools themselves take to run
# (process startup + arg parsing + context creation + the tool's own work),
# as opposed to bench_context/bench_attr which time the underlying libiio
# API calls in-process. Requires WITH_UTILS=ON so iio_info/iio_attr exist.
#
# Usage:
#   benchmarks/bench_cli.sh --uri ip:192.168.2.1 --bin-dir build/utils \
#       [--iterations 20] [--output results.json] [--tag key=value ...] \
#       [--rwdev-device <name>] [--rwdev-channel <name>] \
#       [--rwdev-sizes "256 4096 65536 1048576"]
#
# --rwdev-device enables timing 'iio_rwdev -s <N> <device> [<channel>]'
# (a bounded read capture) swept across --rwdev-sizes sample counts;
# omit it to skip that pass (no generic auto-discovery of a device/channel,
# unlike iio_info/iio_attr which need none).

set -e

BIN_DIR="."
# Kept in sync by hand with the same default in bench_common.c's
# bench_parse_opts() and run_all.sh - see benchmarks/SCHEMA.md.
URI="ip:192.168.2.1"
ITERATIONS=20
OUTPUT=""
TAGS=""
RWDEV_DEVICE=""
RWDEV_CHANNEL=""
RWDEV_SIZES="256 4096 65536 1048576"

while [ $# -gt 0 ]; do
	case "$1" in
	--bin-dir)
		BIN_DIR="$2"
		shift 2
		;;
	--uri)
		URI="$2"
		shift 2
		;;
	--iterations)
		ITERATIONS="$2"
		shift 2
		;;
	--output)
		OUTPUT="$2"
		shift 2
		;;
	--tag)
		case "$2" in
		*=*) ;;
		*)
			echo "--tag expects key=value, got '$2'" >&2
			exit 1
			;;
		esac
		TAGS="$TAGS $2"
		shift 2
		;;
	--rwdev-device)
		RWDEV_DEVICE="$2"
		shift 2
		;;
	--rwdev-channel)
		RWDEV_CHANNEL="$2"
		shift 2
		;;
	--rwdev-sizes)
		RWDEV_SIZES="$2"
		shift 2
		;;
	*)
		echo "Unknown argument: $1" >&2
		exit 1
		;;
	esac
done

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/bench_shell_common.sh"
bench_shell_init

if [ ! -x "$BIN_DIR/iio_info" ]; then
	echo "iio_info not found/executable at $BIN_DIR (build with -DWITH_UTILS=ON)" >&2
	exit 1
fi
if [ ! -x "$BIN_DIR/iio_attr" ]; then
	echo "iio_attr not found/executable at $BIN_DIR (build with -DWITH_UTILS=ON)" >&2
	exit 1
fi

echo "Timing 'iio_info -u $URI' over $ITERATIONS runs..." >&2
time_command "cli_iio_info" "" "$BIN_DIR/iio_info" -u "$URI"

echo "Timing 'iio_attr -u $URI -C' over $ITERATIONS runs..." >&2
time_command "cli_iio_attr" "" "$BIN_DIR/iio_attr" -u "$URI" -C

# iio_rwdev needs a concrete device (no safe generic auto-discovery here,
# unlike iio_info/iio_attr), so this pass is opt-in via --rwdev-device.
# Read-only by design: no -w (actively transmits whatever it's fed, real
# hardware side effect), no -c/-B (cyclic/benchmark modes don't cleanly
# start-and-exit, so they don't fit time_command's fixed-invocation model).
if [ -n "$RWDEV_DEVICE" ]; then
	if [ -x "$BIN_DIR/iio_rwdev" ]; then
		for size in $RWDEV_SIZES; do
			echo "Timing 'iio_rwdev -u $URI -s $size $RWDEV_DEVICE $RWDEV_CHANNEL' over $ITERATIONS runs..." >&2
			time_command "cli_iio_rwdev_read" ",\"samples\":$size" \
				"$BIN_DIR/iio_rwdev" -u "$URI" -s "$size" "$RWDEV_DEVICE" $RWDEV_CHANNEL
		done
	else
		echo "iio_rwdev not found/executable at $BIN_DIR, skipping --rwdev-device timing" >&2
	fi
fi
