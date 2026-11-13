// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * UDP sample mode: a provider for IIOD_OP_UDP_STREAM_SETUP (iiod/udp-stream.h)
 * that sends the samples of an enabled input buffer as UDP datagrams, paced or
 * not, with drops, reordering and duplicates injected on demand, and writes
 * what it did as a JSON oracle. With --udp-count the stream of an enable ends
 * by itself after that many sequence numbers, dropped ones included, which
 * makes the oracle exact whenever the client looks at it.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* sendmmsg() */
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio-lock.h>
#include <iio/iio.h>
#include <iiod/ops.h>
#include <iiod/udp-stream.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "udp-stream.h"

#define DEFAULT_FROM "127.0.0.1:25102"

/* Datagrams per sendmmsg() */
#define BATCH 32
/* The largest payload served, whatever the client accepts */
#define MAX_PAYLOAD_BYTES 9200
/* payload_bytes is a multiple of this: one 128-bit beat */
#define PAYLOAD_ALIGN 16
/* The longest the sender sleeps before it looks at its stop flag again */
#define SLICE_NS 2000000
/* Sequence numbers the generator looks at per call: with all of them dropped,
 * the sender still gets back to its stop flag */
#define SCAN_MAX 4096

/* What a queued datagram adds to the oracle once it is sent */
enum emu_udp_tag {
	TAG_NONE,
	TAG_COPY, /* the second copy of a duplicated datagram */
	TAG_LATE, /* mK - 1, sent after mK */
};

struct emu_udp_pending {
	uint64_t seq;
	enum emu_udp_tag tag;
};

/* The sequence numbers to send, in order: a reordered pair at most, each
 * datagram of it maybe twice */
struct emu_udp_gen {
	uint64_t next; /* the next sequence number to look at */
	struct emu_udp_pending pend[4];
	unsigned int nb_pend, pos;
	uint64_t dropped, reordered, duplicated;
	bool done; /* --udp-count sequence numbers used up */
};

struct emu_udp {
	const struct emu_udp_cfg *cfg;
	char *dir;		/* the XML's directory, with its '/', or "" */
	struct iio_mutex *lock; /* guards <ids> and the oracle file */
	uint64_t ids;		/* stream ID generator */
};

/* One buffer's stream, in entry->udp_priv */
struct emu_udp_buf {
	struct emu_udp *udp;
	int fd; /* bound to --udp-from by the first setup, closed on release */

	/* The setup, armed until an enable uses it or a disable drops it */
	struct sockaddr_in dst;
	uint32_t stream_id;
	uint16_t payload_bytes;
	bool armed;

	/* The stream */
	struct iio_thrd *thrd;
	struct iio_mutex *lock; /* guards <stop> */
	bool stop;
	uint8_t *samples; /* the sample file */
	size_t samples_len;
	uint8_t *slots; /* BATCH datagrams */
	struct mmsghdr msgs[BATCH];
	struct iovec iov[BATCH];
	struct emu_udp_gen gen;
	uint64_t sent; /* datagrams, copies included */
};

static int option_error(const char *opt, const char *arg, const char *what)
{
	fprintf(stderr, "%s: '%s' %s\n\n", opt, arg, what);

	return -EINVAL;
}

/* A whole decimal or 0x number in [min, max] */
static int parse_u64(const char *opt, const char *s, uint64_t min, uint64_t max, uint64_t *val)
{
	unsigned long long v;
	char *end;
	int base = 10;

	if (!s[0] || s[0] == '-' || s[0] == '+' || s[0] == ' ')
		return option_error(opt, s, "is not a number");
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		base = 16;

	errno = 0;
	v = strtoull(s, &end, base);
	if (errno || *end || end == s)
		return option_error(opt, s, "is not a number");

	if (v < min || v > max) {
		fprintf(stderr, "%s: %s is not in %" PRIu64 "..%" PRIu64 "\n\n", opt, s, min, max);
		return -EINVAL;
	}

	*val = v;

	return 0;
}

static int parse_double(const char *opt, const char *s, double min, double max, double *val)
{
	char *end;
	double v;

	errno = 0;
	v = strtod(s, &end);
	if (errno || *end || end == s || !isfinite(v))
		return option_error(opt, s, "is not a number");

	if (v < min || v > max) {
		fprintf(stderr, "%s: %s is not in %g..%g\n\n", opt, s, min, max);
		return -EINVAL;
	}

	*val = v;

	return 0;
}

