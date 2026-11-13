/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Data-plane test of daqiri:<host>,data=tcp against iiod-emu: a buffer
 * streamed through daqiri: must carry the same bytes as one streamed through
 * ip: from the same server, and the context both describe must be the same.
 *
 * iiod-emu serves a fixture whose cf-ad9361-lpc_buf0.bin is the stock file
 * (TESTS_BUF0_BIN) three times over, so every 4096-byte block is filled and
 * the stream is the stock file repeated.
 */

#include <errno.h>
#include <iio/iio.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "test_framework.h"

#ifndef TESTS_BUF0_BIN
#error "TESTS_BUF0_BIN must be the path of tests/resources/xmls/cf-ad9361-lpc_buf0.bin"
#endif

#define DEFAULT_DAQIRI_URI "daqiri:127.0.0.1,data=tcp"
#define DEFAULT_IP_URI "ip:127.0.0.1"
#define EMU_PORT 30431

#define DEV_NAME "cf-ad9361-lpc"
#define NB_BLOCKS 4
#define BLOCK_SIZE 4096
#define NB_DEQUEUES 8
#define STREAM_LEN (NB_DEQUEUES * BLOCK_SIZE)
#define ALIGN_LEN 64

/* A cancelled dequeue must return well before iiod-emu's own 5 s wait for a
 * buffer that was never enabled, and before the 5 s context timeout. */
#define CANCEL_AFTER_MS 200
#define CANCEL_MAX_MS 2000

static const char *daqiri_uri;
static const char *ip_uri;
static unsigned char *ref;
static size_t ref_len;

struct rx {
	struct iio_context *ctx;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *stream;
	struct iio_block *blocks[NB_BLOCKS];
	unsigned int nb_blocks;
};

static const char *uri_from_env(const char *name, const char *def)
{
	const char *val = getenv(name);

	return val && val[0] ? val : def;
}

static int load_ref(void)
{
	FILE *f = fopen(TESTS_BUF0_BIN, "rb");
	long len;

	if (!f)
		return -errno;

	if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < ALIGN_LEN || fseek(f, 0, SEEK_SET)) {
		fclose(f);
		return -EIO;
	}

	ref = malloc((size_t)len);
	if (!ref) {
		fclose(f);
		return -ENOMEM;
	}

	ref_len = fread(ref, 1, (size_t)len, f);
	fclose(f);

	return ref_len == (size_t)len ? 0 : -EIO;
}

static void rx_close(struct rx *rx)
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

/* Context, voltage0+voltage1 of DEV_NAME, buffer 0 opened, NB_BLOCKS blocks. */
static int rx_open(struct rx *rx, const char *uri)
{
	static const char *const chans[] = { "voltage0", "voltage1" };
	const struct iio_device *dev;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	struct iio_block *block;
	unsigned int i;
	int ret;

	memset(rx, 0, sizeof(*rx));

	rx->ctx = iio_create_context(NULL, uri);
	ret = iio_err(rx->ctx);
	if (ret) {
		rx->ctx = NULL;
		printf("  %s: unable to create context: %d\n", uri, ret);
		return ret;
	}

	dev = iio_context_find_device(rx->ctx, DEV_NAME);
	if (!dev)
		return -ENODEV;

	rx->mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!rx->mask)
		return -ENOMEM;

	for (i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
		chn = iio_device_find_channel(dev, chans[i], false);
		if (!chn)
			return -ENOENT;
		iio_channel_enable(chn, rx->mask);
	}

	buf = iio_device_get_buffer(dev, 0);
	if (!buf)
		return -ENODEV;

	rx->stream = iio_buffer_open(buf, rx->mask);
	ret = iio_err(rx->stream);
	if (ret) {
		rx->stream = NULL;
		printf("  %s: unable to open buffer: %d\n", uri, ret);
		return ret;
	}

	for (i = 0; i < NB_BLOCKS; i++) {
		block = iio_buffer_stream_create_block(rx->stream, BLOCK_SIZE);
		ret = iio_err(block);
		if (ret) {
			printf("  %s: unable to create block: %d\n", uri, ret);
			return ret;
		}
		rx->blocks[rx->nb_blocks++] = block;
	}

	return 0;
}

