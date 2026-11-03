/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <iio/iio.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"
#include "test_helpers.h"

#ifdef TESTS_XMLS_DIR
#define FWU_FIXTURE TESTS_XMLS_DIR "/fwu.xml"

static const struct iio_device *fixture_device(const struct iio_context *ctx)
{
	return ctx ? iio_context_find_device(ctx, "iio:device0") : NULL;
}

static struct iio_context *open_fixture(const char *uri)
{
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);

	if (err)
		DEBUG_PRINT("  INFO: iio_create_context(\"%s\") returned %d\n", uri, err);

	TEST_ASSERT(!err, "FWU fixture can be opened");

	return err ? NULL : ctx;
}

static void check_fwu_ids(const struct iio_device *dev)
{
	static const char *const ids[] = { "ad9081-fw", "busy-fw", "dev-busy-fw", "dev-timeout-fw",
		"failed-fw", "fpga:bitstream" };
	unsigned int i, nb = iio_device_get_fwu_count(dev);

	TEST_ASSERT_EQ(nb, 6, "Device has 6 firmware upload instances");

	for (i = 0; i < nb && i < 6; i++)
		TEST_ASSERT_STR_EQ(iio_device_get_fwu_id(dev, i), ids[i],
				"Firmware upload IDs are sorted and kept verbatim");

	TEST_ASSERT_PTR_NULL(iio_device_get_fwu_id(dev, nb), "Out of range index returns NULL");
}
#endif /* TESTS_XMLS_DIR */

TEST_FUNCTION(fwu_xml_roundtrip)
{
#ifdef TESTS_XMLS_DIR
	struct iio_context *ctx = open_fixture("xml:" FWU_FIXTURE);
	struct iio_context *ctx2;
	const struct iio_device *dev = fixture_device(ctx);
	char *xml, *uri;

	if (!dev)
		goto out;

	check_fwu_ids(dev);

	xml = iio_context_get_xml(ctx);
	TEST_ASSERT(!iio_err(xml), "Context XML can be generated");
	if (iio_err(xml))
		goto out;

	TEST_ASSERT(strstr(xml, "<firmware-upload id=\"fpga:bitstream\" />") != NULL,
			"Generated XML lists firmware upload instances");

	uri = malloc(strlen(xml) + sizeof("xml:"));
	if (uri) {
		strcpy(uri, "xml:");
		strcat(uri, xml);
	}
	free(xml);

	TEST_ASSERT_PTR_NOT_NULL(uri, "URI can be allocated");
	if (!uri)
		goto out;

	ctx2 = iio_create_context(NULL, uri);
	free(uri);

	TEST_ASSERT(!iio_err(ctx2), "Generated XML can be parsed back");
	if (!iio_err(ctx2)) {
		check_fwu_ids(fixture_device(ctx2));
		iio_context_destroy(ctx2);
	}
out:
	if (ctx)
		iio_context_destroy(ctx);
#else
	DEBUG_PRINT("  SKIP: TESTS_XMLS_DIR not defined\n");
	TEST_ASSERT(true, "FWU XML test skipped");
#endif
}

