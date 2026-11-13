/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Management-plane test of the daqiri: backend against iiod-emu: the context
 * it builds must match the one ip: builds from the same server.
 */

#include <errno.h>
#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_framework.h"

#define DEFAULT_DAQIRI_URI "daqiri:127.0.0.1,data=tcp"
#define DEFAULT_IP_URI "ip:127.0.0.1"

static const char *daqiri_uri;
static const char *ip_uri;
static struct iio_context *ctx_d;
static struct iio_context *ctx_i;

static const char *uri_from_env(const char *name, const char *def)
{
	const char *val = getenv(name);

	return val && val[0] ? val : def;
}

static struct iio_context *open_ctx(const char *uri)
{
	struct iio_context *ctx = iio_create_context(NULL, uri);

	if (iio_err(ctx)) {
		printf("  Unable to create context %s: %d\n", uri, iio_err(ctx));
		return NULL;
	}

	return ctx;
}

static const char *ctx_attr(const struct iio_context *ctx, const char *name)
{
	const struct iio_attr *attr = iio_context_find_attr(ctx, name);
	const char *val = attr ? iio_attr_get_static_value(attr) : NULL;

	return val ? val : "(missing)";
}

/* For URIs that are expected to fail: keep the expected error off stderr. */
static int create_err(const char *uri)
{
	struct iio_context_params params = { .log_level = LEVEL_NOLOG };
	struct iio_context *ctx = iio_create_context(&params, uri);
	int err = iio_err(ctx);

	if (!err) {
		iio_context_destroy(ctx);
		return 0;
	}

	return err;
}

TEST_FUNCTION(has_backend)
{
	TEST_ASSERT(iio_has_backend(NULL, "daqiri"), "module found");
}

TEST_FUNCTION(bad_key)
{
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,bogus=1"), -EINVAL, "unknown key rejected");
}

TEST_FUNCTION(bad_data)
{
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=xyz"), -EINVAL, "bad data path rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,udp_dst=10.0.0"), -EINVAL,
			"udp_dst not a dotted quad rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,udp_dst=0.0.0.0"), -EINVAL,
			"udp_dst 0.0.0.0 rejected");
}

/* rcvbuf=: a byte count from 0 to INT_MAX, what setsockopt() takes */
TEST_FUNCTION(rcvbuf_key)
{
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=-1"), -EINVAL,
			"rcvbuf=-1 rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=4M"), -EINVAL,
			"rcvbuf=4M rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf="), -EINVAL,
			"empty rcvbuf rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=2147483648"), -EINVAL,
			"rcvbuf above INT_MAX rejected");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=0"), 0, "rcvbuf=0 accepted");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=2147483647"), 0,
			"rcvbuf=INT_MAX accepted");
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,rcvbuf=0x800000"), 0,
			"rcvbuf in hex accepted");
}

/* A key the backend does not know, or a value it cannot read */
TEST_FUNCTION(unknown_keys)
{
	static const char *const unknown[] = {
		"daqiri:127.0.0.1,nosuchkey=1",
		"daqiri:127.0.0.1,data=tcp,nosuchkey=1",
		"daqiri:127.0.0.1,data=nosuchpath",
	};
	unsigned int i;

	for (i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++)
		TEST_ASSERT_EQ(create_err(unknown[i]), -EINVAL, unknown[i]);
}

TEST_FUNCTION(bad_yaml)
{
	TEST_ASSERT_EQ(create_err("daqiri:127.0.0.1,data=tcp,yaml=/nonexistent/daqiri.yaml"),
			-EINVAL, "unreadable yaml rejected");
}

