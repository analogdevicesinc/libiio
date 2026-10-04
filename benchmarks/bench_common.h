/* SPDX-License-Identifier: MIT */
/*
 * bench_common - shared helpers for the libiio benchmark suite
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stddef.h>

struct iio_context;
struct iio_device;
struct iio_channels_mask;

struct bench_opts {
	const char *uri;
	const char *output;
	unsigned int iterations;
	unsigned int duration_ms;
	size_t block_size;
	unsigned int num_blocks;
	char board[128];
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
 * -b/--block-size -c/--num-blocks -o/--output" options. argv[0] is used as
 * the program name in usage/error messages. Exits the process on -h/--help
 * or a parsing error. */
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

/* Fills opts->board from "hw_carrier"/"hw_model" context attrs, falling
 * back to the context description or "unknown". Call once after creating
 * the context, before any bench_report() calls. */
void bench_detect_board(struct iio_context *ctx, struct bench_opts *opts);

/* Finds the first device with input scan-element channels and a buffer,
 * enabling those channels in a freshly-allocated channels mask (returned
 * via 'mask_out', caller-owned). Returns NULL if none found. Same heuristic
 * as scratch/fast_checks.c's find_input_device(), so benchmarks work
 * against whatever board is plugged in. */
struct iio_device *bench_find_input_device(struct iio_context *ctx,
					    struct iio_channels_mask **mask_out);

#endif /* BENCH_COMMON_H */
