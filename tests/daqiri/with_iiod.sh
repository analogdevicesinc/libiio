#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Analog Devices, Inc.
#
# Run a command against an iiod serving the emulated context of <xml> on
# 127.0.0.1:30431.
# Usage: with_iiod.sh [--iiod-args <arg>... --] <xml> <cmd...>
#        The arguments between --iiod-args and -- go to iiod:
#        with_iiod.sh --iiod-args --udp-port 25102 -- <xml> <cmd...>
# Env:   IIOD  path to the iiod binary (default: iiod from PATH)
#
# iiod does not say when it listens: the command has to retry its first
# connection. iiod cannot bind a port that another server holds and exits
# then, which fails the run even if the command was happy with that server.
# Exits with the command's status. iiod's output is shown only when something
# fails.
set -u

PORT=30431

usage() {
	echo "usage: $0 [--iiod-args <arg>... --] <xml> <cmd...>" >&2
	exit 2
}

# iiod's own arguments, each one single-quoted for the eval below
iiod_args=
if [ "${1-}" = --iiod-args ]; then
	shift
	while [ $# -gt 0 ] && [ "$1" != -- ]; do
		iiod_args="$iiod_args '$(printf '%s' "$1" | sed "s/'/'\\\\''/g")'"
		shift
	done
	[ $# -gt 0 ] || usage
	shift
fi

[ $# -ge 2 ] || usage
xml=$1
shift
iiod=${IIOD:-iiod}

log=$(mktemp "${TMPDIR:-/tmp}/with_iiod.XXXXXX") || exit 1
# exec, so that $! is iiod itself
(eval "set -- $iiod_args" && exec "$iiod" -p "$PORT" "$@" -u "emu:$xml") >"$log" 2>&1 &
pid=$!

# Whether iiod still runs: a child that has exited stays a zombie, which
# kill -0 still finds, until it is waited for
running() {
	case "$(cut -d' ' -f3 "/proc/$pid/stat" 2>/dev/null)" in
	'' | Z) return 1 ;;
	esac
	return 0
}

# iiod may take minutes to exit on SIGTERM: where no Avahi daemon answers, its
# DNS-SD thread sleeps between its attempts and is waited for. Give it two
# seconds, then kill it.
cleanup() {
	if kill "$pid" 2>/dev/null; then
		tries=0
		while running && [ "$tries" -lt 20 ]; do
			tries=$((tries + 1))
			sleep 0.1
		done
		kill -9 "$pid" 2>/dev/null
		wait "$pid" 2>/dev/null
	fi
	rm -f "$log"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

"$@"
rc=$?

if ! running; then
	echo "$0: iiod exited during the run; its log follows" >&2
	cat "$log" >&2
	[ "$rc" -ne 0 ] || rc=1
elif [ "$rc" -ne 0 ]; then
	echo "$0: command exited with $rc; iiod log follows" >&2
	cat "$log" >&2
fi

exit "$rc"
