/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Lifecycle test of daqiri:<host>,data=udp over the DAQIRI socket engine,
 * against iiod-emu serving fmcomms2.xml with --udp-mock-provider, with the
 * engine YAML from the DAQIRI_YAML environment variable (tests/daqiri/
 * resources/socket-udp-loopback.yaml). The mock advertises UDP sample mode,
 * answers every setup with stream ID 0x12345678, max_payload rounded down
 * to 16 bytes and the source 127.0.0.1:25102 (the YAML's remote_addr), and
 * never sends a datagram itself.
 *
 * The engine is process-wide and is never shut down, so the cases share
 * state and run in order: (a) initialises the engine with DAQIRI_YAML, which
 * is what makes a second YAML fail in (d); (a)-(c) use one context, which
 * (c) destroys with its buffer still open; (e)-(h) then need the queue that
 * (c) must have released.
 *
 * (h) sends hand-built datagrams of the mock's stream from 127.0.0.1:25102
 * (the engine's socket only accepts that source) to check that blocks
 * complete with the payloads unchanged, fail on a gap, resync on a gap too
 * large to fill, zero-fill with lossy=1, and that the sequence restarts at 0
 * after a new enable; then that a gap too large to fill that comes while no
 * block is queued still fails the next block, even with lossy=1. Loss
 * statistics against a real sender are the UDP loopback tests'.
 *
 * exit_with_open_buffer runs before (a), while this process has no engine:
 * forked children start one each and exit with a buffer streaming.
 *
 * The last cases read the mock's log (DAQIRI_MOCK_LOG) to see which calls a
 * case made: rows of the decision table against a server that advertises UDP
 * sample mode (data=udp without a YAML fails with -EINVAL, data=auto without a
 * YAML streams over TCP, neither sending a setup), a setup refused at enable,
 * which fails the enable with the server's error and no fallback, after a
 * DISABLE that drops the probe's pending setup, and the close: a cancel stops
 * nothing on the server, the close ends the stream, 50 times over.
 *
 * With --mixed (served float_scan_elements.xml), only the channel layout is
 * checked: UDP sample mode takes the device's own layout, so 16- and 32-bit
 * channels open in one buffer. With --wrong-source (served by iiod-emu --udp
 * sending from another address than the YAML's remote_addr), only the source
 * check: the buffer does not open, with data=udp or data=auto. With
 * --enable-fails <yaml>, --disable-fails-once or --disable-fails-twice
 * (served with --udp-mock-fail-disable 0, 1 or 2, so that the mock also logs
 * every DISABLE_BUFFER), only that case: an ENABLE_BUFFER that fails after a
 * successful setup, and a DISABLE_BUFFER that fails once, then twice.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../daqiri-udp-stream.h"
#include "../../iiod-udp-stream.h"
#include "test_framework.h"

#define DEV_NAME "cf-ad9361-lpc"
#define DDS_NAME "cf-ad9361-dds-core-lpc"

/* voltage0+voltage1, le:S16 each: 4 bytes per sample */
#define BLOCK_SIZE (360 * 4)
#define MAX_BLOCKS 4

#define LOCAL_PORT 25101  /* YAML local_addr */
#define SENDER_PORT 25102 /* YAML remote_addr */

/* What the mock answers (iiod-emu/udp-mock.c): the backend asks for 9200
 * bytes, the YAML's 9216-byte buffers less the datagram header */
#define MOCK_STREAM_ID 0x12345678u
#define PAYLOAD 9200
#define SPD (PAYLOAD / 4) /* samples per datagram */

/* CHECK_EQ() evaluates <actual> again to print it: evaluate once */
#define CHECK_EQ(actual, expected, message) \
	do { \
		long check_val_ = (long)(actual); \
		TEST_ASSERT_EQ(check_val_, (long)(expected), message); \
	} while (0)

static const char *yaml;
/* DAQIRI_MOCK_LOG: the log of the mock provider, one line per call */
static const char *mock_log;

struct rx {
	struct iio_context *ctx;
	struct iio_buffer *buf;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *stream;
	struct iio_block *blocks[MAX_BLOCKS];
	unsigned int nb_blocks;
};

/* (a)-(c) share this one */
static struct rx shared;

static struct iio_context *ctx_uri(const char *uri)
{
	struct iio_context *ctx = iio_create_context(NULL, uri);

	if (iio_err(ctx)) {
		printf("  %s: unable to create context: %d\n", uri, iio_err(ctx));
		return NULL;
	}

	return ctx;
}

/* <extra>: more URI options, "" or ",key=value..." */
static struct iio_context *ctx_opts(const char *path, const char *extra)
{
	char uri[4300];

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=udp,yaml=%s%s", path, extra);

	return ctx_uri(uri);
}

static struct iio_context *ctx_for(const char *path)
{
	return ctx_opts(path, "");
}

static void rx_close(struct rx *rx)
{
	while (rx->nb_blocks)
		iio_block_destroy(rx->blocks[--rx->nb_blocks]);
	if (rx->stream)
		iio_buffer_close(rx->stream);
	rx->stream = NULL;
}

static void rx_destroy(struct rx *rx)
{
	rx_close(rx);
	if (rx->mask)
		iio_channels_mask_destroy(rx->mask);
	if (rx->ctx)
		iio_context_destroy(rx->ctx);
	memset(rx, 0, sizeof(*rx));
}

static const char *const iq_chans[] = { "voltage0", "voltage1" };

/* Mask of chans[0]+chans[1] of <dev_name> (outputs if <output>), buffer 0. */
static int rx_init_chans(struct rx *rx, struct iio_context *ctx, const char *dev_name,
		const char *const *chans, bool output)
{
	const struct iio_device *dev;
	struct iio_channel *chn;
	unsigned int i;

	memset(rx, 0, sizeof(*rx));
	rx->ctx = ctx;
	if (!ctx)
		return -ENODEV;

	dev = iio_context_find_device(ctx, dev_name);
	if (!dev)
		return -ENODEV;

	rx->mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!rx->mask)
		return -ENOMEM;

	for (i = 0; i < 2; i++) {
		chn = iio_device_find_channel(dev, chans[i], output);
		if (!chn)
			return -ENOENT;
		iio_channel_enable(chn, rx->mask);
	}

	rx->buf = iio_device_get_buffer(dev, 0);

	return rx->buf ? 0 : -ENODEV;
}

/* voltage0+voltage1 of <dev_name> */
static int rx_init(struct rx *rx, struct iio_context *ctx, const char *dev_name, bool output)
{
	return rx_init_chans(rx, ctx, dev_name, iq_chans, output);
}

static int rx_open_size(struct rx *rx, unsigned int nb_blocks, size_t size)
{
	struct iio_block *block;
	int ret;

	rx->stream = iio_buffer_open(rx->buf, rx->mask);
	ret = iio_err(rx->stream);
	if (ret) {
		rx->stream = NULL;
		return ret;
	}

	while (rx->nb_blocks < nb_blocks) {
		block = iio_buffer_stream_create_block(rx->stream, size);
		ret = iio_err(block);
		if (ret)
			return ret;
		rx->blocks[rx->nb_blocks++] = block;
	}

	return 0;
}

static int rx_open(struct rx *rx, unsigned int nb_blocks)
{
	return rx_open_size(rx, nb_blocks, BLOCK_SIZE);
}

static int enqueue_all(struct rx *rx)
{
	unsigned int i;
	int ret;

	for (i = 0; i < rx->nb_blocks; i++) {
		ret = iio_block_enqueue(rx->blocks[i], 0, false);
		if (ret)
			return ret;
	}

	return 0;
}

static const char *attr_str(const struct iio_buffer *buf, const char *name)
{
	static char val[64];
	const struct iio_attr *attr = iio_buffer_find_attr(buf, name);
	ssize_t ret;

	if (!attr)
		return "(missing)";

	ret = iio_attr_read_raw(attr, val, sizeof(val));
	if (ret < 0)
		snprintf(val, sizeof(val), "(error %zd)", ret);

	return val;
}

static long long attr_ll(const struct iio_buffer *buf, const char *name)
{
	const struct iio_attr *attr = iio_buffer_find_attr(buf, name);
	long long val;

	if (!attr || iio_attr_read_longlong(attr, &val))
		return -1;

	return val;
}

static ssize_t attr_write(const struct iio_buffer *buf, const char *name, const char *val)
{
	const struct iio_attr *attr = iio_buffer_find_attr(buf, name);

	return attr ? iio_attr_write_string(attr, val) : -ENOENT;
}

static bool module_mapped(void)
{
	FILE *f = fopen("/proc/self/maps", "r");
	bool found = false;
	char line[4096];

	if (!f)
		return false;

	while (!found && fgets(line, sizeof(line), f))
		found = strstr(line, "/libiio-daqiri.so") != NULL;
	fclose(f);

	return found;
}

/* Exit (not destroy) with a buffer streaming; the child must exit cleanly */
static void exit_child(unsigned int sleep_ms)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)sleep_ms * 1000000L };
	struct rx rx;

	if (rx_init(&rx, ctx_for(yaml), DEV_NAME, false) || rx_open(&rx, 2) || enqueue_all(&rx) ||
			iio_buffer_stream_start(rx.stream))
		_exit(100);

	nanosleep(&ts, NULL);
	exit(0);
}

