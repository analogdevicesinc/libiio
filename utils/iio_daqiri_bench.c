// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_daqiri_bench - Part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Receive-throughput benchmark. It only uses the public libiio API, so the
 * same run works against daqiri:, ip: or any other backend.
 *
 * The buffer is opened and its blocks queued, the stream is started, and the
 * clock starts when the first block is dequeued: that block is checked but
 * not counted, so the time spent waiting for the sender to start is not part
 * of the rate. Counters (daqiri_* buffer attributes) are read right after the
 * stream started and again at the end; the report holds the difference. The
 * data path that a daqiri: buffer took (daqiri_data_path, udp or tcp) heads
 * the report.
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio-debug.h>
#include <iio/iio.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <dirent.h>
#endif

#include "iio_common.h"

#define MY_NAME "iio_daqiri_bench"
#define MY_OPTS "d:c:s:n:k:j:"

#define DEFAULT_NB_BLOCKS 8
#define MAX_COUNTERS 64
#define MAX_THREADS 256

enum check_mode {
	CHECK_NONE,
	CHECK_RAMP,
};

static const struct option options[] = {
	{ "device", required_argument, 0, 'd' },
	{ "channels", required_argument, 0, 'c' },
	{ "block-size", required_argument, 0, 's' },
	{ "blocks", required_argument, 0, 'n' },
	{ "duration", required_argument, 0, 'T' },
	{ "check", required_argument, 0, 'k' },
	{ "json", required_argument, 0, 'j' },
	{ 0, 0, 0, 0 },
};

static volatile sig_atomic_t app_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	app_running = 0;
}

static void print_help(void)
{
	printf("Usage:\n"
	       "\t" MY_NAME " -u <uri> -d <device> -s <block-bytes> -T <seconds> [OPTION]...\n"
	       "Options:\n"
	       "\t-h, --help\n\t\t\tShow this help and quit.\n"
	       "\t-V, --version\n\t\t\tDisplay libiio version information.\n"
	       "\t-u, --uri [arg]\n\t\t\tUse the context at the provided URI.\n"
	       "\t\t\teg: 'ip:192.168.2.1', 'daqiri:192.168.2.1,data=udp,yaml=f.yaml'\n"
	       "\t-a, --auto <arg>\n\t\t\tUse the only context found by a scan.\n"
	       "\t    --timeout [arg]\n\t\t\tContext timeout in milliseconds.\n"
	       "\t-d, --device [arg]\n\t\t\tDevice to receive from (required).\n"
	       "\t-c, --channels [arg]\n\t\t\tComma separated channels to enable. Default: all scan\n"
	       "\t\t\telements of the first buffer.\n"
	       "\t-s, --block-size [arg]\n\t\t\tBlock size in bytes (required). A multiple of the sample\n"
	       "\t\t\tsize.\n"
	       "\t-n, --blocks [arg]\n\t\t\tNumber of blocks queued. Default: %d.\n"
	       "\t-T, --duration [arg]\n\t\t\tSeconds to measure, from the first block (required).\n"
	       "\t\t\tNote that this is not the context timeout of the other\n"
	       "\t\t\tutilities: use --timeout for that.\n"
	       "\t-k, --check [arg]\n\t\t\t'ramp' verifies the ramp16_le test pattern on host-order\n"
	       "\t\t\tint16 I/Q pairs and counts the bad samples; 'none'\n"
	       "\t\t\t(default) does not look at the data.\n"
	       "\t-j, --json [arg]\n\t\t\tAlso write the result as JSON to this file.\n"
	       "\nThis is free software; see the source for copying conditions.  There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n",
			DEFAULT_NB_BLOCKS);
}

/* ---- CPU time ---------------------------------------------------------- */

struct cpu_snap {
	double wall_s;
	double proc_s; /* user + system; < 0 when unknown */
#ifdef __linux__
	unsigned int nb_threads;
	struct {
		long tid;
		char name[32];
		double cpu_s;
	} thr[MAX_THREADS];
#endif
};

static double now_s(void)
{
	return (double)get_time_us() / 1e6;
}