static int parse_addr(const char *opt, const char *s, struct emu_udp_cfg *cfg)
{
	const char *colon = strrchr(s, ':');
	char ip[INET_ADDRSTRLEN];
	struct in_addr addr;
	uint64_t port;
	int ret;

	if (!colon || (size_t)(colon - s) >= sizeof(ip))
		return option_error(opt, s, "is not IPv4:PORT");

	memcpy(ip, s, (size_t)(colon - s));
	ip[colon - s] = '\0';

	if (inet_pton(AF_INET, ip, &addr) != 1)
		return option_error(opt, ip, "is not an IPv4 address");

	ret = parse_u64(opt, colon + 1, 0, UINT16_MAX, &port);
	if (ret)
		return ret;

	cfg->from_ip = ntohl(addr.s_addr);
	cfg->from_port = (uint16_t)port;

	return 0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

static int parse_list(const char *opt, const char *s, struct emu_udp_cfg *cfg)
{
	char *dup, *tok, *save = NULL, *p;
	uint64_t *list, v;
	int ret = 0;

	/* strtok_r() skips empty fields: refuse them first */
	if (!s[0] || s[0] == ',' || s[strlen(s) - 1] == ',' || strstr(s, ",,"))
		return option_error(opt, s, "is not a list of datagram numbers");

	dup = strdup(s);
	if (!dup)
		return -ENOMEM;

	for (p = dup; !ret && (tok = strtok_r(p, ",", &save)); p = NULL) {
		ret = parse_u64(opt, tok, 0, UINT64_MAX, &v);
		if (ret)
			break;

		list = realloc(cfg->drop_list, (cfg->drop_list_len + 1) * sizeof(*list));
		if (!list) {
			ret = -ENOMEM;
			break;
		}

		list[cfg->drop_list_len++] = v;
		cfg->drop_list = list;
	}

	free(dup);

	if (!ret)
		qsort(cfg->drop_list, cfg->drop_list_len, sizeof(*cfg->drop_list), cmp_u64);

	return ret;
}

void emu_udp_cfg_init(struct emu_udp_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	parse_addr("--udp-from", DEFAULT_FROM, cfg);
	cfg->seed = 1;
}

void emu_udp_cfg_free(struct emu_udp_cfg *cfg)
{
	free(cfg->drop_list);
	cfg->drop_list = NULL;
	cfg->drop_list_len = 0;
}

int emu_udp_parse_option(struct emu_udp_cfg *cfg, int opt, const char *arg)
{
	if (opt != EMU_UDP_OPT_UDP && opt != EMU_UDP_OPT_MOCK_PROVIDER &&
			opt != EMU_UDP_OPT_MOCK_FAIL_DISABLE)
		cfg->stream_opt = true;

	switch (opt) {
	case EMU_UDP_OPT_UDP:
		cfg->udp = true;
		return 0;
	case EMU_UDP_OPT_FROM:
		return parse_addr("--udp-from", arg, cfg);
	case EMU_UDP_OPT_PPS:
		return parse_u64("--udp-pps", arg, 0, 1000000000, &cfg->pps);
	case EMU_UDP_OPT_DROP_EVERY:
		return parse_u64("--udp-drop-every", arg, 1, UINT64_MAX, &cfg->drop_every);
	case EMU_UDP_OPT_DROP_LIST:
		return parse_list("--udp-drop-list", arg, cfg);
	case EMU_UDP_OPT_DROP_PROB:
		return parse_double("--udp-drop-prob", arg, 0.0, 1.0, &cfg->drop_prob);
	case EMU_UDP_OPT_SEED:
		return parse_u64("--udp-seed", arg, 0, UINT64_MAX, &cfg->seed);
	case EMU_UDP_OPT_REORDER_WINDOW:
		return parse_u64("--udp-reorder-window", arg, 2, UINT64_MAX, &cfg->reorder_window);
	case EMU_UDP_OPT_DUP_EVERY:
		return parse_u64("--udp-dup-every", arg, 1, UINT64_MAX, &cfg->dup_every);
	case EMU_UDP_OPT_JSON:
		cfg->json = arg;
		return 0;
	case EMU_UDP_OPT_COUNT:
		return parse_u64("--udp-count", arg, 1, UINT64_MAX, &cfg->count);
	case EMU_UDP_OPT_MOCK_PROVIDER:
		cfg->mock_log = arg;
		return 0;
	case EMU_UDP_OPT_MOCK_FAIL_DISABLE:
		cfg->mock_disables = true;
		return parse_u64("--udp-mock-fail-disable", arg, 0, UINT64_MAX,
				&cfg->mock_fail_disable);
	default:
		return -EINVAL;
	}
}

void emu_udp_usage(void)
{
	printf("\n"
	       "UDP sample mode, a reference server for the daqiri: backend:\n"
	       "\t--udp\t\t\tAdvertise iiod,udp-stream; a buffer the client sets\n"
	       "\t\t\t\tup for UDP sends, once enabled, the bytes of\n"
	       "\t\t\t\t<device>_buf<n>.bin next to the XML, over and over,\n"
	       "\t\t\t\tas datagrams\n"
	       "\t--udp-from <ip:port>\tSend from there (default: %s)\n"
	       "\t--udp-pps <n>\t\tSend <n> datagrams per second, copies included;\n"
	       "\t\t\t\t0 sends them as fast as possible (default: 0)\n"
	       "\t--udp-drop-every <n>\tDrop datagrams n-1, 2n-1, 3n-1, ...\n"
	       "\t--udp-drop-list <a,b,...>\n"
	       "\t\t\t\tDrop these datagrams\n"
	       "\t--udp-drop-prob <p>\tDrop each datagram with probability <p>, decided\n"
	       "\t\t\t\tby --udp-seed and its number alone\n"
	       "\t--udp-seed <s>\t\tSeed for --udp-drop-prob (default: 1)\n"
	       "\t--udp-reorder-window <k>\n"
	       "\t\t\t\tFor every m >= 1, send datagram mk just before\n"
	       "\t\t\t\tmk-1, unless one of them is dropped\n"
	       "\t--udp-dup-every <n>\tSend datagrams n-1, 2n-1, ... twice in a row\n"
	       "\t--udp-count <n>\t\tEnd the stream of an enable after <n> datagram\n"
	       "\t\t\t\tnumbers, dropped ones included; no pair is reordered\n"
	       "\t\t\t\tacross the end\n"
	       "\t--udp-json <file>\tOn every disable, and when --udp-count ends a\n"
	       "\t\t\t\tstream, write there how many datagrams the last\n"
	       "\t\t\t\tenable sent (copies included), dropped, reordered\n"
	       "\t\t\t\t(pairs) and duplicated\n"
	       "Datagrams are numbered from 0 at every enable. A dropped one keeps its\n"
	       "number, so that the client can tell it is missing.\n",
			DEFAULT_FROM);
}

static uint64_t mix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;

	return x ^ (x >> 31);
}

