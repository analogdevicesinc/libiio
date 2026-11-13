/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * The buffer attributes of a daqiri: context against iiod-emu, next to the
 * ip: context of the same server. iiod-client sends a buffer attribute's
 * position in the buffer's list as its index, so the attributes the server
 * serves must keep the server's positions: they read and write the same
 * through daqiri: as through ip:. The backend's own daqiri_* attributes are
 * found by name and read locally, on every input buffer.
 */

#include <iio/iio.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

#define DAQIRI_URI "daqiri:127.0.0.1,data=tcp"
#define IP_URI "ip:127.0.0.1"

/* The attributes the backend serves itself, as doc/source/daqiri-backend.md
 * lists them */
static const char *const local_names[] = {
	"daqiri_data_path",
	"daqiri_rx_packets",
	"daqiri_rx_bytes",
	"daqiri_rx_lost_packets",
	"daqiri_rx_gap_events",
	"daqiri_rx_dropped_no_block",
	"daqiri_rx_overrun_events",
	"daqiri_rx_foreign_packets",
	"daqiri_rx_malformed",
	"daqiri_rx_late_packets",
	"daqiri_rx_duplicate_packets",
	"daqiri_rx_l2_rejected",
	"daqiri_rx_l2_last_reason",
	"daqiri_rx_vlan_packets",
	"daqiri_rx_hds_packets",
	"daqiri_rx_zero_filled_bytes",
	"daqiri_blocks_completed",
	"daqiri_blocks_failed",
	"daqiri_blocks_with_gaps",
	"daqiri_stream_id",
	"daqiri_last_error",
	"daqiri_lossy",
	"daqiri_mem_domain",
	"daqiri_engine_kind",
	"daqiri_rx_socket_buffer",
};

#define NB_LOCAL (sizeof(local_names) / sizeof(local_names[0]))

static struct iio_context *ctx_d;
static struct iio_context *ctx_i;

static bool is_local_name(const char *name)
{
	return !strncmp(name, "daqiri_", sizeof("daqiri_") - 1);
}

static bool is_documented(const char *name)
{
	unsigned int i;

	for (i = 0; i < NB_LOCAL; i++)
		if (!strcmp(name, local_names[i]))
			return true;

	return false;
}

static const char *dev_label(const struct iio_device *dev)
{
	const char *name = iio_device_get_name(dev);

	return name ? name : iio_device_get_id(dev);
}

/* The same buffer in the ip: context */
static const struct iio_buffer *ip_buffer(const struct iio_device *dev_d, unsigned int idx)
{
	const struct iio_device *dev_i = iio_context_find_device(ctx_i, iio_device_get_id(dev_d));

	return dev_i ? iio_device_get_buffer(dev_i, idx) : NULL;
}

/* The value in <dst>, or a description of the error; returns what the read
 * returned */
static ssize_t read_str(const struct iio_attr *attr, char *dst, size_t len)
{
	ssize_t ret = iio_attr_read_raw(attr, dst, len);

	if (ret < 0)
		snprintf(dst, len, "(error %d)", (int)ret);

	return ret;
}

static struct iio_context *open_ctx(const char *uri, unsigned int flags)
{
	struct iio_context_params params = { .flags = flags };
	struct iio_context *ctx = iio_create_context(&params, uri);

	if (iio_err(ctx)) {
		printf("  Unable to create context %s: %d\n", uri, iio_err(ctx));
		return NULL;
	}

	return ctx;
}

TEST_FUNCTION(contexts)
{
	/* iio_context_get_xml() reads every attribute then (indices_after_xml) */
	ctx_d = open_ctx(DAQIRI_URI, IIO_CTX_XML_INCLUDE_VALUES);
	ctx_i = open_ctx(IP_URI, 0);

	TEST_ASSERT_PTR_NOT_NULL(ctx_d, "daqiri context created");
	TEST_ASSERT_PTR_NOT_NULL(ctx_i, "ip context created");
	if (!ctx_d || !ctx_i)
		return;

	TEST_ASSERT_EQ(iio_context_get_devices_count(ctx_d), iio_context_get_devices_count(ctx_i),
			"same device count");
}

