/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * UDP sample mode end to end: daqiri:127.0.0.1,data=udp over the DAQIRI
 * socket engine (DAQIRI_YAML: socket-udp-loopback-1472.yaml, which receives on
 * 127.0.0.1:25101 from 127.0.0.1:25102 only) against iiod-emu --udp, the
 * reference server, which sends from its default 127.0.0.1:25102.
 *
 * The emulator serves a copy of fmcomms2.xml next to a sample file of its
 * own (<sample.bin>, 4099 bytes, which ctest writes at configure time). For
 * cf-ad9361-lpc voltage0+voltage1 (4-byte samples) it answers the backend's
 * 1456-byte limit (the YAML's max_payload_size, 1472, less the header) with
 * 1456-byte payloads, cut from the file as an endless byte stream: datagram s
 * carries the file's bytes from (s * 1456) mod 4099 on, wrapping around at its
 * end, within a datagram too. Every block is one payload, found to hold the
 * payload of datagram s by comparing every byte with the file, or to be all
 * zero; the payloads of the first MAX_SEQ datagrams all differ.
 *
 * Every case runs against an emulator of its own, which with_emu.sh starts
 * with the case's flags and --udp-json <oracle.json>. Without --case this
 * program is the driver that runs every case that way, or the cases named
 * (burst_sized only when named; it exits 77, skipped, where the host does
 * not grant the receive buffer it needs):
 *   test_udp_loopback <with_emu.sh> <xml> <sample.bin> <oracle.json> [<case>...]
 *   test_udp_loopback --case <name> <sample.bin> <oracle.json>
 *
 * Nothing is left to timing. The emulator ends the stream of an enable by
 * itself after --udp-count datagram numbers, dropped ones included, and
 * writes its oracle then: the datagrams it sent (copies included), dropped,
 * reordered (pairs, counted at the late datagram) and duplicated (copies).
 * A case queues blocks for every datagram it expects (no block is queued
 * again), enables, waits for the oracle, dequeues exactly the blocks that the
 * emulator's order of emission gives under the host's rules, waits until the
 * backend has seen every datagram sent, and only then disables: nothing is in
 * flight at DISABLE. A count stays below what even the kernel's default
 * receive buffer (rcvbuf=0) holds of these datagrams (some 90), let alone the
 * 4 MiB the backend asks for, so a receive thread that runs late loses none,
 * and every run is paced (--udp-pps 250) besides. The checks:
 * - the oracle is the model's: sent, dropped, reordered, duplicated;
 * - every datagram sent arrived: rx_packets + late + duplicates == sent;
 * - late == reordered, duplicates == duplicated;
 * - lost == dropped less the drops past the last datagram sent, which the
 *   host cannot see: the numbers 0 .. last were sent once (sent - duplicated)
 *   or dropped, the trailing drops are the rest;
 * - the blocks, byte for byte: a gap fails one block with lossy=0 and
 *   zero-fills one block per lost datagram with lossy=1, exactly.
 *
 * The rcvbuf_* and burst_* cases check the engine socket's receive buffer
 * instead (see rcvbuf_default below); they never enable the buffer.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1 /* SO_RCVBUFFORCE */
#endif

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <iio/iio.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../iiod-udp-stream.h"
#include "test_framework.h"

#define DEV_NAME "cf-ad9361-lpc"

/* voltage0+voltage1: max_payload_size 1472 less the 16-byte header, a
 * multiple of 16 and of the 4-byte sample set */
#define PAYLOAD 1456
#define NB_BLOCKS 64

#define LOCAL_PORT 25101  /* the YAML's local_addr */
#define SENDER_PORT 25102 /* its remote_addr, the emulator's --udp-from */

#define MAX_RECS 512
/* Sequence numbers told apart by their payload; map_offsets() checks it */
#define MAX_SEQ 1024
/* The first bytes of the payloads that must all differ */
#define KEY_LEN 16

#define DEQUEUE_TIMEOUT_MS 3000
#define WAIT_TIMEOUT_MS 10000

/* A block, as dequeued or as expected: the datagram it holds (>= 0) or */
#define BLK_EIO (-1)  /* dequeue returned -EIO */
#define BLK_ZERO (-2) /* all zero */
#define BLK_BAD (-3)  /* neither all zero nor the payload of one datagram */
#define BLK_ERR (-4)  /* another error; ends the case */

#define MAX_STREAMS 8

/* CHECK_EQ() evaluates <actual> again to print it: evaluate once */
#define CHECK_EQ(actual, expected, message) \
	do { \
		long check_val_ = (long)(actual); \
		TEST_ASSERT_EQ(check_val_, (long)(expected), message); \
	} while (0)

struct oracle {
	unsigned long long sent, dropped, reordered, duplicated;
};

/* What the emulator injects, for the model of its order of emission */
struct inject {
	unsigned long long count; /* --udp-count */
	const long *drop_list;
	unsigned int nb_drop;
	long drop_every;
	long reorder_window;
	long dup_every;
};

/* The daqiri_* counters of a buffer */
struct counters {
	long long packets, bytes, lost, gaps, dropped_no_block, overruns, foreign, malformed,
			zero_filled, late, duplicates, completed, failed, with_gaps, last_error;
};

/* The streams in the emulator's log: every setup, every start */
struct streams {
	uint32_t setup[MAX_STREAMS], started[MAX_STREAMS];
	unsigned int payload[MAX_STREAMS];
	unsigned int nb_setup, nb_started;
};

struct lb {
	struct iio_context *ctx;
	struct iio_buffer *buf;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *stream;
	struct iio_block *blocks[NB_BLOCKS];
	unsigned int nb_blocks;
	unsigned int next; /* the block to dequeue next: the queue is in order */
	long recs[MAX_RECS];
	unsigned int nb_recs;
	long long rcvbuf_errors; /* the kernel's count when the case started */
};

static const char *yaml, *oracle_path;
static uint8_t *file;
static size_t file_len;
static size_t seq_off[MAX_SEQ]; /* where the payload of datagram s starts in the file */

/* One at a time; too large for the stack */
static struct lb lb;

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static long ms_since(const struct timespec *t0)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (long)(now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

static int read_file(const char *path, uint8_t **data, size_t *len)
{
	uint8_t *buf = NULL, *p;
	size_t n, size = 0;
	FILE *f;
	int ret = 0;

	f = fopen(path, "rb");
	if (!f)
		return -errno;

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

	if (ret) {
		free(buf);
		return ret;
	}

	*data = buf;
	*len = size;

	return 0;
}

/* Byte <i> of the payload that starts at <off> */
static uint8_t stream_byte(size_t off, size_t i)
{
	return file[(off + i) % file_len];
}

/* Where the payload of each of the first MAX_SEQ datagrams starts; -EINVAL
 * unless their first KEY_LEN bytes all differ, so that a block tells which
 * datagram it holds */
static int map_offsets(void)
{
	unsigned int s, t;
	size_t i;

	for (s = 0; s < MAX_SEQ; s++)
		seq_off[s] = (size_t)((uint64_t)s * PAYLOAD % file_len);

	for (s = 0; s < MAX_SEQ; s++) {
		for (t = s + 1; t < MAX_SEQ; t++) {
			for (i = 0; i < KEY_LEN; i++)
				if (stream_byte(seq_off[s], i) != stream_byte(seq_off[t], i))
					break;
			if (i == KEY_LEN)
				return -EINVAL;
		}
	}

	return 0;
}

/* The payload of datagram <s>, into <out> */
static void payload_of(unsigned int s, uint8_t *out)
{
	size_t i;

	for (i = 0; i < PAYLOAD; i++)
		out[i] = stream_byte(seq_off[s], i);
}

/* BLK_ZERO, the datagram whose payload the block is, or BLK_BAD */
static long classify(const struct iio_block *block)
{
	const uint8_t *p = iio_block_start(block);
	unsigned int s;
	size_t i;

	for (i = 0; i < PAYLOAD && !p[i]; i++)
		;
	if (i == PAYLOAD)
		return BLK_ZERO;

	for (s = 0; s < MAX_SEQ; s++) {
		for (i = 0; i < KEY_LEN && p[i] == stream_byte(seq_off[s], i); i++)
			;
		if (i < KEY_LEN)
			continue;

		/* The only candidate: all of it, then */
		for (i = 0; i < PAYLOAD && p[i] == stream_byte(seq_off[s], i); i++)
			;
		return i == PAYLOAD ? (long)s : BLK_BAD;
	}

	return BLK_BAD;
}

static const char *blk_name(long v, char *buf, size_t len)
{
	static const char *const names[] = { "-EIO", "zero", "bad", "error" };

	if (v >= 0)
		snprintf(buf, len, "datagram %ld", v);
	else
		snprintf(buf, len, "%s", names[-v - 1]);

	return buf;
}

static bool json_number(const char *json, const char *key, unsigned long long *val)
{
	char name[32], *end;
	const char *p;

	snprintf(name, sizeof(name), "\"%s\"", key);
	p = strstr(json, name);
	if (!p)
		return false;

	p += strlen(name);
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p++ != ':')
		return false;

	errno = 0;
	*val = strtoull(p, &end, 10);

	return end != p && !errno;
}

/* The emulator replaces the file in one go */
static int oracle_read(struct oracle *o)
{
	uint8_t *data;
	size_t len;
	char *json;
	int ret;

	ret = read_file(oracle_path, &data, &len);
	if (ret)
		return ret;

	json = realloc(data, len + 1);
	if (!json) {
		free(data);
		return -ENOMEM;
	}
	json[len] = '\0';

	ret = 0;
	if (!json_number(json, "sent", &o->sent) || !json_number(json, "dropped", &o->dropped) ||
			!json_number(json, "reordered", &o->reordered) ||
			!json_number(json, "duplicated", &o->duplicated))
		ret = -EBADMSG;
	free(json);

	return ret;
}

/* The oracle that --udp-count makes the emulator write when the stream of
 * this enable is over; the caller removed the last one */
static int oracle_wait(struct oracle *o)
{
	struct timespec t0;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while ((ret = oracle_read(o)) == -ENOENT && ms_since(&t0) < WAIT_TIMEOUT_MS)
		sleep_ms(2);

	CHECK_EQ(ret, 0, "oracle written when the count was reached");
	if (!ret)
		printf("  oracle: %llu sent, %llu dropped, %llu reordered, %llu duplicated\n",
				o->sent, o->dropped, o->reordered, o->duplicated);

	return ret;
}

/* The setups and starts the emulator logged (udp-stream.c) */
static void streams_read(struct streams *st)
{
	const char *path = getenv("IIOD_EMU_LOG");
	unsigned int id, payload;
	char line[512];
	FILE *f;

	memset(st, 0, sizeof(*st));
	f = path ? fopen(path, "r") : NULL;
	if (!f)
		return;

	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "UDP stream %8x started", &id) == 1 && strstr(line, " started")) {
			if (st->nb_started < MAX_STREAMS)
				st->started[st->nb_started++] = id;
		} else if (sscanf(line, "UDP stream %8x: %*s buffer %*u, %*s to %*s %u-byte", &id,
					   &payload) == 2) {
			if (st->nb_setup < MAX_STREAMS) {
				st->payload[st->nb_setup] = payload;
				st->setup[st->nb_setup++] = id;
			}
		}
	}
	fclose(f);
}