/* The process has no engine yet: each child starts its own */
TEST_FUNCTION(exit_with_open_buffer)
{
	unsigned int i, clean = 0;
	int status;
	pid_t pid;

	for (i = 0; i < 20; i++) {
		pid = fork();
		if (pid < 0)
			break;
		if (!pid)
			exit_child(20 + (i % 4) * 15);

		if (waitpid(pid, &status, 0) != pid)
			continue;
		if (WIFEXITED(status) && !WEXITSTATUS(status))
			clean++;
		else if (WIFSIGNALED(status))
			printf("  child %u: signal %d\n", i, WTERMSIG(status));
		else
			printf("  child %u: exit status %d\n", i, WEXITSTATUS(status));
	}

	CHECK_EQ(clean, 20, "20 children exited with status 0 while streaming");
}

/* (a) blocks queued, nothing sent: nonblocking dequeue is -EBUSY */
TEST_FUNCTION(busy_when_empty)
{
	struct rx *rx = &shared;
	int ret;

	ret = rx_init(rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context and mask");
	if (ret)
		return;

	ret = rx_open(rx, 2);
	CHECK_EQ(ret, 0, "data=udp buffer opens with 2 blocks of 1440");
	if (ret)
		return;

	CHECK_EQ(iio_block_dequeue(rx->blocks[0], false), -EPERM,
			"dequeue of a block never enqueued");
	CHECK_EQ(iio_block_enqueue(rx->blocks[0], BLOCK_SIZE / 2, false), -EINVAL,
			"partial RX block rejected");
	CHECK_EQ(iio_block_enqueue(rx->blocks[0], 0, true), -ENOTSUP, "cyclic RX block rejected");

	CHECK_EQ(enqueue_all(rx), 0, "enqueue both blocks");
	CHECK_EQ(iio_block_enqueue(rx->blocks[0], 0, false), -EPERM, "double enqueue");
	CHECK_EQ(iio_buffer_stream_start(rx->stream), 0, "enable");

	CHECK_EQ(iio_block_dequeue(rx->blocks[0], true), -EBUSY, "nonblocking dequeue");

	/* The block stays queued after a timeout */
	CHECK_EQ(iio_context_set_timeout(rx->ctx, 100), 0, "timeout 100 ms");
	CHECK_EQ(iio_block_dequeue(rx->blocks[0], false), -ETIMEDOUT, "blocking dequeue times out");
	CHECK_EQ(iio_block_dequeue(rx->blocks[0], true), -EBUSY, "still queued");
	CHECK_EQ(iio_context_set_timeout(rx->ctx, 0), 0, "default timeout");
}

/* (b) a cancelled buffer fails blocking dequeues with -ECANCELED */
TEST_FUNCTION(cancel_fails_dequeue)
{
	struct rx *rx = &shared;

	if (!rx->stream || rx->nb_blocks != 2) {
		TEST_ASSERT(false, "needs the buffer from (a)");
		return;
	}

	iio_buffer_stream_cancel(rx->stream);
	CHECK_EQ(iio_block_dequeue(rx->blocks[0], false), -ECANCELED, "block 0 cancelled");
	CHECK_EQ(iio_block_dequeue(rx->blocks[1], false), -ECANCELED, "block 1 cancelled");
	CHECK_EQ(iio_block_dequeue(rx->blocks[0], false), -EPERM, "block 0 now free");
	CHECK_EQ(iio_block_enqueue(rx->blocks[0], 0, false), -ECANCELED, "no enqueue after cancel");
}

/* (c) context destroyed with the buffer and its blocks still open */
TEST_FUNCTION(destroy_with_open_buffer)
{
	struct rx *rx = &shared;

	if (!rx->ctx) {
		TEST_ASSERT(false, "needs the context from (a)");
		return;
	}

	/* The stream and the blocks are leaked on purpose: after this, only
	 * the backend can still reach them. */
	iio_context_destroy(rx->ctx);
	iio_channels_mask_destroy(rx->mask);
	memset(rx, 0, sizeof(*rx));
	TEST_ASSERT(true, "iio_context_destroy returned with a UDP buffer open");

	/* That was the last context, so the core dlclose()d the module: the
	 * engine's threads still run, it must have stayed loaded */
	TEST_ASSERT(module_mapped(), "libiio-daqiri.so still mapped (pinned)");
}

/* (d) a second YAML, even with the same content, cannot get the engine */
TEST_FUNCTION(second_yaml_busy)
{
	const char *tmp = getenv("TMPDIR");
	char path[4096], data[8192];
	struct rx rx;
	size_t len;
	FILE *in, *out;
	int fd, ret;

	snprintf(path, sizeof(path), "%s/daqiri-lifecycle-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
	fd = mkstemp(path);
	TEST_ASSERT(fd >= 0, "temporary YAML copy created");
	if (fd < 0)
		return;

	in = fopen(yaml, "rb");
	out = fdopen(fd, "wb");
	len = in ? fread(data, 1, sizeof(data), in) : 0;
	TEST_ASSERT(in && out && len > 0 && len < sizeof(data) && fwrite(data, 1, len, out) == len,
			"YAML copied");
	if (in)
		fclose(in);
	if (out)
		fclose(out);
	else
		close(fd);

	ret = rx_init(&rx, ctx_for(path), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context with the copy is created");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), -EBUSY, "buffer open with another YAML");

	rx_destroy(&rx);
	unlink(path);
}

/* (e) one (port, queue) per buffer */
TEST_FUNCTION(same_queue_busy)
{
	struct iio_buffer_stream *second;
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context and mask");
	if (ret)
		goto out;

	ret = rx_open(&rx, 0);
	CHECK_EQ(ret, 0, "queue released by (c), buffer opens");
	if (ret)
		goto out;

	second = iio_buffer_open(rx.buf, rx.mask);
	CHECK_EQ(iio_err(second), -EBUSY, "second open of port 0 queue 0");
	if (!iio_err(second))
		iio_buffer_close(second);

	rx_close(&rx);
	ret = rx_open(&rx, 0);
	CHECK_EQ(ret, 0, "queue released by close, buffer opens again");
out:
	rx_destroy(&rx);
}

/* (f) UDP sample mode is RX only */
TEST_FUNCTION(output_rejected)
{
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DDS_NAME, true);
	CHECK_EQ(ret, 0, "context and DDS mask");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), -ENOTSUP, "output buffer");
	rx_destroy(&rx);
}