static uint64_t emu_udp_seed_ids(void)
{
	struct timespec mono, real;
	uint64_t seed = 0;
	FILE *f;

	f = fopen("/dev/urandom", "rb");
	if (f) {
		if (fread(&seed, sizeof(seed), 1, f) != 1)
			seed = 0;
		fclose(f);
	}

	clock_gettime(CLOCK_MONOTONIC, &mono);
	clock_gettime(CLOCK_REALTIME, &real);

	return seed ^ mix64(((uint64_t)getpid() << 32) ^ (uint64_t)real.tv_sec ^
				      ((uint64_t)real.tv_nsec << 20) ^ (uint64_t)mono.tv_nsec);
}

/* Random, never 0, never the buffer's previous one */
static uint32_t emu_udp_new_id(struct emu_udp *udp, uint32_t prev)
{
	uint32_t id;

	iio_mutex_lock(udp->lock);
	do {
		id = (uint32_t)(mix64(++udp->ids) >> 32);
	} while (!id || id == prev);
	iio_mutex_unlock(udp->lock);

	return id;
}

static size_t gcd(size_t a, size_t b)
{
	size_t t;

	while (b) {
		t = a % b;
		a = b;
		b = t;
	}

	return a;
}

static void emu_udp_print_addr(char *buf, size_t len, uint32_t ip, uint16_t port)
{
	snprintf(buf, len, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff,
			ip & 0xff, port);
}