#ifdef __linux__
/* utime + stime of /proc/self/task/<tid>/stat in seconds, comm in name */
static int read_thread(long tid, char *name, size_t name_len, double *cpu_s)
{
	char path[64], buf[1024], *p, *q;
	unsigned long utime, stime;
	long hz = sysconf(_SC_CLK_TCK);
	size_t len;
	FILE *f;

	snprintf(path, sizeof(path), "/proc/self/task/%ld/stat", tid);
	f = fopen(path, "r");
	if (!f)
		return -errno;
	len = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[len] = '\0';

	/* "tid (comm) S ...": comm may hold spaces and parentheses */
	p = strchr(buf, '(');
	q = strrchr(buf, ')');
	if (!p || !q || q < p || hz <= 0)
		return -EINVAL;

	len = (size_t)(q - p - 1);
	if (len >= name_len)
		len = name_len - 1;
	memcpy(name, p + 1, len);
	name[len] = '\0';

	/* After ") ": state is field 3, utime 14, stime 15: 11 fields on */
	if (sscanf(q + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) !=
			2)
		return -EINVAL;

	*cpu_s = (double)(utime + stime) / (double)hz;

	return 0;
}

static void snap_threads(struct cpu_snap *s)
{
	struct dirent *de;
	DIR *d = opendir("/proc/self/task");

	s->nb_threads = 0;
	if (!d)
		return;

	while ((de = readdir(d)) && s->nb_threads < MAX_THREADS) {
		long tid = strtol(de->d_name, NULL, 10);
		unsigned int i = s->nb_threads;

		if (tid <= 0)
			continue;
		if (read_thread(tid, s->thr[i].name, sizeof(s->thr[i].name), &s->thr[i].cpu_s))
			continue;

		s->thr[i].tid = tid;
		s->nb_threads++;
	}

	closedir(d);
}
#endif

static void cpu_snap(struct cpu_snap *s)
{
	s->wall_s = now_s();
	s->proc_s = -1.0;

#ifndef _WIN32
	{
		struct rusage ru;

		if (!getrusage(RUSAGE_SELF, &ru))
			s->proc_s = (double)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
				    (double)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
	}
#endif
#ifdef __linux__
	snap_threads(s);
#endif
}

static double cpu_pct(const struct cpu_snap *a, const struct cpu_snap *b)
{
	double wall = b->wall_s - a->wall_s;

	if (!a->wall_s || a->proc_s < 0 || wall <= 0)
		return -1.0;

	return 100.0 * (b->proc_s - a->proc_s) / wall;
}

/* ---- Latency ----------------------------------------------------------- */

struct lat {
	uint32_t *us;
	size_t n, cap;
};