/* (g) the counters are buffer attributes, served by the backend */
TEST_FUNCTION(counter_attrs)
{
	static const char *const names[] = {
		"daqiri_rx_packets",
		"daqiri_rx_bytes",
		"daqiri_rx_lost_packets",
		"daqiri_rx_gap_events",
		"daqiri_rx_dropped_no_block",
		"daqiri_rx_overrun_events",
		"daqiri_rx_foreign_packets",
		"daqiri_rx_malformed",
		"daqiri_rx_zero_filled_bytes",
		"daqiri_rx_late_packets",
		"daqiri_rx_duplicate_packets",
		"daqiri_blocks_completed",
		"daqiri_blocks_failed",
		"daqiri_blocks_with_gaps",
		"daqiri_stream_id",
		"daqiri_last_error",
		"daqiri_lossy",
	};
	const struct iio_attr *attr;
	unsigned int i;
	char msg[128];
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context and mask");
	if (ret)
		goto out;

	attr = iio_buffer_find_attr(rx.buf, "daqiri_rx_packets");
	TEST_ASSERT_PTR_NOT_NULL(attr, "daqiri_rx_packets exists");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_rx_packets"), "0", "reads 0 with no stream");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_mem_domain"), "host", "mem domain, no stream");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_engine_kind"), "none", "engine, no stream");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "data path, no stream");
	TEST_ASSERT_PTR_NULL(iio_buffer_find_attr(rx.buf, "daqiri_rx_context_packets"),
			"no daqiri_rx_context_packets");
	CHECK_EQ(attr_write(rx.buf, "daqiri_lossy", "1"), -EBADF,
			"daqiri_lossy needs an open stream");

	ret = rx_open(&rx, 0);
	CHECK_EQ(ret, 0, "buffer opens");
	if (ret)
		goto out;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		snprintf(msg, sizeof(msg), "%s reads 0 on a new stream", names[i]);
		TEST_ASSERT_STR_EQ(attr_str(rx.buf, names[i]), "0", msg);
	}
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_mem_domain"), "host", "mem domain");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_engine_kind"), "socket", "engine kind");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "data path");

	TEST_ASSERT(attr_write(rx.buf, "daqiri_lossy", "1") > 0, "daqiri_lossy written");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_lossy"), "1", "daqiri_lossy reads back");
	CHECK_EQ(attr_write(rx.buf, "daqiri_lossy", "2"), -EINVAL, "daqiri_lossy is 0 or 1");
	CHECK_EQ(attr_write(rx.buf, "daqiri_rx_packets", "5"), -EPERM, "counters are read-only");
out:
	rx_destroy(&rx);
}

/* Sample n of the stream, as ramp16_le (gen_golden.py): little-endian
 * int16 I = n & 0x7FFF, Q = (0x8000 - n) & 0xFFFF; datagram s carries samples
 * s * SPD to s * SPD + SPD - 1 */
static void put_sample(uint8_t *p, uint32_t n)
{
	uint16_t i = (uint16_t)(n & 0x7FFF), q = (uint16_t)((0x8000 - n) & 0xFFFF);

	p[0] = (uint8_t)i;
	p[1] = (uint8_t)(i >> 8);
	p[2] = (uint8_t)q;
	p[3] = (uint8_t)(q >> 8);
}

/* One datagram of <stream_id> and <version> with sequence number <seq> */
static int send_dgram(int fd, uint32_t stream_id, uint8_t version, uint64_t seq)
{
	static uint8_t dgram[IIOD_UDP_STREAM_HDR_LEN + PAYLOAD];
	const struct iiod_udp_stream_hdr hdr = {
		.version = version,
		.payload_length = PAYLOAD,
		.stream_id = stream_id,
		.sequence = seq,
	};
	struct sockaddr_in to = {
		.sin_family = AF_INET,
		.sin_port = htons(LOCAL_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	unsigned int n;

	iiod_udp_stream_hdr_pack(&hdr, dgram);
	for (n = 0; n < SPD; n++)
		put_sample(dgram + IIOD_UDP_STREAM_HDR_LEN + 4 * n, (uint32_t)(seq * SPD + n));

	if (sendto(fd, dgram, sizeof(dgram), 0, (struct sockaddr *)&to, sizeof(to)) !=
			sizeof(dgram))
		return -errno;

	return 0;
}

static int send_seq(int fd, uint64_t seq)
{
	return send_dgram(fd, MOCK_STREAM_ID, IIOD_UDP_STREAM_VERSION, seq);
}

/* The payload of datagram <seq>, unchanged: no byte swap */
static bool block_is(const struct iio_block *block, uint64_t seq)
{
	const uint8_t *p = iio_block_start(block);
	uint8_t want[4];
	unsigned int n;

	for (n = 0; n < SPD; n++) {
		put_sample(want, (uint32_t)(seq * SPD + n));
		if (memcmp(p + 4 * n, want, 4)) {
			printf("  sample %u: %02x %02x %02x %02x\n", n, p[4 * n], p[4 * n + 1],
					p[4 * n + 2], p[4 * n + 3]);
			return false;
		}
	}

	return true;
}

static bool block_is_zero(const struct iio_block *block)
{
	const uint8_t *p = iio_block_start(block);
	unsigned int n;

	for (n = 0; n < PAYLOAD; n++)
		if (p[n])
			return false;

	return true;
}

static int sender_open(void)
{
	struct sockaddr_in from = {
		.sin_family = AF_INET,
		.sin_port = htons(SENDER_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	int fd, one = 1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&from, sizeof(from))) {
		close(fd);
		return -errno;
	}

	return fd;
}

/* voltage0 alone: a 2-byte sample set, which the mock refuses (-ENOTSUP) */
static int rx_init_one(struct rx *rx, struct iio_context *ctx)
{
	struct iio_channel *chn;
	int ret;

	ret = rx_init(rx, ctx, DEV_NAME, false);
	if (ret)
		return ret;

	chn = iio_device_find_channel(iio_buffer_get_device(rx->buf), iq_chans[1], false);
	if (!chn)
		return -ENOENT;
	iio_channel_disable(chn, rx->mask);

	return 0;
}

/* A refused probe with data=auto: the buffer streams over TCP on its own
 * connection, and the queue is free for the next UDP buffer */
TEST_FUNCTION(refused_probe_auto)
{
	struct rx rx;
	int ret;

	ret = rx_init_one(&rx, ctx_opts(yaml, ",data=auto"));
	CHECK_EQ(ret, 0, "data=auto context, voltage0 alone");
	if (ret)
		goto out;

	ret = rx_open_size(&rx, 1, 1024);
	CHECK_EQ(ret, 0, "refused setup: the buffer opens all the same");
	if (ret)
		goto out;
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "tcp", "and streams over TCP");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_engine_kind"), "none", "without the engine");

	CHECK_EQ(enqueue_all(&rx), 0, "TCP block enqueued");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "TCP enable");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), 0, "TCP block received");
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "TCP disable");
	rx_close(&rx);

	/* voltage0 and voltage1: accepted, and the fallback gave the queue
	 * back */
	iio_channel_enable(
			iio_device_find_channel(iio_buffer_get_device(rx.buf), iq_chans[1], false),
			rx.mask);
	ret = rx_open(&rx, 0);
	CHECK_EQ(ret, 0, "the next buffer gets the queue");
	if (!ret)
		TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "over UDP");