TEST_FUNCTION(fwu_operations)
{
#if defined(TESTS_XMLS_DIR) && defined(TESTS_WITH_EMU)
	static const char data[] = "firmware image";
	struct iio_context *ctx = open_fixture("emu:" FWU_FIXTURE);
	const struct iio_device *dev = fixture_device(ctx);
	enum iio_fwu_status status;
	uint64_t remaining = 1;
	int ret;

	if (!dev)
		goto out;

	/* Invalid arguments */
	ret = iio_device_fwu_write(dev, "no-such-fw", data, sizeof(data));
	TEST_ASSERT_EQ(ret, -ENOENT, "Unknown ID is rejected");
	ret = iio_device_fwu_write(dev, "ad9081-fw", NULL, 0);
	TEST_ASSERT_EQ(ret, -EINVAL, "Empty image is rejected");
	ret = iio_device_fwu_wait(dev, "no-such-fw", IIO_TIMEOUT_NONBLOCK);
	TEST_ASSERT_EQ(ret, -ENOENT, "Waiting on an unknown ID is rejected");
	ret = iio_device_fwu_wait(dev, "ad9081-fw", -2);
	TEST_ASSERT_EQ(ret, -EINVAL, "Invalid timeout is rejected");

	/* Idle instance: upload succeeds */
	ret = iio_device_fwu_get_status(dev, "ad9081-fw", &status, &remaining);
	TEST_ASSERT_EQ(ret, 0, "Status can be read");
	TEST_ASSERT_EQ(status, IIO_FWU_IDLE, "Instance is idle");
	TEST_ASSERT_EQ(remaining, 0, "Nothing remains to be processed");
	ret = iio_device_fwu_write(dev, "ad9081-fw", data, sizeof(data));
	TEST_ASSERT_EQ(ret, 0, "Image is uploaded");
	ret = iio_device_fwu_wait(dev, "ad9081-fw", IIO_TIMEOUT_NONBLOCK);
	TEST_ASSERT_EQ(ret, 0, "Non-blocking wait reports completion");
	ret = iio_device_fwu_wait(dev, "ad9081-fw", IIO_TIMEOUT_BACKEND);
	TEST_ASSERT_EQ(ret, 0, "Blocking wait reports completion");
	ret = iio_device_fwu_cancel(dev, "ad9081-fw");
	TEST_ASSERT_EQ(ret, 0, "Cancel is accepted");
	ret = iio_device_fwu_write(dev, "fpga:bitstream", data, sizeof(data));
	TEST_ASSERT_EQ(ret, 0, "ID containing a colon is usable");

	/* Busy instance */
	ret = iio_device_fwu_get_status(dev, "busy-fw", &status, &remaining);
	TEST_ASSERT_EQ(ret, 0, "Status of busy instance can be read");
	TEST_ASSERT_EQ(status, IIO_FWU_PROGRAMMING, "Instance is programming");
	TEST_ASSERT_EQ(remaining, 4096, "Remaining size is reported");
	ret = iio_device_fwu_write(dev, "busy-fw", data, sizeof(data));
	TEST_ASSERT_EQ(ret, -EBUSY, "Upload is refused while busy");
	ret = iio_device_fwu_wait(dev, "busy-fw", IIO_TIMEOUT_NONBLOCK);
	TEST_ASSERT_EQ(ret, -EAGAIN, "Non-blocking wait reports the upload in progress");
	ret = iio_device_fwu_wait(dev, "busy-fw", 250);
	TEST_ASSERT_EQ(ret, -EAGAIN, "Wait times out while the upload is in progress");

	/* Failed instance: error is mapped to an errno */
	ret = iio_device_fwu_wait(dev, "failed-fw", IIO_TIMEOUT_INFINITE);
	TEST_ASSERT_EQ(ret, -EIO, "Device error is reported");
	ret = iio_device_fwu_get_status(dev, "failed-fw", &status, NULL);
	TEST_ASSERT_EQ(ret, 0, "Status can be read without remaining size");
	TEST_ASSERT_EQ(status, IIO_FWU_IDLE, "Failed instance is idle");
	ret = iio_device_fwu_wait(dev, "dev-busy-fw", IIO_TIMEOUT_NONBLOCK);
	TEST_ASSERT_EQ(ret, -EBUSY, "Device busy error is distinct from an upload in progress");
	ret = iio_device_fwu_wait(dev, "dev-timeout-fw", 250);
	TEST_ASSERT_EQ(ret, -ETIMEDOUT, "Device timeout error is distinct from an expired wait");
out:
	if (ctx)
		iio_context_destroy(ctx);
#else
	DEBUG_PRINT("  SKIP: EMU backend or TESTS_XMLS_DIR not available\n");
	TEST_ASSERT(true, "FWU operations test skipped");
#endif
}

int main(void)
{
	DEBUG_PRINT("=== libiio Firmware Upload Tests ===\n\n");

	RUN_TEST(fwu_xml_roundtrip);
	RUN_TEST(fwu_operations);

	TEST_SUMMARY();
	return 0;
}
