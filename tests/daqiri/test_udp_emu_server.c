/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * iiod-emu --udp, the reference server of UDP sample mode, seen from a plain
 * UDP socket: the setup command goes through the iiod-client API, then every
 * datagram of the stream is checked (source, header, sequence number, payload
 * against the sample file) and so is the emulator's oracle of what it sent
 * and dropped. Needs neither DAQIRI nor the daqiri: module.
 *
 * Usage: test_udp_emu_server <sample.bin> <oracle.json>
 *        test_udp_emu_server <sample.bin> <oracle.json> --count
 *        test_udp_emu_server <sample.bin> --prompt-stop
 *
 * Run through with_emu.sh, the emulator serving fmcomms2.xml with
 *   --udp --udp-from 127.0.0.1:25102 --udp-pps 2000 --udp-drop-every 10
 *   --udp-json <oracle.json>
 * With --count, the emulator also has --udp-count 100: the stream of the one
 * enable must end by itself after the numbers 0 to 99, the dropped ones
 * included, with the oracle written then and the same after the disable.
 * Or, for --prompt-stop, with --udp --udp-pps 1: then the test only checks
 * that a stream sleeping between two datagrams stops at once on DISABLE,
 * CLOSE_BUFFER and loss of the connection.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio.h>
#include <iio/iiod-client.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../../iiod-udp-stream.h"
#include "emu_client.h"
#include "test_framework.h"

#define EMU_DEVICE "cf-ad9361-lpc"
#define LOOPBACK 0x7f000001u /* 127.0.0.1 */
#define DST_PORT 25101	     /* where the daqiri loopback YAML listens */
#define SRC_PORT 25102	     /* --udp-from */
#define DROP_EVERY 10	     /* --udp-drop-every */
#define MAX_PAYLOAD 1472     /* 1500-byte MTU less the IPv4 and UDP headers */

#define MIN_DATAGRAMS 64
#define COUNT 100 /* --udp-count, with --count */
#define RECV_TIMEOUT_MS 5000
/* After a disable, the stream is over once nothing came for this long */
#define IDLE_MS 300
/* A stream stops within a few ms. The bound leaves room for a loaded machine
 * and still fails a sender that sleeps a whole second at --udp-pps 1. */
#define PROMPT_MS 250
/* At 2000 datagrams per second, far more than a run sends */
#define MAX_SEQ 65536
/* Say what was wrong with the first few bad datagrams only */
#define MAX_REPORTS 5

struct sample_file {
	uint8_t *data;
	size_t len;
};

/* The datagrams of one enable */
struct rx {
	uint32_t stream_id;
	uint16_t payload_bytes;
	uint64_t count, bad;
	uint64_t first_seq;
	uint8_t seen[MAX_SEQ]; /* times each sequence number arrived, saturated */
};

struct oracle {
	unsigned long long sent, dropped, reordered, duplicated;
};

static int read_file(const char *path, struct sample_file *f)
{
	uint8_t *p;
	size_t n;
	FILE *fp;

	f->data = NULL;
	f->len = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return -errno;

	for (;;) {
		p = realloc(f->data, f->len + 65536);
		if (!p) {
			fclose(fp);
			return -ENOMEM;
		}
		f->data = p;
		n = fread(f->data + f->len, 1, 65536, fp);
		f->len += n;
		if (n < 65536)
			break;
	}

	n = ferror(fp);
	fclose(fp);

	return n ? -EIO : 0;
}

static long ms_since(const struct timespec *t0)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (long)(now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

/* A plain UDP socket where the host's receive queue would be */
static int udp_listen(void)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(DST_PORT),
		.sin_addr.s_addr = htonl(LOOPBACK),
	};
	int fd, size = 4 << 20;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	/* Best effort: the kernel caps it at net.core.rmem_max */
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		size = -errno;
		close(fd);
		return size;
	}

	return fd;
}

static void report(struct rx *rx, uint64_t seq, const char *why)
{
	if (rx->bad++ < MAX_REPORTS)
		printf("  bad datagram (sequence %llu): %s\n", (unsigned long long)seq, why);
}

/* The payload of datagram <seq>: the sample file as an endless stream, one
 * payload after the other, so that a dropped datagram still takes its bytes */
static bool payload_matches(const struct sample_file *f, uint64_t seq, const uint8_t *p, size_t len)
{
	uint64_t off = (seq % f->len) * len % f->len;
	size_t i;

	for (i = 0; i < len; i++) {
		if (p[i] != f->data[(off + i) % f->len])
			return false;
	}

	return true;
}

