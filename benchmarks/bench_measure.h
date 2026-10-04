/* SPDX-License-Identifier: MIT */
/*
 * bench_measure - iio-agnostic timing/stats/JSON-reporting core, shared by
 * every benchmark binary regardless of which libiio API (v0 or v1) it links
 * against. Deliberately has no #include <iio/iio.h> and no iio types, so it
 * compiles standalone against a v0-only toolchain - see bench_common.h for
 * the v1-specific discovery helpers built on top of this.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef BENCH_MEASURE_H
#define BENCH_MEASURE_H

#include <stddef.h>

#define BENCH_MAX_TAGS 8

struct bench_tag {
	char key[32];
	char value[64];
};

struct bench_opts {
	const char *uri;
	const char *output;
	unsigned int iterations;
	unsigned int duration_ms;
	size_t block_size;
	unsigned int num_blocks;
	char board[128];
	struct bench_tag tags[BENCH_MAX_TAGS];
	unsigned int num_tags;
};

struct bench_stats {
	unsigned int count;
	double min_us;
	double max_us;
	double mean_us;
	double median_us;
	double p95_us;
};

/* Extra structured metadata for a bench_report_ex() record, beyond the
 * common name/unit/stats. Use -1 for a field that doesn't apply. */
struct bench_extra {
	long long block_size;
	long long ring_depth;
};

/* Parses the common "-u/--uri -n/--iterations -d/--duration-ms
 * -b/--block-size -c/--num-blocks -o/--output --tag key=value" options.
 * --tag is repeatable (up to BENCH_MAX_TAGS) and is folded into the run's
 * meta header, e.g. --tag protocol=v0. argv[0] is used as the program name
 * in usage/error messages. Exits the process on -h/--help or a parsing
 * error. */
void bench_parse_opts(int argc, char *argv[], struct bench_opts *opts);

/* Monotonic clock timestamp in microseconds, suitable for delta timing. */
double bench_now_us(void);

/* Computes min/max/mean/median/p95 over 'samples' (in microseconds).
 * Sorts 'samples' in place. */
void bench_compute_stats(double *samples, unsigned int count, struct bench_stats *out);

/* Appends one JSON result record to 'opts->output' (or prints to stdout if
 * NULL). 'name' identifies the benchmark, 'unit' the unit of the reported
 * values ("us" for latency, "MiB/s" for throughput, etc). If the output is
 * empty (a fresh file), a one-line run-metadata header ("type":"meta") is
 * written first, so a run's timestamp/git_sha/host/board/uri appear once
 * per file instead of once per record. */
void bench_report(const struct bench_opts *opts, const char *name,
		   const char *unit, const struct bench_stats *stats);

/* Same as bench_report(), with extra structured fields (e.g. block size,
 * ring depth) included in the record instead of baked into 'name'. */
void bench_report_ex(const struct bench_opts *opts, const char *name,
		      const char *unit, const struct bench_stats *stats,
		      const struct bench_extra *extra);

/* Callback for bench_run_timed()/bench_run_paired(): perform iteration 'i'
 * of whatever is being measured, using 'arg' for per-benchmark state.
 * Returns 0 on success; a nonzero return stops the loop, and the iteration
 * that failed is not counted in the reported stats. */
typedef int (*bench_op_fn)(void *arg, unsigned int i);

/* Times up to 'iterations' calls to 'fn', computes stats over the per-call
 * latencies of the calls that succeeded, and reports them as 'name'/'unit'
 * via bench_report(). Stops early if 'fn' returns nonzero. Returns 0 if at
 * least one iteration succeeded (and was reported), -1 otherwise (OOM, or
 * every iteration failed). */
int bench_run_timed(const struct bench_opts *opts, const char *name, const char *unit,
		     unsigned int iterations, bench_op_fn fn, void *arg);

/* Like bench_run_timed(), but times two operations per iteration ('op_a'
 * then 'op_b') and reports them as two separate metrics sharing the same
 * set of iterations - e.g. create-then-destroy, or dequeue-then-enqueue. An
 * iteration only counts (and is reported) if both ops succeed. */
int bench_run_paired(const struct bench_opts *opts,
		      const char *name_a, const char *name_b, const char *unit,
		      unsigned int iterations, bench_op_fn op_a, bench_op_fn op_b,
		      void *arg);

#endif /* BENCH_MEASURE_H */
