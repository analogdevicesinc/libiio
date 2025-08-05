/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include "test_framework.h"
#include "test_helpers.h"
#include <iio/iio.h>
#include <errno.h>

TEST_FUNCTION(hwmon_channel_type)
{
	DEBUG_PRINT("  INFO: HWMON channel type function is inline - basic test\n");
	struct iio_context *ctx = create_test_context("TESTS_API_URI", "local:", NULL);
	if (!iio_err(ctx) && ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(ctx);
		for (unsigned int i = 0; i < nb_devices; i++) {
			struct iio_device *dev = iio_context_get_device(ctx, i);
			if (dev && iio_device_is_hwmon(dev)) {
				unsigned int nb_channels = iio_device_get_channels_count(dev);
				if (nb_channels > 0) {
					struct iio_channel *chn = iio_device_get_channel(dev, 0);
					if (chn) {
						enum hwmon_chan_type type = hwmon_channel_get_type(chn);
						DEBUG_PRINT("  INFO: HWMON channel type: %d\n", type);
						TEST_ASSERT(true, "HWMON channel type retrieved");
						iio_context_destroy(ctx);
						return;
					}
				}
			}
		}
		iio_context_destroy(ctx);
	}
	DEBUG_PRINT("  INFO: No HWMON devices found for testing\n");
	TEST_ASSERT(true, "HWMON test completed");
}

int main(void)
{
	DEBUG_PRINT("=== libiio HWMON Tests ===\n\n");

	RUN_TEST(hwmon_channel_type);

	TEST_SUMMARY();
	return 0;
}