out:
	rx_destroy(&rx);
}

/* A refused probe with data=udp: -ENOTSUP, and the context goes on */
TEST_FUNCTION(refused_probe_udp)
{
	const struct iio_device *phy;
	const struct iio_channel *chn;
	const struct iio_attr *attr;
	char val[64] = "";
	struct rx rx;
	int ret;

	ret = rx_init_one(&rx, ctx_for(yaml));
	CHECK_EQ(ret, 0, "data=udp context, voltage0 alone");
	if (ret)
		goto out;

	CHECK_EQ(rx_open(&rx, 0), -ENOTSUP, "refused setup: -ENOTSUP");

	phy = iio_context_find_device(rx.ctx, "ad9361-phy");
	chn = phy ? iio_device_find_channel(phy, "voltage0", false) : NULL;
	attr = chn ? iio_channel_find_attr(chn, "sampling_frequency") : NULL;
	TEST_ASSERT(attr && iio_attr_read_raw(attr, val, sizeof(val)) > 0,
			"the context still reads attributes");

	iio_channel_enable(
			iio_device_find_channel(iio_buffer_get_device(rx.buf), iq_chans[1], false),
			rx.mask);
	CHECK_EQ(rx_open(&rx, 0), 0, "and opens the next buffer on the queue");
out:
	rx_destroy(&rx);
}

/* --wrong-source, against iiod-emu --udp sending from another port than the
 * YAML's remote_addr: the socket engine would never receive the stream */
TEST_FUNCTION(wrong_source)
{
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "data=udp context and mask");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), -EADDRNOTAVAIL, "data=udp: -EADDRNOTAVAIL");
	rx_destroy(&rx);

	/* Not a refusal: no fallback either */
	ret = rx_init(&rx, ctx_opts(yaml, ",data=auto"), DEV_NAME, false);
	CHECK_EQ(ret, 0, "data=auto context and mask");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), -EADDRNOTAVAIL, "data=auto: -EADDRNOTAVAIL, no TCP");
	rx_destroy(&rx);
}

/* The mock picks payloads of max_payload rounded down to 16 bytes: 9200 is
 * no whole number of 6-byte samples */
TEST_FUNCTION(payload_not_whole_samples)
{
	static const char *const three[] = { "voltage0", "voltage1", "voltage2" };
	struct iio_channel *chn;
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context and mask");
	if (ret)
		goto out;

	chn = iio_device_find_channel(iio_buffer_get_device(rx.buf), three[2], false);
	TEST_ASSERT_PTR_NOT_NULL(chn, "voltage2");
	if (!chn)
		goto out;
	iio_channel_enable(chn, rx.mask);

	CHECK_EQ(rx_open(&rx, 0), -EPROTO, "a payload of no whole number of samples");
	CHECK_EQ(rx_open(&rx, 0), -EPROTO, "and again: the queue was given back");
out:
	rx_destroy(&rx);
}

/* (h) datagrams in: DONE, -EIO on a gap, a resync, zero-fill, a restart */
TEST_FUNCTION(data_smoke)
{
	struct iio_block **b;
	struct rx rx;
	int fd, ret;

	fd = sender_open();
	TEST_ASSERT(fd >= 0, "sender bound to 127.0.0.1:25102");
	if (fd < 0)
		return;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	if (!ret)
		ret = rx_open_size(&rx, 4, PAYLOAD);
	CHECK_EQ(ret, 0, "buffer with 4 blocks of one payload");
	if (ret)
		goto out;
	b = rx.blocks;

	CHECK_EQ(enqueue_all(&rx), 0, "enqueue 4");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_stream_id"), "0x12345678",
			"stream ID from the setup");

	/* Two consecutive datagrams fill two blocks */
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(send_seq(fd, 1), 0, "send 1");
	CHECK_EQ(iio_block_dequeue(b[0], false), 0, "block 0 done");
	TEST_ASSERT(block_is(b[0], 0), "block 0 is datagram 0, unchanged");
	CHECK_EQ(iio_block_dequeue(b[1], false), 0, "block 1 done");
	TEST_ASSERT(block_is(b[1], 1), "block 1 is datagram 1, unchanged");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_packets"), 2, "2 datagrams");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_bytes"), 2 * PAYLOAD, "2 payloads");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_completed"), 2, "2 blocks completed");

	/* 2 never arrives: block 2 fails, 3 lands in block 3 */
	CHECK_EQ(send_seq(fd, 3), 0, "send 3");
	CHECK_EQ(iio_block_dequeue(b[2], false), -EIO, "block 2 failed on the gap");
	CHECK_EQ(iio_block_dequeue(b[3], false), 0, "block 3 done");
	TEST_ASSERT(block_is(b[3], 3), "block 3 is datagram 3");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_lost_packets"), 1, "1 lost");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_gap_events"), 1, "1 gap event");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_failed"), 1, "1 block failed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_with_gaps"), 1, "1 block with a gap");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_last_error"), -EIO, "last error");

	/* lossy=1, one-datagram blocks: a gap past the 1024-datagram fill cap
	 * is a resync even right after a block completed. The next block
	 * fails, the datagram starts the one after, no zeros. */
	TEST_ASSERT(attr_write(rx.buf, "daqiri_lossy", "1") > 0, "lossy on");
	CHECK_EQ(iio_block_enqueue(b[0], 0, false), 0, "re-enqueue block 0");
	CHECK_EQ(iio_block_enqueue(b[1], 0, false), 0, "re-enqueue block 1");
	CHECK_EQ(iio_block_enqueue(b[2], 0, false), 0, "re-enqueue block 2");
	CHECK_EQ(send_seq(fd, 4), 0, "send 4");
	CHECK_EQ(iio_block_dequeue(b[0], false), 0, "block 0 done, no block current");
	CHECK_EQ(send_seq(fd, 4 + 2000), 0, "send the datagram 2000 after 4");
	CHECK_EQ(iio_block_dequeue(b[1], false), -EIO, "block 1 fails: resync");
	CHECK_EQ(iio_block_dequeue(b[2], false), 0, "block 2 done");
	TEST_ASSERT(block_is(b[2], 4 + 2000), "block 2 is the datagram after the gap");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_lost_packets"), 1 + 1999, "1999 more lost");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_zero_filled_bytes"), 0, "nothing zero-filled");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_failed"), 2, "2 blocks failed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_with_gaps"), 2, "2 blocks with a gap");

	/* Dropped and counted, no block touched: another stream, another
	 * version, the last one again, one that comes too late */
	CHECK_EQ(send_dgram(fd, MOCK_STREAM_ID + 1, IIOD_UDP_STREAM_VERSION, 2005), 0,
			"send another stream ID");
	CHECK_EQ(send_dgram(fd, MOCK_STREAM_ID, 2, 2005), 0, "send version 2");
	CHECK_EQ(send_seq(fd, 2004), 0, "send 2004 again");
	CHECK_EQ(send_seq(fd, 2003), 0, "send 2003, late");

	/* lossy=1, a gap of 1: one payload of zeros, then the datagram. The
	 * zeros fill block 0, so datagram 2006 waits in the carry for the next
	 * datagram, which attaches block 1. */
	CHECK_EQ(iio_block_enqueue(b[0], 0, false), 0, "re-enqueue block 0");
	CHECK_EQ(iio_block_enqueue(b[1], 0, false), 0, "re-enqueue block 1");
	CHECK_EQ(iio_block_enqueue(b[2], 0, false), 0, "re-enqueue block 2");
	CHECK_EQ(send_seq(fd, 2006), 0, "send 2006: 2005 lost");
	CHECK_EQ(send_seq(fd, 2007), 0, "send 2007");
	CHECK_EQ(iio_block_dequeue(b[0], false), 0, "block 0 done");
	TEST_ASSERT(block_is_zero(b[0]), "block 0 is the zero-filled datagram 2005");
	CHECK_EQ(iio_block_dequeue(b[1], false), 0, "block 1 done");
	TEST_ASSERT(block_is(b[1], 2006), "block 1 is datagram 2006");
	CHECK_EQ(iio_block_dequeue(b[2], false), 0, "block 2 done");
	TEST_ASSERT(block_is(b[2], 2007), "block 2 is datagram 2007");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_foreign_packets"), 1, "1 foreign");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_malformed"), 1, "1 malformed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_duplicate_packets"), 1, "1 duplicate");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_late_packets"), 1, "1 late");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_lost_packets"), 1999 + 1, "late one taken back");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_zero_filled_bytes"), PAYLOAD, "one payload of zeros");

	/* A new enable has a new setup: the sequence starts over at 0 */
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable");
	CHECK_EQ(iio_block_enqueue(b[3], 0, false), 0, "enqueue while disabled");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable again");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(iio_block_dequeue(b[3], false), 0, "block 3 done after re-enable");
	TEST_ASSERT(block_is(b[3], 0), "block 3 is datagram 0 of the new stream");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_duplicate_packets"), 1, "not a duplicate");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_late_packets"), 1, "not late");