/*
 * The server's attributes keep the server's positions, which ip:'s are: the
 * first attributes of a daqiri: buffer are ip:'s, in ip:'s order, and the
 * daqiri_* ones follow. Sorted into the list, the daqiri_* names would come
 * before data_available and every other name. <when> goes in the messages.
 */
static void check_remote_indices(const char *when)
{
	unsigned int i, j, k, n_d, n_i, nb_bufs = 0;
	const struct iio_buffer *bd, *bi;
	const struct iio_device *dev;
	char msg[200];

	for (i = 0; i < iio_context_get_devices_count(ctx_d); i++) {
		dev = iio_context_get_device(ctx_d, i);

		for (j = 0; j < iio_device_get_buffers_count(dev); j++) {
			bd = iio_device_get_buffer(dev, j);
			bi = ip_buffer(dev, j);
			snprintf(msg, sizeof(msg), "%s: %s buffer%u in the ip context", when,
					dev_label(dev), j);
			TEST_ASSERT_PTR_NOT_NULL(bi, msg);
			if (!bi)
				continue;

			nb_bufs++;
			n_d = iio_buffer_get_attrs_count(bd);
			n_i = iio_buffer_get_attrs_count(bi);

			snprintf(msg, sizeof(msg),
					"%s: %s buffer%u (%s): the server's %u attributes%s", when,
					dev_label(dev), j,
					iio_buffer_is_output(bd) ? "output" : "input", n_i,
					iio_buffer_is_output(bd) ? "" : " and the daqiri_* ones");
			TEST_ASSERT_EQ(n_d, n_i + (iio_buffer_is_output(bd) ? 0 : NB_LOCAL), msg);

			for (k = 0; k < n_i && k < n_d; k++) {
				snprintf(msg, sizeof(msg),
						"%s: %s buffer%u attr %u as through ip:", when,
						dev_label(dev), j, k);
				TEST_ASSERT_STR_EQ(iio_attr_get_name(iio_buffer_get_attr(bd, k)),
						iio_attr_get_name(iio_buffer_get_attr(bi, k)), msg);
			}

			for (k = n_i; k < n_d; k++) {
				snprintf(msg, sizeof(msg),
						"%s: %s buffer%u attr %u is a daqiri_* one", when,
						dev_label(dev), j, k);
				TEST_ASSERT(is_local_name(iio_attr_get_name(
							    iio_buffer_get_attr(bd, k))),
						msg);
			}
		}
	}

	snprintf(msg, sizeof(msg), "%s: buffers compared", when);
	TEST_ASSERT(nb_bufs > 0, msg);
}

TEST_FUNCTION(remote_indices)
{
	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	check_remote_indices("at creation");
}

/* Every attribute that the server serves reads the same through both */
TEST_FUNCTION(remote_reads)
{
	unsigned int i, j, k, nb_read = 0;
	const struct iio_attr *attr_d, *attr_i;
	const struct iio_buffer *bd, *bi;
	const struct iio_device *dev;
	char val_d[256], val_i[256];
	const char *name;
	char msg[160];

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	for (i = 0; i < iio_context_get_devices_count(ctx_d); i++) {
		dev = iio_context_get_device(ctx_d, i);

		for (j = 0; j < iio_device_get_buffers_count(dev); j++) {
			bd = iio_device_get_buffer(dev, j);
			bi = ip_buffer(dev, j);
			if (!bi)
				continue; /* remote_indices says so */

			for (k = 0; k < iio_buffer_get_attrs_count(bd); k++) {
				attr_d = iio_buffer_get_attr(bd, k);
				name = iio_attr_get_name(attr_d);
				if (is_local_name(name))
					continue;

				attr_i = iio_buffer_find_attr(bi, name);
				snprintf(msg, sizeof(msg), "%s buffer%u %s on the server (ip:)",
						dev_label(dev), j, name);
				TEST_ASSERT_PTR_NOT_NULL(attr_i, msg);
				if (!attr_i)
					continue;

				nb_read++;
				snprintf(msg, sizeof(msg), "%s buffer%u %s readable through ip:",
						dev_label(dev), j, name);
				TEST_ASSERT(read_str(attr_i, val_i, sizeof(val_i)) > 0, msg);

				read_str(attr_d, val_d, sizeof(val_d));
				snprintf(msg, sizeof(msg),
						"%s buffer%u %s through daqiri: as through ip:",
						dev_label(dev), j, name);
				TEST_ASSERT_STR_EQ(val_d, val_i, msg);
			}
		}
	}

	TEST_ASSERT(nb_read > 0, "server buffer attributes read");
}