/* UDP datagrams the kernel dropped for a full receive buffer, system-wide:
 * the RcvbufErrors column of the Udp lines of /proc/net/snmp, or -1 */
static long long udp_rcvbuf_errors(void)
{
	char head[1024], vals[1024], *hs = NULL, *vs = NULL, *h, *v;
	long long ret = -1;
	FILE *f;

	f = fopen("/proc/net/snmp", "r");
	if (!f)
		return -1;

	while (fgets(head, sizeof(head), f)) {
		if (strncmp(head, "Udp: ", 5) || !strstr(head, "RcvbufErrors"))
			continue;
		if (!fgets(vals, sizeof(vals), f))
			break;

		h = strtok_r(head, " \n", &hs);
		v = strtok_r(vals, " \n", &vs);
		while (h && v && strcmp(h, "RcvbufErrors")) {
			h = strtok_r(NULL, " \n", &hs);
			v = strtok_r(NULL, " \n", &vs);
		}
		if (h && v)
			ret = strtoll(v, NULL, 10);
		break;
	}
	fclose(f);

	return ret;
}

/* Say what the kernel dropped meanwhile when datagrams sent did not arrive */
static void missed(long long missing, unsigned long long sent)
{
	long long now = udp_rcvbuf_errors();

	printf("  %lld of %llu datagrams sent did not reach the backend; the kernel dropped %lld "
	       "UDP datagrams for a full receive buffer meanwhile (RcvbufErrors, system-wide)\n",
			missing, sent,
			now >= 0 && lb.rcvbuf_errors >= 0 ? now - lb.rcvbuf_errors : -1LL);
}

static long long attr_ll(const char *name)
{
	const struct iio_attr *attr = iio_buffer_find_attr(lb.buf, name);
	long long val;

	if (!attr || iio_attr_read_longlong(attr, &val))
		return -1;

	return val;
}

static const char *attr_str(const char *name)
{
	static char val[64];
	const struct iio_attr *attr = iio_buffer_find_attr(lb.buf, name);
	ssize_t ret;

	if (!attr)
		return "(missing)";

	ret = iio_attr_read_raw(attr, val, sizeof(val));
	if (ret < 0)
		snprintf(val, sizeof(val), "(error %zd)", ret);

	return val;
}

static void counters_read(struct counters *c)
{
	c->packets = attr_ll("daqiri_rx_packets");
	c->bytes = attr_ll("daqiri_rx_bytes");
	c->lost = attr_ll("daqiri_rx_lost_packets");
	c->gaps = attr_ll("daqiri_rx_gap_events");
	c->dropped_no_block = attr_ll("daqiri_rx_dropped_no_block");
	c->overruns = attr_ll("daqiri_rx_overrun_events");
	c->foreign = attr_ll("daqiri_rx_foreign_packets");
	c->malformed = attr_ll("daqiri_rx_malformed");
	c->zero_filled = attr_ll("daqiri_rx_zero_filled_bytes");
	c->late = attr_ll("daqiri_rx_late_packets");
	c->duplicates = attr_ll("daqiri_rx_duplicate_packets");
	c->completed = attr_ll("daqiri_blocks_completed");
	c->failed = attr_ll("daqiri_blocks_failed");
	c->with_gaps = attr_ll("daqiri_blocks_with_gaps");
	c->last_error = attr_ll("daqiri_last_error");
}

/* Datagrams the backend has seen: delivered or not, late or duplicate */
static long long arrived(const struct counters *c)
{
	return c->packets + c->late + c->duplicates;
}

/* Until the backend has seen <sent> datagrams in all; false after
 * WAIT_TIMEOUT_MS, saying how many are missing */
static bool wait_arrived(unsigned long long sent)
{
	struct counters c;
	struct timespec t0;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		counters_read(&c);
		if (arrived(&c) >= (long long)sent || ms_since(&t0) >= WAIT_TIMEOUT_MS)
			break;
		sleep_ms(1);
	}

	CHECK_EQ(arrived(&c), sent,
			"every datagram sent arrived before the disable: rx_packets + late + duplicates");
	if (arrived(&c) != (long long)sent)
		missed((long long)sent - arrived(&c), sent);

	return arrived(&c) == (long long)sent;
}

/* All the counters of the case at once */
static void check_all(const struct counters *want)
{
	struct counters c;

	counters_read(&c);
	CHECK_EQ(c.packets, want->packets, "rx_packets");
	CHECK_EQ(c.bytes, want->packets * PAYLOAD, "rx_bytes: their payloads");
	CHECK_EQ(c.lost, want->lost, "lost_packets");
	CHECK_EQ(c.gaps, want->gaps, "gap_events");
	CHECK_EQ(c.dropped_no_block, want->dropped_no_block, "dropped_no_block");
	CHECK_EQ(c.overruns, want->overruns, "overrun_events");
	CHECK_EQ(c.foreign, 0, "foreign_packets");
	CHECK_EQ(c.malformed, 0, "malformed");
	CHECK_EQ(c.zero_filled, want->zero_filled, "zero_filled_bytes");
	CHECK_EQ(c.late, want->late, "late_packets");
	CHECK_EQ(c.duplicates, want->duplicates, "duplicate_packets");
	CHECK_EQ(c.completed, want->completed, "blocks_completed");
	CHECK_EQ(c.failed, want->failed, "blocks_failed");
	CHECK_EQ(c.with_gaps, want->with_gaps, "blocks_with_gaps");
	CHECK_EQ(c.last_error, want->last_error, "last_error");
}