TEST_FUNCTION(topology)
{
	unsigned int i, nb;

	ctx_d = open_ctx(daqiri_uri);
	ctx_i = open_ctx(ip_uri);
	TEST_ASSERT_PTR_NOT_NULL(ctx_d, "daqiri context created");
	TEST_ASSERT_PTR_NOT_NULL(ctx_i, "ip context created");
	if (!ctx_d || !ctx_i)
		return;

	TEST_ASSERT_STR_EQ(iio_context_get_name(ctx_d), "daqiri", "context name");

	nb = iio_context_get_devices_count(ctx_d);
	TEST_ASSERT(nb > 0, "daqiri context has devices");
	TEST_ASSERT_EQ(nb, iio_context_get_devices_count(ctx_i), "same device count");
	if (nb != iio_context_get_devices_count(ctx_i))
		return;

	for (i = 0; i < nb; i++) {
		const struct iio_device *dd = iio_context_get_device(ctx_d, i);
		const struct iio_device *di = iio_context_get_device(ctx_i, i);

		TEST_ASSERT_STR_EQ(iio_device_get_id(dd), iio_device_get_id(di), "same device id");
		TEST_ASSERT_EQ(iio_device_get_channels_count(dd), iio_device_get_channels_count(di),
				"same channel count");
		TEST_ASSERT_EQ(iio_device_get_attrs_count(dd), iio_device_get_attrs_count(di),
				"same device attr count");
	}
}

TEST_FUNCTION(ctx_attrs)
{
	if (!ctx_d) {
		TEST_ASSERT(false, "daqiri context available");
		return;
	}

	TEST_ASSERT_STR_EQ(ctx_attr(ctx_d, "daqiri,data"), "tcp", "data attr");
	TEST_ASSERT_STR_EQ(ctx_attr(ctx_d, "daqiri,mem"), "host", "mem attr");
	TEST_ASSERT_STR_EQ(ctx_attr(ctx_d, "uri"), daqiri_uri, "uri attr");
	TEST_ASSERT_STR_EQ(ctx_attr(ctx_d, "ip,ip-addr"), "127.0.0.1", "ip-addr attr");
	TEST_ASSERT(strcmp(ctx_attr(ctx_d, "daqiri,version"), "(missing)") != 0,
			"version attr present");
	TEST_ASSERT(ctx_attr(ctx_d, "daqiri,version")[0] != '\0', "version attr non-empty");
}

static ssize_t read_chn_attr(struct iio_context *ctx, const char *dev_name, const char *chn_name,
		const char *attr_name, char *dst, size_t len)
{
	const struct iio_device *dev = iio_context_find_device(ctx, dev_name);
	const struct iio_channel *chn = dev ? iio_device_find_channel(dev, chn_name, false) : NULL;
	const struct iio_attr *attr = chn ? iio_channel_find_attr(chn, attr_name) : NULL;

	if (!attr)
		return -ENOENT;

	return iio_attr_read_raw(attr, dst, len);
}

TEST_FUNCTION(attr_roundtrip)
{
	char via_d[256], via_i[256];
	ssize_t ret_d, ret_i;

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	ret_d = read_chn_attr(ctx_d, "ad9361-phy", "voltage0", "sampling_frequency", via_d,
			sizeof(via_d));
	ret_i = read_chn_attr(ctx_i, "ad9361-phy", "voltage0", "sampling_frequency", via_i,
			sizeof(via_i));
	TEST_ASSERT(ret_d > 0, "read via daqiri");
	TEST_ASSERT(ret_i > 0, "read via ip");
	if (ret_d <= 0 || ret_i <= 0)
		return;

	TEST_ASSERT_STR_EQ(via_d, via_i, "same value via both");
	TEST_ASSERT_STR_EQ(via_d, "30720000", "value from fmcomms2.xml");
}

TEST_FUNCTION(ping_timeout)
{
	if (!ctx_d) {
		TEST_ASSERT(false, "daqiri context available");
		return;
	}

	TEST_ASSERT_EQ(iio_context_set_timeout(ctx_d, 1000), 0, "set_timeout");
	TEST_ASSERT_EQ(iio_context_ping(ctx_d), 0, "ping");
}

