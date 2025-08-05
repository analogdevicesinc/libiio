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

static void setup_test_device(void)
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
		if (nb_devices > 0) {
			test_dev = iio_context_get_device(test_ctx, 0);
		}
	}
}

static void cleanup_test_device(void)
{
	test_dev = NULL;
	if (test_ctx) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(device_properties)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	const char *id = iio_device_get_id(test_dev);
	TEST_ASSERT_PTR_NOT_NULL(id, "Device ID should not be NULL");
	DEBUG_PRINT("  INFO: Device ID: '%s'\n", id ? id : "NULL");

	const char *name = iio_device_get_name(test_dev);
	DEBUG_PRINT("  INFO: Device name: '%s'\n", name ? name : "NULL");

	const char *label = iio_device_get_label(test_dev);
	DEBUG_PRINT("  INFO: Device label: '%s'\n", label ? label : "NULL");

	const struct iio_context *ctx = iio_device_get_context(test_dev);
	TEST_ASSERT(ctx == test_ctx, "Device context should match original context");
}

TEST_FUNCTION(device_channels)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	unsigned int nb_channels = iio_device_get_channels_count(test_dev);
	DEBUG_PRINT("  INFO: Device has %u channels\n", nb_channels);

	for (unsigned int i = 0; i < nb_channels && i < 5; i++) {
		struct iio_channel *chn = iio_device_get_channel(test_dev, i);
		TEST_ASSERT_PTR_NOT_NULL(chn, "Channel should exist");

		if (chn) {
			const char *id = iio_channel_get_id(chn);
			const char *name = iio_channel_get_name(chn);
			bool is_output = iio_channel_is_output(chn);
			bool is_scan = iio_channel_is_scan_element(chn);

			DEBUG_PRINT("  INFO: Channel %u: id='%s', name='%s', output=%s, scan=%s\n",
				   i, id ? id : "NULL", name ? name : "NULL",
				   is_output ? "YES" : "NO", is_scan ? "YES" : "NO");
		}
	}

	struct iio_channel *invalid_chn = iio_device_get_channel(test_dev, nb_channels + 10);
	TEST_ASSERT_PTR_NULL(invalid_chn, "Invalid channel index should return NULL");

	if (nb_channels > 0) {
		struct iio_channel *first_chn = iio_device_get_channel(test_dev, 0);
		if (first_chn) {
			const char *id = iio_channel_get_id(first_chn);
			if (id) {
				struct iio_channel *found_input = iio_device_find_channel(test_dev, id, false);
				struct iio_channel *found_output = iio_device_find_channel(test_dev, id, true);

				bool is_output = iio_channel_is_output(first_chn);
				if (is_output) {
					TEST_ASSERT(found_output == first_chn, "Found output channel should match");
				} else {
					TEST_ASSERT(found_input == first_chn, "Found input channel should match");
				}
			}
		}
	}
}

TEST_FUNCTION(device_attributes)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	unsigned int nb_attrs = iio_device_get_attrs_count(test_dev);
	DEBUG_PRINT("  INFO: Device has %u attributes\n", nb_attrs);

	for (unsigned int i = 0; i < nb_attrs; i++) {
		const struct iio_attr *attr = iio_device_get_attr(test_dev, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Device attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Device attribute %u: '%s'\n", i, name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_device_get_attr(test_dev, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid attribute index should return NULL");

	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_device_get_attr(test_dev, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr = iio_device_find_attr(test_dev, name);
				TEST_ASSERT(found_attr == first_attr, "Found attribute should match original");
			}
		}
	}

	const struct iio_attr *nonexistent = iio_device_find_attr(test_dev, "nonexistent_attr");
	TEST_ASSERT_PTR_NULL(nonexistent, "Nonexistent attribute should return NULL");
}

