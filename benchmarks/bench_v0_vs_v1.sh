#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Times CLI-level operations that differ in tool name between the v0
# (libiio-v0 branch) and v1 (main) APIs, tagging each record with
# libiio_version so a single result file holds both and compare.py/
# report.py can diff them directly.
#
# v0 splits buffer reads into a separate iio_readdev tool; v1 merges
# read/write into iio_rwdev (no -w flag = read). --version picks the
# right one; iio_info and iio_attr's -C flag are the same in both.
#
# Usage:
#   benchmarks/bench_v0_vs_v1.sh --bin-dir <dir> --version v0|v1 \
#       --uri ip:10.48.69.125 [--device cf-ad9361-lpc] [--channel voltage0] \
#       [--iterations 20] [--rwdev-sizes "65536 1048576"] [--output results.json]
#
# --device enables the read-capture pass (iio_readdev/iio_rwdev), swept
# across --rwdev-sizes; omit it to only time iio_attr -C. Read-only by
# design, same reasoning as bench_cli.sh's --rwdev-device: no -w (real
# hardware TX side effect), no cyclic/benchmark modes (don't cleanly
# start-and-exit).

set -e

BIN_DIR="."
VERSION=""
URI="ip:192.168.2.1"
DEVICE=""
CHANNEL=""
ITERATIONS=20
RWDEV_SIZES="65536 1048576"
OUTPUT=""

while [ $# -gt 0 ]; do
	case "$1" in
	--bin-dir)
		BIN_DIR="$2"
		shift 2
		;;
	--version)
		VERSION="$2"
		shift 2
		;;
	--uri)
		URI="$2"
		shift 2
		;;
	--device)
		DEVICE="$2"
		shift 2
		;;
	--channel)
		CHANNEL="$2"
		shift 2
		;;
	--iterations)
		ITERATIONS="$2"
		shift 2
		;;
	--rwdev-sizes)
		RWDEV_SIZES="$2"
		shift 2
		;;
	--output)
		OUTPUT="$2"
		shift 2
		;;
	*)
		echo "Unknown argument: $1" >&2
		exit 1
		;;
	esac
done

case "$VERSION" in
v0|v1) ;;
*)
	echo "--version must be v0 or v1" >&2
	exit 1
	;;
esac

TAGS="libiio_version=$VERSION"

if [ "$VERSION" = v0 ]; then
	READ_BIN="$BIN_DIR/iio_readdev"
else
	READ_BIN="$BIN_DIR/iio_rwdev"
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/bench_shell_common.sh"
bench_shell_init

if [ ! -x "$BIN_DIR/iio_info" ]; then
	echo "iio_info not found/executable at $BIN_DIR" >&2
	exit 1
fi
if [ ! -x "$BIN_DIR/iio_attr" ]; then
	echo "iio_attr not found/executable at $BIN_DIR" >&2
	exit 1
fi

echo "Timing 'iio_info -u $URI' ($VERSION) over $ITERATIONS runs..." >&2
time_command "cli_iio_info" "" "$BIN_DIR/iio_info" -u "$URI"

echo "Timing 'iio_attr -u $URI -C' ($VERSION) over $ITERATIONS runs..." >&2
time_command "cli_iio_attr" "" "$BIN_DIR/iio_attr" -u "$URI" -C

if [ -n "$DEVICE" ]; then
	if [ -x "$READ_BIN" ]; then
		for size in $RWDEV_SIZES; do
			echo "Timing '$READ_BIN -s $size $DEVICE $CHANNEL' ($VERSION) over $ITERATIONS runs..." >&2
			time_command "cli_rwdev_read" ",\"samples\":$size" \
				"$READ_BIN" -u "$URI" -s "$size" "$DEVICE" $CHANNEL
		done
	else
		echo "$READ_BIN not found/executable, skipping --device timing" >&2
	fi
fi
