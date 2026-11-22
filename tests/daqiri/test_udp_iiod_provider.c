// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * iiod's UDP sample stream provider (iiod --udp-port), seen from a plain UDP
 * socket: iiod serves the emulated context of fmcomms2.xml, the setup command
 * goes through the iiod-client API, then every datagram of the stream is
 * checked (source, header, sequence number, payload against the sample file).
 * Needs neither DAQIRI nor the daqiri: module.
 *
 * Usage: test_udp_iiod_provider <sample.bin>
 *
 * Run through with_iiod.sh, iiod started with
 *   --udp-port 25102 --udp-block-size 5824
 * and <sample.bin> the sample file next to the XML, longer than a block: the
 * emulated device fills a block from that file with at most one wrap.
 *
 * Nothing paces the provider, and a loopback socket drops what the test does
 * not read in time: missing sequence numbers are counted, not failed. The
 * payload of a datagram depends on its number alone, so the others are still
 * checked byte for byte.
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
#define SRC_PORT 25102	     /* --udp-port */
#define PAYLOAD 1456	     /* a quarter of --udp-block-size */

#define MIN_DATAGRAMS 64
#define RECV_TIMEOUT_MS 5000
/* After a disable, the stream is over once nothing came for this long */
#define IDLE_MS 300
/* A stop joins the sender and takes its blocks back, which the emulated
 * device fills at once. The bound leaves room for a loaded machine. */
#define PROMPT_MS 2000
/* How long to wait for iiod to listen, and to let go of a buffer */
#define SERVER_WAIT_MS 10000
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
	uint64_t count, bad, missing;
	uint64_t first_seq, next_seq;
	size_t base; /* where datagram 0 starts in the sample file */
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

static void nap(void)
{
	const struct timespec ts = { .tv_nsec = 50 * 1000000 };

	nanosleep(&ts, NULL);
}

/* with_iiod.sh starts iiod and the test together */
static int wait_for_server(void)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(EMU_PORT),
		.sin_addr.s_addr = htonl(LOOPBACK),
	};
	struct timespec t0;
	int fd, ret;

	clock_gettime(CLOCK_MONOTONIC, &t0);

	do {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			return -errno;

		ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
		close(fd);
		if (!ret)
			return 0;

		nap();
	} while (ms_since(&t0) < SERVER_WAIT_MS);

	return -ETIMEDOUT;
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

static bool matches_at(const struct sample_file *f, size_t off, const uint8_t *p, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (p[i] != f->data[(off + i) % f->len])
			return false;
	}

	return true;
}

/* The stream is the sample file over and over, one payload after the other.
 * Where it starts is not the test's business (a stop reads blocks it does not
 * send): the first datagram received says, and the others must agree. */
static bool payload_matches(
		struct rx *rx, const struct sample_file *f, uint64_t seq, const uint8_t *p)
{
	size_t len = rx->payload_bytes;
	size_t shift = (size_t)((seq % f->len) * (len % f->len) % f->len);
	size_t off;

	if (rx->count)
		return matches_at(f, (rx->base + shift) % f->len, p, len);

	for (off = 0; off < f->len; off++) {
		if (matches_at(f, off, p, len)) {
			rx->base = (off + f->len - shift) % f->len;
			return true;
		}
	}

	return false;
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
	else if (rx->count && hdr.sequence < rx->next_seq)
		report(rx, hdr.sequence, "sequence number repeated or out of order");
	else if (!payload_matches(rx, f, hdr.sequence, payload))
		report(rx, hdr.sequence, "payload is not the sample file's bytes");
	else {
		if (!rx->count)
			rx->first_seq = hdr.sequence;
		else
			rx->missing += hdr.sequence - rx->next_seq;
		rx->next_seq = hdr.sequence + 1;
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

/* One stream: enable, at least MIN_DATAGRAMS, a setup refused meanwhile,
 * disable, and silence afterwards */
static void one_stream(struct iiod_client_buffer_pdata *buf, int udp, const struct sample_file *f,
		const struct iiod_udp_stream_resp *resp, const char *name)
{
	static struct rx rx;
	struct iiod_udp_stream_resp other;
	struct timespec t0;
	uint64_t count;
	long ms;
	int ret;

	printf("%s\n", name);

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable");

	rx_reset(&rx, resp);
	receive(udp, &rx, f, MIN_DATAGRAMS, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count >= MIN_DATAGRAMS, "at least 64 datagrams within 5 s");
	TEST_ASSERT(rx.count && rx.first_seq == 0, "sequence starts at 0");

	ret = setup(buf, LOOPBACK, DST_PORT, PAYLOAD, &other);
	TEST_ASSERT_EQ(ret, -EBUSY, "setup refused while the stream is active");

	clock_gettime(CLOCK_MONOTONIC, &t0);
	ret = iiod_client_enable_buffer(buf, 0, false, false);
	ms = ms_since(&t0);
	TEST_ASSERT_EQ(ret, 0, "disable");
	TEST_ASSERT(ms < PROMPT_MS, "disable answered promptly");

	/* Whatever was sent before the stop */
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.bad, 0, "every datagram valid, from its payload to its source");
	printf("  %llu datagrams, %llu sequence numbers missing\n", (unsigned long long)rx.count,
			(unsigned long long)rx.missing);

	count = rx.count;
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count == count && !rx.bad, "nothing sent after the disable");
}