static int lat_add(struct lat *l, uint64_t us)
{
	if (l->n == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 65536;
		uint32_t *p = realloc(l->us, cap * sizeof(*p));

		if (!p)
			return -ENOMEM;
		l->us = p;
		l->cap = cap;
	}

	l->us[l->n++] = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;

	return 0;
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* Nearest rank, in milliseconds */
static double lat_pct(const struct lat *l, double pct)
{
	size_t rank;

	if (!l->n)
		return 0.0;

	rank = (size_t)((pct / 100.0) * (double)l->n + 0.999999);
	if (rank < 1)
		rank = 1;
	if (rank > l->n)
		rank = l->n;

	return (double)l->us[rank - 1] / 1000.0;
}

/* ---- ramp16_le --------------------------------------------------------- */

/*
 * ramp16_le (tests/daqiri/gen_golden.py), the test pattern of the board's UDP
 * framer: sample n has I = n & 0x7FFF and Q = (0x8000 - n) & 0xFFFF, both
 * little-endian 16-bit words, I at offset 0 and Q at offset 2. UDP sample mode
 * hands the payload over unchanged, so on a little-endian host they are the
 * host-order words read here.
 *
 * <next> is the index the first sample of the block should have. The first
 * block of a run, and the first after a dequeue error, seed it from their own
 * first sample (from Q, which holds n modulo 65536, where I only holds 15
 * bits), because the stream may be joined anywhere. A block that does not
 * continue the previous one is counted against the previous one, then the
 * pattern is joined again from its own first sample: one lost packet costs the
 * samples of one block, not of the rest of the run.
 *
 * Returns the number of samples that do not follow the ramp.
 */
static uint64_t ramp_check(const void *data, size_t bytes, uint32_t *next, bool seed)
{
	const uint8_t *p = data;
	uint64_t bad = 0;
	size_t k, nb = bytes / 4;
	uint16_t i, q;
	uint32_t n, own;

	if (!nb)
		return 0;

	memcpy(&q, p + 2, 2);
	own = (0x8000u - q) & 0xFFFF;
	if (seed)
		*next = own;

	for (k = 0, n = *next; k < nb; k++, n++) {
		memcpy(&i, p + 4 * k, 2);
		memcpy(&q, p + 4 * k + 2, 2);
		if (i != (uint16_t)(n & 0x7FFF) || q != (uint16_t)((0x8000 - n) & 0xFFFF))
			bad++;
	}

	/* Not a continuation (or not a ramp at all): follow the block */
	*next = (own + (uint32_t)nb) & 0xFFFF;
	if (bad == 0)
		*next = n;

	return bad;
}

/* ---- Counters ---------------------------------------------------------- */

struct counter {
	const struct iio_attr *attr;
	const char *name;
	char val[64];
	long long base, end;
	bool numeric;
	bool delta; /* monotonic counter: reported as end - base */
};

static bool has_prefix(const char *s, const char *prefix)
{
	return !strncmp(s, prefix, strlen(prefix));
}

static void counter_read(struct counter *c, bool is_base)
{
	char buf[64];
	char *end;
	ssize_t ret = iio_attr_read_raw(c->attr, buf, sizeof(buf));
	long long v;

	if (ret <= 0) {
		strcpy(c->val, "n/a");
		c->numeric = false;
		return;
	}

	buf[sizeof(buf) - 1] = '\0';
	/* Drop trailing whitespace */
	for (end = buf + strlen(buf); end > buf && (end[-1] == '\n' || end[-1] == ' '); end--)
		end[-1] = '\0';

	v = strtoll(buf, &end, 10);
	c->numeric = buf[0] && !*end;
	if (c->numeric) {
		if (is_base)
			c->base = v;
		else
			c->end = v;
	}
	if (!is_base)
		snprintf(c->val, sizeof(c->val), "%s", buf);
}

/* The daqiri_* buffer attributes; only listed when daqiri_rx_* is there */
static unsigned int counters_find(const struct iio_buffer *buf, struct counter *cs)
{
	unsigned int i, n = 0, nb = iio_buffer_get_attrs_count(buf);
	bool rx = false;

	for (i = 0; i < nb; i++) {
		const char *name = iio_attr_get_name(iio_buffer_get_attr(buf, i));

		if (has_prefix(name, "daqiri_rx_"))
			rx = true;
	}
	if (!rx)
		return 0;

	for (i = 0; i < nb && n < MAX_COUNTERS; i++) {
		const struct iio_attr *attr = iio_buffer_get_attr(buf, i);
		const char *name = iio_attr_get_name(attr);

		if (!has_prefix(name, "daqiri_"))
			continue;

		memset(&cs[n], 0, sizeof(cs[n]));
		cs[n].attr = attr;
		cs[n].name = name;
		/* daqiri_rx_socket_buffer is a size, not a count */
		cs[n].delta = (has_prefix(name, "daqiri_rx_") ||
					      has_prefix(name, "daqiri_blocks_")) &&
			      strcmp(name, "daqiri_rx_socket_buffer");
		n++;
	}

	return n;
}

/* The path a daqiri: buffer streams on (daqiri_data_path: udp or tcp), into
 * <dst>; NULL when the buffer has no such attribute */
static const char *data_path_read(const struct iio_buffer *buf, char *dst, size_t len)
{
	const struct iio_attr *attr = iio_buffer_find_attr(buf, "daqiri_data_path");
	ssize_t ret;

	if (!attr)
		return NULL;

	ret = iio_attr_read_raw(attr, dst, len);
	if (ret <= 0)
		snprintf(dst, len, "n/a");

	return dst;
}

/* ---- Output ------------------------------------------------------------ */

static void json_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			fprintf(f, "\\%c", *s);
		else if ((unsigned char)*s < 0x20)
			fprintf(f, "\\u%04x", *s);
		else
			fputc(*s, f);
	}
	fputc('"', f);
}

