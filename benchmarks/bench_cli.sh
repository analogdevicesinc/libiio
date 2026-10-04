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
#       [--iterations 20] [--output results.json]

set -e

BIN_DIR="."
URI="ip:192.168.2.1"
ITERATIONS=20
OUTPUT=""

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
	*)
		echo "Unknown argument: $1" >&2
		exit 1
		;;
	esac
done

TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT

SHORTSHA=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
HOST=$(hostname 2>/dev/null || echo unknown)
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

# Writes the one-line run-metadata header to $OUTPUT, but only if it's
# still empty/missing - mirrors bench_report_ex()'s rule in bench_common.c
# so multiple tools can share one output file with a single header,
# regardless of which one runs first. No-op when writing to stdout, same
# as the C side (piped stdout output isn't a shared file to de-duplicate
# a header in).
write_meta_header_if_needed() {
	if [ -z "$OUTPUT" ] || [ -s "$OUTPUT" ]; then
		return
	fi

	header=$(printf '{"type":"meta","timestamp":"%s","git_sha":"%s","host":"%s","board":"unknown","uri":"%s"}' \
		"$TIMESTAMP" "$SHORTSHA" "$HOST" "$URI")

	printf '%s\n' "$header" >> "$OUTPUT"
}

# $1 = metric name, remaining args = command to time
time_command() {
	name="$1"
	shift

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

	record=$(printf '{"name":"%s","unit":"us","count":%d,"min":%s,"max":%s,"mean":%s,"median":%s,"p95":%s}\n' \
		"$name" "$ITERATIONS" "$min" "$max" "$mean" "$median" "$p95")

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
time_command "cli_iio_info" "$BIN_DIR/iio_info" -u "$URI"

echo "Timing 'iio_attr -u $URI -C' over $ITERATIONS runs..." >&2
time_command "cli_iio_attr" "$BIN_DIR/iio_attr" -u "$URI" -C