/* The daqiri_* attributes: on every input buffer, found by name and read
 * locally; the server has none of them */
TEST_FUNCTION(local_attrs)
{
	unsigned int i, j, k, nb_inputs = 0;
	const struct iio_buffer *bd, *bi;
	const struct iio_attr *attr;
	const struct iio_device *dev;
	char val[256], msg[160];
	const char *name;

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	for (i = 0; i < iio_context_get_devices_count(ctx_d); i++) {
		dev = iio_context_get_device(ctx_d, i);

		for (j = 0; j < iio_device_get_buffers_count(dev); j++) {
			bd = iio_device_get_buffer(dev, j);
			bi = ip_buffer(dev, j);
			if (iio_buffer_is_output(bd))
				continue;

			nb_inputs++;

			for (k = 0; k < NB_LOCAL; k++) {
				attr = iio_buffer_find_attr(bd, local_names[k]);
				snprintf(msg, sizeof(msg), "%s buffer%u %s found by name",
						dev_label(dev), j, local_names[k]);
				TEST_ASSERT_PTR_NOT_NULL(attr, msg);
				if (!attr)
					continue;

				snprintf(msg, sizeof(msg), "%s buffer%u %s readable",
						dev_label(dev), j, local_names[k]);
				TEST_ASSERT(iio_attr_read_raw(attr, val, sizeof(val)) > 0, msg);

				if (bi) {
					snprintf(msg, sizeof(msg),
							"%s buffer%u %s not on the server",
							dev_label(dev), j, local_names[k]);
					TEST_ASSERT_PTR_NULL(
							iio_buffer_find_attr(bi, local_names[k]),
							msg);
				}
			}

			/* None but the documented ones */
			for (k = 0; k < iio_buffer_get_attrs_count(bd); k++) {
				name = iio_attr_get_name(iio_buffer_get_attr(bd, k));
				if (!is_local_name(name))
					continue;

				snprintf(msg, sizeof(msg), "%s buffer%u %s is documented",
						dev_label(dev), j, name);
				TEST_ASSERT(is_documented(name), msg);
			}

			/* The name finds that very attribute */
			attr = iio_buffer_find_attr(bd, "daqiri_data_path");
			if (attr) {
				read_str(attr, val, sizeof(val));
				snprintf(msg, sizeof(msg), "%s buffer%u daqiri_data_path",
						dev_label(dev), j);
				TEST_ASSERT_STR_EQ(val, "tcp", msg);
			}
		}
	}

	TEST_ASSERT(nb_inputs > 0, "input buffers checked");
}

/* Nothing sorts the attributes again later: not a buffer opened (checked while
 * it is open) and closed */
TEST_FUNCTION(indices_after_buffer)
{
	static const char *const chans[] = { "voltage0", "voltage1" };
	struct iio_buffer_stream *stream;
	struct iio_channels_mask *mask;
	const struct iio_device *dev;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	unsigned int i;
	int ret;

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	dev = iio_context_find_device(ctx_d, "cf-ad9361-lpc");
	buf = dev ? iio_device_get_buffer(dev, 0) : NULL;
	TEST_ASSERT_PTR_NOT_NULL(buf, "cf-ad9361-lpc buffer0 via daqiri:");
	if (!buf)
		return;

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	TEST_ASSERT_PTR_NOT_NULL(mask, "channel mask");
	if (!mask)
		return;

	for (i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
		chn = iio_device_find_channel(dev, chans[i], false);
		TEST_ASSERT_PTR_NOT_NULL(chn, chans[i]);
		if (chn)
			iio_channel_enable(chn, mask);
	}

	stream = iio_buffer_open(buf, mask);
	ret = iio_err(stream);
	TEST_ASSERT_EQ(ret, 0, "cf-ad9361-lpc buffer0 opened through daqiri:");
	if (!ret) {
		check_remote_indices("with a buffer open");
		iio_buffer_close(stream);
		check_remote_indices("after the buffer closed");
	}

	iio_channels_mask_destroy(mask);
}