static int emu_udp_bind(const struct emu_udp *udp)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(udp->cfg->from_port),
		.sin_addr.s_addr = htonl(udp->cfg->from_ip),
	};
	char addr[32];
	int fd, err, one = 1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	/* All the buffers' streams come from this one address */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
			bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		err = -errno;
		emu_udp_print_addr(addr, sizeof(addr), udp->cfg->from_ip, udp->cfg->from_port);
		fprintf(stderr, "UDP stream: unable to bind %s: %s\n", addr, strerror(-err));
		close(fd);
		return err;
	}

	return fd;
}

static struct emu_udp_buf *emu_udp_buf_new(struct emu_udp *udp)
{
	struct emu_udp_buf *ub = calloc(1, sizeof(*ub));

	if (!ub)
		return NULL;

	ub->lock = iio_mutex_create();
	if (iio_err(ub->lock)) {
		free(ub);
		return NULL;
	}

	ub->udp = udp;
	ub->fd = -1;

	return ub;
}

static int emu_udp_setup(void *priv, struct buffer_entry *entry,
		const struct iiod_udp_stream_req *req, struct iiod_udp_stream_resp *resp)
{
	struct emu_udp *udp = priv;
	struct emu_udp_buf *ub = entry->udp_priv;
	const struct iio_channels_mask *mask;
	struct sockaddr_in src;
	socklen_t src_len = sizeof(src);
	size_t step, limit, payload;
	char dst[32], from[32];
	ssize_t sample_size;
	int ret;

	if (!entry->buf_stream)
		return -EINVAL;

	/* The samples come from the device: an output buffer has none to send */
	if (entry->is_tx)
		return -ENOTSUP;

	/* The responder answers that itself; better safe */
	if (ub && ub->thrd)
		return -EBUSY;

	mask = iio_buffer_stream_get_channels_mask(entry->buf_stream);
	sample_size = iio_device_get_sample_size(entry->dev, mask);
	if (sample_size <= 0)
		return -ENOTSUP;

	/* The largest multiple of 16 and of the sample-set size that fits */
	step = PAYLOAD_ALIGN / gcd(PAYLOAD_ALIGN, (size_t)sample_size) * (size_t)sample_size;
	limit = req->max_payload < MAX_PAYLOAD_BYTES ? req->max_payload : MAX_PAYLOAD_BYTES;
	payload = limit / step * step;
	if (payload < PAYLOAD_ALIGN)
		return -EINVAL;

	if (!req->dst_ip || !req->dst_port)
		return -EINVAL;

	/* Nothing above touched the buffer's state: a failed setup leaves the
	 * previous one as it was */
	if (!ub) {
		ub = emu_udp_buf_new(udp);
		if (!ub)
			return -ENOMEM;
		entry->udp_priv = ub;
	}

	if (ub->fd < 0) {
		ret = emu_udp_bind(udp);
		if (ret < 0)
			return ret;
		ub->fd = ret;
	}

	if (getsockname(ub->fd, (struct sockaddr *)&src, &src_len) < 0)
		return -errno;

	memset(&ub->dst, 0, sizeof(ub->dst));
	ub->dst.sin_family = AF_INET;
	ub->dst.sin_port = htons(req->dst_port);
	ub->dst.sin_addr.s_addr = htonl(req->dst_ip);
	ub->payload_bytes = (uint16_t)payload;
	ub->stream_id = emu_udp_new_id(udp, ub->stream_id);
	ub->armed = true;

	resp->stream_id = ub->stream_id;
	resp->payload_bytes = ub->payload_bytes;
	resp->src_ip = ntohl(src.sin_addr.s_addr);
	resp->src_port = ntohs(src.sin_port);

	emu_udp_print_addr(dst, sizeof(dst), req->dst_ip, req->dst_port);
	emu_udp_print_addr(from, sizeof(from), resp->src_ip, resp->src_port);
	printf("UDP stream %08" PRIx32 ": %s buffer %u, %s to %s, %u-byte payloads\n",
			ub->stream_id, iio_device_get_id(entry->dev), entry->idx, from, dst,
			ub->payload_bytes);

	return 0;
}

