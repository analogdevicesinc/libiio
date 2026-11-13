/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * The same emulated device and mask streamed twice from one iiod-emu --udp:
 * once over TCP (daqiri:...,data=tcp, the iiod binary protocol) and once in
 * UDP sample mode (data=udp, through the DAQIRI socket engine, DAQIRI_YAML:
 * socket-udp-loopback-1472.yaml). Both paths serve the sample file as an
 * endless byte stream: the captures must be the same bytes, the file
 * repeated.
 *
 * cf-ad9361-lpc voltage0+voltage1 of a copy of fmcomms2.xml, whose
 * cf-ad9361-lpc_buf0.bin (<sample.bin>, which ctest writes) is 4099 bytes: no
 * whole number of 4-byte samples, of 2016-byte blocks or of 1456-byte
 * payloads, so the stream wraps in the middle of samples, blocks and
 * datagrams. Blocks are 2016 bytes: the emulator's TCP path wraps the file at
 * most once per block, so a block must not be longer than the file. 26 of
 * them hold 36 payloads exactly: the emulator runs with --udp-count 36, the
 * UDP capture queues the 26 blocks at once, waits for the oracle and until
 * the backend has seen the 36 datagrams, and only then disables, with nothing
 * in flight. The captures are aligned on the file's first bytes and compared
 * over 12 cycles of the file or so, at least 3.
 *
 * Usage: test_udp_equivalence <sample.bin> <oracle.json>
 */

#include <errno.h>
#include <iio/iio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_framework.h"

#define DEV_NAME "cf-ad9361-lpc"
#define BLOCK_SIZE 2016 /* 504 samples */
#define NB_BLOCKS 26	/* at most: the TCP capture queues 16 */
#define TCP_BLOCKS 16
#define NB_DEQUEUES 26
#define STREAM_LEN (NB_DEQUEUES * BLOCK_SIZE)
#define UDP_COUNT 36 /* --udp-count: 36 payloads of 1456 bytes, 26 blocks */
#define ALIGN_LEN 64
#define TIMEOUT_MS 3000
#define WAIT_TIMEOUT_MS 10000

/* CHECK_EQ() evaluates <actual> again to print it: evaluate once */
#define CHECK_EQ(actual, expected, message) \
	do { \
		long check_val_ = (long)(actual); \
		TEST_ASSERT_EQ(check_val_, (long)(expected), message); \
	} while (0)

static uint8_t *ref;
static size_t ref_len;
static const char *oracle_path;

struct capture {
	char data_path[16];
	long long lost, dropped_no_block, failed;
	long long sent, arrived; /* UDP: the oracle's, and what the backend saw */
};

static int read_file(const char *path)
{
	uint8_t *p;
	size_t n;
	FILE *f;

	f = fopen(path, "rb");
	if (!f)
		return -errno;

	for (;;) {
		p = realloc(ref, ref_len + 65536);
		if (!p) {
			fclose(f);
			return -ENOMEM;
		}
		ref = p;
		n = fread(ref + ref_len, 1, 65536, f);
		ref_len += n;
		if (n < 65536)
			break;
	}
	n = ferror(f);
	fclose(f);

	return n ? -EIO : 0;
}

static long long attr_ll(const struct iio_buffer *buf, const char *name)
{
	const struct iio_attr *attr = iio_buffer_find_attr(buf, name);
	long long val;

	if (!attr || iio_attr_read_longlong(attr, &val))
		return -1;

	return val;
}