static void lb_close(void)
{
	while (lb.nb_blocks)
		iio_block_destroy(lb.blocks[--lb.nb_blocks]);
	if (lb.stream)
		iio_buffer_close(lb.stream);
	if (lb.mask)
		iio_channels_mask_destroy(lb.mask);
	if (lb.ctx)
		iio_context_destroy(lb.ctx);

	memset(&lb, 0, sizeof(lb));
}

/* Where the context of the next lb_open() logs its warnings and errors;
 * NULL: stderr */
static FILE *lb_log;

/* Context with <extra> URI options, buffer with NB_BLOCKS blocks */
static int lb_open(const char *extra)
{
	static const char *const chans[] = { "voltage0", "voltage1" };
	struct iio_context_params params = { .err = lb_log };
	const struct iio_device *dev;
	struct iio_channel *chn;
	struct iio_block *block;
	char uri[4300];
	unsigned int i;
	int ret;

	memset(&lb, 0, sizeof(lb));

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=udp,yaml=%s%s", yaml, extra);
	lb.ctx = iio_create_context(&params, uri);
	ret = iio_err(lb.ctx);
	if (ret) {
		printf("  %s: unable to create context: %d\n", uri, ret);
		lb.ctx = NULL;
		return ret;
	}

	ret = iio_context_set_timeout(lb.ctx, DEQUEUE_TIMEOUT_MS);
	if (ret)
		return ret;

	dev = iio_context_find_device(lb.ctx, DEV_NAME);
	if (!dev)
		return -ENODEV;

	lb.mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!lb.mask)
		return -ENOMEM;

	for (i = 0; i < 2; i++) {
		chn = iio_device_find_channel(dev, chans[i], false);
		if (!chn)
			return -ENOENT;
		iio_channel_enable(chn, lb.mask);
	}

	lb.buf = iio_device_get_buffer(dev, 0);
	if (!lb.buf)
		return -ENODEV;

	lb.stream = iio_buffer_open(lb.buf, lb.mask);
	ret = iio_err(lb.stream);
	if (ret) {
		lb.stream = NULL;
		return ret;
	}

	while (lb.nb_blocks < NB_BLOCKS) {
		block = iio_buffer_stream_create_block(lb.stream, PAYLOAD);
		ret = iio_err(block);
		if (ret)
			return ret;
		lb.blocks[lb.nb_blocks++] = block;
	}

	return 0;
}

static int lb_enqueue(unsigned int first, unsigned int nb)
{
	unsigned int i;
	int ret;

	for (i = first; i < first + nb; i++) {
		ret = iio_block_enqueue(lb.blocks[i % NB_BLOCKS], 0, false);
		if (ret)
			return ret;
	}

	return 0;
}

/* A fresh buffer (lossy=1 with <lossy>), <queued> blocks queued, enabled */
static int lb_start(bool lossy, unsigned int queued)
{
	int ret;

	/* What is there after the count is reached comes from this case */
	remove(oracle_path);

	ret = lb_open(lossy ? ",lossy=1" : "");
	lb.rcvbuf_errors = udp_rcvbuf_errors();
	if (!ret)
		ret = lb_enqueue(0, queued);
	if (!ret)
		ret = iio_buffer_stream_start(lb.stream);

	return ret;
}

static void lb_record(long rec)
{
	if (lb.nb_recs < MAX_RECS)
		lb.recs[lb.nb_recs++] = rec;
}

/* Dequeue the next block, waiting for it unless <nonblock>, and record what
 * it holds. -EBUSY or -EPERM with <nonblock>: the block is still queued, or
 * was not, and nothing is recorded. Any other error than -EIO fails the case,
 * naming the block and the error. */
static int lb_take(bool nonblock)
{
	struct iio_block *b = lb.blocks[lb.next];
	char msg[64];
	int ret;

	ret = iio_block_dequeue(b, nonblock);
	if (nonblock && (ret == -EBUSY || ret == -EPERM))
		return ret;

	if (ret && ret != -EIO) {
		snprintf(msg, sizeof(msg), "block %u: done or -EIO", lb.nb_recs);
		CHECK_EQ(ret, 0, msg);
		lb_record(BLK_ERR);
		return ret;
	}

	lb_record(ret ? BLK_EIO : classify(b));
	lb.next = (lb.next + 1) % NB_BLOCKS;

	return 0;
}

/* Dequeue <nb> blocks; false, failing the case, on an error */
static bool lb_run(unsigned int nb)
{
	unsigned int i;
	char msg[64];

	for (i = 0; i < nb && !lb_take(false); i++)
		;

	snprintf(msg, sizeof(msg), "%u blocks dequeued", nb);
	CHECK_EQ(i, nb, msg);

	return i == nb;
}

/* No block completed beyond the ones dequeued */
static void check_no_more(const char *when)
{
	char msg[96];
	int ret;

	ret = lb_take(true);
	snprintf(msg, sizeof(msg), "no further block completed %s", when);
	TEST_ASSERT(ret == -EBUSY || ret == -EPERM, msg);
}

/* Disable with nothing in flight; false on an error */
static bool lb_stop(void)
{
	int ret;

	check_no_more("before the disable");
	ret = iio_buffer_stream_stop(lb.stream);
	CHECK_EQ(ret, 0, "disabled");
	check_no_more("after it");

	return !ret;
}

static bool dropped_by(const struct inject *inj, long seq)
{
	unsigned int i;

	if (inj->drop_every && (seq + 1) % inj->drop_every == 0)
		return true;

	for (i = 0; i < inj->nb_drop; i++)
		if (inj->drop_list[i] == seq)
			return true;

	return false;
}

static unsigned int emit(
		const struct inject *inj, long seq, long *order, unsigned int n, struct oracle *m)
{
	if (n < MAX_RECS)
		order[n++] = seq;
	m->sent++;
	if (inj->dup_every && (seq + 1) % inj->dup_every == 0) {
		if (n < MAX_RECS)
			order[n++] = seq;
		m->sent++;
		m->duplicated++;
	}

	return n;
}

/*
 * The datagrams the emulator sends with <inj>, in order, and the oracle that
 * goes with them (its generator, iiod-emu/udp-stream.c): the numbers 0 to
 * count - 1; the dropped ones skipped; for a reorder window k, datagram mk
 * sent just before mk-1 unless one of them is dropped or past the count; with
 * dup_every n, datagrams n-1, 2n-1, ... sent twice in a row.
 */
static unsigned int emission(const struct inject *inj, long *order, struct oracle *m)
{
	long seq = 0, k = inj->reorder_window, count = (long)inj->count;
	unsigned int n = 0;

	memset(m, 0, sizeof(*m));

	while (seq < count) {
		if (dropped_by(inj, seq)) {
			m->dropped++;
			seq++;
		} else if (k && seq % k == k - 1 && seq + 1 < count && !dropped_by(inj, seq + 1)) {
			n = emit(inj, seq + 1, order, n, m);
			n = emit(inj, seq, order, n, m);
			m->reordered++;
			seq += 2;
		} else {
			n = emit(inj, seq, order, n, m);
			seq++;
		}
	}

	return n;
}

/* What the host makes of datagrams arriving in <order> */
struct expect {
	long blocks[MAX_RECS];
	unsigned int nb_blocks;
	long long packets, lost, gaps, late, duplicates, eio, zeros, last_seq;
};