static void streams(struct emu_client *ec, struct iiod_client_buffer_pdata *buf, int udp,
		const struct sample_file *f)
{
	static struct rx rx;
	struct iiod_udp_stream_resp resp, other;
	struct iio_block_pdata *block;
	uint32_t first_id;
	void *data;
	int ret;

	ret = setup(buf, LOOPBACK, DST_PORT, 15, &other);
	TEST_ASSERT_EQ(ret, -EINVAL, "setup: max_payload 15 leaves no room for 16 bytes");
	ret = setup(buf, LOOPBACK, 0, PAYLOAD, &other);
	TEST_ASSERT_EQ(ret, -EINVAL, "setup: destination port 0 refused");
	ret = setup(buf, 0, DST_PORT, PAYLOAD, &other);
	TEST_ASSERT_EQ(ret, -EINVAL, "setup: destination address 0 refused");

	/* The largest multiple of 16 and of the sample-set size (4 bytes here)
	 * that is at most max_payload */
	ret = setup(buf, LOOPBACK, DST_PORT, PAYLOAD + 15, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup: max_payload 1471");
	if (ret)
		return;
	TEST_ASSERT_EQ(resp.payload_bytes, PAYLOAD, "setup: payload_bytes rounded down to 1456");
	TEST_ASSERT(resp.src_ip == LOOPBACK, "setup: source is 127.0.0.1");
	TEST_ASSERT_EQ(resp.src_port, SRC_PORT, "setup: source port is --udp-port's");
	TEST_ASSERT(resp.stream_id != 0, "setup: stream ID is not 0");

	/* The server cannot enable a buffer without a block; UDP mode never
	 * enqueues it */
	block = iiod_client_create_block(buf, PAYLOAD, &data);
	ret = iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block created");
	if (ret)
		return;

	one_stream(buf, udp, f, &resp, "stream 1");

	/* An enable without a new setup starts nothing */
	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable without a setup");
	rx_reset(&rx, &resp);
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.count + rx.bad, 0, "enable without a setup: nothing sent");
	ret = iiod_client_enable_buffer(buf, 0, false, false);
	TEST_ASSERT_EQ(ret, 0, "disable after it");

	/* A new setup on the same open buffer: a new stream ID, the sequence
	 * from 0 again */
	first_id = resp.stream_id;
	ret = setup(buf, LOOPBACK, DST_PORT, PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup 2");
	TEST_ASSERT(resp.stream_id != 0 && resp.stream_id != first_id, "setup 2: a new stream ID");
	TEST_ASSERT_EQ(resp.payload_bytes, PAYLOAD, "setup 2: payload_bytes 1456");

	one_stream(buf, udp, f, &resp, "stream 2");

	/* Another payload size needs other blocks. The provider has the old
	 * ones back after a stream, so this works; but the new ones are queued
	 * at once, and until a stream has run a third size finds them held. */
	ret = setup(buf, LOOPBACK, DST_PORT, 4096, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup 3: max_payload 4096");
	TEST_ASSERT_EQ(resp.payload_bytes, 4096, "setup 3: payload_bytes 4096");
	ret = setup(buf, LOOPBACK, DST_PORT, PAYLOAD, &other);
	TEST_ASSERT_EQ(ret, -EBUSY, "setup: another size refused while one is pending");

	iiod_client_free_block(block);
	iiod_client_close_buffer(buf);

	ret = iiod_client_nop(ec->client);
	TEST_ASSERT_EQ(ret, 0, "connection usable after close");

	rx_reset(&rx, &resp);
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.count + rx.bad, 0, "nothing sent after close");
}

/* Loss of the connection stops the stream and frees the buffer */
static void connection_loss(int udp, const struct sample_file *f)
{
	static struct rx rx;
	struct iiod_client_buffer_pdata *buf = NULL;
	struct iio_channels_mask *mask = NULL;
	struct iiod_udp_stream_resp resp;
	const struct iio_device *dev;
	struct iio_block_pdata *block;
	struct emu_client ec;
	struct timespec t0;
	uint64_t count;
	void *data;
	int ret;

	printf("connection loss\n");

	ret = emu_client_open(&ec);
	TEST_ASSERT_EQ(ret, 0, "second connection");
	if (ret)
		return;

	dev = iio_context_find_device(ec.ctx, EMU_DEVICE);
	mask = dev ? iio_create_channels_mask(iio_device_get_channels_count(dev)) : NULL;
	TEST_ASSERT_PTR_NOT_NULL(mask, "device found, mask created");
	if (!mask)
		goto out_close;

	iio_channel_enable(iio_device_find_channel(dev, "voltage0", false), mask);
	iio_channel_enable(iio_device_find_channel(dev, "voltage1", false), mask);

	buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened again: the first close freed it");
	if (ret)
		goto out_close;

	ret = setup(buf, LOOPBACK, DST_PORT, PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup");
	block = iiod_client_create_block(buf, PAYLOAD, &data);
	ret = ret ? ret : iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block created");
	if (ret)
		goto out_close;

	ret = iiod_client_enable_buffer(buf, 0, true, false);
	TEST_ASSERT_EQ(ret, 0, "enable");

	rx_reset(&rx, &resp);
	receive(udp, &rx, f, MIN_DATAGRAMS, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count >= MIN_DATAGRAMS, "at least 64 datagrams within 5 s");

	/* No disable, no close */
	iio_channels_mask_destroy(mask);
	emu_client_close(&ec);

	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT_EQ(rx.bad, 0, "every datagram valid");
	count = rx.count;
	receive(udp, &rx, f, 0, RECV_TIMEOUT_MS);
	TEST_ASSERT(rx.count == count && !rx.bad, "nothing sent after the connection went");

	/* The server lets go of the buffer on its own time */
	ret = emu_client_open(&ec);
	TEST_ASSERT_EQ(ret, 0, "third connection");
	if (ret)
		return;

	dev = iio_context_find_device(ec.ctx, EMU_DEVICE);
	mask = dev ? iio_create_channels_mask(iio_device_get_channels_count(dev)) : NULL;
	if (!mask)
		goto out_close;
	iio_channel_enable(iio_device_find_channel(dev, "voltage0", false), mask);
	iio_channel_enable(iio_device_find_channel(dev, "voltage1", false), mask);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	do {
		buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
		ret = iio_err(buf);
		if (ret)
			nap();
	} while (ret && ms_since(&t0) < SERVER_WAIT_MS);
	TEST_ASSERT_EQ(ret, 0, "buffer free again after the connection went");
	if (!ret)
		iiod_client_close_buffer(buf);

out_close:
	if (mask)
		iio_channels_mask_destroy(mask);
	emu_client_close(&ec);
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
	int ret, udp = -1;

	if (argc != 2) {
		printf("Usage: %s <sample.bin>\n", argv[0]);
		return 2;
	}

	ret = read_file(argv[1], &f);
	if (ret || !f.len) {
		printf("Unable to read the sample file %s: %d\n", argv[1], ret);
		free(f.data);
		return 1;
	}

	ret = wait_for_server();
	TEST_ASSERT_EQ(ret, 0, "iiod listens within 10 s");
	if (ret)
		goto out_free;

	ret = emu_client_open(&ec);
	TEST_ASSERT_EQ(ret, 0, "connected, context created");
	if (ret)
		goto out_free;

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

	buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (!ret) {
		streams(&ec, buf, udp, &f);
		connection_loss(udp, &f);
	}

out_close:
	if (udp >= 0)
		close(udp);
	if (mask)
		iio_channels_mask_destroy(mask);
	emu_client_close(&ec);
out_free:
	free(f.data);

	printf("test_udp_iiod_provider: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