/*
 * The sample file the emu backend serves on the TCP path for this buffer, as
 * emu_make_data_path() names it: <name>_buf<idx>.bin in the XML's directory,
 * with the device's name or, if it has none, its ID, and ':' turned into '_'.
 */
static int emu_udp_load(const struct emu_udp *udp, const struct iio_device *dev, unsigned int idx,
		uint8_t **data, size_t *len)
{
	const char *name = iio_device_get_name(dev);
	size_t dir_len = strlen(udp->dir), path_len, size = 0, n;
	uint8_t *buf = NULL, *p;
	char *path, *c;
	int ret = 0;
	FILE *f;

	if (!name)
		name = iio_device_get_id(dev);

	path_len = dir_len + strlen(name) + 32;
	path = malloc(path_len);
	if (!path)
		return -ENOMEM;

	snprintf(path, path_len, "%s%s_buf%u.bin", udp->dir, name, idx);
	for (c = path + dir_len; *c; c++) {
		if (*c == ':')
			*c = '_';
	}

	f = fopen(path, "rb");
	if (!f) {
		/* As emu_enable_buffer(), which then failed the enable already */
		fprintf(stderr, "UDP stream: no sample file at %s\n", path);
		free(path);
		return -ENOENT;
	}

	for (;;) {
		p = realloc(buf, size + 65536);
		if (!p) {
			ret = -ENOMEM;
			break;
		}
		buf = p;

		n = fread(buf + size, 1, 65536, f);
		size += n;
		if (n < 65536) {
			if (ferror(f))
				ret = -EIO;
			break;
		}
	}

	fclose(f);

	/* The TCP path fails its transfers with -EIO on an empty file */
	if (!ret && !size) {
		fprintf(stderr, "UDP stream: %s is empty\n", path);
		ret = -EIO;
	}

	free(path);

	if (ret) {
		free(buf);
		return ret;
	}

	*data = buf;
	*len = size;

	return 0;
}

static bool emu_udp_dropped(const struct emu_udp_cfg *cfg, uint64_t seq)
{
	double u;

	if (cfg->drop_list_len &&
			bsearch(&seq, cfg->drop_list, cfg->drop_list_len, sizeof(seq), cmp_u64))
		return true;
	if (cfg->drop_every && (seq + 1) % cfg->drop_every == 0)
		return true;
	if (cfg->drop_prob > 0) {
		/* Uniform in [0, 1), from the seed and the number alone */
		u = (double)(mix64(cfg->seed ^ mix64(seq)) >> 11) * (1.0 / 9007199254740992.0);
		return u < cfg->drop_prob;
	}

	return false;
}

static void emu_udp_gen_push(const struct emu_udp_cfg *cfg, struct emu_udp_gen *g, uint64_t seq,
		enum emu_udp_tag tag)
{
	g->pend[g->nb_pend].seq = seq;
	g->pend[g->nb_pend++].tag = tag;

	if (cfg->dup_every && (seq + 1) % cfg->dup_every == 0) {
		g->pend[g->nb_pend].seq = seq;
		g->pend[g->nb_pend++].tag = TAG_COPY;
	}
}

/* Queue the next datagram, or reordered pair, to send. Returns false if all
 * the SCAN_MAX sequence numbers it looked at were dropped, or once the
 * --udp-count ones are used up (g->done). */
static bool emu_udp_gen_refill(const struct emu_udp_cfg *cfg, struct emu_udp_gen *g)
{
	uint64_t seq, k = cfg->reorder_window;
	unsigned int n;

	g->nb_pend = 0;
	g->pos = 0;

	for (n = 0; n < SCAN_MAX; n++) {
		if (cfg->count && g->next >= cfg->count) {
			g->done = true;
			return false;
		}

		seq = g->next++;
		if (emu_udp_dropped(cfg, seq)) {
			g->dropped++;
			continue;
		}

		/* A pair needs both numbers within the count */
		if (k && seq % k == k - 1 && (!cfg->count || seq + 1 < cfg->count) &&
				!emu_udp_dropped(cfg, seq + 1)) {
			g->next++;
			emu_udp_gen_push(cfg, g, seq + 1, TAG_NONE);
			emu_udp_gen_push(cfg, g, seq, TAG_LATE);
		} else {
			emu_udp_gen_push(cfg, g, seq, TAG_NONE);
		}

		return true;
	}

	return false;
}

