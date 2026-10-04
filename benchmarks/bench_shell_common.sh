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