out:
	rx_destroy(&rx);
	close(fd);
}

/* Until the counter <name> reads <val>, at most <timeout_ms> */
static long long wait_attr(
		const struct iio_buffer *buf, const char *name, long long val, int timeout_ms)
{
	const struct timespec ms = { .tv_nsec = 1000000 };
	long long cur;

	while ((cur = attr_ll(buf, name)) != val && timeout_ms-- > 0)
		nanosleep(&ms, NULL);

	return cur;
}

/* A gap too large to zero-fill that comes while no block is queued: there is
 * no block to fail then, so the next block a datagram finds fails with -EIO,
 * also with lossy=1, and the datagram starts the one after */
TEST_FUNCTION(resync_without_block)
{
	struct iio_block **b;
	struct rx rx;
	int fd, ret;

	fd = sender_open();
	TEST_ASSERT(fd >= 0, "sender bound to 127.0.0.1:25102");
	if (fd < 0)
		return;

	ret = rx_init(&rx, ctx_opts(yaml, ",lossy=1"), DEV_NAME, false);
	if (!ret)
		ret = iio_context_set_timeout(rx.ctx, 1000);
	if (!ret)
		ret = rx_open_size(&rx, 2, PAYLOAD);
	CHECK_EQ(ret, 0, "lossy=1 buffer with 2 blocks of one payload");
	if (ret)
		goto out;
	b = rx.blocks;

	CHECK_EQ(enqueue_all(&rx), 0, "enqueue 2");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(send_seq(fd, 1), 0, "send 1");
	CHECK_EQ(iio_block_dequeue(b[0], false), 0, "block 0 done");
	CHECK_EQ(iio_block_dequeue(b[1], false), 0, "block 1 done");

	/* 2 to 2001 lost, and no block queued to fail */
	CHECK_EQ(send_seq(fd, 2002), 0, "send 2002, nothing queued");
	CHECK_EQ(wait_attr(rx.buf, "daqiri_rx_dropped_no_block", 1, 2000), 1,
			"datagram 2002 dropped: no block");

	CHECK_EQ(iio_block_enqueue(b[0], 0, false), 0, "re-enqueue block 0");
	CHECK_EQ(iio_block_enqueue(b[1], 0, false), 0, "re-enqueue block 1");
	CHECK_EQ(send_seq(fd, 2003), 0, "send 2003");
	CHECK_EQ(iio_block_dequeue(b[0], false), -EIO, "block 0 fails: the resync, lossy=1 or not");
	CHECK_EQ(iio_block_dequeue(b[1], false), 0, "block 1 done");
	TEST_ASSERT(block_is(b[1], 2003), "block 1 is datagram 2003, whole");

	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_packets"), 4, "4 datagrams");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_lost_packets"), 2000, "2000 lost");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_gap_events"), 1, "one gap");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_overrun_events"), 1,
			"one overrun: 2002 found no block");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_zero_filled_bytes"), 0, "nothing zero-filled");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_completed"), 3, "3 blocks completed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_failed"), 1, "1 block failed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_with_gaps"), 1, "1 block with a gap");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_last_error"), -EIO, "last error");
out:
	rx_destroy(&rx);
	close(fd);
}

#define LATE_DATAGRAMS 12
#define LATE_GAP_MS 5

struct late_sender {
	int fd;
	int ret;
};

/* What keeps coming after DISABLE: LATE_DATAGRAMS datagrams of the stream,
 * LATE_GAP_MS apart, some 55 ms of them */
static void *send_late(void *d)
{
	const struct timespec gap = { .tv_nsec = LATE_GAP_MS * 1000000L };
	struct late_sender *ls = d;
	unsigned int i;

	for (i = 0; !ls->ret && i < LATE_DATAGRAMS; i++) {
		ls->ret = send_seq(ls->fd, i);
		nanosleep(&gap, NULL);
	}

	return NULL;
}

static long long ns_since(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return (long long)(t.tv_sec - t0->tv_sec) * 1000000000LL + (t.tv_nsec - t0->tv_nsec);
}

/* After DISABLE_BUFFER the backend receives until the queue has been quiet
 * for 20 ms, 500 ms at most: test_udp_drain checks that rule on made-up
 * times. Here datagrams 5 ms apart go on for some 55 ms after the answer.
 * On an idle host all 12 arrive; a thread that the host holds back for
 * 20 ms ends the drain sooner, as the rule wants, so only what holds however
 * the threads run is checked: the disable drains for 20 ms at least, and
 * what arrived is counted and assembled in order. */
