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

TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT

SHORTSHA=$(bench_git_shortsha)
HOST=$(hostname 2>/dev/null || echo unknown)
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

# Writes the one-line run-metadata header to $OUTPUT, but only if it's
# still empty/missing - mirrors bench_report_ex()'s rule in bench_common.c
# so multiple tools can share one output file with a single header,
# regardless of which one runs first. No-op when writing to stdout, same
# as the C side (piped stdout output isn't a shared file to de-duplicate
# a header in).
#
# This writes the same JSON shape as write_meta_header() in
# bench_common.c, independently (no shared code between C and shell) -
# keep the two in sync by hand, bumping SCHEMA_VERSION in both places
# together. See benchmarks/SCHEMA.md.
SCHEMA_VERSION=2

write_meta_header_if_needed() {
	if [ -z "$OUTPUT" ] || [ -s "$OUTPUT" ]; then
		return
	fi

	header=$(printf '{"type":"meta","schema_version":%d,"timestamp":"%s","git_sha":"%s","host":"%s","board":"unknown","uri":"%s"' \
		"$SCHEMA_VERSION" "$TIMESTAMP" "$SHORTSHA" "$HOST" "$URI")

	if [ -n "$TAGS" ]; then
		tags_json=""
		for kv in $TAGS; do
			key="${kv%%=*}"
			value="${kv#*=}"
			sep=","
			[ -z "$tags_json" ] && sep=""
			tags_json="${tags_json}${sep}\"${key}\":\"${value}\""
		done
		header="${header},\"tags\":{${tags_json}}"
	fi

	header="${header}}"

	printf '%s\n' "$header" >> "$OUTPUT"
}

# $1 = metric name, $2 = extra JSON fragment to splice in before the closing
# '}' (e.g. ',"samples":4096'), or "" for none, remaining args = command to time
time_command() {
	name="$1"
	extra="$2"
	shift 2

	: > "$TMPFILE"

	i=0
	while [ "$i" -lt "$ITERATIONS" ]; do
		t0=$(date +%s.%N)
		"$@" >/dev/null 2>&1 || true
		t1=$(date +%s.%N)
		awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.3f\n", (b - a) * 1000000 }' >> "$TMPFILE"
		i=$((i + 1))
	done

	stats=$(sort -n "$TMPFILE" | awk -v n="$ITERATIONS" '
		{ a[NR] = $1; sum += $1 }
		END {
			mean = sum / n
			median = a[int(n / 2) + 1]
			p95_idx = int(n * 0.95); if (p95_idx >= n) p95_idx = n - 1
			p95 = a[p95_idx + 1]
			printf "%.3f %.3f %.3f %.3f %.3f", a[1], a[n], mean, median, p95
		}')

	set -- $stats
	min=$1; max=$2; mean=$3; median=$4; p95=$5

	write_meta_header_if_needed

	record=$(printf '{"name":"%s","unit":"us","count":%d,"min":%s,"max":%s,"mean":%s,"median":%s,"p95":%s%s}\n' \
		"$name" "$ITERATIONS" "$min" "$max" "$mean" "$median" "$p95" "$extra")

	if [ -n "$OUTPUT" ]; then
		printf '%s\n' "$record" >> "$OUTPUT"
	else
		printf '%s\n' "$record"
	fi
}

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