struct result {
	const char *uri, *device;
	const char *data_path; /* daqiri_data_path; NULL when the buffer has none */
	size_t block_bytes;
	double duration_s;
	uint64_t bytes, blocks;
	double gbps, blocks_per_s, p50_ms, p99_ms, cpu_pct;
	const char *check;
	uint64_t bad_samples;
	struct counter *counters;
	unsigned int nb_counters;
};

static void write_json(FILE *f, const struct result *r)
{
	unsigned int i;

	fprintf(f, "{\n  \"schema_version\": 1,\n  \"uri\": ");
	json_str(f, r->uri);
	fprintf(f, ",\n  \"device\": ");
	json_str(f, r->device);
	fprintf(f, ",\n  \"data_path\": ");
	if (r->data_path)
		json_str(f, r->data_path);
	else
		fprintf(f, "null");
	fprintf(f, ",\n  \"block_bytes\": %zu,\n  \"duration_s\": %.6f,\n", r->block_bytes,
			r->duration_s);
	fprintf(f, "  \"bytes\": %" PRIu64 ",\n  \"blocks\": %" PRIu64 ",\n", r->bytes, r->blocks);
	fprintf(f, "  \"gbps\": %.6f,\n  \"blocks_per_s\": %.3f,\n", r->gbps, r->blocks_per_s);
	fprintf(f, "  \"dequeue_ms\": { \"p50\": %.6f, \"p99\": %.6f },\n", r->p50_ms, r->p99_ms);
	fprintf(f, "  \"cpu_pct\": %.3f,\n  \"counters\": {", r->cpu_pct);

	for (i = 0; i < r->nb_counters; i++) {
		const struct counter *c = &r->counters[i];

		fprintf(f, "%s\n    \"%s\": ", i ? "," : "", c->name);
		if (c->numeric)
			fprintf(f, "%lld", c->delta ? c->end - c->base : c->end);
		else
			json_str(f, c->val);
	}
	fprintf(f, "%s},\n", r->nb_counters ? "\n  " : "");

	fprintf(f, "  \"check\": { \"mode\": \"%s\", \"bad_samples\": %" PRIu64 " }\n}\n", r->check,
			r->bad_samples);
}

static void print_report(const struct result *r)
{
	unsigned int i;

	printf("uri            %s\n", r->uri);
	printf("device         %s\n", r->device);
	if (r->data_path)
		printf("data_path      %s\n", r->data_path);
	printf("block_bytes    %zu\n", r->block_bytes);
	printf("duration_s     %.3f\n", r->duration_s);
	printf("bytes          %" PRIu64 "\n", r->bytes);
	printf("blocks         %" PRIu64 "\n", r->blocks);
	printf("gbps           %.4f\n", r->gbps);
	printf("blocks_per_s   %.1f\n", r->blocks_per_s);
	printf("dequeue_p50_ms %.4f\n", r->p50_ms);
	printf("dequeue_p99_ms %.4f\n", r->p99_ms);
	if (r->cpu_pct < 0)
		printf("cpu_pct        n/a\n");
	else
		printf("cpu_pct        %.1f\n", r->cpu_pct);
	if (!strcmp(r->check, "ramp"))
		printf("bad_samples    %" PRIu64 "\n", r->bad_samples);

	for (i = 0; i < r->nb_counters; i++) {
		const struct counter *c = &r->counters[i];

		if (c->numeric)
			printf("%-30s %lld\n", c->name, c->delta ? c->end - c->base : c->end);
		else
			printf("%-30s %s\n", c->name, c->val);
	}
}

/* ---- Setup ------------------------------------------------------------- */

/* Enable the comma separated channels of <list> in <mask>, or every input
 * scan element of <dev> when there is no list. Not taken from the buffer's own
 * scan element list: a device whose XML has no <buffer> element has none. */