TEST_FUNCTION(drain_until_idle)
{
	const struct timespec head_start = { .tv_nsec = 7 * 1000000L };
	struct late_sender ls = { .fd = -1 };
	long long n, full, ns;
	struct timespec t0;
	struct iio_block *b;
	pthread_t thread;
	unsigned int i, done = 0;
	struct rx rx;
	int ret;

	ls.fd = sender_open();
	TEST_ASSERT(ls.fd >= 0, "sender bound to 127.0.0.1:25102");
	if (ls.fd < 0)
		return;

	memset(&rx, 0, sizeof(rx));
	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	if (!ret)
		ret = rx_open_size(&rx, MAX_BLOCKS, PAYLOAD);
	CHECK_EQ(ret, 0, "buffer with 4 blocks of one payload");
	if (ret)
		goto out;

	CHECK_EQ(enqueue_all(&rx), 0, "enqueue 4");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable");

	ret = pthread_create(&thread, NULL, send_late, &ls);
	CHECK_EQ(ret, 0, "sender thread started");
	if (ret)
		goto out;

	/* The first datagram or so before the disable, the rest after */
	nanosleep(&head_start, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable, while datagrams still come");
	ns = ns_since(&t0);
	pthread_join(thread, NULL);
	CHECK_EQ(ls.ret, 0, "12 datagrams sent, 5 ms apart");

	n = attr_ll(rx.buf, "daqiri_rx_packets");
	printf("  disable: %lld ms, %lld of the %d datagrams received\n", ns / 1000000, n,
			LATE_DATAGRAMS);
	TEST_ASSERT(ns >= DQ_UDP_DRAIN_IDLE_MS * 1000000LL,
			"the disable drained for 20 ms at least");
	TEST_ASSERT(n >= 0 && n <= LATE_DATAGRAMS, "no more received than sent");

	/* One block per datagram while the 4 last, the others for want of one */
	full = n < MAX_BLOCKS ? n : MAX_BLOCKS;
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_completed"), full,
			"a block per datagram, 4 at most");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_dropped_no_block"), n - full,
			"the others dropped for want of a block");
	for (i = 0; i < full; i++) {
		b = rx.blocks[i];
		if (!iio_block_dequeue(b, true) && block_is(b, i))
			done++;
	}
	CHECK_EQ(done, full, "the completed blocks hold datagrams 0, 1, ... in order");
out:
	rx_destroy(&rx);
	close(ls.fd);
}

/* The mock's log from byte <from> on, at most <len> - 1 bytes; the length of
 * the whole log in <total> */
static size_t log_read(long from, char *buf, size_t len, long *total)
{
	FILE *f = fopen(mock_log, "r");
	size_t n = 0;

	*total = 0;
	if (f) {
		if (!fseek(f, 0, SEEK_END))
			*total = ftell(f);
		if (from <= *total && !fseek(f, from, SEEK_SET))
			n = fread(buf, 1, len - 1, f);
		fclose(f);
	}
	buf[n] = '\0';

	return n;
}

/* Where the next case's calls will start in the mock's log */
static long log_mark(void)
{
	char buf[1];
	long total;

	log_read(0, buf, sizeof(buf), &total);

	return total;
}

/* Whether the mock logged exactly <calls>, one per line, since <mark>; says
 * what it logged when not */
static bool calls_are(long mark, const char *calls)
{
	const struct timespec ms = { .tv_nsec = 1000000 };
	static char log[4096];
	unsigned int wait;
	long total;
	bool same;

	/* A close answers after release() logged, but allow for a slow disk */
	for (wait = 0;; wait++) {
		log_read(mark, log, sizeof(log), &total);
		same = !strcmp(log, calls);
		if (same || wait == 1000)
			break;
		nanosleep(&ms, NULL);
	}

	if (!same)
		printf("    provider calls:\n%s    expected:\n%s", log, calls);

	return same;
}

static void expect_calls(long mark, const char *calls, const char *what)
{
	TEST_ASSERT(calls_are(mark, calls), what);
}

/* Decision table, data=udp without a YAML: -EINVAL before the server is asked
 * anything, though it advertises UDP sample mode */
TEST_FUNCTION(udp_without_yaml)
{
	long mark = log_mark();
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_uri("daqiri:127.0.0.1,data=udp"), DEV_NAME, false);
	CHECK_EQ(ret, 0, "data=udp context without a YAML");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), -EINVAL, "buffer open: -EINVAL");
	rx_destroy(&rx);

	expect_calls(mark, "", "no setup sent");
}

/* Decision table, data=auto without a YAML: TCP, no probe, though the server
 * advertises UDP sample mode */
TEST_FUNCTION(auto_without_yaml)
{
	long mark = log_mark();
	struct rx rx;
	int ret;

	ret = rx_init(&rx, ctx_uri("daqiri:127.0.0.1,data=auto"), DEV_NAME, false);
	CHECK_EQ(ret, 0, "data=auto context without a YAML");
	if (ret)
		goto out;
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "tcp", "TCP, before the open");

	ret = rx_open_size(&rx, 1, 1024);
	CHECK_EQ(ret, 0, "buffer opens");
	if (ret)
		goto out;
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "tcp", "and streams over TCP");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_engine_kind"), "none", "without the engine");

	CHECK_EQ(enqueue_all(&rx), 0, "TCP block enqueued");
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "TCP enable");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), 0, "TCP block received");
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "TCP disable");
out:
	rx_destroy(&rx);

	expect_calls(mark, "", "no setup sent");
}

/* The setup of an enable refused: the enable fails with the server's error,
 * the buffer stays a UDP one, and DISABLE drops the probe's setup that was
 * still pending, which the next setup shows. The mock refuses the second
 * setup of 8-byte sample sets, voltage0 to voltage3. */
TEST_FUNCTION(refused_at_enable)
{
	static const char *const more[] = { "voltage2", "voltage3" };
	struct iio_channel *chn;
	unsigned int i;
	struct rx rx;
	long mark;
	int ret;

	ret = rx_init(&rx, ctx_opts(yaml, ",data=auto"), DEV_NAME, false);
	CHECK_EQ(ret, 0, "data=auto context and mask");
	if (ret)
		goto out;
	for (i = 0; i < 2; i++) {
		chn = iio_device_find_channel(iio_buffer_get_device(rx.buf), more[i], false);
		TEST_ASSERT_PTR_NOT_NULL(chn, more[i]);
		if (!chn)
			goto out;
		iio_channel_enable(chn, rx.mask);
	}

	mark = log_mark();
	ret = rx_open(&rx, 2);
	CHECK_EQ(ret, 0, "8-byte sample sets: the probe is answered");
	if (ret)
		goto out;
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "a UDP buffer");
	CHECK_EQ(enqueue_all(&rx), 0, "enqueue both blocks");

	CHECK_EQ(iio_buffer_stream_start(rx.stream), -EBUSY,
			"enable: the setup refused with -EBUSY, which the enable returns");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "no fallback to TCP");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_stream_id"), "0", "no stream programmed");
	expect_calls(mark, "setup 9200\nsetup 9200 pending\n",
			"the probe, then the enable's setup with the probe's still pending");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable again: answered");
	expect_calls(mark, "setup 9200\nstart\n",
			"nothing pending any more: the failed enable sent DISABLE");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_stream_id"), "0x12345678",
			"the stream of the second setup");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable");
	rx_close(&rx);
	expect_calls(mark, "stop\nrelease\n", "disable and close");
out:
	rx_destroy(&rx);
}

/* A round trip on the management connection: an attribute of ad9361-phy */
static bool phy_attr_reads(const struct iio_context *ctx)
{
	const struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
	const struct iio_channel *chn;
	const struct iio_attr *attr;
	char val[64];

	chn = phy ? iio_device_find_channel(phy, "voltage0", false) : NULL;
	attr = chn ? iio_channel_find_attr(chn, "sampling_frequency") : NULL;

	return attr && iio_attr_read_raw(attr, val, sizeof(val)) > 0;
}

#define CLOSE_LOOPS 50

/* The mock's calls for a buffer opened and enabled: the probe, the enable's
 * setup with the probe's still pending, the start */
#define STREAMING "setup 9200\nsetup 9200 pending\nstart\n"