/* Nor iio_context_get_xml(), which with IIO_CTX_XML_INCLUDE_VALUES reads every
 * attribute into a table as long as the list, and frees it again */
TEST_FUNCTION(indices_after_xml)
{
	static const char value[] = "<attribute name=\"daqiri_data_path\" value=\"tcp\"";
	char *xml;
	int ret;

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	xml = iio_context_get_xml(ctx_d);
	ret = iio_err(xml);
	TEST_ASSERT_EQ(ret, 0, "iio_context_get_xml() of the daqiri context");
	if (!ret) {
		TEST_ASSERT(strstr(xml, value) != NULL, "the XML has the values");
		free(xml);
	}

	check_remote_indices("after iio_context_get_xml()");
}

/*
 * A write through daqiri: reaches the server's attribute. The emulator takes
 * a write to any attribute but those whose names end in _available (it
 * answers -EACCES), so of fmcomms2.xml's buffer attributes, data_available
 * and length_align_bytes, only length_align_bytes: written, seen through
 * ip:, then put back.
 */
TEST_FUNCTION(remote_write)
{
	const struct iio_device *dev_d, *dev_i;
	const struct iio_buffer *buf_d, *buf_i;
	const struct iio_attr *attr_d, *attr_i;
	char val[256], orig[256];
	ssize_t ret;

	if (!ctx_d || !ctx_i) {
		TEST_ASSERT(false, "both contexts available");
		return;
	}

	dev_d = iio_context_find_device(ctx_d, "cf-ad9361-lpc");
	dev_i = iio_context_find_device(ctx_i, "cf-ad9361-lpc");
	buf_d = dev_d ? iio_device_get_buffer(dev_d, 0) : NULL;
	buf_i = dev_i ? iio_device_get_buffer(dev_i, 0) : NULL;
	attr_d = buf_d ? iio_buffer_find_attr(buf_d, "length_align_bytes") : NULL;
	attr_i = buf_i ? iio_buffer_find_attr(buf_i, "length_align_bytes") : NULL;
	TEST_ASSERT_PTR_NOT_NULL(attr_d, "cf-ad9361-lpc buffer0 length_align_bytes via daqiri:");
	TEST_ASSERT_PTR_NOT_NULL(attr_i, "cf-ad9361-lpc buffer0 length_align_bytes via ip:");
	if (!attr_d || !attr_i)
		return;

	ret = iio_attr_read_raw(attr_i, orig, sizeof(orig));
	TEST_ASSERT(ret > 0, "length_align_bytes readable through ip:");
	if (ret <= 0)
		return;
	TEST_ASSERT(strcmp(orig, "16") != 0, "length_align_bytes is not 16 yet");

	/* Sent with an index that the server does not have, a write is
	 * answered -EINVAL before the server reads the value, and the
	 * connection is out of step afterwards: stop there */
	ret = iio_attr_write_string(attr_d, "16");
	TEST_ASSERT_EQ(ret, (ssize_t)sizeof("16"), "length_align_bytes written through daqiri:");
	if (ret < 0)
		return;

	read_str(attr_i, val, sizeof(val));
	TEST_ASSERT_STR_EQ(val, "16", "seen through ip:");
	read_str(attr_d, val, sizeof(val));
	TEST_ASSERT_STR_EQ(val, "16", "read back through daqiri:");

	ret = iio_attr_write_string(attr_d, orig);
	TEST_ASSERT_EQ(ret, (ssize_t)strlen(orig) + 1, "length_align_bytes put back");
	read_str(attr_i, val, sizeof(val));
	TEST_ASSERT_STR_EQ(val, orig, "put back, through ip:");
}

int main(void)
{
	RUN_TEST(contexts);
	RUN_TEST(remote_indices);
	RUN_TEST(remote_reads);
	RUN_TEST(local_attrs);
	RUN_TEST(indices_after_buffer);
	RUN_TEST(indices_after_xml);
	RUN_TEST(remote_write);

	if (ctx_d)
		iio_context_destroy(ctx_d);
	if (ctx_i)
		iio_context_destroy(ctx_i);

	printf("test_daqiri_buffer_attrs: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