static int enable_channels(const struct iio_device *dev, char *list, struct iio_channels_mask *mask)
{
	unsigned int i, nb = iio_device_get_channels_count(dev);
	char *save = NULL, *tok;
	const struct iio_channel *ch;

	if (!list) {
		for (i = 0; i < nb; i++) {
			ch = iio_device_get_channel(dev, i);
			if (iio_channel_is_scan_element(ch) && !iio_channel_is_output(ch))
				iio_channel_enable(ch, mask);
		}
		return 0;
	}

	for (tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		ch = iio_device_find_channel(dev, tok, false);
		if (!ch) {
			fprintf(stderr, "Input channel \"%s\" not found\n", tok);
			return -ENXIO;
		}
		if (!iio_channel_is_scan_element(ch)) {
			fprintf(stderr, "Channel \"%s\" is not a scan element\n", tok);
			return -EINVAL;
		}

		iio_channel_enable(ch, mask);
	}

	return 0;
}

/* argv without -T / --duration and their values: the common option parser
 * would take them for the context timeout */
static char **strip_duration(char **argw, int argc, int *new_argc)
{
	char **out = xmalloc(((size_t)argc + 1) * sizeof(char *), MY_NAME);
	int i, n = 0;

	if (!out)
		exit(EXIT_FAILURE);

	for (i = 0; i < argc; i++) {
		if (!strcmp(argw[i], "-T") || !strcmp(argw[i], "--duration")) {
			i++;
			continue;
		}
		if ((argw[i][0] == '-' && argw[i][1] == 'T') || has_prefix(argw[i], "--duration="))
			continue;

		out[n++] = argw[i];
	}
	out[n] = NULL;
	*new_argc = n;

	return out;
}

int main(int argc, char **argv)
{
	char **argw, **cargv;
	const char *dev_arg = NULL, *json_path = NULL, *uri_arg = NULL;
	char *chn_arg = NULL;
	size_t block_bytes = 0, nb_blocks = DEFAULT_NB_BLOCKS;
	double duration = 0;
	enum check_mode check = CHECK_NONE;
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_buffer *buffer;
	struct iio_buffer_stream *stream = NULL;
	struct iio_channels_mask *mask = NULL;
	struct iio_block **blocks = NULL;
	struct counter counters[MAX_COUNTERS];
	unsigned int nb_counters = 0;
	char data_path_buf[16];
	const char *data_path = NULL;
	struct cpu_snap *c0 = NULL, *c1 = NULL;
	struct lat lat = { 0 };
	struct result res;
	struct option *opts;
	ssize_t sample_size;
	uint64_t bytes = 0, blocks_done = 0, bad = 0;
	double t_first = 0, t_last = 0;
	uint32_t next_n = 0;
	bool seed = true, got_first = false, run_failed = false;
	size_t i, nb_created = 0;
	int c, cargc, ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	for (i = 1; i < (size_t)argc; i++) {
		if (!strcmp(argw[i], "-h") || !strcmp(argw[i], "--help")) {
			print_help();
			free_argw(argc, argw);
			return EXIT_SUCCESS;
		}
	}

	cargv = strip_duration(argw, argc, &cargc);
	ctx = handle_common_opts(MY_NAME, cargc, cargv, MY_OPTS, options, NULL, NULL, &ret);
	free(cargv);

	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		goto err_free_argw;
	}

	optind = 1;
	for (;;) {
		int idx = -1;

		c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS, /* Flawfinder: ignore */
				opts, &idx);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case 'V':
			break;
		case 'T':
			/* -T and --duration are the duration; --timeout is the
			 * context timeout, which the common parser handled */
			if (idx < 0 || strcmp(opts[idx].name, "timeout"))
				duration = strtod(optarg, NULL);
			break;
		case 'u':
			uri_arg = optarg;
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argw[optind] != NULL &&
					argw[optind][0] != '-')
				optind++;
			break;
		case 'd':
			dev_arg = optarg;
			break;
		case 'c':
			chn_arg = optarg;
			break;
		case 's':
			block_bytes = sanitize_clamp("block size", optarg, 1, SIZE_MAX / 2);
			break;
		case 'n':
			nb_blocks = sanitize_clamp("number of blocks", optarg, 1, 4096);
			break;
		case 'k':
			if (!strcmp(optarg, "ramp")) {
				check = CHECK_RAMP;
			} else if (strcmp(optarg, "none")) {
				fprintf(stderr, "--check takes 'ramp' or 'none'\n");
				free(opts);
				goto err_free_ctx;
			}
			break;
		case 'j':
			json_path = optarg;
			break;
		case '?':
		default:
			fprintf(stderr, "Unknown argument\n");
			free(opts);
			goto err_free_ctx;
		}
	}
	free(opts);

	if (!ctx)
		goto err_free_argw;

	if (!dev_arg || !block_bytes || !(duration > 0)) {
		fprintf(stderr, "-d <device>, -s <block-bytes> and -T <seconds> are required\n");
		goto err_free_ctx;
	}

	dev = iio_context_find_device(ctx, dev_arg);
	if (!dev) {
		fprintf(stderr, "Device %s not found\n", dev_arg);
		goto err_free_ctx;
	}

	buffer = iio_device_get_buffer(dev, 0);
	if (!buffer || iio_buffer_is_output(buffer)) {
		fprintf(stderr, "Device %s has no input buffer\n", dev_arg);
		goto err_free_ctx;
	}

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!mask) {
		fprintf(stderr, "Unable to create channels mask\n");
		goto err_free_ctx;
	}
	if (enable_channels(dev, chn_arg, mask))
		goto err_free_ctx;

	sample_size = iio_device_get_sample_size(dev, mask);
	if (sample_size <= 0) {
		if (sample_size < 0)
			dev_perror(dev, (int)sample_size, "Unable to get the sample size");
		else
			fprintf(stderr, "The enabled channels have no samples\n");
		goto err_free_ctx;
	}
	if (block_bytes % (size_t)sample_size) {
		fprintf(stderr, "Block size %zu is not a multiple of the sample size (%zd)\n",
				block_bytes, sample_size);
		goto err_free_ctx;
	}
	if (check == CHECK_RAMP && sample_size != 4) {
		fprintf(stderr, "--check ramp needs 4-byte samples (an I/Q pair of int16), got %zd\n",
				sample_size);
		goto err_free_ctx;
	}

	c0 = calloc(1, sizeof(*c0));
	c1 = calloc(1, sizeof(*c1));
	blocks = calloc(nb_blocks, sizeof(*blocks));
	if (!c0 || !c1 || !blocks) {
		fprintf(stderr, "Out of memory\n");
		goto err_free_ctx;
	}