/*
 * A cancel wakes the buffer and leaves its connection open: the server hears
 * of nothing until the close, whose FREE_BLOCK and CLOSE_BUFFER go on the
 * management connection, ends the stream. Were the buffer's connection shut
 * down at the cancel, the server would tear the buffer down by itself while
 * the close frees its block. Then CLOSE_LOOPS buffers closed while streaming,
 * the routine path: each close stops and releases its stream once, and the
 * server is still there afterwards. Run against an iiod-emu built with
 * -fsanitize=address, this shows that the server uses nothing it freed.
 */
TEST_FUNCTION(close_loop)
{
	unsigned int i, ok = 0;
	struct rx rx;
	long mark;
	int ret;

	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context and mask");
	if (ret)
		goto out;

	mark = log_mark();
	ret = rx_open(&rx, 1);
	if (!ret)
		ret = enqueue_all(&rx);
	if (!ret)
		ret = iio_buffer_stream_start(rx.stream);
	CHECK_EQ(ret, 0, "buffer open, a block queued, enabled");
	if (ret)
		goto out;
	expect_calls(mark, STREAMING, "the stream started");

	mark = log_mark();
	iio_buffer_stream_cancel(rx.stream);
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), -ECANCELED, "the block cancelled");
	TEST_ASSERT(phy_attr_reads(rx.ctx), "the context reads attributes after the cancel");
	expect_calls(mark, "", "the cancel alone stops nothing on the server");
	rx_close(&rx);
	expect_calls(mark, "stop\nrelease\n", "the close stops and releases the stream, once");

	for (i = 0; i < CLOSE_LOOPS; i++) {
		mark = log_mark();
		ret = rx_open(&rx, 1);
		if (!ret)
			ret = enqueue_all(&rx);
		if (!ret)
			ret = iio_buffer_stream_start(rx.stream);
		if (ret)
			printf("  close %u: open, enqueue or enable: %d\n", i, ret);
		rx_close(&rx);
		if (ret || !calls_are(mark, STREAMING "stop\nrelease\n")) {
			printf("  close %u failed\n", i);
			break;
		}
		ok++;
	}
	CHECK_EQ(ok, CLOSE_LOOPS, "50 buffers closed while streaming: stop and release once each");
	TEST_ASSERT(phy_attr_reads(rx.ctx), "the server still answers");
out:
	rx_destroy(&rx);
}

/* --enable-fails <yaml>: a YAML with max_payload_size 1250 */
static const char *enable_fails_yaml;

/* With <enable_fails_yaml>, so in a process of its own: one YAML per process */
static void enable_fails_child(void)
{
	struct rx rx;
	long mark;
	int ret;

	ret = rx_init(&rx, ctx_for(enable_fails_yaml), DEV_NAME, false);
	CHECK_EQ(ret, 0, "context with max_payload_size 1250, and mask");
	if (ret)
		goto out;

	mark = log_mark();
	ret = rx_open(&rx, 2);
	CHECK_EQ(ret, 0, "the buffer opens: the probe is answered");
	if (ret)
		goto out;
	CHECK_EQ(enqueue_all(&rx), 0, "enqueue both blocks");

	CHECK_EQ(iio_buffer_stream_start(rx.stream), -EADDRINUSE,
			"enable: ENABLE_BUFFER fails, with the server's error");
	expect_calls(mark, "setup 1234\nsetup 1234 pending\nstart failed\ndisable\n",
			"1234-byte payloads, the start failed, then a DISABLE dropped the setup");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "no fallback to TCP");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], true), -EBUSY, "the blocks stay queued");

	/* Still disabled: the next enable sets up again */
	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_start(rx.stream), -EADDRINUSE, "enable again: the same error");
	expect_calls(mark, "setup 1234\nstart failed\ndisable\n",
			"a fresh setup, then the same unwind: the buffer stayed disabled");

	mark = log_mark();
	rx_close(&rx);
	expect_calls(mark, "release\n", "the close releases the buffer, which never streamed");

	mark = log_mark();
	CHECK_EQ(rx_open(&rx, 0), 0, "the next buffer gets the queue");
	rx_close(&rx);
	expect_calls(mark, "setup 1234\nrelease\n", "its probe, its release");
out:
	rx_destroy(&rx);
}

/*
 * --enable-fails, against the mock with --udp-mock-fail-disable 0, which logs
 * every DISABLE_BUFFER: the enable's setup succeeds, then ENABLE_BUFFER fails
 * (the mock does not start a stream of 1234-byte payloads). The enable returns
 * that error after a DISABLE that drops the setup, the buffer stays disabled,
 * and the close gives the queue back. The child exits, so with the loopback
 * YAML this process then opens a buffer that streams.
 */
TEST_FUNCTION(enable_fails)
{
	int fd = -1, ret, status;
	struct rx rx;
	long mark;
	pid_t pid;

	memset(&rx, 0, sizeof(rx));

	/* No engine in this process yet: the child starts its own */
	pid = fork();
	if (pid < 0) {
		TEST_ASSERT(false, "fork");
		return;
	}
	if (!pid) {
		enable_fails_child();
		printf("  enable_fails child: %d/%d passed, %d failed\n", test_passed, test_count,
				test_failed);
		exit(test_failed ? 1 : 0);
	}

	ret = waitpid(pid, &status, 0) == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	CHECK_EQ(ret, 0, "the child's checks passed");

	fd = sender_open();
	TEST_ASSERT(fd >= 0, "sender bound to 127.0.0.1:25102");
	if (fd < 0)
		return;

	mark = log_mark();
	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	if (!ret)
		ret = rx_open_size(&rx, 1, PAYLOAD);
	if (!ret)
		ret = enqueue_all(&rx);
	if (!ret)
		ret = iio_buffer_stream_start(rx.stream);
	CHECK_EQ(ret, 0, "with the loopback YAML, a buffer opens and enables");
	if (ret)
		goto out;
	expect_calls(mark, STREAMING, "its stream starts");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), 0, "block 0 done");
	TEST_ASSERT(block_is(rx.blocks[0], 0), "block 0 is datagram 0");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable");
	rx_close(&rx);
	expect_calls(mark, "stop\ndisable\nrelease\n", "disable and close");
out:
	rx_destroy(&rx);
	close(fd);
}

/* DQ_UDP_DISABLE_RETRY_MS (daqiri-udp.c): when a failed DISABLE is sent again */
#define DISABLE_RETRY_MS 100

/* A buffer of <nb> blocks of one payload, all queued, enabled */
static int rx_streaming(struct rx *rx, unsigned int nb)
{
	int ret;

	ret = rx_open_size(rx, nb, PAYLOAD);
	if (!ret)
		ret = enqueue_all(rx);
	if (!ret)
		ret = iio_buffer_stream_start(rx->stream);

	return ret;
}

/*
 * --disable-fails-once, against the mock with --udp-mock-fail-disable 1: the
 * first DISABLE_BUFFER fails with -ETIMEDOUT, the stream stopped, as a board
 * answers when its sender did not drain. The backend sends it again 100 ms
 * later, which succeeds: the disable succeeds, and the next enable streams.
 */
