/* SPDX-License-Identifier: MIT */
/*
 * bench_measure - iio-agnostic timing/stats/JSON-reporting core, shared by
 * every benchmark binary regardless of which libiio API (v0 or v1) it links
 * against. See bench_measure.h.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_measure.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int cmp_double(const void *a, const void *b)
{
	double da = *(const double *)a, db = *(const double *)b;

	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

double bench_now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

void bench_compute_stats(double *samples, unsigned int count, struct bench_stats *out)
{
	double sum = 0.0, mean;
	unsigned int i;

	memset(out, 0, sizeof(*out));
	if (count == 0)
		return;

	qsort(samples, count, sizeof(*samples), cmp_double);

	for (i = 0; i < count; i++)
		sum += samples[i];
	mean = sum / (double)count;

	out->count = count;
	out->min_us = samples[0];
	out->max_us = samples[count - 1];
	out->mean_us = mean;
	out->median_us = samples[count / 2];
	out->p95_us = samples[(unsigned int)((double)count * 0.95)
			      < count ? (unsigned int)((double)count * 0.95) : count - 1];
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [OPTIONS]\n"
		"  -u, --uri <uri>          Context URI (default: ip:192.168.2.1)\n"
		"  -n, --iterations <N>     Number of iterations (default: 1000)\n"
		"  -d, --duration-ms <N>    Duration in ms, for streaming benchmarks (default: 5000)\n"
		"  -b, --block-size <N>     Block size in bytes, for buffer benchmarks (default: 4096)\n"
		"  -c, --num-blocks <N>     Ring buffer depth, for pipelined benchmarks (default: 4)\n"
		"  -o, --output <path>      Append JSON result to this file (default: stdout)\n"
		"  --tag <key>=<value>      Tag this run in the meta header (repeatable, e.g. --tag protocol=v0)\n"
		"  -h, --help                Show this help\n",
		prog);
}

#define OPT_TAG 256

static void bench_add_tag(struct bench_opts *opts, char *arg, const char *prog)
{
	char *eq = strchr(arg, '=');

	if (opts->num_tags >= BENCH_MAX_TAGS) {
		fprintf(stderr, "%s: too many --tag options (max %d)\n", prog, BENCH_MAX_TAGS);
		exit(EXIT_FAILURE);
	}

	if (!eq || eq == arg || !eq[1]) {
		fprintf(stderr, "%s: --tag expects key=value, got '%s'\n", prog, arg);
		exit(EXIT_FAILURE);
	}

	*eq = 0;
	strncpy(opts->tags[opts->num_tags].key, arg, sizeof(opts->tags[0].key) - 1);
	strncpy(opts->tags[opts->num_tags].value, eq + 1, sizeof(opts->tags[0].value) - 1);
	opts->num_tags++;
}

void bench_parse_opts(int argc, char *argv[], struct bench_opts *opts)
{
	static const struct option longopts[] = {
		{ "uri",		required_argument, 0, 'u' },
		{ "iterations",		required_argument, 0, 'n' },
		{ "duration-ms",	required_argument, 0, 'd' },
		{ "block-size",		required_argument, 0, 'b' },
		{ "num-blocks",		required_argument, 0, 'c' },
		{ "output",		required_argument, 0, 'o' },
		{ "tag",		required_argument, 0, OPT_TAG },
		{ "help",		no_argument,       0, 'h' },
		{ 0, 0, 0, 0 },
	};
	int c;

	/* Kept in sync by hand with the same default in run_all.sh and
	 * bench_cli.sh - see benchmarks/SCHEMA.md. */
	opts->uri = "ip:192.168.2.1";
	opts->output = NULL;
	opts->iterations = 1000;
	opts->duration_ms = 5000;
	opts->block_size = 4096;
	opts->num_blocks = 4;
	opts->num_tags = 0;
	opts->board[0] = 0;

	while ((c = getopt_long(argc, argv, "u:n:d:b:c:o:h", longopts, NULL)) != -1) {
		switch (c) {
		case 'u':
			opts->uri = optarg;
			break;
		case 'n':
			opts->iterations = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'd':
			opts->duration_ms = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'b':
			opts->block_size = (size_t)strtoul(optarg, NULL, 10);
			break;
		case 'c':
			opts->num_blocks = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'o':
			opts->output = optarg;
			break;
		case OPT_TAG:
			bench_add_tag(opts, optarg, argv[0]);
			break;
		case 'h':
			print_usage(argv[0]);
			exit(EXIT_SUCCESS);
		default:
			print_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}
}

static char *bench_git_sha(char *buf, size_t len)
{
	FILE *p = popen("git rev-parse --short HEAD 2>/dev/null", "r");

	buf[0] = 0;
	if (!p)
		return buf;

	if (fgets(buf, (int)len, p)) {
		size_t n = strlen(buf);

		if (n && buf[n - 1] == '\n')
			buf[n - 1] = 0;
	}
	pclose(p);

	return buf;
}

/* Returns non-zero if 'out' is currently empty (a fresh file, or stdout on
 * its first write - never treated as needing a header there since piping
 * concatenates uncontrollably). */