static void expect_blocks(const long *order, unsigned int nb, bool lossy, struct expect *e)
{
	static bool seen[MAX_SEQ];
	long p, g, last = -1;
	unsigned int i;

	memset(e, 0, sizeof(*e));
	memset(seen, 0, sizeof(seen));

	for (i = 0; i < nb; i++) {
		p = order[i];
		if (p < 0 || p >= MAX_SEQ)
			continue;

		if (p <= last) {
			/* The last accepted one again, or an older one */
			if (seen[p])
				e->duplicates++;
			else
				e->late++;
			seen[p] = true;
			continue;
		}

		if (p > last + 1) {
			e->gaps++;
			if (!lossy && e->nb_blocks < MAX_RECS) {
				e->blocks[e->nb_blocks++] = BLK_EIO;
				e->eio++;
			}
			for (g = p - last - 1; lossy && g && e->nb_blocks < MAX_RECS; g--) {
				e->blocks[e->nb_blocks++] = BLK_ZERO;
				e->zeros++;
			}
		}

		if (e->nb_blocks < MAX_RECS)
			e->blocks[e->nb_blocks++] = p;
		e->packets++;
		seen[p] = true;
		last = p;
	}

	/* Sequence numbers up to the last one that never arrived */
	e->last_seq = last;
	e->lost = last + 1 - e->packets - e->late;
}

/* The blocks dequeued are exactly <exp> */
static void check_blocks(const struct expect *e)
{
	unsigned int i, bad = 0;
	char got[32], want[32];

	CHECK_EQ(lb.nb_recs, e->nb_blocks, "blocks dequeued: as the emulator's order gives");
	for (i = 0; i < lb.nb_recs && i < e->nb_blocks; i++) {
		if (lb.recs[i] == e->blocks[i])
			continue;
		if (bad++ < 4)
			printf("  block %u: %s, expected %s\n", i,
					blk_name(lb.recs[i], got, sizeof(got)),
					blk_name(e->blocks[i], want, sizeof(want)));
	}
	CHECK_EQ(bad, 0, "every block as expected, its bytes those of the sample file");
}

/* One probe at open (whose stream never starts), then one setup and one start
 * per enable, of the stream daqiri_stream_id names last */
static void check_streams(unsigned int enables)
{
	char id[16];
	struct streams st;
	unsigned int i;
	bool ok;

	streams_read(&st);
	CHECK_EQ(st.nb_setup, 1 + enables, "emulator: a setup at open and one per enable");
	CHECK_EQ(st.nb_started, enables, "emulator: one stream started per enable");
	if (st.nb_setup != 1 + enables || st.nb_started != enables)
		return;

	for (ok = true, i = 0; i < st.nb_setup; i++)
		ok = ok && st.payload[i] == PAYLOAD;
	TEST_ASSERT(ok, "emulator: 1456-byte payloads");

	for (ok = true, i = 0; i < enables; i++)
		ok = ok && st.started[i] == st.setup[i + 1] && st.started[i] != st.setup[0];
	TEST_ASSERT(ok, "the streams of the enables' setups start, the probe's never");

	snprintf(id, sizeof(id), "0x%08" PRIx32, st.started[enables - 1]);
	TEST_ASSERT_STR_EQ(
			attr_str("daqiri_stream_id"), id, "daqiri_stream_id: the last one started");
}

/* Stream, stop, then check the oracle, the blocks and the counters against the
 * model of what the emulator sends with <inj>. Returns the trailing drops. */
static long long injection_case(const struct inject *inj, bool lossy, struct oracle *o)
{
	static long order[MAX_RECS];
	static struct expect e;
	struct counters want;
	long long trailing;
	struct oracle m;
	unsigned int nb;
	bool ok;
	int ret;

	memset(o, 0, sizeof(*o));

	nb = emission(inj, order, &m);
	expect_blocks(order, nb, lossy, &e);
	TEST_ASSERT(e.nb_blocks <= NB_BLOCKS, "the blocks queued suffice");

	ret = lb_start(lossy, NB_BLOCKS);
	CHECK_EQ(ret, 0,
			lossy ? "lossy=1 buffer enabled with 64 blocks queued"
			      : "buffer enabled with 64 blocks queued");
	if (ret || oracle_wait(o))
		return -1;
	TEST_ASSERT_STR_EQ(attr_str("daqiri_data_path"), "udp", "data path");

	CHECK_EQ(o->sent, m.sent, "oracle sent: as modelled");
	CHECK_EQ(o->dropped, m.dropped, "oracle dropped: as modelled");
	CHECK_EQ(o->reordered, m.reordered, "oracle reordered: as modelled");
	CHECK_EQ(o->duplicated, m.duplicated, "oracle duplicated: as modelled");

	/* The blocks, also when the stream stopped short: what came */
	ok = lb_run(e.nb_blocks) && wait_arrived(o->sent) && lb_stop();
	check_blocks(&e);
	if (!ok)
		return -1;

	CHECK_EQ(lb.nb_recs ? lb.recs[lb.nb_recs - 1] : -1, e.last_seq,
			"the last block holds the last datagram sent");

	/* 0 .. last were sent once or dropped; the drops past last are the
	 * trailing ones, which the host cannot see */
	trailing = (long long)(o->sent - o->duplicated + o->dropped) - 1 - e.last_seq;
	printf("  last datagram %lld, %lld trailing drops\n", e.last_seq, trailing);
	TEST_ASSERT(trailing >= 0, "oracle and blocks agree on the datagrams sent");

	memset(&want, 0, sizeof(want));
	want.packets = e.packets;
	want.lost = (long long)o->dropped - trailing;
	want.gaps = e.gaps;
	want.zero_filled = e.zeros * PAYLOAD;
	want.late = (long long)o->reordered;
	want.duplicates = (long long)o->duplicated;
	want.completed = e.nb_blocks - e.eio;
	want.failed = e.eio;
	want.with_gaps = e.eio + e.zeros;
	want.last_error = e.eio ? -EIO : 0;
	CHECK_EQ(want.lost, e.lost, "lost: the oracle's arithmetic and the model agree");
	check_all(&want);
	check_streams(1);

	return trailing;
}

/* Nothing injected: block k is datagram k */
TEST_FUNCTION(clean)
{
	const struct inject inj = { .count = 64 };
	struct oracle o;

	injection_case(&inj, false, &o);
	CHECK_EQ(o.sent, 64, "oracle: 64 sent");
}

static const long drops[] = { 5, 6, 20 };

/* lossy=0: the gap of 5 and 6 fails one block, that of 20 another */
TEST_FUNCTION(drop_list)
{
	const struct inject inj = { .count = 64, .drop_list = drops, .nb_drop = 3 };
	struct oracle o;

	injection_case(&inj, false, &o);
	CHECK_EQ(o.dropped, 3, "oracle: 5, 6 and 20 dropped");
	if (lb.nb_recs > 20) {
		CHECK_EQ(lb.recs[5], BLK_EIO, "block 5: -EIO, datagrams 5 and 6 lost");
		CHECK_EQ(lb.recs[6], 7, "block 6: datagram 7");
		CHECK_EQ(lb.recs[19], BLK_EIO, "block 19: -EIO, datagram 20 lost");
		CHECK_EQ(lb.recs[20], 21, "block 20: datagram 21");
	}
}

/* lossy=1: blocks 5, 6 and 20 are all zero, every other block k datagram k */
TEST_FUNCTION(drop_list_lossy)
{
	const struct inject inj = { .count = 64, .drop_list = drops, .nb_drop = 3 };
	struct oracle o;

	injection_case(&inj, true, &o);
	CHECK_EQ(o.dropped, 3, "oracle: 5, 6 and 20 dropped");
	if (lb.nb_recs > 21) {
		CHECK_EQ(lb.recs[4], 4, "block 4: datagram 4");
		CHECK_EQ(lb.recs[5], BLK_ZERO, "block 5: zeros for datagram 5");
		CHECK_EQ(lb.recs[6], BLK_ZERO, "block 6: zeros for datagram 6");
		CHECK_EQ(lb.recs[7], 7, "block 7: datagram 7, the stream in place");
		CHECK_EQ(lb.recs[20], BLK_ZERO, "block 20: zeros for datagram 20");
		CHECK_EQ(lb.recs[21], 21, "block 21: datagram 21");
	}
}

/* --udp-drop-every 2: the odd numbers are dropped, 63 the last of them, past
 * the last datagram sent (62): the host sees 31 of the 32 drops */