#ifndef _WIN32
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
#endif

	stream = iio_buffer_open(buffer, mask);
	ret = iio_err(stream);
	if (ret) {
		dev_perror(dev, ret, "Unable to open buffer");
		stream = NULL;
		ret = EXIT_FAILURE;
		goto err_free_ctx;
	}

	/* Settled at the open */
	data_path = data_path_read(buffer, data_path_buf, sizeof(data_path_buf));

	for (i = 0; i < nb_blocks; i++) {
		blocks[i] = iio_buffer_stream_create_block(stream, block_bytes);
		ret = iio_err(blocks[i]);
		if (ret) {
			dev_perror(dev, ret, "Unable to create block %zu", i);
			ret = EXIT_FAILURE;
			goto err_destroy;
		}
		nb_created++;

		ret = iio_block_enqueue(blocks[i], 0, false);
		if (ret) {
			dev_perror(dev, ret, "Unable to enqueue block %zu", i);
			ret = EXIT_FAILURE;
			goto err_destroy;
		}
	}

	ret = iio_buffer_stream_start(stream);
	if (ret) {
		dev_perror(dev, ret, "Unable to start the stream");
		ret = EXIT_FAILURE;
		goto err_destroy;
	}

	/* After the start: packets that arrived earlier were dropped */
	nb_counters = counters_find(buffer, counters);
	for (i = 0; i < nb_counters; i++)
		counter_read(&counters[i], true);

	fprintf(stderr, "ready\n");

	for (i = 0; app_running;) {
		struct iio_block *b = blocks[i];
		uint64_t t0 = get_time_us(), t1;

		ret = iio_block_dequeue(b, false);
		t1 = get_time_us();

		if (ret && ret != -EIO) {
			if (app_running)
				dev_perror(dev, ret, "Dequeue failed");
			run_failed = app_running;
			break;
		}

		if (lat_add(&lat, t1 - t0)) {
			fprintf(stderr, "Out of memory\n");
			break;
		}

		if (!got_first) {
			got_first = true;
			t_first = now_s();
		} else {
			blocks_done += ret ? 0 : 1;
			bytes += ret ? 0 : block_bytes;
			t_last = now_s();
		}

		if (ret) {
			/* Lost data (lossy=0): resynchronise the pattern */
			seed = true;
		} else if (check == CHECK_RAMP) {
			bad += ramp_check(iio_block_start(b), block_bytes, &next_n, seed);
			seed = false;
		}

		if (got_first && now_s() - t_first >= duration)
			break;

		ret = iio_block_enqueue(b, 0, false);
		if (ret) {
			dev_perror(dev, ret, "Enqueue failed");
			run_failed = true;
			break;
		}
		i = (i + 1) % nb_blocks;

		/* Only now: the snapshot reads /proc and must not hold up the
		 * block that is queued again */
		if (!c0->wall_s)
			cpu_snap(c0);
	}

	if (!got_first) {
		fprintf(stderr, "No block received\n");
		ret = EXIT_FAILURE;
		goto err_destroy;
	}

	/* Before the buffer is closed: the pollers still exist */
	cpu_snap(c1);
	for (i = 0; i < nb_counters; i++)
		counter_read(&counters[i], false);

	qsort(lat.us, lat.n, sizeof(*lat.us), cmp_u32);

	memset(&res, 0, sizeof(res));
	if (!uri_arg) {
		const struct iio_attr *attr = iio_context_find_attr(ctx, "uri");

		uri_arg = attr ? iio_attr_get_static_value(attr) : NULL;
	}
	res.uri = uri_arg ? uri_arg : "";
	res.device = dev_arg;
	res.data_path = data_path;
	res.block_bytes = block_bytes;
	res.duration_s = t_last - t_first;
	res.bytes = bytes;
	res.blocks = blocks_done;
	if (res.duration_s > 0) {
		res.gbps = (double)bytes * 8.0 / res.duration_s / 1e9;
		res.blocks_per_s = (double)blocks_done / res.duration_s;
	}
	res.p50_ms = lat_pct(&lat, 50.0);
	res.p99_ms = lat_pct(&lat, 99.0);
	res.cpu_pct = cpu_pct(c0, c1);
	res.check = check == CHECK_RAMP ? "ramp" : "none";
	res.bad_samples = bad;
	res.counters = counters;
	res.nb_counters = nb_counters;

	print_report(&res);