static long ms_since(const struct timespec *t0)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (long)(now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* "sent" of the oracle the emulator writes once --udp-count ended the
 * stream, waiting for it; -1 if it does not come */
static long long oracle_sent(void)
{
	char json[512], *p;
	struct timespec t0;
	size_t len = 0;
	FILE *f;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (!(f = fopen(oracle_path, "r")) && ms_since(&t0) < WAIT_TIMEOUT_MS)
		sleep_ms(2);
	if (f) {
		len = fread(json, 1, sizeof(json) - 1, f);
		fclose(f);
	}
	json[len] = '\0';

	p = strstr(json, "\"sent\":");

	return p ? strtoll(p + strlen("\"sent\":"), NULL, 10) : -1;
}

/* Until the backend has seen <sent> datagrams */
static long long wait_arrived(const struct iio_buffer *buf, long long sent)
{
	struct timespec t0;
	long long n;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	do {
		n = attr_ll(buf, "daqiri_rx_packets") + attr_ll(buf, "daqiri_rx_late_packets") +
		    attr_ll(buf, "daqiri_rx_duplicate_packets");
		if (n >= sent)
			break;
		sleep_ms(1);
	} while (ms_since(&t0) < WAIT_TIMEOUT_MS);

	return n;
}

/* Stream NB_DEQUEUES blocks of voltage0+voltage1 through <uri> into <out>,
 * with <nb_blocks> queued from the start and nothing in flight once the last
 * block is in: a block is queued again only while more are to come, and with
 * <udp> the disable waits until every datagram the emulator sent arrived */
static int capture(const char *uri, unsigned int nb_blocks, bool udp, uint8_t *out,
		struct capture *cap)
{
	static const char *const chans[] = { "voltage0", "voltage1" };
	struct iio_block *blocks[NB_BLOCKS], *block;
	struct iio_buffer_stream *stream = NULL;
	struct iio_channels_mask *mask = NULL;
	const struct iio_attr *attr;
	const struct iio_device *dev;
	struct iio_context *ctx;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	unsigned int i, nb = 0;
	int ret;

	memset(cap, 0, sizeof(*cap));

	ctx = iio_create_context(NULL, uri);
	ret = iio_err(ctx);
	if (ret) {
		printf("  %s: unable to create context: %d\n", uri, ret);
		return ret;
	}

	ret = iio_context_set_timeout(ctx, TIMEOUT_MS);
	if (ret)
		goto out;

	dev = iio_context_find_device(ctx, DEV_NAME);
	mask = dev ? iio_create_channels_mask(iio_device_get_channels_count(dev)) : NULL;
	buf = dev ? iio_device_get_buffer(dev, 0) : NULL;
	ret = -ENODEV;
	if (!mask || !buf)
		goto out;
	for (i = 0; i < 2; i++) {
		chn = iio_device_find_channel(dev, chans[i], false);
		if (!chn)
			goto out;
		iio_channel_enable(chn, mask);
	}

	stream = iio_buffer_open(buf, mask);
	ret = iio_err(stream);
	if (ret) {
		stream = NULL;
		printf("  %s: unable to open the buffer: %d\n", uri, ret);
		goto out;
	}

	attr = iio_buffer_find_attr(buf, "daqiri_data_path");
	if (!attr || iio_attr_read_raw(attr, cap->data_path, sizeof(cap->data_path)) <= 0)
		snprintf(cap->data_path, sizeof(cap->data_path), "?");

	while (nb < nb_blocks) {
		block = iio_buffer_stream_create_block(stream, BLOCK_SIZE);
		ret = iio_err(block);
		if (ret)
			goto out;
		blocks[nb++] = block;

		ret = iio_block_enqueue(block, 0, false);
		if (ret)
			goto out;
	}

	if (udp)
		remove(oracle_path);

	ret = iio_buffer_stream_start(stream);
	for (i = 0; !ret && i < NB_DEQUEUES; i++) {
		ret = iio_block_dequeue(blocks[i % nb_blocks], false);
		if (!ret) {
			memcpy(out + (size_t)i * BLOCK_SIZE, iio_block_start(blocks[i % nb_blocks]),
					BLOCK_SIZE);
			if (i + nb_blocks < NB_DEQUEUES)
				ret = iio_block_enqueue(blocks[i % nb_blocks], 0, false);
		}
	}
	if (ret)
		printf("  %s: block %u: %d\n", uri, i, ret);

	if (!ret && udp) {
		cap->sent = oracle_sent();
		cap->arrived = wait_arrived(buf, cap->sent);
	}
	if (!ret)
		ret = iio_buffer_stream_stop(stream);

	cap->lost = attr_ll(buf, "daqiri_rx_lost_packets");
	cap->dropped_no_block = attr_ll(buf, "daqiri_rx_dropped_no_block");
	cap->failed = attr_ll(buf, "daqiri_blocks_failed");
out:
	while (nb)
		iio_block_destroy(blocks[--nb]);
	if (stream)
		iio_buffer_close(stream);
	if (mask)
		iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);

	return ret;
}

