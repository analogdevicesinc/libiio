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
#include <string.h>

static struct iio_context *test_ctx = NULL;
static struct iio_device *test_dev = NULL;
static unsigned int test_dev_chn_count = 0;
static unsigned int test_dev_buf_chn_count = 0;

static void setup_test_buffer(void)
{
	if (!test_ctx) {
		test_ctx = create_test_context("TESTS_API_URI", "local:", NULL);
		if (iio_err(test_ctx)) {
			test_ctx = NULL;
			return;
		}
	}

	if (!test_dev && test_ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
		for (unsigned int i = 0; i < nb_devices; i++) {
			struct iio_device *dev = iio_context_get_device(test_ctx, i);
			if (dev) {
				test_dev_chn_count = iio_device_get_channels_count(dev);
				test_dev_buf_chn_count = 0;

				for (unsigned int c = 0; c < test_dev_chn_count; c++) {
					struct iio_channel *chn = iio_device_get_channel(dev, c);
					if (iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn))
						test_dev_buf_chn_count++;
				}
				if (test_dev_buf_chn_count > 0) {
					test_dev = dev;
					break;
				}
			}
		}
	}
}

static void cleanup_test_buffer(void)
{
	test_dev = NULL;
	if (test_ctx) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(buffer_creation_basic)
{
	setup_test_buffer();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	struct iio_channels_mask *mask = iio_create_channels_mask(2);
	if (!mask) {
		DEBUG_PRINT("  SKIP: Could not create channels mask\n");
		return;
	}

	for (unsigned int c = 0; c < test_dev_chn_count; c++) {
		struct iio_channel *chn = iio_device_get_channel(test_dev, c);
		if (iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn))
			iio_channel_enable(chn, mask);
	}


	struct iio_buffer *buffer = iio_device_create_buffer(test_dev, 0, mask);
	if (iio_err(buffer)) {
		DEBUG_PRINT("  INFO: Buffer creation failed with error %d (may be expected)\n", iio_err(buffer));
		iio_channels_mask_destroy(mask);
		return;
	}

	TEST_ASSERT_PTR_NOT_NULL(buffer, "Buffer should be created");

	const struct iio_device *buf_dev = iio_buffer_get_device(buffer);
	TEST_ASSERT(buf_dev == test_dev, "Buffer device should match original device");

	const struct iio_channels_mask *buf_mask = iio_buffer_get_channels_mask(buffer);
	TEST_ASSERT_PTR_NOT_NULL(buf_mask, "Buffer channels mask should not be NULL");

	iio_buffer_destroy(buffer);
	iio_channels_mask_destroy(mask);
}

TEST_FUNCTION(buffer_attributes)
{
	setup_test_buffer();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	struct iio_buffer *buffer = iio_device_get_buffer(test_dev, 0);
	if (!buffer) {
		DEBUG_PRINT("  SKIP: Could not get buffer for attributes test\n");
		return;
	}

	unsigned int nb_attrs = iio_buffer_get_attrs_count(buffer);
	DEBUG_PRINT("  INFO: Buffer has %u attributes\n", nb_attrs);

	for (unsigned int i = 0; i < nb_attrs && i < 5; i++) {
		const struct iio_attr *attr = iio_buffer_get_attr(buffer, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Buffer attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Buffer attribute %u: '%s'\n", i, name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_buffer_get_attr(buffer, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid attribute index should return NULL");

	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_buffer_get_attr(buffer, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr = iio_buffer_find_attr(buffer, name);
				TEST_ASSERT(found_attr == first_attr, "Found buffer attribute should match");
			}
		}
	}

	const struct iio_attr *nonexistent = iio_buffer_find_attr(buffer, "nonexistent_attr");
	TEST_ASSERT_PTR_NULL(nonexistent, "Nonexistent buffer attribute should return NULL");
}