TEST_FUNCTION(device_debug_attributes)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	unsigned int nb_debug_attrs = iio_device_get_debug_attrs_count(test_dev);
	DEBUG_PRINT("  INFO: Device has %u debug attributes\n", nb_debug_attrs);

	for (unsigned int i = 0; i < nb_debug_attrs; i++) {
		const struct iio_attr *attr = iio_device_get_debug_attr(test_dev, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Debug attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Debug attribute %u: '%s'\n", i, name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_debug = iio_device_get_debug_attr(test_dev, nb_debug_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_debug, "Invalid debug attribute index should return NULL");

	if (nb_debug_attrs > 0) {
		const struct iio_attr *first_debug = iio_device_get_debug_attr(test_dev, 0);
		if (first_debug) {
			const char *name = iio_attr_get_name(first_debug);
			if (name) {
				const struct iio_attr *found_debug = iio_device_find_debug_attr(test_dev, name);
				TEST_ASSERT(found_debug == first_debug, "Found debug attribute should match original");
			}
		}
	}

	const struct iio_attr *nonexistent_debug = iio_device_find_debug_attr(test_dev, "nonexistent_debug");
	TEST_ASSERT_PTR_NULL(nonexistent_debug, "Nonexistent debug attribute should return NULL");
}

TEST_FUNCTION(device_trigger_operations)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	const struct iio_device *current_trigger = iio_device_get_trigger(test_dev);
	if (iio_err(current_trigger)) {
		DEBUG_PRINT("  INFO: Device has no trigger (error %d)\n", iio_err(current_trigger));
	} else if (current_trigger) {
		const char *trigger_id = iio_device_get_id(current_trigger);
		DEBUG_PRINT("  INFO: Device trigger: '%s'\n", trigger_id ? trigger_id : "NULL");

		bool is_trigger = iio_device_is_trigger(current_trigger);
		TEST_ASSERT(is_trigger, "Current trigger should be identified as a trigger");
	} else {
		DEBUG_PRINT("  INFO: Device has no trigger (NULL)\n");
	}

	bool device_is_trigger = iio_device_is_trigger(test_dev);
	DEBUG_PRINT("  INFO: Test device is trigger: %s\n", device_is_trigger ? "YES" : "NO");

	if (!device_is_trigger && test_ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
		struct iio_device *trigger_device = NULL;

		for (unsigned int i = 0; i < nb_devices; i++) {
			struct iio_device *dev = iio_context_get_device(test_ctx, i);
			if (dev && iio_device_is_trigger(dev)) {
				trigger_device = dev;
				break;
			}
		}

		if (trigger_device) {
			int ret = iio_device_set_trigger(test_dev, trigger_device);
			if (ret == 0) {
				DEBUG_PRINT("  INFO: Successfully set trigger\n");

				const struct iio_device *new_trigger = iio_device_get_trigger(test_dev);
				if (!iio_err(new_trigger)) {
					TEST_ASSERT(new_trigger == trigger_device, "Set trigger should match");
				}
			} else {
				DEBUG_PRINT("  INFO: Setting trigger failed with error %d\n", ret);
			}

			ret = iio_device_set_trigger(test_dev, NULL);
			if (ret == 0) {
				DEBUG_PRINT("  INFO: Successfully cleared trigger\n");
			} else {
				DEBUG_PRINT("  INFO: Clearing trigger failed with error %d\n", ret);
			}
		} else {
			DEBUG_PRINT("  INFO: No trigger devices available for testing\n");
		}
	}
}

TEST_FUNCTION(device_register_operations)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	uint32_t test_addr = 0x0;
	uint32_t value;

	int ret = iio_device_reg_read(test_dev, test_addr, &value);
	if (ret == 0) {
		DEBUG_PRINT("  INFO: Successfully read register 0x%x: 0x%x\n", test_addr, value);

		ret = iio_device_reg_write(test_dev, test_addr, value);
		if (ret == 0) {
			DEBUG_PRINT("  INFO: Successfully wrote register 0x%x: 0x%x\n", test_addr, value);
		} else {
			DEBUG_PRINT("  INFO: Register write failed with error %d\n", ret);
		}
	} else {
		DEBUG_PRINT("  INFO: Register read failed with error %d (may not support register access)\n", ret);
	}

	uint32_t invalid_addrs[] = {0xFFFFFFFF, 0x12345678, 0xDEADBEEF};
	for (size_t i = 0; i < sizeof(invalid_addrs) / sizeof(invalid_addrs[0]); i++) {
		ret = iio_device_reg_read(test_dev, invalid_addrs[i], &value);
		if (ret != 0) {
			DEBUG_PRINT("  INFO: Register read at invalid address 0x%x correctly failed\n", invalid_addrs[i]);
		}
	}
}

TEST_FUNCTION(device_user_data)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	void *initial_data = iio_device_get_data(test_dev);
	TEST_ASSERT_PTR_NULL(initial_data, "Initial device data should be NULL");

	int test_value = 123;
	iio_device_set_data(test_dev, &test_value);

	void *retrieved_data = iio_device_get_data(test_dev);
	TEST_ASSERT(retrieved_data == &test_value, "Retrieved device data should match");

	iio_device_set_data(test_dev, NULL);
	retrieved_data = iio_device_get_data(test_dev);
	TEST_ASSERT_PTR_NULL(retrieved_data, "Device data should be NULL after clearing");
}

TEST_FUNCTION(device_hwmon_detection)
{
	setup_test_device();

	if (!test_dev) {
		DEBUG_PRINT("  SKIP: No test device available\n");
		return;
	}

	bool is_hwmon = iio_device_is_hwmon(test_dev);
	DEBUG_PRINT("  INFO: Device is HWMON: %s\n", is_hwmon ? "YES" : "NO");

	if (test_ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
		unsigned int hwmon_count = 0;

		for (unsigned int i = 0; i < nb_devices; i++) {
			struct iio_device *dev = iio_context_get_device(test_ctx, i);
			if (dev && iio_device_is_hwmon(dev)) {
				hwmon_count++;
				const char *id = iio_device_get_id(dev);
				DEBUG_PRINT("  INFO: HWMON device found: '%s'\n", id ? id : "NULL");
			}
		}

		DEBUG_PRINT("  INFO: Total HWMON devices: %u\n", hwmon_count);
	}
}

int main(void)
{
	DEBUG_PRINT("=== libiio Device Tests ===\n\n");

	RUN_TEST(device_properties);
	RUN_TEST(device_channels);
	RUN_TEST(device_attributes);
	RUN_TEST(device_debug_attributes);
	RUN_TEST(device_trigger_operations);
	RUN_TEST(device_register_operations);
	RUN_TEST(device_user_data);
	RUN_TEST(device_hwmon_detection);

	cleanup_test_device();

	TEST_SUMMARY();
	return 0;
}