TEST_FUNCTION(drop_every)
{
	const struct inject inj = { .count = 64, .drop_every = 2 };
	unsigned int i, ok = 0;
	long long trailing;
	struct oracle o;

	trailing = injection_case(&inj, false, &o);
	CHECK_EQ(o.dropped, 32, "oracle: 32 dropped");
	CHECK_EQ(trailing, 1, "one trailing drop: 63");
	for (i = 0; i < lb.nb_recs; i++)
		ok += lb.recs[i] == (i % 2 ? BLK_EIO : (long)i);
	CHECK_EQ(ok, lb.nb_recs, "block k: datagram k for even k, -EIO for odd k");
}

/* --udp-reorder-window 3: 3 before 2, 6 before 5, ... Each pair fails the
 * block of the late datagram's place (a gap of one, then the late one takes
 * the loss back): block k is datagram k, or -EIO for k = 3m + 2 */
TEST_FUNCTION(reorder)
{
	const struct inject inj = { .count = 64, .reorder_window = 3 };
	unsigned int i, ok = 0;
	struct oracle o;

	injection_case(&inj, false, &o);
	CHECK_EQ(o.reordered, 21, "oracle: 21 pairs reordered");
	for (i = 0; i < lb.nb_recs; i++)
		ok += lb.recs[i] == (i % 3 == 2 ? BLK_EIO : (long)i);
	CHECK_EQ(ok, lb.nb_recs, "block k: -EIO where k = 3m + 2, datagram k elsewhere");
}

/* --udp-dup-every 8: 7, 15, 23, ... sent twice; the copies are dropped */
TEST_FUNCTION(duplicate)
{
	const struct inject inj = { .count = 64, .dup_every = 8 };
	unsigned int i, ok = 0;
	struct oracle o;

	injection_case(&inj, false, &o);
	CHECK_EQ(o.duplicated, 8, "oracle: one copy of each of 7, 15, ..., 63");
	for (i = 0; i < lb.nb_recs; i++)
		ok += lb.recs[i] == (long)i;
	CHECK_EQ(ok, lb.nb_recs, "block k is datagram k: no copy delivered");
}

/* Datagram <seq> of the stream <stream_id>, as the emulator would send it,
 * from where it sends (the engine takes nothing from elsewhere) */
static int send_continuation(uint32_t stream_id, uint64_t seq)
{
	struct sockaddr_in from = {
		.sin_family = AF_INET,
		.sin_port = htons(SENDER_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct sockaddr_in to = {
		.sin_family = AF_INET,
		.sin_port = htons(LOCAL_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	const struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.payload_length = PAYLOAD,
		.stream_id = stream_id,
		.sequence = seq,
	};
	uint8_t dgram[IIOD_UDP_STREAM_HDR_LEN + PAYLOAD];
	int fd, one = 1, ret = 0;

	iiod_udp_stream_hdr_pack(&hdr, dgram);
	payload_of((unsigned int)seq, dgram + IIOD_UDP_STREAM_HDR_LEN);

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	/* The emulator's socket holds the address too */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
			bind(fd, (struct sockaddr *)&from, sizeof(from)) ||
			sendto(fd, dgram, sizeof(dgram), 0, (struct sockaddr *)&to, sizeof(to)) !=
					sizeof(dgram))
		ret = -errno;
	close(fd);

	return ret;
}

#define OVERRUN_COUNT 12

/*
 * A host overrun, made exact by the count: two blocks queued, and the
 * emulator's whole stream, 12 datagrams, goes by before the consumer dequeues
 * anything. Datagrams 0 and 1 fill the blocks, 2 to 11 find none: 10 dropped,
 * one overrun event. The consumer then queues 4 more blocks and the stream
 * goes on with datagrams 12 and 13, which the test sends in its place (the
 * stream ID from daqiri_stream_id, the payloads from the sample file): the
 * first block that 12 finds follows a gap of unknown size. With lossy=0 it
 * fails with -EIO and 12 and 13 fill the next two; with lossy=1, 12 fills it,
 * flagged, not zero-filled, and 13 the next.
 */
static void slow_consumer_case(bool lossy)
{
	struct counters want;
	unsigned long sid;
	struct oracle o;
	char *end;
	int ret;

	ret = lb_start(lossy, 2);
	CHECK_EQ(ret, 0, "buffer enabled with 2 blocks queued");
	if (ret || oracle_wait(&o))
		return;
	CHECK_EQ(o.sent, OVERRUN_COUNT, "oracle: 12 sent");
	CHECK_EQ(o.dropped + o.reordered + o.duplicated, 0, "oracle: nothing injected");
	if (!wait_arrived(o.sent))
		return;

	CHECK_EQ(attr_ll("daqiri_rx_dropped_no_block"), OVERRUN_COUNT - 2,
			"datagrams 2 to 11 dropped: no block");
	CHECK_EQ(attr_ll("daqiri_rx_overrun_events"), 1, "one overrun event, not one per datagram");
	CHECK_EQ(lb_take(true), 0, "block 0 done");
	CHECK_EQ(lb_take(true), 0, "block 1 done");
	if (lb.nb_recs != 2)
		return;
	CHECK_EQ(lb.recs[0], 0, "block 0: datagram 0");
	CHECK_EQ(lb.recs[1], 1, "block 1: datagram 1");

	ret = lb_enqueue(2, 4);
	CHECK_EQ(ret, 0, "4 more blocks queued");
	errno = 0;
	sid = strtoul(attr_str("daqiri_stream_id"), &end, 16);
	TEST_ASSERT(!errno && !*end, "daqiri_stream_id read");
	if (ret || errno || *end)
		return;
	CHECK_EQ(send_continuation((uint32_t)sid, OVERRUN_COUNT), 0, "datagram 12 sent");
	CHECK_EQ(send_continuation((uint32_t)sid, OVERRUN_COUNT + 1), 0, "datagram 13 sent");

	if (!lb_run(lossy ? 2 : 3) || !wait_arrived(OVERRUN_COUNT + 2) || !lb_stop())
		return;

	if (lossy) {
		CHECK_EQ(lb.recs[2], OVERRUN_COUNT, "block 2: datagram 12, whole, flagged");
		CHECK_EQ(lb.recs[3], OVERRUN_COUNT + 1, "block 3: datagram 13");
	} else {
		CHECK_EQ(lb.recs[2], BLK_EIO, "block 2: -EIO, the overrun");
		CHECK_EQ(lb.recs[3], OVERRUN_COUNT, "block 3: datagram 12");
		CHECK_EQ(lb.recs[4], OVERRUN_COUNT + 1, "block 4: datagram 13");
	}

	memset(&want, 0, sizeof(want));
	want.packets = OVERRUN_COUNT + 2;
	want.dropped_no_block = OVERRUN_COUNT - 2;
	want.overruns = 1;
	want.completed = 4;
	want.failed = lossy ? 0 : 1;
	want.with_gaps = 1;
	want.last_error = lossy ? 0 : -EIO;
	check_all(&want);
	check_streams(1);
}

TEST_FUNCTION(slow_consumer)
{
	slow_consumer_case(false);
}

TEST_FUNCTION(slow_consumer_lossy)
{
	slow_consumer_case(true);
}

#define BURST 32

/*
 * Two bursts of 32 datagrams with 500 ms of silence in between: enable, 32
 * blocks, disable, wait, enable again, 32 blocks, disable. The second enable
 * has a setup of its own: another stream ID, the sequence from 0, and the
 * payloads from the start of the file again. The blocks still queued at the
 * first disable take the second burst; the counters add up over both.
 */
TEST_FUNCTION(burst_gap)
{
	char id1[16] = "";
	struct counters want;
	unsigned int i, ok = 0;
	struct oracle o1, o2;
	int ret;

	ret = lb_start(false, NB_BLOCKS);
	CHECK_EQ(ret, 0, "buffer enabled with 64 blocks queued");
	if (ret || oracle_wait(&o1))
		return;
	CHECK_EQ(o1.sent, BURST, "first burst: 32 sent");
	if (!lb_run(BURST) || !wait_arrived(BURST) || !lb_stop())
		return;
	snprintf(id1, sizeof(id1), "%s", attr_str("daqiri_stream_id"));

	/* The disable wrote it again: the next one is the second burst's */
	remove(oracle_path);
	sleep_ms(500);

	ret = iio_buffer_stream_start(lb.stream);
	CHECK_EQ(ret, 0, "enabled again after 500 ms");
	if (ret || oracle_wait(&o2))
		return;
	CHECK_EQ(o2.sent, BURST, "second burst: 32 sent");
	CHECK_EQ(o1.dropped + o1.reordered + o1.duplicated + o2.dropped + o2.reordered +
					o2.duplicated,
			0, "oracles: nothing injected");
	if (!lb_run(BURST) || !wait_arrived(2 * BURST) || !lb_stop())
		return;
	TEST_ASSERT(strcmp(attr_str("daqiri_stream_id"), id1), "a new stream ID");

	for (i = 0; i < lb.nb_recs; i++)
		ok += lb.recs[i] == (long)(i < BURST ? i : i - BURST);
	CHECK_EQ(lb.nb_recs, 2 * BURST, "64 blocks");
	CHECK_EQ(ok, lb.nb_recs, "each burst: datagrams 0, 1, 2, ... in order");

	memset(&want, 0, sizeof(want));
	want.packets = 2 * BURST;
	want.completed = 2 * BURST;
	check_all(&want);
	check_streams(2);
}

/*
 * The receive buffer of the engine's socket (daqiri_rx_socket_buffer), which
 * the backend sizes when it opens the buffer: SO_RCVBUF first, which the
 * kernel caps at net.core.rmem_max, then SO_RCVBUFFORCE when that came out
 * short and the process may force it (CAP_NET_ADMIN in the initial user
 * namespace). The kernel reports twice what it granted, and never less than
 * SOCK_MIN_RCVBUF. Every case runs in a process, and so on an engine socket,
 * of its own: what an earlier open asked for does not carry over.
 */

/* SOCK_MIN_RCVBUF on 64-bit hosts: 2 * (2048 + the aligned struct sk_buff) */
#define KERNEL_MIN_RCVBUF 4608

/* The default for socket-udp-loopback-1472.yaml: 256 x 2048 is below 4 MiB */
#define DEFAULT_RCVBUF (4LL << 20)

#define INT_MAX_LL 0x7fffffffLL

/* A whole number from /proc/sys, or -1 */
static long long proc_ll(const char *path)
{
	long long val = -1;
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (fscanf(f, "%lld", &val) != 1)
		val = -1;
	fclose(f);

	return val;
}

/* Whether this process may force a receive buffer past net.core.rmem_max:
 * tried on a socket of its own, since CAP_NET_ADMIN only counts in the
 * initial user namespace */
static bool can_force(void)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0), val = 65536;
	bool ok;

	if (fd < 0)
		return false;
	ok = !setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &val, sizeof(val));
	close(fd);

	return ok;
}