/* Stream NB_DEQUEUES blocks from <uri> into <out>, then tear everything down. */
static int capture(const char *uri, unsigned char *out)
{
	struct iio_block *block;
	unsigned int i;
	struct rx rx;
	int ret;

	ret = rx_open(&rx, uri);
	if (ret)
		goto out;

	for (i = 0; i < NB_BLOCKS; i++) {
		ret = iio_block_enqueue(rx.blocks[i], 0, false);
		if (ret)
			goto out;
	}

	ret = iio_buffer_stream_start(rx.stream);
	if (ret)
		goto out;

	for (i = 0; i < NB_DEQUEUES; i++) {
		block = rx.blocks[i % NB_BLOCKS];

		ret = iio_block_dequeue(block, false);
		if (ret)
			goto out;

		memcpy(out + (size_t)i * BLOCK_SIZE, iio_block_start(block), BLOCK_SIZE);

		/* Leave nothing in flight once the last block is in */
		if (i + NB_BLOCKS < NB_DEQUEUES) {
			ret = iio_block_enqueue(block, 0, false);
			if (ret)
				goto out;
		}
	}

	ret = iio_buffer_stream_stop(rx.stream);

out:
	if (ret)
		printf("  %s: capture failed: %d\n", uri, ret);
	rx_close(&rx);
	return ret;
}

/* Offset of the first ALIGN_LEN bytes of the reference in <s>, or -1. */
static long find_ref(const unsigned char *s, size_t len)
{
	size_t i;

	for (i = 0; i + ALIGN_LEN <= len; i++)
		if (!memcmp(s + i, ref, ALIGN_LEN))
			return (long)i;

	return -1;
}

/* Number of bytes of <s> that follow the reference file, repeated. */
static size_t ref_run(const unsigned char *s, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (s[i] != ref[i % ref_len])
			break;

	return i;
}

TEST_FUNCTION(stream_equivalence)
{
	unsigned char *s_ip = malloc(STREAM_LEN), *s_dq = malloc(STREAM_LEN);
	long off_ip, off_dq;
	size_t len;
	int ret;

	TEST_ASSERT(s_ip && s_dq, "capture buffers allocated");
	if (!s_ip || !s_dq)
		goto out;

	/* Different fill so that bytes nobody wrote cannot compare equal */
	memset(s_ip, 0xa5, STREAM_LEN);
	memset(s_dq, 0x5a, STREAM_LEN);

	/* One after the other: the ip: buffer is closed before daqiri: opens */
	ret = capture(ip_uri, s_ip);
	TEST_ASSERT_EQ(ret, 0, "8 blocks streamed via ip:");
	if (ret)
		goto out;

	ret = capture(daqiri_uri, s_dq);
	TEST_ASSERT_EQ(ret, 0, "8 blocks streamed via daqiri:");
	if (ret)
		goto out;

	off_ip = find_ref(s_ip, STREAM_LEN);
	off_dq = find_ref(s_dq, STREAM_LEN);
	printf("  aligned at ip:%ld daqiri:%ld\n", off_ip, off_dq);
	TEST_ASSERT(off_ip >= 0 && (size_t)off_ip < ref_len, "reference found in ip: stream");
	TEST_ASSERT(off_dq >= 0 && (size_t)off_dq < ref_len, "reference found in daqiri: stream");
	if (off_ip < 0 || off_dq < 0 || (size_t)off_ip >= ref_len || (size_t)off_dq >= ref_len)
		goto out;

	len = STREAM_LEN - (size_t)(off_ip > off_dq ? off_ip : off_dq);

	TEST_ASSERT(!memcmp(s_ip + off_ip, s_dq + off_dq, len), "same bytes via ip: and daqiri:");
	TEST_ASSERT_EQ(ref_run(s_dq + off_dq, len), len,
			"daqiri: stream is the reference repeated");
	TEST_ASSERT_EQ(ref_run(s_ip + off_ip, len), len, "ip: stream is the reference repeated");

out:
	free(s_ip);
	free(s_dq);
}

static bool strip_ctx_attr(const char *name, size_t len)
{
	return (len == strlen("uri") && !strncmp(name, "uri", len)) ||
	       (len == strlen("ip,ip-addr") && !strncmp(name, "ip,ip-addr", len)) ||
	       (len > strlen("daqiri,") && !strncmp(name, "daqiri,", strlen("daqiri,")));
}

/* The daqiri: backend's own buffer attributes (daqiri_data_path, the
 * counters), in <buffer> and as the legacy <buffer-attribute> */
static bool strip_buf_attr(const char *name, size_t len)
{
	return len > strlen("daqiri_") && !strncmp(name, "daqiri_", strlen("daqiri_"));
}