TEST_FUNCTION(disable_fails_once)
{
	struct timespec t0;
	struct rx rx;
	long mark;
	int fd, ret;

	fd = sender_open();
	TEST_ASSERT(fd >= 0, "sender bound to 127.0.0.1:25102");
	if (fd < 0)
		return;

	mark = log_mark();
	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	if (!ret)
		ret = rx_streaming(&rx, 2);
	CHECK_EQ(ret, 0, "buffer with 2 blocks of one payload, enabled");
	if (ret)
		goto out;
	expect_calls(mark, STREAMING, "the stream started");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), 0, "block 0 done");

	mark = log_mark();
	clock_gettime(CLOCK_MONOTONIC, &t0);
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0,
			"disable: DISABLE_BUFFER sent again succeeds");
	TEST_ASSERT(ns_since(&t0) >= DISABLE_RETRY_MS * 1000000LL, "sent again 100 ms later");
	expect_calls(mark, "stop\ndisable failed\ndisable\n",
			"the stream stopped, the first DISABLE failed, the second did not");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "a UDP buffer still");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_last_error"), 0, "no error");
	CHECK_EQ(iio_block_dequeue(rx.blocks[1], true), -EBUSY, "block 1 still queued");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_start(rx.stream), 0, "enable again");
	expect_calls(mark, "setup 9200\nstart\n", "a fresh setup, nothing pending, a start");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0 of the new stream");
	CHECK_EQ(iio_block_dequeue(rx.blocks[1], false), 0, "block 1 done");
	TEST_ASSERT(block_is(rx.blocks[1], 0), "block 1 is datagram 0");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable");
	rx_close(&rx);
	expect_calls(mark, "stop\ndisable\nrelease\n", "disable and close");
out:
	rx_destroy(&rx);
	close(fd);
}

/*
 * --disable-fails-twice, against the mock with --udp-mock-fail-disable 2: the
 * DISABLE_BUFFER sent again fails too. The board may still be streaming: the
 * backend closes the buffer on the server (CLOSE_BUFFER, on the management
 * connection) and returns the error, with no fallback to TCP. The queued
 * blocks fail with it, the counters still read, an enable gets -EBADF, the
 * close sends nothing more, and the context and the queue serve the next
 * buffer.
 */
TEST_FUNCTION(disable_fails_twice)
{
	struct iio_block **b;
	struct rx rx;
	long mark;
	int fd, ret;

	fd = sender_open();
	TEST_ASSERT(fd >= 0, "sender bound to 127.0.0.1:25102");
	if (fd < 0)
		return;

	mark = log_mark();
	ret = rx_init(&rx, ctx_for(yaml), DEV_NAME, false);
	if (!ret)
		ret = rx_streaming(&rx, 3);
	CHECK_EQ(ret, 0, "buffer with 3 blocks of one payload, enabled");
	if (ret)
		goto out;
	b = rx.blocks;
	expect_calls(mark, STREAMING, "the stream started");
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(iio_block_dequeue(b[0], false), 0, "block 0 done");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), -ETIMEDOUT,
			"disable: both DISABLE_BUFFERs fail, and so does the disable");
	expect_calls(mark, "stop\ndisable failed\ndisable failed\nrelease\n",
			"two DISABLEs, then CLOSE_BUFFER released the buffer");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp", "no fallback to TCP");
	CHECK_EQ(iio_block_dequeue(b[1], true), -ETIMEDOUT, "block 1 failed with the error");
	CHECK_EQ(iio_block_dequeue(b[2], true), -ETIMEDOUT, "block 2 too");
	CHECK_EQ(iio_block_enqueue(b[1], 0, false), 0, "enqueue after the failure");
	CHECK_EQ(iio_block_dequeue(b[1], true), -ETIMEDOUT, "that block fails with it at once");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_rx_packets"), 1, "the counters still read");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_blocks_failed"), 3, "3 blocks failed");
	CHECK_EQ(attr_ll(rx.buf, "daqiri_last_error"), -ETIMEDOUT, "last error");
	TEST_ASSERT(phy_attr_reads(rx.ctx), "the context reads attributes");

	mark = log_mark();
	CHECK_EQ(iio_buffer_stream_start(rx.stream), -EBADF, "enable: closed on the server");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "udp",
			"a UDP buffer until the close");
	rx_close(&rx);
	expect_calls(mark, "", "no setup, and the close sends nothing more");

	mark = log_mark();
	ret = rx_streaming(&rx, 1);
	CHECK_EQ(ret, 0, "the next buffer opens on the queue and enables");
	if (ret)
		goto out;
	CHECK_EQ(send_seq(fd, 0), 0, "send 0");
	CHECK_EQ(iio_block_dequeue(rx.blocks[0], false), 0, "its block done");
	CHECK_EQ(iio_buffer_stream_stop(rx.stream), 0, "disable: this DISABLE_BUFFER succeeds");
	rx_close(&rx);
	expect_calls(mark, STREAMING "stop\ndisable\nrelease\n", "the next buffer's stream");
out:
	rx_destroy(&rx);
	close(fd);
}

/* --mixed, against float_scan_elements.xml: float-dev voltage1 is le:F16/16,
 * voltage2 le:f32/32 */
TEST_FUNCTION(mixed_lengths)
{
	static const char *const mixed[] = { "voltage1", "voltage2" };
	struct rx rx;
	int ret;

	ret = rx_init_chans(&rx, ctx_for(yaml), "float-dev", mixed, false);
	CHECK_EQ(ret, 0, "16 + 32-bit mask");
	if (!ret)
		CHECK_EQ(rx_open(&rx, 0), 0, "the device's own layout opens");
	rx_destroy(&rx);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	yaml = getenv("DAQIRI_YAML");
	if (!yaml || !yaml[0]) {
		printf("DAQIRI_YAML must be the path of socket-udp-loopback.yaml\n");
		return 1;
	}
	mock_log = getenv("DAQIRI_MOCK_LOG");

	if (argc > 1 && !strcmp(argv[1], "--mixed")) {
		RUN_TEST(mixed_lengths);
		goto out;
	}
	if (argc > 1 && !strcmp(argv[1], "--wrong-source")) {
		RUN_TEST(wrong_source);
		goto out;
	}
	if (argc > 1 && (!strcmp(argv[1], "--enable-fails") ||
					!strcmp(argv[1], "--disable-fails-once") ||
					!strcmp(argv[1], "--disable-fails-twice"))) {
		if (!mock_log || !mock_log[0]) {
			TEST_ASSERT(false, "DAQIRI_MOCK_LOG names the mock provider's log");
		} else if (!strcmp(argv[1], "--enable-fails")) {
			enable_fails_yaml = argc > 2 ? argv[2] : "";
			RUN_TEST(enable_fails);
		} else if (!strcmp(argv[1], "--disable-fails-once")) {
			RUN_TEST(disable_fails_once);
		} else {
			RUN_TEST(disable_fails_twice);
		}
		goto out;
	}

	RUN_TEST(exit_with_open_buffer);
	RUN_TEST(busy_when_empty);
	RUN_TEST(cancel_fails_dequeue);
	RUN_TEST(destroy_with_open_buffer);
	RUN_TEST(second_yaml_busy);
	RUN_TEST(same_queue_busy);
	RUN_TEST(output_rejected);
	RUN_TEST(counter_attrs);
	RUN_TEST(payload_not_whole_samples);
	RUN_TEST(refused_probe_auto);
	RUN_TEST(refused_probe_udp);
	RUN_TEST(data_smoke);
	RUN_TEST(resync_without_block);
	RUN_TEST(drain_until_idle);

	if (!mock_log || !mock_log[0]) {
		TEST_ASSERT(false, "DAQIRI_MOCK_LOG names the mock provider's log");
		goto out;
	}
	RUN_TEST(udp_without_yaml);
	RUN_TEST(auto_without_yaml);
	RUN_TEST(refused_at_enable);
	RUN_TEST(close_loop);
out:
	printf("test_daqiri_lifecycle: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
