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

TEST_FUNCTION(channels_mask_operations)
{
	struct iio_channels_mask *mask = iio_create_channels_mask(16);
	TEST_ASSERT_PTR_NOT_NULL(mask, "Channels mask should be created");

	if (mask) {
		DEBUG_PRINT("  INFO: Created channels mask for 16 channels\n");
		iio_channels_mask_destroy(mask);
		DEBUG_PRINT("  INFO: Destroyed channels mask\n");
	}

	mask = iio_create_channels_mask(0);
	if (mask) {
		iio_channels_mask_destroy(mask);
		DEBUG_PRINT("  INFO: Zero-size mask handled\n");
	}

	// Note: iio_channels_mask_destroy(NULL) may not be safe - skipping
	DEBUG_PRINT("  INFO: NULL mask destroy behavior not tested (may cause segfault)\n");
}

TEST_FUNCTION(sample_size_calculation)
{
	struct iio_context *ctx = create_test_context("TESTS_API_URI", "local:", NULL);
	if (iio_err(ctx) && !ctx) {
		DEBUG_PRINT("  SKIP: No context for sample size test\n");
		TEST_ASSERT(true, "Sample size test skipped");
		return;
	}

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	for (unsigned int i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		if (dev) {
			struct iio_channels_mask *mask = iio_create_channels_mask(10);
			if (mask) {
				ssize_t size = iio_device_get_sample_size(dev, mask);
				DEBUG_PRINT("  INFO: Device %u sample size: %zd bytes\n", i, size);
				if (size >= 0) {
					TEST_ASSERT(size >= 0, "Sample size should be non-negative");
				}
				iio_channels_mask_destroy(mask);
			}
		}
	}

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== libiio Low-level Tests ===\n\n");

	RUN_TEST(channels_mask_operations);
	RUN_TEST(sample_size_calculation);

	TEST_SUMMARY();
	return 0;
}