/* Where an attribute tag at <p> starts its name and whether to strip it */
static const char *attr_tag_name(const char *p, bool *strip_ctx)
{
	static const char *const tags[] = {
		"<context-attribute name=\"",
		"<attribute name=\"",
		"<buffer-attribute name=\"",
	};
	unsigned int i;

	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		if (!strncmp(p, tags[i], strlen(tags[i]))) {
			*strip_ctx = !i;
			return p + strlen(tags[i]);
		}
	}

	return NULL;
}

/*
 * The context XML minus what legitimately differs between backends: the
 * <context> opening tag (backend name, description), the uri, ip,ip-addr
 * and daqiri,* context attributes, and the daqiri_* buffer attributes.
 */
static char *normalize_xml(const char *xml)
{
	const char *p, *name, *name_end, *end;
	bool strip, is_ctx;
	char *out, *o;

	out = malloc(strlen(xml) + 1);
	if (!out)
		return NULL;

	p = strstr(xml, "<context ");
	end = p ? strchr(p, '>') : NULL;
	if (!end)
		goto err_free;

	o = out;
	memcpy(o, xml, (size_t)(p - xml));
	o += p - xml;

	for (p = end + 1; *p;) {
		name = attr_tag_name(p, &is_ctx);
		if (!name) {
			*o++ = *p++;
			continue;
		}

		name_end = strchr(name, '"');
		end = strstr(p, "/>");
		if (!name_end || !end)
			goto err_free;
		end += 2;

		strip = is_ctx ? strip_ctx_attr(name, (size_t)(name_end - name))
			       : strip_buf_attr(name, (size_t)(name_end - name));
		if (!strip) {
			memcpy(o, p, (size_t)(end - p));
			o += end - p;
		}
		p = end;
	}

	*o = '\0';
	return out;

err_free:
	free(out);
	return NULL;
}

TEST_FUNCTION(xml_equivalence)
{
	struct iio_context *ctx_i = iio_create_context(NULL, ip_uri);
	struct iio_context *ctx_d = iio_create_context(NULL, daqiri_uri);
	char *xml_i = NULL, *xml_d = NULL, *norm_i = NULL, *norm_d = NULL;
	size_t i;

	TEST_ASSERT(!iio_err(ctx_i), "ip: context created");
	TEST_ASSERT(!iio_err(ctx_d), "daqiri: context created");
	if (iio_err(ctx_i) || iio_err(ctx_d))
		goto out;

	xml_i = iio_context_get_xml(ctx_i);
	xml_d = iio_context_get_xml(ctx_d);
	if (iio_err(xml_i))
		xml_i = NULL;
	if (iio_err(xml_d))
		xml_d = NULL;
	TEST_ASSERT(xml_i && xml_d, "both context XMLs generated");
	if (!xml_i || !xml_d)
		goto out;

	norm_i = normalize_xml(xml_i);
	norm_d = normalize_xml(xml_d);
	TEST_ASSERT(norm_i && norm_d, "both context XMLs normalized");
	if (!norm_i || !norm_d)
		goto out;

	TEST_ASSERT(strstr(norm_d, "<device id=") != NULL, "XML lists devices");
	TEST_ASSERT(!strcmp(norm_i, norm_d), "same context XML via ip: and daqiri:");
	if (strcmp(norm_i, norm_d)) {
		for (i = 0; norm_i[i] && norm_i[i] == norm_d[i]; i++)
			;
		printf("  first difference at %zu:\n    ip:     %.80s\n    daqiri: %.80s\n", i,
				norm_i + i, norm_d + i);
	}

out:
	free(norm_i);
	free(norm_d);
	free(xml_i);
	free(xml_d);
	if (!iio_err(ctx_i))
		iio_context_destroy(ctx_i);
	if (!iio_err(ctx_d))
		iio_context_destroy(ctx_d);
}

static long elapsed_ms(const struct timespec *t0)
{
	struct timespec t1;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0->tv_sec) * 1000L + (t1.tv_nsec - t0->tv_nsec) / 1000000L;
}

static void *cancel_later(void *d)
{
	struct timespec ts = { 0, CANCEL_AFTER_MS * 1000000L };

	nanosleep(&ts, NULL);
	iio_buffer_stream_cancel(d);

	return NULL;
}

/* Block in a dequeue that can never complete (the buffer is not enabled) and
 * cancel the buffer from another thread. */
