#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Analog Devices, Inc.
#
# Run a command against an iiod-emu instance serving <xml> on 127.0.0.1:30431.
# Usage: with_emu.sh [--emu-args <arg>... --] <xml> <cmd...>
#        The arguments between --emu-args and -- go to iiod-emu, ahead of the
#        URI: with_emu.sh --emu-args --udp --udp-pps 2000 -- <xml> <cmd...>
# Env:   IIOD_EMU  path to the iiod-emu binary (default: iiod-emu from PATH)
#
# The command runs with IIOD_EMU_LOG set to the file that collects the
# emulator's output, which it may read; the file goes when this script exits.
# Exits with the command's status. The emulator's output is shown only when
# something fails.
#
# The emulator says when it listens, and it cannot bind a port that another
# server holds: this never tests against some other server, and needs no
# more than the shell, coreutils, grep and sed.
set -u

PORT=30431

usage() {
	echo "usage: $0 [--emu-args <arg>... --] <xml> <cmd...>" >&2
	exit 2
}

# The emulator's own arguments, each one single-quoted for the eval below
emu_args=
if [ "${1-}" = --emu-args ]; then
	shift
	while [ $# -gt 0 ] && [ "$1" != -- ]; do
		emu_args="$emu_args '$(printf '%s' "$1" | sed "s/'/'\\\\''/g")'"
		shift
	done
	[ $# -gt 0 ] || usage
	shift
fi

[ $# -ge 2 ] || usage
xml=$1
shift
emu=${IIOD_EMU:-iiod-emu}

log=$(mktemp "${TMPDIR:-/tmp}/with_emu.XXXXXX") || exit 1
# exec, so that $! is the emulator itself
(eval "set -- $emu_args" && exec "$emu" -p "$PORT" "$@" "emu:$xml") >"$log" 2>&1 &
pid=$!

cleanup() {
	kill "$pid" 2>/dev/null && wait "$pid" 2>/dev/null
	rm -f "$log"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

fail() {
	echo "$0: $*" >&2
	cat "$log" >&2
	exit 1
}

# Wait up to 10 s for the emulator to listen: it prints this line, unbuffered,
# once the port is bound, and the one below when another server has it.
tries=0
until grep -q "^Listening on port $PORT;" "$log"; do
	grep -q "^Unable to bind port $PORT:" "$log" &&
		fail "127.0.0.1:$PORT is already in use; refusing to test against it"
	kill -0 "$pid" 2>/dev/null || fail "iiod-emu exited before listening"
	tries=$((tries + 1))
	[ "$tries" -ge 100 ] && fail "iiod-emu not listening on port $PORT after 10 s"
	sleep 0.1
done

IIOD_EMU_LOG=$log "$@"
rc=$?

if [ "$rc" -ne 0 ]; then
	echo "$0: command exited with $rc; iiod-emu log follows" >&2
	cat "$log" >&2
fi

exit "$rc"