static void check_datagram(struct rx *rx, const struct sample_file *f, const uint8_t *buf,
		ssize_t len, const struct sockaddr_in *from)
{
	const uint8_t *payload = buf + IIOD_UDP_STREAM_HDR_LEN;
	struct iiod_udp_stream_hdr hdr;

	if (iiod_udp_stream_hdr_unpack(buf, (size_t)len, &hdr)) {
		report(rx, 0, "no valid header");
		return;
	}

	/* What DAQIRI's socket engine filters on: its socket is connected to
	 * the YAML's remote_addr */
	if (from->sin_addr.s_addr != htonl(LOOPBACK) || from->sin_port != htons(SRC_PORT))
		report(rx, hdr.sequence, "not from 127.0.0.1:25102");
	else if (hdr.flags != 0)
		report(rx, hdr.sequence, "flags not 0");
	else if (hdr.stream_id != rx->stream_id)
		report(rx, hdr.sequence, "another stream ID");
	else if (hdr.payload_length != rx->payload_bytes)
		report(rx, hdr.sequence, "payload length is not payload_bytes");
	else if ((size_t)len != (size_t)IIOD_UDP_STREAM_HDR_LEN + rx->payload_bytes)
		report(rx, hdr.sequence, "datagram is not header + payload_bytes long");
	else if (hdr.sequence >= MAX_SEQ)
		report(rx, hdr.sequence, "sequence number out of range");
	else if (!payload_matches(f, hdr.sequence, payload, rx->payload_bytes))
		report(rx, hdr.sequence, "payload is not the sample file's bytes");
	else {
		if (!rx->count)
			rx->first_seq = hdr.sequence;
		if (rx->seen[hdr.sequence] < UINT8_MAX)
			rx->seen[hdr.sequence]++;
		rx->count++;
	}
}

/* Receive until <want> good datagrams have arrived in all, or with <want> = 0
 * until the socket stays quiet for IDLE_MS. Gives up after <timeout_ms>. */
static void receive(
		int fd, struct rx *rx, const struct sample_file *f, uint64_t want, int timeout_ms)
{
	static uint8_t buf[65536];
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	struct sockaddr_in from;
	struct timespec t0;
	socklen_t from_len;
	ssize_t len;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &t0);

	while (!want || rx->count < want) {
		ret = poll(&pfd, 1, want ? 50 : IDLE_MS);
		if (ret < 0 && errno != EINTR)
			break;
		if (!ret && !want)
			break;

		if (ret > 0) {
			from_len = sizeof(from);
			len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from,
					&from_len);
			if (len >= 0)
				check_datagram(rx, f, buf, len, &from);
		}

		if (ms_since(&t0) > timeout_ms)
			break;
	}
}