static bool emu_udp_gen_peek(const struct emu_udp_cfg *cfg, struct emu_udp_gen *g, uint64_t *seq)
{
	if (g->pos == g->nb_pend && !emu_udp_gen_refill(cfg, g))
		return false;

	*seq = g->pend[g->pos].seq;

	return true;
}

static void emu_udp_gen_pop(struct emu_udp_gen *g)
{
	switch (g->pend[g->pos++].tag) {
	case TAG_COPY:
		g->duplicated++;
		break;
	case TAG_LATE:
		g->reordered++;
		break;
	default:
		break;
	}
}

/*
 * The payload of datagram <seq>: the sample file as an endless stream cut into
 * <len>-byte pieces, wrapping around at its end, within a datagram too. A
 * datagram the sender drops still takes its piece. This is the byte stream the
 * emu backend serves on the TCP path, which reads the file from its start at
 * each enable and wraps around at its end (emu.c, emu_dequeue_block()).
 */
static void emu_udp_fill(const uint8_t *data, size_t size, uint64_t seq, size_t len, uint8_t *out)
{
	size_t off = (size_t)((seq % size) * len % size), n;

	while (len) {
		n = size - off < len ? size - off : len;
		memcpy(out, data + off, n);
		out += n;
		len -= n;
		off = 0;
	}
}

static void emu_udp_build(const struct emu_udp_buf *ub, uint64_t seq, uint8_t *dgram)
{
	const struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.flags = 0,
		.payload_length = ub->payload_bytes,
		.stream_id = ub->stream_id,
		.sequence = seq,
	};

	iiod_udp_stream_hdr_pack(&hdr, dgram);
	emu_udp_fill(ub->samples, ub->samples_len, seq, ub->payload_bytes,
			dgram + IIOD_UDP_STREAM_HDR_LEN);
}

static void ts_add_ns(struct timespec *ts, uint64_t ns)
{
	ns += (uint64_t)ts->tv_nsec;
	ts->tv_sec += (time_t)(ns / 1000000000u);
	ts->tv_nsec = (long)(ns % 1000000000u);
}

static bool ts_after(const struct timespec *a, const struct timespec *b)
{
	return a->tv_sec > b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec);
}

/* Datagram <k> is due <k> / <pps> seconds after <t0> */
static struct timespec emu_udp_due(const struct timespec *t0, uint64_t k, uint64_t pps)
{
	struct timespec due = *t0;

	ts_add_ns(&due, k / pps * 1000000000u + k % pps * 1000000000u / pps);

	return due;
}

/* Sleep until <until>, or for SLICE_NS if that is sooner or <until> is NULL */
static void emu_udp_nap(const struct timespec *until)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	ts_add_ns(&t, SLICE_NS);
	if (until && ts_after(&t, until))
		t = *until;

	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR)
		;
}

static bool emu_udp_stopping(struct emu_udp_buf *ub)
{
	bool stop;

	iio_mutex_lock(ub->lock);
	stop = ub->stop;
	iio_mutex_unlock(ub->lock);

	return stop;
}

/* Send the first <nb> slots. A full socket is waited for, a slice at a time,
 * unless the stream is being stopped. Returns 0 or a negative error code. */
static int emu_udp_send(struct emu_udp_buf *ub, unsigned int nb)
{
	unsigned int done = 0;
	ssize_t ret;

	while (done < nb) {
		ret = sendmmsg(ub->fd, ub->msgs + done, nb - done, MSG_DONTWAIT);
		if (ret > 0) {
			done += (unsigned int)ret;
			ub->sent += (uint64_t)ret;
			continue;
		}

		if (ret < 0 && errno == EINTR)
			continue;
		if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)
			return -errno;

		if (emu_udp_stopping(ub))
			break;
		emu_udp_nap(NULL);
	}

	return 0;
}

static void emu_udp_write_oracle(struct emu_udp *udp, const struct emu_udp_buf *ub);

