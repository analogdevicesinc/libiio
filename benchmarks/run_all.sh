#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Runs every benchmark binary against a given URI and collects their JSON
# output into a single timestamped results file under benchmarks/results/.
#
# Usage:
#   benchmarks/run_all.sh --uri ip:192.168.2.1 [--bin-dir build/benchmarks] \
#       [--utils-bin-dir build/utils] [--board fmcomms2] \
#       [extra args passed through to the C benchmarks]
#
# --utils-bin-dir points at the built iio_info/iio_attr binaries (WITH_UTILS=ON)
# and enables the bench_cli.sh wall-clock timing pass; omit it to skip that pass.
#
# --board <name> overrides the auto-detected board name (from libiio's
# "board" field, see bench_detect_board() in bench_common.c) folded into the
# output filename.

set -e

BIN_DIR="."
UTILS_BIN_DIR=""
URI="ip:192.168.2.1"
BOARD=""
EXTRA_ARGS=""

while [ $# -gt 0 ]; do
	case "$1" in
	--bin-dir)
		BIN_DIR="$2"
		shift 2
		;;
	--utils-bin-dir)
		UTILS_BIN_DIR="$2"
		shift 2
		;;
	--uri)
		URI="$2"
		shift 2
		;;
	--board)
		BOARD="$2"
		shift 2
		;;
	*)
		EXTRA_ARGS="$EXTRA_ARGS $1"
		shift
		;;
	esac
done

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RESULTS_DIR="$SCRIPT_DIR/results"
mkdir -p "$RESULTS_DIR"

SHORTSHA=$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)
TIMESTAMP=$(date -u +%Y%m%dT%H%M%SZ)

# Collect into a temp file first; board name (for the final filename) isn't
# known until after the first benchmark run.
TMP_OUTPUT="$RESULTS_DIR/.run_all_tmp_$$.json"
rm -f "$TMP_OUTPUT"

for bench in bench_context bench_attr bench_buffer bench_streaming; do
	BIN="$BIN_DIR/$bench"
	if [ ! -x "$BIN" ]; then
		echo "Skipping $bench: not found or not executable at $BIN" >&2
		continue
	fi
	echo "Running $bench..." >&2
	"$BIN" --uri "$URI" --output "$TMP_OUTPUT" $EXTRA_ARGS
done

# bench_block reports one (enqueue, dequeue, sample-rate) triple per
# invocation, named after its --block-size/--num-blocks; sweep the sizes
# here (ring depth stays at the default of 4, override via $EXTRA_ARGS).
BENCH_BLOCK="$BIN_DIR/bench_block"
if [ -x "$BENCH_BLOCK" ]; then
	for size in 512 4096 65536 1048576; do
		echo "Running bench_block --block-size $size..." >&2
		"$BENCH_BLOCK" --uri "$URI" --block-size "$size" --output "$TMP_OUTPUT" $EXTRA_ARGS
	done
else
	echo "Skipping bench_block: not found or not executable at $BENCH_BLOCK" >&2
fi

if [ -n "$UTILS_BIN_DIR" ]; then
	echo "Running bench_cli (iio_info/iio_attr wall-clock time)..." >&2
	"$SCRIPT_DIR/bench_cli.sh" --bin-dir "$UTILS_BIN_DIR" --uri "$URI" --output "$TMP_OUTPUT"
fi

if [ -z "$BOARD" ] && [ -f "$TMP_OUTPUT" ]; then
	BOARD=$(head -n1 "$TMP_OUTPUT" | sed -n 's/.*"board":"\([^"]*\)".*/\1/p')
fi

if [ -n "$BOARD" ] && [ "$BOARD" != "unknown" ]; then
	# Sanitize for a filename: squeeze non-alnum runs to one '_', trim edges.
	BOARD_SLUG=$(printf '%s' "$BOARD" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')
	BOARD_SLUG=$(printf '%s' "$BOARD_SLUG" | sed 's/^_*//; s/_*$//')
	OUTPUT="$RESULTS_DIR/${TIMESTAMP}_${SHORTSHA}_${BOARD_SLUG}.json"
else
	OUTPUT="$RESULTS_DIR/${TIMESTAMP}_${SHORTSHA}.json"
fi

mv "$TMP_OUTPUT" "$OUTPUT"
echo "Results written to $OUTPUT"
