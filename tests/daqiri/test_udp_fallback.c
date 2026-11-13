/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * The daqiri: backend with a DAQIRI configuration (DAQIRI_YAML:
 * socket-udp-loopback.yaml) against the plain iiod-emu, without --udp: the
 * server does not advertise iiod,udp-stream, and the backend must never send
 * it the setup command. The server logs every setup it refuses for want of a
 * provider (with_emu.sh passes the log in IIOD_EMU_LOG), and a DAQIRI engine
 * started in this process would hold 127.0.0.1:25101 until it exits.
 *
 * --auto-fallback: data=auto streams over TCP. daqiri_data_path reads tcp
 *   before and after the open, the blocks are the sample file repeated, the
 *   log has no refused setup and the engine never started. Then a setup sent
 *   for the buffer through the iiod-client API is refused with -ENOTSUP and
 *   logged once: the log would have told.
 * --refused: data=udp: the buffer open fails with -ENOTSUP, again on a second
 *   try, without asking the server or starting the engine. The context still
 *   reads attributes, and a data=tcp context of the same server, opened while
 *   the first is alive, opens the same buffer and streams.
 *
 * Usage: test_udp_fallback --auto-fallback|--refused <sample.bin>
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio.h>
#include <iio/iiod-client.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../iiod-udp-stream.h"
#include "emu_client.h"
#include "test_framework.h"

#define DEV_NAME "cf-ad9361-lpc"
#define ENGINE_PORT 25101 /* the YAML's local_addr */
#define BLOCK_SIZE 2016	  /* at most the 2019-byte file: one wrap per block */
#define NB_BLOCKS 4
#define NB_DEQUEUES 8
#define STREAM_LEN (NB_DEQUEUES * BLOCK_SIZE)
#define ALIGN_LEN 64
#define TIMEOUT_MS 3000

#define REFUSAL "UDP stream setup for device"

/* CHECK_EQ() evaluates <actual> again to print it: evaluate once */
#define CHECK_EQ(actual, expected, message) \
	do { \
		long check_val_ = (long)(actual); \
		TEST_ASSERT_EQ(check_val_, (long)(expected), message); \
	} while (0)

static const char *yaml;
static uint8_t *ref;
static size_t ref_len;

struct rx {
	struct iio_context *ctx;
	struct iio_buffer *buf;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *stream;
	struct iio_block *blocks[NB_BLOCKS];
	unsigned int nb_blocks;
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

/* The setups the server refused for want of a provider, from its log */
static int refusals(void)
{
	const char *path = getenv("IIOD_EMU_LOG");
	char line[512];
	int n = 0;
	FILE *f;

	f = path ? fopen(path, "r") : NULL;
	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f))
		if (strstr(line, REFUSAL))
			n++;
	fclose(f);

	return n;
}

/* No DAQIRI engine in this process: it would hold the YAML's local_addr */
static bool engine_port_free(void)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(ENGINE_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	bool free_port;

	if (fd < 0)
		return false;
	free_port = !bind(fd, (struct sockaddr *)&sa, sizeof(sa));
	close(fd);

	return free_port;
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

/* A context of the server at <uri>, voltage0+voltage1 of DEV_NAME */
static int rx_init(struct rx *rx, const char *uri)
{
	static const char *const chans[] = { "voltage0", "voltage1" };
	const struct iio_device *dev;
	struct iio_channel *chn;
	unsigned int i;
	int ret;

	memset(rx, 0, sizeof(*rx));

	rx->ctx = iio_create_context(NULL, uri);
	ret = iio_err(rx->ctx);
	if (ret) {
		printf("  %s: unable to create context: %d\n", uri, ret);
		rx->ctx = NULL;
		return ret;
	}

	ret = iio_context_set_timeout(rx->ctx, TIMEOUT_MS);
	if (ret)
		return ret;

	dev = iio_context_find_device(rx->ctx, DEV_NAME);
	if (!dev)
		return -ENODEV;

	rx->mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!rx->mask)
		return -ENOMEM;

	for (i = 0; i < 2; i++) {
		chn = iio_device_find_channel(dev, chans[i], false);
		if (!chn)
			return -ENOENT;
		iio_channel_enable(chn, rx->mask);
	}

	rx->buf = iio_device_get_buffer(dev, 0);

	return rx->buf ? 0 : -ENODEV;
}