static int emu_udp_sender(void *d)
{
	struct emu_udp_buf *ub = d;
	const struct emu_udp_cfg *cfg = ub->udp->cfg;
	size_t len = IIOD_UDP_STREAM_HDR_LEN + ub->payload_bytes;
	struct emu_udp_gen *g = &ub->gen;
	struct timespec t0, now, due = { 0 };
	unsigned int nb;
	bool waiting;
	uint64_t seq;
	int err = 0;

	clock_gettime(CLOCK_MONOTONIC, &t0);

	while (!err && !emu_udp_stopping(ub)) {
		waiting = false;

		for (nb = 0; nb < BATCH && emu_udp_gen_peek(cfg, g, &seq); nb++) {
			if (cfg->pps) {
				due = emu_udp_due(&t0, ub->sent + nb, cfg->pps);
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (ts_after(&due, &now)) {
					waiting = true;
					break;
				}
			}

			emu_udp_build(ub, seq, ub->slots + nb * len);
			emu_udp_gen_pop(g);
		}

		if (nb)
			err = emu_udp_send(ub, nb);
		else if (g->done)
			break;
		else
			emu_udp_nap(waiting ? &due : NULL);
	}

	/* --udp-count reached: the oracle is final, until the next enable */
	if (!err && g->done) {
		printf("UDP stream %08" PRIx32 " done after %" PRIu64 " datagram numbers: %" PRIu64
		       " sent, %" PRIu64 " dropped, %" PRIu64 " reordered, %" PRIu64
		       " duplicated\n",
				ub->stream_id, cfg->count, ub->sent, g->dropped, g->reordered,
				g->duplicated);
		emu_udp_write_oracle(ub->udp, ub);
	}

	/* Finish a reordered pair or a duplicate already begun, so that what
	 * went out is a whole number of them; one not begun is left out */
	if (!err && g->pos && g->pos < g->nb_pend) {
		for (nb = 0; g->pos < g->nb_pend; nb++) {
			emu_udp_build(ub, g->pend[g->pos].seq, ub->slots + nb * len);
			emu_udp_gen_pop(g);
		}

		err = emu_udp_send(ub, nb);
	}

	if (err)
		fprintf(stderr, "UDP stream %08" PRIx32 ": unable to send: %s\n", ub->stream_id,
				strerror(-err));

	return err;
}

static int emu_udp_start(void *priv, struct buffer_entry *entry)
{
	struct emu_udp *udp = priv;
	struct emu_udp_buf *ub = entry->udp_priv;
	unsigned int i;
	size_t len;
	int ret;

	/* The responder only starts a stream after a successful setup */
	if (!ub || !ub->armed)
		return -EINVAL;
	if (ub->thrd)
		return -EBUSY;

	/* Used up, whatever happens next */
	ub->armed = false;

	ret = emu_udp_load(udp, entry->dev, entry->idx, &ub->samples, &ub->samples_len);
	if (ret)
		return ret;

	len = IIOD_UDP_STREAM_HDR_LEN + ub->payload_bytes;
	ub->slots = malloc(BATCH * len);
	if (!ub->slots) {
		ret = -ENOMEM;
		goto err_free_samples;
	}

	memset(ub->msgs, 0, sizeof(ub->msgs));
	for (i = 0; i < BATCH; i++) {
		ub->iov[i].iov_base = ub->slots + i * len;
		ub->iov[i].iov_len = len;
		ub->msgs[i].msg_hdr.msg_name = &ub->dst;
		ub->msgs[i].msg_hdr.msg_namelen = sizeof(ub->dst);
		ub->msgs[i].msg_hdr.msg_iov = &ub->iov[i];
		ub->msgs[i].msg_hdr.msg_iovlen = 1;
	}

	memset(&ub->gen, 0, sizeof(ub->gen));
	ub->sent = 0;
	ub->stop = false;

	ub->thrd = iio_thrd_create(emu_udp_sender, ub, "iiod-emu-udp");
	ret = iio_err(ub->thrd);
	if (ret) {
		ub->thrd = NULL;
		goto err_free_slots;
	}

	printf("UDP stream %08" PRIx32 " started\n", ub->stream_id);

	return 0;

err_free_slots:
	free(ub->slots);
	ub->slots = NULL;
err_free_samples:
	free(ub->samples);
	ub->samples = NULL;
	return ret;
}

