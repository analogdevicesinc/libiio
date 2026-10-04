# SPDX-License-Identifier: MIT
#
# Shell idioms shared between run_all.sh and bench_cli.sh. Sourced, not
# executed - has no shebang/exec bit of its own.
#
# The two scripts' arg-parsing while/case loops are NOT factored out here:
# they parse different flag sets entirely (run_all.sh: --bin-dir/
# --utils-bin-dir/--uri/--board plus passthrough; bench_cli.sh: --bin-dir/
# --uri/--iterations/--output/--tag with validation/--rwdev-*, erroring on
# unknown flags). Sharing that would need an eval-based dynamic dispatcher,
# which is more fragile than the few duplicated lines it would save in a
# small, hand-run tool - see benchmarks/SCHEMA.md's spirit of "maintainable,
# not bulletproof."

# Prints the current commit's short SHA, or "unknown" outside a git repo /
# in a source tarball. $1, if given, is passed as `git -C $1` (run_all.sh
# needs this since it can be invoked from outside the repo; bench_cli.sh
# runs from the repo root and doesn't).
bench_git_shortsha() {
	if [ -n "$1" ]; then
		git -C "$1" rev-parse --short HEAD 2>/dev/null || echo unknown
	else
		git rev-parse --short HEAD 2>/dev/null || echo unknown
	fi
}

# time_command()/write_meta_header_if_needed() below are shared by
# bench_cli.sh and bench_v0_vs_v1.sh - both time CLI tool invocations and
# write the same JSONL shape. They read $OUTPUT/$TAGS/$ITERATIONS/$SHORTSHA/
# $HOST/$TIMESTAMP/$URI as globals rather than taking them as arguments,
# matching the rest of this file's/bench_cli.sh's style; callers set those
# up (see bench_shell_init()) before calling time_command().
#
# This writes the same JSON shape as write_meta_header() in
# bench_common.c, independently (no shared code between C and shell) -
# keep the two in sync by hand, bumping SCHEMA_VERSION in both places
# together. See benchmarks/SCHEMA.md.
SCHEMA_VERSION=2

# Sets TMPFILE (with an EXIT trap to clean it up), SHORTSHA, HOST, and
# TIMESTAMP for time_command()/write_meta_header_if_needed() to use. Call
# once after parsing args, before the first time_command() call.
bench_shell_init() {
	TMPFILE=$(mktemp)
	trap 'rm -f "$TMPFILE"' EXIT

	SHORTSHA=$(bench_git_shortsha)
	HOST=$(hostname 2>/dev/null || echo unknown)
	TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
}

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