/* Every input buffer says which data path it takes; with data=tcp, TCP */
TEST_FUNCTION(data_path_attr)
{
	const struct iio_device *dev = ctx_d ? iio_context_find_device(ctx_d, "cf-ad9361-lpc")
					     : NULL;
	const struct iio_buffer *buf = dev ? iio_device_get_buffer(dev, 0) : NULL;
	const struct iio_attr *attr = buf ? iio_buffer_find_attr(buf, "daqiri_data_path") : NULL;
	char val[16] = "";
	ssize_t ret;

	TEST_ASSERT_PTR_NOT_NULL(attr, "daqiri_data_path on cf-ad9361-lpc buffer 0");
	if (!attr)
		return;

	ret = iio_attr_read_raw(attr, val, sizeof(val));
	TEST_ASSERT(ret > 0, "daqiri_data_path readable");
	TEST_ASSERT_STR_EQ(val, "tcp", "daqiri_data_path is tcp");
	TEST_ASSERT_PTR_NULL(iio_buffer_find_attr(buf, "daqiri_rx_context_packets"),
			"no daqiri_rx_context_packets");

	/* No socket over TCP */
	attr = iio_buffer_find_attr(buf, "daqiri_rx_socket_buffer");
	TEST_ASSERT_PTR_NOT_NULL(attr, "daqiri_rx_socket_buffer on cf-ad9361-lpc buffer 0");
	if (!attr)
		return;
	ret = iio_attr_read_raw(attr, val, sizeof(val));
	TEST_ASSERT(ret > 0, "daqiri_rx_socket_buffer readable");
	TEST_ASSERT_STR_EQ(val, "0", "daqiri_rx_socket_buffer is 0 over TCP");
}

/* Same schema as CI/daqiri/probe-socket.cpp; parsed only, never initialised. */
static const char socket_yaml[] = "daqiri:\n"
				  "  cfg:\n"
				  "    version: 1\n"
				  "    stream_type: socket\n"
				  "    master_core: 0\n"
				  "    memory_regions:\n"
				  "    - name: HOST_MR\n"
				  "      kind: host\n"
				  "      affinity: 0\n"
				  "      num_bufs: 64\n"
				  "      buf_size: 9216\n"
				  "    interfaces:\n"
				  "    - name: data\n"
				  "      address: 127.0.0.1\n"
				  "      socket_config:\n"
				  "        mode: server\n"
				  "        local_addr: 'udp://127.0.0.1:25101'\n"
				  "        remote_addr: 'udp://127.0.0.1:25102'\n"
				  "        max_payload_size: 9216\n"
				  "      rx:\n"
				  "        queues:\n"
				  "        - name: rx0\n"
				  "          id: 0\n"
				  "          cpu_core: 0\n"
				  "          batch_size: 1\n"
				  "          memory_regions:\n"
				  "          - HOST_MR\n";

TEST_FUNCTION(yaml_engine)
{
	char path[] = "/tmp/test_daqiri_context_XXXXXX";
	struct iio_context *ctx;
	char uri[256];
	FILE *f;
	int fd;

	fd = mkstemp(path);
	TEST_ASSERT(fd >= 0, "temp yaml created");
	if (fd < 0)
		return;

	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(path);
		TEST_ASSERT(false, "temp yaml opened");
		return;
	}
	fputs(socket_yaml, f);
	fclose(f);

	snprintf(uri, sizeof(uri), "daqiri:127.0.0.1,data=tcp,yaml=%s", path);
	ctx = open_ctx(uri);
	TEST_ASSERT_PTR_NOT_NULL(ctx, "context with valid yaml");
	if (ctx) {
		TEST_ASSERT_STR_EQ(ctx_attr(ctx, "daqiri,engine"), "socket", "engine attr");
		TEST_ASSERT_STR_EQ(ctx_attr(ctx, "daqiri,yaml"), path, "yaml attr");
		iio_context_destroy(ctx);
	}

	unlink(path);
}

int main(void)
{
	daqiri_uri = uri_from_env("TESTS_DAQIRI_URI", DEFAULT_DAQIRI_URI);
	ip_uri = uri_from_env("TESTS_IP_URI", DEFAULT_IP_URI);

	RUN_TEST(has_backend);
	RUN_TEST(bad_key);
	RUN_TEST(bad_data);
	RUN_TEST(rcvbuf_key);
	RUN_TEST(unknown_keys);
	RUN_TEST(bad_yaml);
	RUN_TEST(topology);
	RUN_TEST(ctx_attrs);
	RUN_TEST(attr_roundtrip);
	RUN_TEST(ping_timeout);
	RUN_TEST(data_path_attr);
	RUN_TEST(yaml_engine);

	if (ctx_d)
		iio_context_destroy(ctx_d);
	if (ctx_i)
		iio_context_destroy(ctx_i);

	printf("test_daqiri_context: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