#define ORACLE_FORMAT \
	"{\"sent\": %" PRIu64 ", \"dropped\": %" PRIu64 ", \"reordered\": %" PRIu64 \
	", \"duplicated\": %" PRIu64 "}\n"

/* The counts of the stream that just stopped, replacing the file in one go so
 * that a reader never sees half of it */
static void emu_udp_write_oracle(struct emu_udp *udp, const struct emu_udp_buf *ub)
{
	const char *path = udp->cfg->json;
	size_t len;
	char *tmp;
	FILE *f;
	bool ok;

	if (!path)
		return;

	len = strlen(path) + sizeof(".tmp");
	tmp = malloc(len);
	if (!tmp) {
		fprintf(stderr, "UDP stream: unable to write %s: out of memory\n", path);
		return;
	}
	snprintf(tmp, len, "%s.tmp", path);

	iio_mutex_lock(udp->lock);

	f = fopen(tmp, "w");
	ok = !!f;
	if (f) {
		ok = fprintf(f, ORACLE_FORMAT, ub->sent, ub->gen.dropped, ub->gen.reordered,
				     ub->gen.duplicated) > 0;
		if (fclose(f))
			ok = false;
	}
	if (ok && rename(tmp, path))
		ok = false;
	if (!ok) {
		fprintf(stderr, "UDP stream: unable to write %s: %s\n", path, strerror(errno));
		remove(tmp);
	}

	iio_mutex_unlock(udp->lock);

	free(tmp);
}

static void emu_udp_stop(void *priv, struct buffer_entry *entry)
{
	struct emu_udp *udp = priv;
	struct emu_udp_buf *ub = entry->udp_priv;

	if (!ub)
		return;

	/* A pending setup goes with the stream */
	ub->armed = false;

	if (!ub->thrd)
		return;

	iio_mutex_lock(ub->lock);
	ub->stop = true;
	iio_mutex_unlock(ub->lock);

	iio_thrd_join_and_destroy(ub->thrd);
	ub->thrd = NULL;

	printf("UDP stream %08" PRIx32 " stopped: %" PRIu64 " sent, %" PRIu64 " dropped, %" PRIu64
	       " reordered, %" PRIu64 " duplicated\n",
			ub->stream_id, ub->sent, ub->gen.dropped, ub->gen.reordered,
			ub->gen.duplicated);
	emu_udp_write_oracle(udp, ub);

	free(ub->slots);
	ub->slots = NULL;
	free(ub->samples);
	ub->samples = NULL;
}

static void emu_udp_release(void *priv, struct buffer_entry *entry)
{
	struct emu_udp_buf *ub = entry->udp_priv;

	if (!ub)
		return;

	/* The responder has stopped it already */
	emu_udp_stop(priv, entry);

	if (ub->fd >= 0)
		close(ub->fd);
	iio_mutex_destroy(ub->lock);
	free(ub);

	entry->udp_priv = NULL;
}

const struct iiod_udp_stream_ops emu_udp_ops = {
	.setup = emu_udp_setup,
	.start = emu_udp_start,
	.stop = emu_udp_stop,
	.release = emu_udp_release,
};

struct emu_udp *emu_udp_create(const struct emu_udp_cfg *cfg, const char *xml_path)
{
	const char *sep = strrchr(xml_path, '/');
	size_t dir_len = sep ? (size_t)(sep - xml_path + 1) : 0;
	struct emu_udp *udp;

	udp = calloc(1, sizeof(*udp));
	if (!udp)
		return NULL;

	udp->dir = malloc(dir_len + 1);
	if (!udp->dir)
		goto err_free_udp;

	memcpy(udp->dir, xml_path, dir_len);
	udp->dir[dir_len] = '\0';

	udp->lock = iio_mutex_create();
	if (iio_err(udp->lock))
		goto err_free_dir;

	udp->cfg = cfg;
	udp->ids = emu_udp_seed_ids();

	return udp;

err_free_dir:
	free(udp->dir);
err_free_udp:
	free(udp);
	return NULL;
}

void emu_udp_destroy(struct emu_udp *udp)
{
	if (!udp)
		return;

	iio_mutex_destroy(udp->lock);
	free(udp->dir);
	free(udp);
}