/* Offset of the file's first ALIGN_LEN bytes in <s>, or -1 */
static long find_ref(const uint8_t *s, size_t len)
{
	size_t i;

	for (i = 0; i + ALIGN_LEN <= len; i++)
		if (!memcmp(s + i, ref, ALIGN_LEN))
			return (long)i;

	return -1;
}

/* How many bytes of <s> follow the file, repeated */
static size_t ref_run(const uint8_t *s, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (s[i] != ref[i % ref_len])
			break;

	return i;
}

TEST_FUNCTION(tcp_and_udp)
{
	const char *yaml = getenv("DAQIRI_YAML");
	uint8_t *s_tcp = malloc(STREAM_LEN), *s_udp = malloc(STREAM_LEN);
	struct capture tcp, udp;
	long off_tcp, off_udp;
	char uri[4300];
	size_t len;
	int ret;

	TEST_ASSERT(s_tcp && s_udp, "capture buffers allocated");
	if (!s_tcp || !s_udp)
		goto out;

	/* Different fill so that bytes nobody wrote cannot compare equal */
	memset(s_tcp, 0xa5, STREAM_LEN);
	memset(s_udp, 0x5a, STREAM_LEN);

	ret = capture("daqiri:127.0.0.1,data=tcp", TCP_BLOCKS, false, s_tcp, &tcp);
	CHECK_EQ(ret, 0, "26 blocks over TCP");
	TEST_ASSERT_STR_EQ(tcp.data_path, "tcp", "data=tcp: daqiri_data_path tcp");
	if (ret)
		goto out;

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=udp,yaml=%s", yaml);
	ret = capture(uri, NB_BLOCKS, true, s_udp, &udp);
	CHECK_EQ(ret, 0, "26 blocks in UDP sample mode");
	CHECK_EQ(udp.sent, UDP_COUNT, "UDP: the emulator sent its 36 datagrams");
	CHECK_EQ(udp.arrived, udp.sent, "UDP: all of them arrived before the disable");
	TEST_ASSERT_STR_EQ(udp.data_path, "udp", "data=udp: daqiri_data_path udp");
	CHECK_EQ(udp.lost + udp.dropped_no_block + udp.failed, 0,
			"UDP: no datagram lost or dropped, no block failed");
	if (ret)
		goto out;

	off_tcp = find_ref(s_tcp, STREAM_LEN);
	off_udp = find_ref(s_udp, STREAM_LEN);
	printf("  the file starts at byte %ld of the TCP capture, %ld of the UDP one\n", off_tcp,
			off_udp);
	TEST_ASSERT(off_tcp >= 0 && (size_t)off_tcp < ref_len, "TCP capture aligned on the file");
	TEST_ASSERT(off_udp >= 0 && (size_t)off_udp < ref_len, "UDP capture aligned on the file");
	if (off_tcp < 0 || off_udp < 0 || (size_t)off_tcp >= ref_len || (size_t)off_udp >= ref_len)
		goto out;

	len = STREAM_LEN - (size_t)(off_tcp > off_udp ? off_tcp : off_udp);
	printf("  %zu bytes compared, %zu cycles of the %zu-byte file\n", len, len / ref_len,
			ref_len);
	TEST_ASSERT(len >= 3 * ref_len, "at least 3 cycles of the file compared");

	TEST_ASSERT(!memcmp(s_tcp + off_tcp, s_udp + off_udp, len),
			"the same bytes over TCP and UDP");
	CHECK_EQ(ref_run(s_tcp + off_tcp, len), len, "TCP: the file, repeated");
	CHECK_EQ(ref_run(s_udp + off_udp, len), len, "UDP: the file, repeated");
out:
	free(s_tcp);
	free(s_udp);
}

int main(int argc, char **argv)
{
	const char *yaml = getenv("DAQIRI_YAML");
	int ret;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc != 3) {
		printf("Usage: %s <sample.bin> <oracle.json>\n", argv[0]);
		return 2;
	}
	oracle_path = argv[2];
	if (!yaml || !yaml[0]) {
		printf("DAQIRI_YAML must be the path of socket-udp-loopback.yaml\n");
		return 1;
	}

	ret = read_file(argv[1]);
	if (ret || ref_len < ALIGN_LEN) {
		printf("Unable to read the sample file %s: %d\n", argv[1], ret);
		return 1;
	}

	RUN_TEST(tcp_and_udp);

	free(ref);

	printf("test_udp_equivalence: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