static int file_is_empty(FILE *out)
{
	long pos;

	if (out == stdout)
		return 0;

	if (fseek(out, 0, SEEK_END))
		return 0;

	pos = ftell(out);
	return pos == 0;
}

/* Bumped whenever the shape of the meta/record JSON changes (a field is
 * added, removed, or renamed) so consumers can tell old result files apart
 * from new ones instead of silently misparsing them. See benchmarks/SCHEMA.md.
 * bench_cli.sh's write_meta_header_if_needed() writes the same shape
 * independently in shell and must be kept in sync by hand if this changes. */
#define BENCH_SCHEMA_VERSION 2

static void write_meta_header(FILE *out, const struct bench_opts *opts)
{
	char sha[64], host[256], ts[32];
	time_t now = time(NULL);
	struct tm tm_now;

	bench_git_sha(sha, sizeof(sha));
	if (gethostname(host, sizeof(host)))
		host[0] = 0;
	host[sizeof(host) - 1] = 0;

	gmtime_r(&now, &tm_now);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_now);

	fprintf(out,
		"{\"type\":\"meta\",\"schema_version\":%d,\"timestamp\":\"%s\","
		"\"git_sha\":\"%s\",\"host\":\"%s\",\"board\":\"%s\",\"uri\":\"%s\"",
		BENCH_SCHEMA_VERSION, ts, sha, host,
		opts->board[0] ? opts->board : "unknown", opts->uri);

	if (opts->num_tags > 0) {
		unsigned int i;

		fprintf(out, ",\"tags\":{");
		for (i = 0; i < opts->num_tags; i++) {
			fprintf(out, "%s\"%s\":\"%s\"", i ? "," : "",
				opts->tags[i].key, opts->tags[i].value);
		}
		fprintf(out, "}");
	}

	fprintf(out, "}\n");
}

void bench_report_ex(const struct bench_opts *opts, const char *name,
		      const char *unit, const struct bench_stats *stats,
		      const struct bench_extra *extra)
{
	FILE *out;

	if (opts->output) {
		out = fopen(opts->output, "a");
		if (!out) {
			fprintf(stderr, "Could not open '%s' for append, printing to stdout\n",
				opts->output);
			out = stdout;
		}
	} else {
		out = stdout;
	}

	if (file_is_empty(out))
		write_meta_header(out, opts);

	fprintf(out,
		"{\"name\":\"%s\",\"unit\":\"%s\",\"count\":%u,\"min\":%.3f,"
		"\"max\":%.3f,\"mean\":%.3f,\"median\":%.3f,\"p95\":%.3f",
		name, unit, stats->count, stats->min_us, stats->max_us,
		stats->mean_us, stats->median_us, stats->p95_us);

	if (extra) {
		if (extra->block_size >= 0)
			fprintf(out, ",\"block_size\":%lld", extra->block_size);
		if (extra->ring_depth >= 0)
			fprintf(out, ",\"ring_depth\":%lld", extra->ring_depth);
	}

	fprintf(out, "}\n");

	if (out != stdout)
		fclose(out);
}

void bench_report(const struct bench_opts *opts, const char *name,
		   const char *unit, const struct bench_stats *stats)
{
	bench_report_ex(opts, name, unit, stats, NULL);
}

int bench_run_timed(const struct bench_opts *opts, const char *name, const char *unit,
		     unsigned int iterations, bench_op_fn fn, void *arg)
{
	double *samples = calloc(iterations, sizeof(*samples));
	struct bench_stats stats;
	unsigned int i;

	if (!samples) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	for (i = 0; i < iterations; i++) {
		double t0 = bench_now_us();
		int ret = fn(arg, i);
		double dt = bench_now_us() - t0;

		if (ret)
			break;

		samples[i] = dt;
	}

	if (i > 0) {
		bench_compute_stats(samples, i, &stats);
		bench_report(opts, name, unit, &stats);
	}

	free(samples);
	return i > 0 ? 0 : -1;
}

int bench_run_paired(const struct bench_opts *opts,
		      const char *name_a, const char *name_b, const char *unit,
		      unsigned int iterations, bench_op_fn op_a, bench_op_fn op_b,
		      void *arg)
{
	double *samples_a = calloc(iterations, sizeof(*samples_a));
	double *samples_b = calloc(iterations, sizeof(*samples_b));
	struct bench_stats stats;
	unsigned int i;

	if (!samples_a || !samples_b) {
		fprintf(stderr, "Out of memory\n");
		free(samples_a);
		free(samples_b);
		return -1;
	}

	for (i = 0; i < iterations; i++) {
		double t0 = bench_now_us();
		int ret = op_a(arg, i);
		double dt_a = bench_now_us() - t0;

		if (ret)
			break;

		t0 = bench_now_us();
		ret = op_b(arg, i);
		if (ret)
			break;

		samples_a[i] = dt_a;
		samples_b[i] = bench_now_us() - t0;
	}

	if (i > 0) {
		bench_compute_stats(samples_a, i, &stats);
		bench_report(opts, name_a, unit, &stats);

		bench_compute_stats(samples_b, i, &stats);
		bench_report(opts, name_b, unit, &stats);
	}

	free(samples_a);
	free(samples_b);
	return i > 0 ? 0 : -1;
}