/* What the kernel reports for <asked> bytes, as the backend asks for them */
static long long rcvbuf_effective(long long asked)
{
	long long rmem_max = proc_ll("/proc/sys/net/core/rmem_max");
	long long val = asked;

	if (rmem_max >= 0 && val > rmem_max && !can_force())
		val = rmem_max;
	if (val > INT_MAX_LL / 2)
		val = INT_MAX_LL / 2;

	return val * 2 > KERNEL_MIN_RCVBUF ? val * 2 : KERNEL_MIN_RCVBUF;
}

/* The receive buffer of this process's UDP socket bound to
 * 127.0.0.1:LOCAL_PORT and connected to 127.0.0.1:<peer>, read by the test
 * itself; -1 when there is not exactly one */
static long long engine_socket_rcvbuf(unsigned int peer)
{
	struct sockaddr_in sin;
	struct dirent *de;
	long long ret = -1;
	unsigned int nb = 0;
	socklen_t len;
	int fd, val;
	DIR *d;

	d = opendir("/proc/self/fd");
	if (!d)
		return -1;

	while ((de = readdir(d))) {
		fd = atoi(de->d_name);
		if (de->d_name[0] == '.' || fd == dirfd(d))
			continue;

		len = sizeof(val);
		if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &val, &len) || val != SOCK_DGRAM)
			continue;
		len = sizeof(sin);
		if (getsockname(fd, (struct sockaddr *)&sin, &len) || sin.sin_family != AF_INET ||
				ntohs(sin.sin_port) != LOCAL_PORT ||
				sin.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
			continue;
		len = sizeof(sin);
		if (getpeername(fd, (struct sockaddr *)&sin, &len) || ntohs(sin.sin_port) != peer)
			continue;

		len = sizeof(val);
		if (!getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, &len))
			ret = val;
		nb++;
	}
	closedir(d);

	return nb == 1 ? ret : -1;
}

/* Open the buffer with <extra>, read daqiri_rx_socket_buffer; -1, failing
 * the case, when the buffer did not open or has no such number */
static long long rcvbuf_open(const char *extra)
{
	long long val;
	int ret = lb_open(extra);

	CHECK_EQ(ret, 0, "data=udp buffer opened");
	if (ret)
		return -1;
	TEST_ASSERT_STR_EQ(attr_str("daqiri_data_path"), "udp", "data path");
	printf("  %s: daqiri_rx_socket_buffer %s (net.core.rmem_max %lld, rmem_default %lld%s)\n",
			extra[0] ? extra + 1 : "no rcvbuf=", attr_str("daqiri_rx_socket_buffer"),
			proc_ll("/proc/sys/net/core/rmem_max"),
			proc_ll("/proc/sys/net/core/rmem_default"),
			can_force() ? ", may force" : "");

	val = attr_ll("daqiri_rx_socket_buffer");
	TEST_ASSERT(val >= 0, "daqiri_rx_socket_buffer read as a number");

	return val;
}

/* Start collecting what the next context logs */
static bool log_start(void)
{
	lb_log = tmpfile();
	TEST_ASSERT_PTR_NOT_NULL(lb_log, "log file");

	return lb_log != NULL;
}

/* What it logged, into <text> */
static void log_end(char *text, size_t len)
{
	size_t n;

	rewind(lb_log);
	n = fread(text, 1, len - 1, lb_log);
	text[n] = '\0';
	fclose(lb_log);
	lb_log = NULL;
	printf("  logged: %s", text[0] ? text : "nothing\n");
}

/* Without rcvbuf=: num_bufs * buf_size, at least 4 MiB */
TEST_FUNCTION(rcvbuf_default)
{
	long long got = rcvbuf_open("");

	TEST_ASSERT(got >= KERNEL_MIN_RCVBUF, "at least the kernel's minimum");
	CHECK_EQ(got, rcvbuf_effective(DEFAULT_RCVBUF), "4 MiB, as the kernel grants it");
	CHECK_EQ(engine_socket_rcvbuf(SENDER_PORT), got, "the engine's socket has it");
}

/* Without rcvbuf=, a YAML whose packet buffers take more than 4 MiB:
 * socket-udp-loopback-9m.yaml, next to the usual one, 4608 x 2048 */
TEST_FUNCTION(rcvbuf_from_yaml)
{
	static char path[4096];
	const char *slash = strrchr(yaml, '/');
	long long got;

	snprintf(path, sizeof(path), "%.*ssocket-udp-loopback-9m.yaml",
			slash ? (int)(slash - yaml + 1) : 0, yaml);
	yaml = path;

	got = rcvbuf_open("");
	CHECK_EQ(got, rcvbuf_effective(4608LL * 2048),
			"num_bufs * buf_size, as the kernel grants it");
}

/* rcvbuf=0: nothing asked, nothing looked up, nothing logged; the socket
 * keeps the kernel's default and the attribute reads 0 */
TEST_FUNCTION(rcvbuf_zero)
{
	char text[4096] = "";
	long long got;

	if (!log_start())
		return;
	got = rcvbuf_open(",rcvbuf=0");
	log_end(text, sizeof(text));

	CHECK_EQ(got, 0, "daqiri_rx_socket_buffer 0: not sized");
	CHECK_EQ(engine_socket_rcvbuf(SENDER_PORT), proc_ll("/proc/sys/net/core/rmem_default"),
			"the engine's socket keeps net.core.rmem_default");
	TEST_ASSERT(strstr(text, "WARNING: ") == NULL, "no warning");
	TEST_ASSERT(strstr(text, "ERROR: ") == NULL, "no error");
}