static void cancel_case(const char *uri)
{
	struct timespec t0;
	pthread_t thread;
	struct rx rx;
	long ms = -1;
	int ret;

	ret = rx_open(&rx, uri);
	TEST_ASSERT_EQ(ret, 0, uri);
	if (ret)
		goto out;

	ret = iio_block_enqueue(rx.blocks[0], 0, false);
	TEST_ASSERT_EQ(ret, 0, "block enqueued");
	if (ret)
		goto out;

	ret = pthread_create(&thread, NULL, cancel_later, rx.stream);
	TEST_ASSERT_EQ(ret, 0, "cancel thread started");
	if (ret)
		goto out;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	ret = iio_block_dequeue(rx.blocks[0], false);
	ms = elapsed_ms(&t0);
	pthread_join(thread, NULL);

	printf("  %s: dequeue returned %d after %ld ms\n", uri, ret, ms);
	TEST_ASSERT(ret < 0, "cancelled dequeue fails");
	TEST_ASSERT(ms < CANCEL_MAX_MS, "cancel wakes the blocked dequeue");

out:
	rx_close(&rx);
}

TEST_FUNCTION(cancel_wakes_dequeue)
{
	cancel_case(ip_uri);
	cancel_case(daqiri_uri);
}

TEST_FUNCTION(data_port_explicit)
{
	unsigned char *s = malloc(STREAM_LEN);
	char uri[512];
	long off;
	int ret;

	TEST_ASSERT_PTR_NOT_NULL(s, "capture buffer allocated");
	if (!s)
		return;

	memset(s, 0x5a, STREAM_LEN);
	snprintf(uri, sizeof(uri), "%s,data_port=%d", daqiri_uri, EMU_PORT);

	ret = capture(uri, s);
	TEST_ASSERT_EQ(ret, 0, "8 blocks streamed with data_port set");
	if (!ret) {
		off = find_ref(s, STREAM_LEN);
		TEST_ASSERT(off >= 0, "reference found in stream");
		if (off >= 0)
			TEST_ASSERT_EQ(ref_run(s + off, STREAM_LEN - (size_t)off),
					STREAM_LEN - (size_t)off,
					"stream is the reference repeated");
	}

	free(s);
}

TEST_FUNCTION(data_port_refused)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t addrlen = sizeof(addr);
	struct rx rx;
	char uri[512];
	int fd, ret;

	/* Bound but not listening: connecting to it is refused */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	ret = fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
	      getsockname(fd, (struct sockaddr *)&addr, &addrlen);
	TEST_ASSERT(!ret, "unused port reserved");
	if (ret)
		goto out;

	snprintf(uri, sizeof(uri), "%s,data_port=%u", daqiri_uri, ntohs(addr.sin_port));

	ret = rx_open(&rx, uri);
	TEST_ASSERT(rx.ctx != NULL, "context does not use the data port");
	TEST_ASSERT(ret < 0 && rx.stream == NULL, "buffer open fails on a refused data port");
	rx_close(&rx);

out:
	if (fd >= 0)
		close(fd);
}

TEST_FUNCTION(auto_without_yaml_is_tcp)
{
	const struct iio_attr *attr;
	char uri[512], *comma, val[16] = "";
	struct rx rx;
	int ret;

	/* Same host, the default data path (auto), no engine YAML: the buffer
	 * streams over TCP (ctest clears IIO_DAQIRI_YAML for this test) */
	snprintf(uri, sizeof(uri), "%s", daqiri_uri);
	comma = strchr(uri, ',');
	if (comma)
		*comma = '\0';

	ret = rx_open(&rx, uri);
	TEST_ASSERT_EQ(ret, 0, "data=auto buffer opens without a yaml");
	if (!ret) {
		attr = iio_buffer_find_attr(
				iio_device_get_buffer(iio_context_find_device(rx.ctx, DEV_NAME), 0),
				"daqiri_data_path");
		TEST_ASSERT(attr && iio_attr_read_raw(attr, val, sizeof(val)) > 0,
				"daqiri_data_path read");
		TEST_ASSERT_STR_EQ(val, "tcp", "data=auto without a yaml streams over TCP");
	}
	rx_close(&rx);
}

int main(void)
{
	int ret;

	daqiri_uri = uri_from_env("TESTS_DAQIRI_URI", DEFAULT_DAQIRI_URI);
	ip_uri = uri_from_env("TESTS_IP_URI", DEFAULT_IP_URI);

	ret = load_ref();
	if (ret) {
		printf("Unable to read %s: %d\n", TESTS_BUF0_BIN, ret);
		return 1;
	}

	RUN_TEST(xml_equivalence);
	RUN_TEST(stream_equivalence);
	RUN_TEST(cancel_wakes_dequeue);
	RUN_TEST(data_port_explicit);
	RUN_TEST(data_port_refused);
	RUN_TEST(auto_without_yaml_is_tcp);

	free(ref);

	printf("test_block_equivalence: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