static int rx_open(struct rx *rx)
{
	struct iio_block *block;
	int ret;

	rx->stream = iio_buffer_open(rx->buf, rx->mask);
	ret = iio_err(rx->stream);
	if (ret) {
		rx->stream = NULL;
		return ret;
	}

	while (rx->nb_blocks < NB_BLOCKS) {
		block = iio_buffer_stream_create_block(rx->stream, BLOCK_SIZE);
		ret = iio_err(block);
		if (ret)
			return ret;
		rx->blocks[rx->nb_blocks++] = block;
	}

	return 0;
}

static void rx_destroy(struct rx *rx)
{
	while (rx->nb_blocks)
		iio_block_destroy(rx->blocks[--rx->nb_blocks]);
	if (rx->stream)
		iio_buffer_close(rx->stream);
	if (rx->mask)
		iio_channels_mask_destroy(rx->mask);
	if (rx->ctx)
		iio_context_destroy(rx->ctx);

	memset(rx, 0, sizeof(*rx));
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

/* NB_DEQUEUES blocks of the open buffer are the sample file, repeated */
static void stream_file(struct rx *rx, const char *what)
{
	uint8_t *s = malloc(STREAM_LEN);
	unsigned int i;
	char msg[128];
	size_t k;
	long off;
	int ret;

	TEST_ASSERT_PTR_NOT_NULL(s, "capture buffer");
	if (!s)
		return;

	for (i = 0, ret = 0; !ret && i < rx->nb_blocks; i++)
		ret = iio_block_enqueue(rx->blocks[i], 0, false);
	if (!ret)
		ret = iio_buffer_stream_start(rx->stream);
	for (i = 0; !ret && i < NB_DEQUEUES; i++) {
		ret = iio_block_dequeue(rx->blocks[i % NB_BLOCKS], false);
		if (!ret) {
			memcpy(s + (size_t)i * BLOCK_SIZE,
					iio_block_start(rx->blocks[i % NB_BLOCKS]), BLOCK_SIZE);
			if (i + NB_BLOCKS < NB_DEQUEUES)
				ret = iio_block_enqueue(rx->blocks[i % NB_BLOCKS], 0, false);
		}
	}
	if (!ret)
		ret = iio_buffer_stream_stop(rx->stream);
	snprintf(msg, sizeof(msg), "%s: 8 blocks streamed", what);
	CHECK_EQ(ret, 0, msg);

	off = ret ? -1 : find_ref(s, STREAM_LEN);
	snprintf(msg, sizeof(msg), "%s: the file found in the stream", what);
	TEST_ASSERT(off >= 0 && (size_t)off < ref_len, msg);
	if (off >= 0) {
		for (k = (size_t)off; k < STREAM_LEN && s[k] == ref[(k - (size_t)off) % ref_len];
				k++)
			;
		snprintf(msg, sizeof(msg), "%s: the stream is the file, repeated", what);
		CHECK_EQ(k, STREAM_LEN, msg);
	}

	free(s);
}

/* What the backend never did */
static void check_untouched(const char *what)
{
	char msg[128];

	snprintf(msg, sizeof(msg), "%s: no setup reached the server", what);
	CHECK_EQ(refusals(), 0, msg);
	snprintf(msg, sizeof(msg), "%s: no DAQIRI engine started (127.0.0.1:25101 free)", what);
	TEST_ASSERT(engine_port_free(), msg);
}

/* The control: the setup sent for the buffer anyway is refused and logged */
static void setup_through_client(void)
{
	const struct iiod_udp_stream_req req = {
		.dst_ip = 0x7f000001, /* 127.0.0.1 */
		.dst_port = ENGINE_PORT,
		.max_payload = 9200,
	};
	struct iiod_client_buffer_pdata *buf;
	struct iiod_udp_stream_resp resp;
	struct iio_channels_mask *mask;
	const struct iio_device *dev;
	struct emu_client ec;
	int ret;

	ret = emu_client_open(&ec);
	CHECK_EQ(ret, 0, "control: iiod-client connection");
	if (ret)
		return;

	dev = iio_context_find_device(ec.ctx, DEV_NAME);
	mask = dev ? iio_create_channels_mask(iio_device_get_channels_count(dev)) : NULL;
	TEST_ASSERT_PTR_NOT_NULL(mask, "control: mask");
	if (!mask)
		goto out_close;
	iio_channel_enable(iio_device_find_channel(dev, "voltage0", false), mask);
	iio_channel_enable(iio_device_find_channel(dev, "voltage1", false), mask);

	buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
	ret = iio_err(buf);
	CHECK_EQ(ret, 0, "control: buffer opened");
	if (!ret) {
		ret = iiod_client_udp_stream_setup(buf, &req, &resp);
		CHECK_EQ(ret, -ENOTSUP, "control: the setup refused, -ENOTSUP");
		iiod_client_close_buffer(buf);
	}
	CHECK_EQ(refusals(), 1, "control: the server logged that one refusal");

	iio_channels_mask_destroy(mask);
out_close:
	emu_client_close(&ec);
}

TEST_FUNCTION(auto_fallback)
{
	char uri[4300];
	struct rx rx;
	int ret;

	TEST_ASSERT(engine_port_free(), "127.0.0.1:25101 free to start with");
	CHECK_EQ(refusals(), 0, "the server's log readable, no refusal yet");

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=auto,yaml=%s", yaml);
	ret = rx_init(&rx, uri);
	CHECK_EQ(ret, 0, "data=auto context with a YAML");
	if (ret)
		goto out;
	TEST_ASSERT_PTR_NULL(iio_context_find_attr(rx.ctx, "iiod,udp-stream"),
			"the server does not advertise iiod,udp-stream");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "tcp", "TCP, before the open");

	ret = rx_open(&rx);
	CHECK_EQ(ret, 0, "buffer opens");
	if (ret)
		goto out;
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_data_path"), "tcp", "TCP, open");
	TEST_ASSERT_STR_EQ(attr_str(rx.buf, "daqiri_engine_kind"), "none", "no engine");
	stream_file(&rx, "data=auto");
out:
	rx_destroy(&rx);

	check_untouched("data=auto");
	setup_through_client();
}