/* rcvbuf=N: N, as the kernel grants it */
TEST_FUNCTION(rcvbuf_set)
{
	long long got = rcvbuf_open(",rcvbuf=1000000");

	TEST_ASSERT(got >= KERNEL_MIN_RCVBUF, "at least the kernel's minimum");
	CHECK_EQ(got, rcvbuf_effective(1000000), "1000000 bytes, as the kernel grants it");
}

/* More than the kernel grants: a warning that names net.core.rmem_max and
 * rcvbuf=, and the buffer opens all the same. The kernel never grants
 * INT_MAX (it grants INT_MAX / 2 at most). */
TEST_FUNCTION(rcvbuf_short)
{
	char text[4096] = "";
	long long got;

	if (!log_start())
		return;
	got = rcvbuf_open(",rcvbuf=2147483647");
	log_end(text, sizeof(text));

	CHECK_EQ(got, rcvbuf_effective(INT_MAX_LL), "as the kernel grants it");
	TEST_ASSERT(strstr(text, "WARNING: ") != NULL, "a warning");
	TEST_ASSERT(strstr(text, "ERROR: ") == NULL, "no error");
	TEST_ASSERT(strstr(text, "net.core.rmem_max") != NULL, "naming net.core.rmem_max");
	TEST_ASSERT(strstr(text, "rcvbuf=") != NULL, "and the rcvbuf= key");
}

/*
 * socket-udp-loopback-shared.yaml: interfaces a and b both on local_addr
 * 127.0.0.1:25101 (a connected to :25102, b to :25103), so DAQIRI answers
 * the same connection, a's, for both. A buffer on b must not size a's
 * socket: the backend says it cannot tell them apart, does not blame
 * net.core.rmem_max, and reads back b's own socket, untouched.
 */
#define SHARED_PEER_PORT 25103

TEST_FUNCTION(rcvbuf_shared_port)
{
	static char path[4096];
	const char *slash = strrchr(yaml, '/');
	long long rmem_default = proc_ll("/proc/sys/net/core/rmem_default");
	char text[4096] = "";
	long long got;

	snprintf(path, sizeof(path), "%.*ssocket-udp-loopback-shared.yaml",
			slash ? (int)(slash - yaml + 1) : 0, yaml);
	yaml = path;

	if (!log_start())
		return;
	got = rcvbuf_open(",iface=b");
	log_end(text, sizeof(text));

	CHECK_EQ(got, rmem_default, "b's socket read back: the kernel's default");
	CHECK_EQ(engine_socket_rcvbuf(SHARED_PEER_PORT), rmem_default, "b's socket untouched");
	CHECK_EQ(engine_socket_rcvbuf(SENDER_PORT), rmem_default, "a's socket untouched");
	TEST_ASSERT(strstr(text, "WARNING: ") != NULL, "a warning");
	TEST_ASSERT(strstr(text, "ERROR: ") == NULL, "no error");
	TEST_ASSERT(strstr(text, "local_addr") != NULL, "naming the shared local_addr");
	TEST_ASSERT(strstr(text, "net.core.rmem_max") == NULL, "not blaming net.core.rmem_max");
}

/*
 * A burst into a stalled consumer: the buffer is open, never enabled, so the
 * backend takes nothing from the engine's queue. The engine's receive thread
 * moves datagrams from the socket into that queue until its 256 packet
 * buffers (num_bufs) are taken, and the kernel holds the rest up to the
 * socket's receive buffer. BURST_DGRAMS datagrams of 1472 bytes go to the
 * engine's socket from its remote_addr as fast as the test can send them;
 * the kernel counts what found the receive buffer full in the drops column
 * of the socket's /proc/net/udp line.
 *
 * How many the engine's thread takes during the burst, and when the kernel
 * processes the loopback backlog, depends on scheduling. The checks hold
 * whatever it is: a small buffer loses at least BURST_DGRAMS - 256 - what it
 * holds, and burst_sized runs only where the buffer holds the whole burst,
 * even with nothing taken by the engine. The burst stays below the default
 * net.core.netdev_max_backlog (1000), whose drops /proc/net/udp would not
 * show; the test prints those of /proc/net/softnet_stat besides.
 */
#define BURST_DGRAMS 768
/* What one 1472-byte datagram takes of the receive buffer on loopback, at
 * most (its skb's truesize: about 2.3 KiB) */
#define DGRAM_TRUESIZE_MAX 4096
/* burst_small: 128 KiB as reported, some 55 datagrams */
#define SMALL_RCVBUF 65536

/* The drops of the socket bound to 127.0.0.1:LOCAL_PORT and connected to
 * 127.0.0.1:SENDER_PORT, -1 if none */
static long long socket_drops(void)
{
	char line[512], local[64], rem[64];
	unsigned long long drops;
	long long ret = -1;
	unsigned int port, rport;
	FILE *f = fopen("/proc/net/udp", "r");

	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f)) {
		/* sl local rem st tx:rx tr:when retrnsmt uid timeout inode ref
		 * pointer drops */
		if (sscanf(line, " %*u: %63s %63s %*x %*s %*s %*s %*u %*u %*u %*u %*s %llu", local,
				    rem, &drops) != 3)
			continue;
		if (sscanf(local, "0100007F:%x", &port) == 1 && port == LOCAL_PORT &&
				sscanf(rem, "0100007F:%x", &rport) == 1 && rport == SENDER_PORT) {
			ret = (long long)drops;
			break;
		}
	}
	fclose(f);

	return ret;
}

/* Packets the kernel dropped from its per-CPU backlogs, all CPUs (the second
 * column of /proc/net/softnet_stat, hex), or -1 */
