/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/ztest.h>
#include <iio/iio.h>

#include <iio_device_fake.h>

DEFINE_FFF_GLOBALS;

#define FAKE0 DEVICE_DT_GET(DT_NODELABEL(fake0))

static struct iio_context *ctx;

static const struct iio_device *find_device(const char *name)
{
	const struct iio_device *dev = iio_context_find_device(ctx, name);

	zassert_not_null(dev, "device %s not found", name);
	return dev;
}

static const struct iio_attr *fake0_raw(void)
{
	const struct iio_channel *chn;

	chn = iio_device_find_channel(find_device("fake0"), "voltage0", false);
	zassert_not_null(chn);
	return iio_channel_find_attr(chn, "raw");
}

static void backend_before(void *fixture)
{
	ARG_UNUSED(fixture);

	ctx = iio_create_context(NULL, "zephyr:");
	zassert_false(iio_err(ctx), "iio_create_context: %d", iio_err(ctx));
}

static void backend_after(void *fixture)
{
	ARG_UNUSED(fixture);

	iio_context_destroy(ctx);
	ctx = NULL;
}

ZTEST(libiio_backend, test_context_describes_the_kernel)
{
	const char *desc = iio_context_get_description(ctx);

	zassert_equal(strncmp(desc, "Zephyr ", 7), 0, "description: %s", desc);
	zassert_equal(iio_context_get_version_major(ctx), KERNEL_VERSION_MAJOR);
	zassert_equal(iio_context_get_version_minor(ctx), KERNEL_VERSION_MINOR);
	zassert_equal(iio_context_get_version_patch(ctx), KERNEL_PATCHLEVEL);
	zassert_not_null(strstr(desc, iio_context_get_version_tag(ctx)));
}

ZTEST(libiio_backend, test_only_ready_devices_are_listed)
{
	zassert_equal(iio_context_get_devices_count(ctx), 2);
	zassert_is_null(iio_context_find_device(ctx, "broken"));
}

ZTEST(libiio_backend, test_device_ids_are_sequential)
{
	zassert_not_null(iio_context_find_device(ctx, "iio:device0"));
	zassert_not_null(iio_context_find_device(ctx, "iio:device1"));
}

ZTEST(libiio_backend, test_name_falls_back_to_the_node_name)
{
	find_device("fake0");
	find_device("fake-iio-device-1");
}

ZTEST(libiio_backend, test_channels_are_added_once_per_ready_device)
{
	zassert_equal(iio_device_fake_add_channels_fake.call_count, 2);
	zassert_not_null(iio_device_find_channel(find_device("fake0"), "voltage0", false));
	zassert_not_null(iio_device_find_attr(find_device("fake0"), "sampling_frequency"));
}

static int read_42(const struct device *dev, const struct iio_device *iio_device,
		   const struct iio_attr *attr, char *dst, size_t len)
{
	return snprintf(dst, len, "42") + 1;
}

ZTEST(libiio_backend, test_read_reaches_the_owning_device)
{
	const struct iio_attr *attr = fake0_raw();
	char buf[16];

	iio_device_fake_read_attr_fake.custom_fake = read_42;

	zassert_equal(iio_attr_read_raw(attr, buf, sizeof(buf)), 3);
	zassert_str_equal(buf, "42");
	zassert_equal(iio_device_fake_read_attr_fake.call_count, 1);
	zassert_equal_ptr(iio_device_fake_read_attr_fake.arg0_val, FAKE0);
	zassert_equal_ptr(iio_device_fake_read_attr_fake.arg2_val, attr);
	zassert_equal(iio_device_fake_read_attr_fake.arg4_val, sizeof(buf));
}

ZTEST(libiio_backend, test_read_error_is_returned)
{
	char buf[16];

	iio_device_fake_read_attr_fake.return_val = -EIO;

	zassert_equal(iio_attr_read_raw(fake0_raw(), buf, sizeof(buf)), -EIO);
}

static char written[16];

static int capture_write(const struct device *dev, const struct iio_device *iio_device,
			 const struct iio_attr *attr, const char *src, size_t len)
{
	memcpy(written, src, MIN(len, sizeof(written) - 1));
	return len;
}

ZTEST(libiio_backend, test_write_reaches_the_owning_device)
{
	const struct iio_attr *attr = fake0_raw();

	memset(written, 0, sizeof(written));
	iio_device_fake_write_attr_fake.custom_fake = capture_write;

	zassert_equal(iio_attr_write_raw(attr, "7", 2), 2);
	zassert_str_equal(written, "7");
	zassert_equal(iio_device_fake_write_attr_fake.call_count, 1);
	zassert_equal_ptr(iio_device_fake_write_attr_fake.arg0_val, FAKE0);
	zassert_equal_ptr(iio_device_fake_write_attr_fake.arg2_val, attr);
}

ZTEST(libiio_backend, test_write_error_is_returned)
{
	iio_device_fake_write_attr_fake.return_val = -EINVAL;

	zassert_equal(iio_attr_write_raw(fake0_raw(), "7", 2), -EINVAL);
}

ZTEST(libiio_backend, test_devices_have_no_trigger)
{
	zassert_is_null(iio_device_get_trigger(find_device("fake0")));
}

ZTEST_SUITE(libiio_backend, NULL, NULL, backend_before, backend_after, NULL);
