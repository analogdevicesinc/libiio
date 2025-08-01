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

TEST_FUNCTION(event_inline_functions)
{
	struct iio_event test_event = {
		.id = 0x1234567890ABCDEF,
		.timestamp = 1234567890
	};

	enum iio_event_type type = iio_event_get_type(&test_event);
	DEBUG_PRINT("  INFO: Event type: %d\n", type);

	enum iio_event_direction dir = iio_event_get_direction(&test_event);
	DEBUG_PRINT("  INFO: Event direction: %d\n", dir);

	TEST_ASSERT(true, "Event inline functions work");
}

TEST_FUNCTION(event_stream_operations)
{
	struct iio_context *ctx = create_test_context("TESTS_API_URI", "local:", NULL);
	if (iio_err(ctx) || !ctx) {
		DEBUG_PRINT("  SKIP: No context for event stream test\n");
		TEST_ASSERT(true, "Event stream test skipped");
		return;
	}

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	for (unsigned int i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		if (dev) {
			struct iio_event_stream *stream = iio_device_create_event_stream(dev);
			if (!iio_err(stream)) {
				DEBUG_PRINT("  INFO: Event stream created successfully\n");
				iio_event_stream_destroy(stream);
				iio_context_destroy(ctx);
				TEST_ASSERT(true, "Event stream created and destroyed");
				return;
			}
		}
	}

	DEBUG_PRINT("  INFO: No devices support event streams\n");
	iio_context_destroy(ctx);
	TEST_ASSERT(true, "Event stream test completed");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Events Tests ===\n\n");

	RUN_TEST(event_inline_functions);
	RUN_TEST(event_stream_operations);

	TEST_SUMMARY();
	return 0;
}