TEST_FUNCTION(udp_refused)
{
	const struct iio_channel *chn;
	const struct iio_device *phy;
	const struct iio_attr *attr;
	char uri[4300], val[64] = "";
	struct rx rx, tcp;
	int ret;

	memset(&tcp, 0, sizeof(tcp));

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=udp,yaml=%s", yaml);
	ret = rx_init(&rx, uri);
	CHECK_EQ(ret, 0, "data=udp context with a YAML");
	if (ret)
		goto out;

	CHECK_EQ(rx_open(&rx), -ENOTSUP, "buffer open: -ENOTSUP");
	CHECK_EQ(rx_open(&rx), -ENOTSUP, "and again");

	phy = iio_context_find_device(rx.ctx, "ad9361-phy");
	chn = phy ? iio_device_find_channel(phy, "voltage0", false) : NULL;
	attr = chn ? iio_channel_find_attr(chn, "sampling_frequency") : NULL;
	TEST_ASSERT(attr && iio_attr_read_raw(attr, val, sizeof(val)) > 0,
			"the context still reads attributes");

	/* The same buffer, through TCP, while the refused context lives */
	ret = rx_init(&tcp, "daqiri:127.0.0.1,data=tcp");
	CHECK_EQ(ret, 0, "data=tcp context of the same server");
	if (!ret)
		ret = rx_open(&tcp);
	CHECK_EQ(ret, 0, "data=tcp: the buffer opens");
	if (!ret) {
		TEST_ASSERT_STR_EQ(attr_str(tcp.buf, "daqiri_data_path"), "tcp", "over TCP");
		stream_file(&tcp, "data=tcp");
	}
	rx_destroy(&tcp);

	val[0] = '\0';
	TEST_ASSERT(attr && iio_attr_read_raw(attr, val, sizeof(val)) > 0,
			"the refused context reads attributes still");
out:
	rx_destroy(&rx);

	check_untouched("data=udp");
}

int main(int argc, char **argv)
{
	bool refused;
	int ret;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc != 3 || (strcmp(argv[1], "--auto-fallback") && strcmp(argv[1], "--refused"))) {
		printf("Usage: %s --auto-fallback|--refused <sample.bin>\n", argv[0]);
		return 2;
	}
	refused = !strcmp(argv[1], "--refused");

	yaml = getenv("DAQIRI_YAML");
	if (!yaml || !yaml[0]) {
		printf("DAQIRI_YAML must be the path of socket-udp-loopback.yaml\n");
		return 1;
	}

	ret = read_file(argv[2]);
	if (ret || ref_len < ALIGN_LEN) {
		printf("Unable to read the sample file %s: %d\n", argv[2], ret);
		return 1;
	}

	if (refused)
		RUN_TEST(udp_refused);
	else
		RUN_TEST(auto_fallback);

	free(ref);

	printf("test_udp_fallback %s: %d/%d passed, %d failed\n", argv[1], test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