#ifdef __linux__
	{
		unsigned int a, b;
		double wall = c0->wall_s ? c1->wall_s - c0->wall_s : 0.0;

		for (b = 0; b < c1->nb_threads; b++) {
			double used = c1->thr[b].cpu_s;

			for (a = 0; a < c0->nb_threads; a++)
				if (c0->thr[a].tid == c1->thr[b].tid)
					used -= c0->thr[a].cpu_s;

			/* Threads that started during the run count from zero */
			printf("thread %-8ld %-16s cpu_pct %.1f\n", c1->thr[b].tid, c1->thr[b].name,
					wall > 0 ? 100.0 * used / wall : 0.0);
		}
	}
#endif

	ret = run_failed ? EXIT_FAILURE : EXIT_SUCCESS;
	if (json_path) {
		FILE *f = fopen(json_path, "w");

		if (!f) {
			fprintf(stderr, "Unable to write %s\n", json_path);
			ret = EXIT_FAILURE;
		} else {
			write_json(f, &res);
			if (fclose(f)) {
				fprintf(stderr, "Unable to write %s\n", json_path);
				ret = EXIT_FAILURE;
			}
		}
	}

err_destroy:
	iio_buffer_stream_cancel(stream);
	while (nb_created)
		iio_block_destroy(blocks[--nb_created]);
	iio_buffer_close(stream);
	stream = NULL;
err_free_ctx:
	if (stream)
		iio_buffer_close(stream);
	if (mask)
		iio_channels_mask_destroy(mask);
	if (ctx)
		iio_context_destroy(ctx);
	free(lat.us);
	free(blocks);
	free(c0);
	free(c1);
err_free_argw:
	free_argw(argc, argw);
	return ret;
}