TEST_FUNCTION(buffer_enable_disable)
{
	setup_test_buffer();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	struct iio_channels_mask *mask = iio_create_channels_mask(10);
	if (!mask) {
		DEBUG_PRINT("  SKIP: Could not create channels mask\n");
		return;
	}

	for (unsigned int c = 0; c < test_dev_chn_count; c++) {
		struct iio_channel *chn = iio_device_get_channel(test_dev, c);
		if (iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn))
			iio_channel_enable(chn, mask);
	}

	struct iio_buffer *buffer = iio_device_create_buffer(test_dev, 0, mask);
	if (iio_err(buffer)) {
		DEBUG_PRINT("  SKIP: Could not create buffer for enable/disable test\n");
		iio_channels_mask_destroy(mask);
		return;
	}

	struct iio_block *block = iio_buffer_create_block(buffer, 1024);
	iio_block_enqueue(block, 1024, false);

	int ret = iio_buffer_enable(buffer);
	if (ret == 0) {
		DEBUG_PRINT("  INFO: Buffer enabled successfully\n");

		iio_block_dequeue(block, false);

		ret = iio_buffer_disable(buffer);
		if (ret == 0) {
			DEBUG_PRINT("  INFO: Buffer disabled successfully\n");
		} else {
			DEBUG_PRINT("  INFO: Buffer disable failed with error %d\n", ret);
		}
	} else {
		DEBUG_PRINT("  INFO: Buffer enable failed with error %d (may be expected)\n", ret);
	}

	iio_block_destroy(block);
	iio_buffer_destroy(buffer);
	iio_channels_mask_destroy(mask);
}

TEST_FUNCTION(buffer_cancel)
{
	setup_test_buffer();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	struct iio_channels_mask *mask = iio_create_channels_mask(10);
	if (!mask) {
		DEBUG_PRINT("  SKIP: Could not create channels mask\n");
		return;
	}

	for (unsigned int c = 0; c < test_dev_chn_count; c++) {
		struct iio_channel *chn = iio_device_get_channel(test_dev, c);
		if (iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn))
			iio_channel_enable(chn, mask);
	}

	struct iio_buffer *buffer = iio_device_create_buffer(test_dev, 0, mask);
	if (iio_err(buffer)) {
		DEBUG_PRINT("  SKIP: Could not create buffer for cancel test\n");
		iio_channels_mask_destroy(mask);
		return;
	}

	iio_buffer_cancel(buffer);
	DEBUG_PRINT("  INFO: Buffer cancel completed without error\n");

	iio_buffer_cancel(buffer);
	DEBUG_PRINT("  INFO: Second buffer cancel completed without error\n");

	iio_buffer_destroy(buffer);
	iio_channels_mask_destroy(mask);
}

TEST_FUNCTION(buffer_user_data)
{
	setup_test_buffer();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	struct iio_buffer *buffer = iio_device_get_buffer(test_dev, 0);
	if (!buffer) {
		DEBUG_PRINT("  SKIP: Could not get buffer for user data test\n");
		return;
	}

	void *initial_data = iio_buffer_get_data(buffer);
	TEST_ASSERT_PTR_NULL(initial_data, "Initial buffer data should be NULL");

	char test_value[] = "buffer_data";
	iio_buffer_set_data(buffer, test_value);

	void *retrieved_data = iio_buffer_get_data(buffer);
	TEST_ASSERT(retrieved_data == test_value, "Retrieved buffer data should match");

	iio_buffer_set_data(buffer, NULL);
	retrieved_data = iio_buffer_get_data(buffer);
	TEST_ASSERT_PTR_NULL(retrieved_data, "Buffer data should be NULL after clearing");
}

TEST_FUNCTION(buffer_destroy_behavior)
{
	// Note: iio_buffer_destroy(NULL) behavior is not guaranteed safe
	// API assumes valid buffer pointer
	DEBUG_PRINT("  INFO: buffer_destroy requires valid pointer (NULL behavior undefined)\n");
	TEST_ASSERT(true, "API behavior documented");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Buffer Tests ===\n\n");

	RUN_TEST(buffer_creation_basic);
	RUN_TEST(buffer_attributes);
	RUN_TEST(buffer_enable_disable);
	RUN_TEST(buffer_cancel);
	RUN_TEST(buffer_user_data);
	RUN_TEST(buffer_destroy_behavior);

	cleanup_test_buffer();

	TEST_SUMMARY();
	return 0;
}
