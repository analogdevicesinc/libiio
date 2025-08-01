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
static struct iio_channel *test_chn = NULL;

static void setup_test_channel(void)
{
	if (!test_ctx) {
		test_ctx = create_test_context("TESTS_API_URI", "local:", NULL);
		if (iio_err(test_ctx)) {
			test_ctx = NULL;
			return;
		}
	}

	if (!test_chn && test_ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
		for (unsigned int i = 0; i < nb_devices && !test_chn; i++) {
			struct iio_device *dev = iio_context_get_device(test_ctx, i);
			if (dev) {
				unsigned int nb_channels = iio_device_get_channels_count(dev);
				if (nb_channels > 0) {
					test_chn = iio_device_get_channel(dev, 0);
					break;
				}
			}
		}
	}
}

static void cleanup_test_channel(void)
{
	test_chn = NULL;
	if (test_ctx) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(channel_properties)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	const char *id = iio_channel_get_id(test_chn);
	TEST_ASSERT_PTR_NOT_NULL(id, "Channel ID should not be NULL");
	DEBUG_PRINT("  INFO: Channel ID: '%s'\n", id ? id : "NULL");

	const char *name = iio_channel_get_name(test_chn);
	DEBUG_PRINT("  INFO: Channel name: '%s'\n", name ? name : "NULL");

	const char *label = iio_channel_get_label(test_chn);
	DEBUG_PRINT("  INFO: Channel label: '%s'\n", label ? label : "NULL");

	bool is_output = iio_channel_is_output(test_chn);
	DEBUG_PRINT("  INFO: Channel is output: %s\n", is_output ? "YES" : "NO");

	bool is_scan = iio_channel_is_scan_element(test_chn);
	DEBUG_PRINT("  INFO: Channel is scan element: %s\n", is_scan ? "YES" : "NO");

	const struct iio_device *dev = iio_channel_get_device(test_chn);
	TEST_ASSERT_PTR_NOT_NULL(dev, "Channel device should not be NULL");
}

TEST_FUNCTION(channel_type_and_modifier)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	enum iio_chan_type type = iio_channel_get_type(test_chn);
	DEBUG_PRINT("  INFO: Channel type: %d\n", type);

	enum iio_modifier modifier = iio_channel_get_modifier(test_chn);
	DEBUG_PRINT("  INFO: Channel modifier: %d\n", modifier);

	enum hwmon_chan_type hwmon_type = hwmon_channel_get_type(test_chn);
	DEBUG_PRINT("  INFO: HWMON channel type: %d\n", hwmon_type);
}

TEST_FUNCTION(channel_attributes)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	unsigned int nb_attrs = iio_channel_get_attrs_count(test_chn);
	DEBUG_PRINT("  INFO: Channel has %u attributes\n", nb_attrs);

	for (unsigned int i = 0; i < nb_attrs && i < 5; i++) {
		const struct iio_attr *attr = iio_channel_get_attr(test_chn, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Channel attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Channel attribute %u: '%s'\n", i, name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_channel_get_attr(test_chn, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid attribute index should return NULL");

	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_channel_get_attr(test_chn, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr = iio_channel_find_attr(test_chn, name);
				TEST_ASSERT(found_attr == first_attr, "Found attribute should match original");
			}
		}
	}
}

TEST_FUNCTION(channel_mask_operations)
{
	setup_test_channel();

	/* Find a channel that is a scan element in order to test enabling/disabling */
	struct iio_channel *iio_chn = NULL;
	unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
	for (unsigned int i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(test_ctx, i);
		if (dev) {
			unsigned int nb_channels = iio_device_get_channels_count(dev);
			for (unsigned int j = 0; j < nb_channels; j++) {
				struct iio_channel *chn = iio_device_get_channel(dev, j);
				if (iio_channel_is_scan_element(chn)) {
					iio_chn = chn;
					break;
				}
			}
		}
	}
	if (!iio_chn) {
		DEBUG_PRINT("  SKIP: No scan element channel available\n");
		return;
	}

	struct iio_channels_mask *mask = iio_create_channels_mask(10);
	TEST_ASSERT_PTR_NOT_NULL(mask, "Channels mask should be created");

	if (mask) {
		bool initial_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(!initial_state, "Channel should initially be disabled");

		iio_channel_enable(iio_chn, mask);
		bool enabled_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(enabled_state, "Channel should be enabled after enable call");

		iio_channel_disable(iio_chn, mask);
		bool disabled_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(!disabled_state, "Channel should be disabled after disable call");

		iio_channels_mask_destroy(mask);
	}
}

TEST_FUNCTION(channel_index_and_format)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	long index = iio_channel_get_index(test_chn);
	DEBUG_PRINT("  INFO: Channel index: %ld\n", index);

	const struct iio_data_format *format = iio_channel_get_data_format(test_chn);
	if (format) {
		DEBUG_PRINT("  INFO: Data format - length:%u, bits:%u, shift:%u, signed:%s, be:%s\n",
			   format->length, format->bits, format->shift,
			   format->is_signed ? "YES" : "NO",
			   format->is_be ? "YES" : "NO");
		DEBUG_PRINT("  INFO: Data format - scale:%f, offset:%f, repeat:%u\n",
			   format->scale, format->offset, format->repeat);
	} else {
		DEBUG_PRINT("  INFO: No data format available\n");
	}
}

TEST_FUNCTION(channel_conversion)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	const struct iio_data_format *format = iio_channel_get_data_format(test_chn);
	if (!format) {
		DEBUG_PRINT("  SKIP: No data format for conversion test\n");
		return;
	}

	uint8_t raw_data[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
	uint8_t converted_data[16];
	uint8_t restored_data[16];

	iio_channel_convert(test_chn, converted_data, raw_data);
	iio_channel_convert_inverse(test_chn, restored_data, converted_data);

	DEBUG_PRINT("  INFO: Conversion test completed (format-dependent results)\n");
}

TEST_FUNCTION(channel_user_data)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	void *initial_data = iio_channel_get_data(test_chn);
	TEST_ASSERT_PTR_NULL(initial_data, "Initial channel data should be NULL");

	double test_value = 3.14159;
	iio_channel_set_data(test_chn, &test_value);

	void *retrieved_data = iio_channel_get_data(test_chn);
	TEST_ASSERT(retrieved_data == &test_value, "Retrieved channel data should match");

	iio_channel_set_data(test_chn, NULL);
	retrieved_data = iio_channel_get_data(test_chn);
	TEST_ASSERT_PTR_NULL(retrieved_data, "Channel data should be NULL after clearing");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Channel Tests ===\n\n");

	RUN_TEST(channel_properties);
	RUN_TEST(channel_type_and_modifier);
	RUN_TEST(channel_attributes);
	RUN_TEST(channel_mask_operations);
	RUN_TEST(channel_index_and_format);
	RUN_TEST(channel_conversion);
	RUN_TEST(channel_user_data);

	cleanup_test_channel();

	TEST_SUMMARY();
	return 0;
}