static long long backlog_drops(void)
{
	unsigned int processed, dropped;
	long long ret = 0;
	char line[512];
	FILE *f = fopen("/proc/net/softnet_stat", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%x %x", &processed, &dropped) == 2)
			ret += dropped;
	fclose(f);

	return ret;
}

/* Send the burst; the drops it caused, or -1 */
static long long burst(void)
{
	struct sockaddr_in from = {
		.sin_family = AF_INET,
		.sin_port = htons(SENDER_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct sockaddr_in to = {
		.sin_family = AF_INET,
		.sin_port = htons(LOCAL_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	static uint8_t dgram[IIOD_UDP_STREAM_HDR_LEN + PAYLOAD];
	long long before, after, last, bl_before, bl_after;
	unsigned int i, sent = 0;
	struct timespec t0;
	int fd, one = 1;

	before = socket_drops();
	TEST_ASSERT(before >= 0, "the engine's socket in /proc/net/udp");
	if (before < 0)
		return -1;
	bl_before = backlog_drops();

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
			bind(fd, (struct sockaddr *)&from, sizeof(from))) {
		TEST_ASSERT(false, "sender bound to the YAML's remote_addr");
		if (fd >= 0)
			close(fd);
		return -1;
	}

	for (i = 0; i < BURST_DGRAMS; i++)
		sent += sendto(fd, dgram, sizeof(dgram), 0, (struct sockaddr *)&to, sizeof(to)) ==
			sizeof(dgram);
	close(fd);
	CHECK_EQ(sent, BURST_DGRAMS, "every datagram of the burst sent");

	/* Loopback delivers on the sender's way out; wait until the count has
	 * stayed put for 100 ms all the same */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	after = socket_drops();
	do {
		last = after;
		sleep_ms(100);
		after = socket_drops();
	} while (after != last && ms_since(&t0) < WAIT_TIMEOUT_MS);
	bl_after = backlog_drops();

	printf("  %u datagrams sent into a stalled consumer: %lld dropped by the socket, %lld "
	       "from the backlogs (all CPUs)\n",
			sent, after - before,
			bl_before >= 0 && bl_after >= 0 ? bl_after - bl_before : -1LL);

	return after - before;
}

/* A small buffer, whatever the host's net.core.rmem_default: rcvbuf=65536
 * loses datagrams of the burst */
TEST_FUNCTION(burst_small)
{
	long long got = rcvbuf_open(",rcvbuf=65536"), drops;

	if (got < 0)
		return;
	CHECK_EQ(got, rcvbuf_effective(SMALL_RCVBUF), "65536 bytes, as the kernel grants it");

	drops = burst();
	TEST_ASSERT(drops > 0, "the small receive buffer loses datagrams of the burst");
}

/* Without rcvbuf=: nothing lost. Whether the host grants room for the whole
 * burst is decided from net.core.rmem_max and whether the process may force
 * the size, never from the attribute under test; where it does not, the case
 * exits 77 (skipped). */
static int burst_sized_skip;

TEST_FUNCTION(burst_sized)
{
	const long long need = (long long)BURST_DGRAMS * DGRAM_TRUESIZE_MAX;
	long long want = rcvbuf_effective(DEFAULT_RCVBUF), got, drops;

	if (want < need) {
		printf("  SKIP: this host grants %lld bytes for the 4 MiB asked (net.core.rmem_max "
		       "%lld), %lld are needed: raise net.core.rmem_max to %lld or more\n",
				want, proc_ll("/proc/sys/net/core/rmem_max"), need, need / 2);
		burst_sized_skip = 1;
		return;
	}

	got = rcvbuf_open("");
	if (got < 0)
		return;
	CHECK_EQ(got, want, "4 MiB, as the kernel grants it, before the burst");
	if (got != want)
		return;

	drops = burst();
	CHECK_EQ(drops, 0, "the sized receive buffer loses nothing of the burst");
}

struct lb_case {
	const char *name;
	void (*run)(void);
	/* The emulator's options after --udp */
	const char *const flags[8];
	/* Runs only when named on the command line */
	bool alone;
};

static const struct lb_case cases[] = {
	{ "clean", test_clean, { "--udp-pps", "250", "--udp-count", "64" } },
	{ "drop_list", test_drop_list,
			{ "--udp-pps", "250", "--udp-count", "64", "--udp-drop-list", "5,6,20" } },
	{ "drop_list_lossy", test_drop_list_lossy,
			{ "--udp-pps", "250", "--udp-count", "64", "--udp-drop-list", "5,6,20" } },
	{ "drop_every", test_drop_every,
			{ "--udp-pps", "250", "--udp-count", "64", "--udp-drop-every", "2" } },
	{ "reorder", test_reorder,
			{ "--udp-pps", "250", "--udp-count", "64", "--udp-reorder-window", "3" } },
	{ "duplicate", test_duplicate,
			{ "--udp-pps", "250", "--udp-count", "64", "--udp-dup-every", "8" } },
	{ "slow_consumer", test_slow_consumer, { "--udp-pps", "250", "--udp-count", "12" } },
	{ "slow_consumer_lossy", test_slow_consumer_lossy,
			{ "--udp-pps", "250", "--udp-count", "12" } },
	{ "burst_gap", test_burst_gap, { "--udp-pps", "250", "--udp-count", "32" } },
	{ "rcvbuf_default", test_rcvbuf_default, { NULL } },
	{ "rcvbuf_from_yaml", test_rcvbuf_from_yaml, { NULL } },
	{ "rcvbuf_zero", test_rcvbuf_zero, { NULL } },
	{ "rcvbuf_set", test_rcvbuf_set, { NULL } },
	{ "rcvbuf_short", test_rcvbuf_short, { NULL } },
	{ "rcvbuf_shared_port", test_rcvbuf_shared_port, { "--udp-from", "127.0.0.1:25103" } },
	{ "burst_small", test_burst_small, { NULL } },
	{ "burst_sized", test_burst_sized, { NULL }, true },
};

#define NB_CASES (sizeof(cases) / sizeof(cases[0]))

/* One case, against the emulator with_emu.sh started for it */
static int run_case(const char *name)
{
	unsigned int i;
	int ret;

	yaml = getenv("DAQIRI_YAML");
	if (!yaml || !yaml[0]) {
		printf("DAQIRI_YAML must be the path of socket-udp-loopback-1472.yaml\n");
		return 1;
	}

	for (i = 0; i < NB_CASES && strcmp(cases[i].name, name); i++)
		;
	if (i == NB_CASES) {
		printf("No case %s\n", name);
		return 2;
	}

	ret = map_offsets();
	if (ret) {
		printf("The sample file does not tell %d datagrams apart: %d\n", MAX_SEQ, ret);
		return 1;
	}

	printf("Running test: %s\n", name);
	cases[i].run();
	lb_close();

	printf("test_udp_loopback --case %s: %d/%d passed, %d failed\n", name, test_passed,
			test_count, test_failed);

	if (test_failed)
		return 1;
	return burst_sized_skip ? 77 : 0;
}

/* Whether case <i> runs: every one but the "alone" ones without <names>,
 * else the ones named */
static bool selected(unsigned int i, char *const *names, int nb_names)
{
	int k;

	if (!nb_names)
		return !cases[i].alone;

	for (k = 0; k < nb_names; k++)
		if (!strcmp(names[k], cases[i].name))
			return true;

	return false;
}

/* Each case in a process of its own, under with_emu.sh with its flags; 77
 * when every case that ran was skipped */
static int run_all(const char *with_emu, const char *xml, const char *sample, char *const *names,
		int nb_names)
{
	const char *argv[32];
	unsigned int i, j, n, ran = 0, failed = 0, skipped = 0;
	char self[4096];
	ssize_t len;
	int k, status;
	pid_t pid;

	for (k = 0; k < nb_names; k++) {
		for (i = 0; i < NB_CASES && strcmp(names[k], cases[i].name); i++)
			;
		if (i == NB_CASES) {
			printf("No case %s\n", names[k]);
			return 2;
		}
	}

	len = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (len < 0) {
		perror("/proc/self/exe");
		return 1;
	}
	self[len] = '\0';

	for (i = 0; i < NB_CASES; i++) {
		if (!selected(i, names, nb_names))
			continue;

		ran++;
		n = 0;
		argv[n++] = with_emu;
		argv[n++] = "--emu-args";
		argv[n++] = "--udp";
		for (j = 0; j < sizeof(cases[i].flags) / sizeof(cases[i].flags[0]) &&
				cases[i].flags[j];
				j++)
			argv[n++] = cases[i].flags[j];
		argv[n++] = "--udp-json";
		argv[n++] = oracle_path;
		argv[n++] = "--";
		argv[n++] = xml;
		argv[n++] = self;
		argv[n++] = "--case";
		argv[n++] = cases[i].name;
		argv[n++] = sample;
		argv[n++] = oracle_path;
		argv[n] = NULL;

		printf("== %s\n", cases[i].name);
		fflush(stdout);

		pid = fork();
		if (pid < 0) {
			perror("fork");
			return 1;
		}
		if (!pid) {
			execv(with_emu, (char *const *)argv);
			_exit(127);
		}

		while (waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR) {
				perror("waitpid");
				return 1;
			}
		}

		if (WIFEXITED(status) && WEXITSTATUS(status) == 77) {
			printf("== %s SKIPPED\n", cases[i].name);
			skipped++;
		} else if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			printf("== %s FAILED\n", cases[i].name);
			failed++;
		}
	}

	printf("test_udp_loopback: %u/%u cases passed, %u skipped\n", ran - failed - skipped, ran,
			skipped);

	if (failed)
		return 1;
	return skipped && skipped == ran ? 77 : 0;
}

int main(int argc, char **argv)
{
	int ret;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc == 5 && !strcmp(argv[1], "--case")) {
		oracle_path = argv[4];
		ret = read_file(argv[3], &file, &file_len);
		if (ret || !file_len) {
			printf("Unable to read the sample file %s: %d\n", argv[3], ret);
			return 1;
		}

		ret = run_case(argv[2]);
		free(file);
		return ret;
	}

	if (argc < 5 || !strcmp(argv[1], "--case")) {
		printf("Usage: %s <with_emu.sh> <xml> <sample.bin> <oracle.json> [<case>...]\n"
		       "       %s --case <name> <sample.bin> <oracle.json>\n",
				argv[0], argv[0]);
		return 2;
	}

	oracle_path = argv[4];

	return run_all(argv[1], argv[2], argv[3], argv + 5, argc - 5);
}