static void rx_reset(struct rx *rx, const struct iiod_udp_stream_resp *resp)
{
	memset(rx, 0, sizeof(*rx));
	rx->stream_id = resp->stream_id;
	rx->payload_bytes = resp->payload_bytes;
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

static int read_oracle(const char *path, struct oracle *o)
{
	struct sample_file f;
	char *json;
	int ret;

	ret = read_file(path, &f);
	if (ret)
		return ret;

	json = realloc(f.data, f.len + 1);
	if (!json) {
		free(f.data);
		return -ENOMEM;
	}
	json[f.len] = '\0';

	ret = 0;
	if (!json_number(json, "sent", &o->sent) || !json_number(json, "dropped", &o->dropped) ||
			!json_number(json, "reordered", &o->reordered) ||
			!json_number(json, "duplicated", &o->duplicated))
		ret = -EBADMSG;
	free(json);

	return ret;
}

/* The oracle of a run with --udp-drop-every 10 and no other injection, and
 * what arrived: the sender went through the sequence numbers 0 .. T-1, where
 * T = sent + dropped, dropped 9, 19, 29, ... of them and sent the rest. */
static void check_oracle(const char *path, const struct rx *rx)
{
	unsigned long long total, missing = 0, extra = 0, i;
	struct oracle o;
	bool expected;
	int ret;

	ret = read_oracle(path, &o);
	TEST_ASSERT_EQ(ret, 0, "oracle written on disable");
	if (ret)
		return;

	total = o.sent + o.dropped;
	TEST_ASSERT_EQ(o.sent, rx->count, "oracle: every datagram sent arrived, once");
	TEST_ASSERT_EQ(o.dropped, total / DROP_EVERY, "oracle: dropped every 10th datagram");
	TEST_ASSERT_EQ(o.reordered, 0, "oracle: nothing reordered");
	TEST_ASSERT_EQ(o.duplicated, 0, "oracle: nothing duplicated");
	TEST_ASSERT(total < MAX_SEQ, "oracle: within the test's range");

	for (i = 0; i < MAX_SEQ; i++) {
		expected = i < total && (i + 1) % DROP_EVERY;
		if (expected && !rx->seen[i])
			missing++;
		else if (rx->seen[i] != expected)
			extra++;
	}

	TEST_ASSERT_EQ(missing, 0, "received: no sequence number missing but the dropped ones");
	TEST_ASSERT_EQ(extra, 0, "received: no dropped, repeated or unsent sequence number");
}

static int setup(struct iiod_client_buffer_pdata *buf, uint32_t dst_ip, uint16_t dst_port,
		uint16_t max_payload, struct iiod_udp_stream_resp *resp)
{
	const struct iiod_udp_stream_req req = {
		.dst_ip = dst_ip,
		.dst_port = dst_port,
		.max_payload = max_payload,
	};

	memset(resp, 0, sizeof(*resp));

	return iiod_client_udp_stream_setup(buf, &req, resp);
}

/* payload_bytes = the largest multiple of 16 and of the sample-set size (4
 * bytes here) that is at most min(max_payload, 9200) */
static void check_sizing(struct iiod_client_buffer_pdata *buf)
{
	struct iiod_udp_stream_resp resp;
	int ret;

	ret = setup(buf, LOOPBACK, DST_PORT, 15, &resp);
	TEST_ASSERT_EQ(ret, -EINVAL, "setup: max_payload 15 leaves no room for 16 bytes");

	ret = setup(buf, LOOPBACK, DST_PORT, 65535, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup: max_payload 65535");
	TEST_ASSERT_EQ(resp.payload_bytes, 9200, "setup: payload_bytes capped at 9200");

	ret = setup(buf, LOOPBACK, DST_PORT, 1471, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup: max_payload 1471");
	TEST_ASSERT_EQ(resp.payload_bytes, 1456, "setup: payload_bytes rounded down to 1456");
}

/* (b) to (f) of the brief: one enable, a second one, close */
static void stream(struct emu_client *ec, struct iiod_client_buffer_pdata *buf, int udp,
		const struct sample_file *f, const char *oracle)
{
	static struct rx rx;
	struct iiod_udp_stream_resp resp, other;
	struct iio_block_pdata *block;
	struct timespec t0;
	uint32_t first_id;
	long ms;
	void *data;
	int ret;

	check_sizing(buf);

	ret = setup(buf, LOOPBACK, DST_PORT, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup: max_payload 1472");
	if (ret)
		return;
	TEST_ASSERT_EQ(resp.payload_bytes, 1472, "setup: 1472 is a multiple of 16 and of 4");
	TEST_ASSERT(resp.src_ip == LOOPBACK, "setup: source is 127.0.0.1");
	TEST_ASSERT_EQ(resp.src_port, SRC_PORT, "setup: source port is --udp-from's");
	TEST_ASSERT(resp.stream_id != 0, "setup: stream ID is not 0");

	/* A failed setup must leave the earlier success pending, unchanged */
	ret = setup(buf, LOOPBACK, 0, MAX_PAYLOAD, &other);
	TEST_ASSERT_EQ(ret, -EINVAL, "setup: destination port 0 refused");

	/* The server cannot enable a buffer without a block; UDP mode never
	 * enqueues it */
	block = iiod_client_create_block(buf, resp.payload_bytes, &data);
	ret = iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block of payload_bytes created");
	if (ret)
		return;

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable 1");

	rx_reset(&rx, &resp);
	receive(udp, &rx, f, MIN_DATAGRAMS, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count >= MIN_DATAGRAMS, "enable 1: at least 64 datagrams within 5 s");

	clock_gettime(CLOCK_MONOTONIC, &t0);
	ret = iiod_client_enable_buffer(buf, 0, false, false);
	ms = ms_since(&t0);
	TEST_ASSERT_EQ(ret, 0, "disable 1");
	TEST_ASSERT(ms < PROMPT_MS, "disable 1: answered promptly");

	/* Whatever was sent before the stop */
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.bad, 0, "enable 1: every datagram valid, from its payload to its source");
	check_oracle(oracle, &rx);

	/* (e) A new setup, a new stream ID, the sequence from 0 again */
	first_id = resp.stream_id;
	ret = setup(buf, LOOPBACK, DST_PORT, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup 2");
	TEST_ASSERT(resp.stream_id != 0 && resp.stream_id != first_id, "setup 2: a new stream ID");

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable 2");

	rx_reset(&rx, &resp);
	receive(udp, &rx, f, MIN_DATAGRAMS, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count >= MIN_DATAGRAMS, "enable 2: at least 64 datagrams within 5 s");
	TEST_ASSERT(rx.count && rx.first_seq == 0, "enable 2: sequence starts at 0 again");

	ret = iiod_client_enable_buffer(buf, 0, false, false);
	TEST_ASSERT_EQ(ret, 0, "disable 2");

	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.bad, 0, "enable 2: every datagram valid");
	check_oracle(oracle, &rx);

	/* (f) */
	iiod_client_free_block(block);
	iiod_client_close_buffer(buf);

	ret = iiod_client_nop(ec->client);
	TEST_ASSERT_EQ(ret, 0, "connection usable after close");

	rx_reset(&rx, &resp);
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.count + rx.bad, 0, "nothing sent after close");
}

/* --udp-count 100: the stream ends by itself, and the oracle is written then */
static void count_stream(struct iiod_client_buffer_pdata *buf, int udp, const struct sample_file *f,
		const char *oracle)
{
	static struct rx rx;
	struct iiod_udp_stream_resp resp;
	struct iio_block_pdata *block;
	struct oracle o1, o2;
	void *data;
	int ret;

	ret = setup(buf, LOOPBACK, DST_PORT, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup");
	if (ret)
		return;

	block = iiod_client_create_block(buf, resp.payload_bytes, &data);
	ret = iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block of payload_bytes created");
	if (ret)
		return;

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable");

	/* Until nothing came for IDLE_MS: the end, with no disable */
	rx_reset(&rx, &resp);
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.bad, 0, "every datagram valid");

	ret = read_oracle(oracle, &o1);
	TEST_ASSERT_EQ(ret, 0, "oracle written when the count was reached, before any disable");
	if (!ret)
		TEST_ASSERT_EQ(o1.sent + o1.dropped - o1.duplicated, COUNT,
				"oracle: the numbers 0 to 99, sent or dropped");
	/* --udp-drop-every 10 dropped 9, 19, ..., 99: the last one too */
	check_oracle(oracle, &rx);

	ret = iiod_client_enable_buffer(buf, 0, false, false);
	TEST_ASSERT_EQ(ret, 0, "disable");
	ret = read_oracle(oracle, &o2);
	TEST_ASSERT_EQ(ret, 0, "oracle written at the disable");
	TEST_ASSERT(!ret && !memcmp(&o1, &o2, sizeof(o1)), "and the same: nothing sent since");

	iiod_client_free_block(block);
	iiod_client_close_buffer(buf);
}

/* Enable a stream paced at one datagram per second and wait for its first
 * datagram: the sender then sleeps for a second before the next one */
static struct iio_block_pdata *start_slow(struct iiod_client_buffer_pdata *buf, int udp,
		const struct sample_file *f, struct rx *rx, const char *what)
{
	struct iiod_udp_stream_resp resp;
	struct iio_block_pdata *block;
	void *data;
	int ret;

	ret = setup(buf, LOOPBACK, DST_PORT, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, what);
	if (ret)
		return NULL;

	block = iiod_client_create_block(buf, resp.payload_bytes, &data);
	if (iio_err(block)) {
		TEST_ASSERT(0, "block created");
		return NULL;
	}

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "slow stream enabled");

	rx_reset(rx, &resp);
	receive(udp, rx, f, 1, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx->count, 1, "first datagram of the slow stream");

	return block;
}

static void prompt_stop(struct emu_client *ec, const struct iio_device *dev,
		struct iio_channels_mask *mask, int udp, const struct sample_file *f)
{
	static struct rx rx;
	struct iiod_client_buffer_pdata *buf;
	const struct iio_device *other_dev;
	struct iio_block_pdata *block;
	struct emu_client other;
	struct timespec t0;
	long ms;
	int ret;

	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (ret)
		return;

	block = start_slow(buf, udp, f, &rx, "setup before DISABLE");
	if (!block)
		return;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	ret = iiod_client_enable_buffer(buf, 0, false, false);
	ms = ms_since(&t0);
	TEST_ASSERT_EQ(ret, 0, "disabled");
	TEST_ASSERT(ms < PROMPT_MS, "DISABLE stops a sleeping stream at once");
	iiod_client_free_block(block);

	block = start_slow(buf, udp, f, &rx, "setup before CLOSE_BUFFER");
	if (!block)
		return;
	iiod_client_free_block(block);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	iiod_client_close_buffer(buf);
	ms = ms_since(&t0);
	TEST_ASSERT(ms < PROMPT_MS, "CLOSE_BUFFER stops a sleeping stream at once");

	/* Connection loss: the server stops the stream with its buffer list
	 * locked, and another connection can open the buffer only once that is
	 * done. That one is ready beforehand, so that only the teardown counts. */
	ret = emu_client_open(&other);
	TEST_ASSERT_EQ(ret, 0, "second connection");
	if (ret)
		return;
	other_dev = iio_context_find_device(other.ctx, EMU_DEVICE);

	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened again");
	block = ret ? NULL : start_slow(buf, udp, f, &rx, "setup before the disconnect");
	if (!block || !other_dev) {
		emu_client_close(&other);
		return;
	}
	/* The block lives on the client: free it while there is one */
	iiod_client_free_block(block);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	emu_client_close(ec);
	free(buf); /* iiod_client_close_buffer() without the CLOSE_BUFFER */

	do {
		buf = iiod_client_open_buffer(other.client, other.client, other_dev, 0, mask);
		ret = iio_err(buf);
		if (ret == -EEXIST)
			nanosleep(&(struct timespec){ .tv_nsec = 1000000 }, NULL);
	} while (ret == -EEXIST && ms_since(&t0) < RECV_TIMEOUT_MS);
	ms = ms_since(&t0);
	TEST_ASSERT_EQ(ret, 0, "buffer opened by the other connection");
	TEST_ASSERT(ms < PROMPT_MS, "connection loss stops a sleeping stream at once");
	if (!ret)
		iiod_client_close_buffer(buf);

	emu_client_close(&other);
}

int main(int argc, char **argv)
{
	struct iiod_client_buffer_pdata *buf;
	struct iio_channels_mask *mask = NULL;
	const struct iio_device *dev;
	const struct iio_attr *attr;
	struct emu_client ec;
	struct sample_file f;
	const char *value;
	bool slow, count;
	int ret, udp = -1;

	if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "--count"))) {
		printf("Usage: %s <sample.bin> <oracle.json> [--count] | --prompt-stop\n", argv[0]);
		return 2;
	}
	slow = !strcmp(argv[2], "--prompt-stop");
	count = argc == 4;

	ret = read_file(argv[1], &f);
	if (ret || !f.len) {
		printf("Unable to read the sample file %s: %d\n", argv[1], ret);
		free(f.data);
		return 1;
	}

	/* What is there after the first disable comes from this run */
	if (!slow)
		remove(argv[2]);

	ret = emu_client_open(&ec);
	TEST_ASSERT_EQ(ret, 0, "connected, context created");
	if (ret)
		goto out_free;

	/* (a) */
	attr = iio_context_find_attr(ec.ctx, "iiod,udp-stream");
	value = attr ? iio_attr_get_static_value(attr) : NULL;
	TEST_ASSERT_PTR_NOT_NULL(attr, "context advertises iiod,udp-stream");
	TEST_ASSERT(value && !strcmp(value, "1"), "iiod,udp-stream is 1");

	dev = iio_context_find_device(ec.ctx, EMU_DEVICE);
	TEST_ASSERT_PTR_NOT_NULL(dev, EMU_DEVICE " found");
	if (!dev)
		goto out_close;

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	TEST_ASSERT_PTR_NOT_NULL(mask, "mask created");
	if (!mask)
		goto out_close;

	iio_channel_enable(iio_device_find_channel(dev, "voltage0", false), mask);
	iio_channel_enable(iio_device_find_channel(dev, "voltage1", false), mask);

	udp = udp_listen();
	TEST_ASSERT(udp >= 0, "UDP socket bound to 127.0.0.1:25101");
	if (udp < 0)
		goto out_close;

	if (slow) {
		prompt_stop(&ec, dev, mask, udp, &f);
		goto out_close;
	}

	buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (!ret && count)
		count_stream(buf, udp, &f, argv[2]);
	else if (!ret)
		stream(&ec, buf, udp, &f, argv[2]);

out_close:
	if (udp >= 0)
		close(udp);
	if (mask)
		iio_channels_mask_destroy(mask);
	emu_client_close(&ec);
out_free:
	free(f.data);

	printf("test_udp_emu_server%s: %d/%d passed, %d failed\n",
			slow	? " --prompt-stop"
			: count ? " --count"
				: "",
			test_passed, test_count, test_failed);

	return test_failed ? 1 : 0;
}
