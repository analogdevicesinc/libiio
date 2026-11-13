#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Analog Devices, Inc.
#
# Run the api ctest label through daqiri:, once per XML: each run gets its own
# iiod-emu (with_emu.sh) and TESTS_API_URI pointing at it.
# Usage: run_loopback.sh <build-dir> <test_daqiri_open> [<xml-dir>]
# Env:   IIOD_EMU             see with_emu.sh
#        DAQIRI_LOOPBACK_URI  URI under test (default daqiri:127.0.0.1,data=tcp);
#                             set it to ip:127.0.0.1 for the control run
#
# The api tests skip quietly when a buffer does not open, so before them each
# XML gets test_daqiri_open, which must open a buffer through the URI.
# Stops at the first failing XML and exits non-zero.
set -u

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
	echo "usage: $0 <build-dir> <test_daqiri_open> [<xml-dir>]" >&2
	exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
build=$1
open_test=$2
xmls=${3:-$here/../resources/xmls}
uri=${DAQIRI_LOOPBACK_URI:-daqiri:127.0.0.1,data=tcp}

fail() {
	echo "$0: $*" >&2
	exit 1
}

# -L is a regex: match the api label only, not daqiri-loopback. A ctest that
# selects nothing exits 0, so check the count up front and per run.
count=$(cd "$build" && ctest -N -L '^api$' | sed -n 's/^Total Tests: //p')
case $count in
'' | 0 | *[!0-9]*) fail "no tests labelled api in $build" ;;
esac

log=$(mktemp "${TMPDIR:-/tmp}/run_loopback.XXXXXX") || exit 1
trap 'rm -f "$log"' EXIT
trap 'exit 130' INT TERM

found=0
for xml in "$xmls"/*.xml; do
	[ -e "$xml" ] || continue
	found=1
	name=$(basename "$xml")

	echo "== $name via $uri"

	case $name in
	# No device in these has an input scan element: there is no buffer to
	# open, and test_daqiri_open asserts exactly that.
	event_attrs.xml | hwmon.xml) open_args=--no-buffer ;;
	*) open_args= ;;
	esac

	# shellcheck disable=SC2086 # open_args is empty or one word
	"$here/with_emu.sh" "$xml" env TESTS_API_URI="$uri" "$open_test" $open_args ||
		fail "buffer open check failed for $name via $uri"

	(cd "$build" && "$here/with_emu.sh" "$xml" \
		env TESTS_API_URI="$uri" ctest -L '^api$' -j1 --output-on-failure) >"$log" 2>&1
	rc=$?
	cat "$log"

	[ "$rc" -eq 0 ] || fail "api tests failed for $name via $uri"
	grep -q "No tests were found" "$log" && fail "no api tests ran for $name"
	grep -q "tests passed, 0 tests failed out of $count\$" "$log" ||
		fail "expected $count api tests to pass for $name"
done

[ "$found" -eq 1 ] || fail "no XML files in $xmls"
