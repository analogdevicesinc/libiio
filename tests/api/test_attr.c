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

static void setup_test_context(void)
{
	if (!test_ctx) {
		test_ctx = create_test_context("TESTS_API_URI", "local:", NULL);
		if (iio_err(test_ctx)) {
			test_ctx = NULL;
		}
	}
}

static void cleanup_test_context(void)
{
	if (test_ctx && !iio_err(test_ctx)) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(attr_basic_operations)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	DEBUG_PRINT("  INFO: Found %u context attributes\n", nb_attrs);

	if (nb_attrs > 0) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, 0);
		TEST_ASSERT_PTR_NOT_NULL(attr, "First context attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			TEST_ASSERT_PTR_NOT_NULL(name, "Attribute name should not be NULL");

			const char *filename = iio_attr_get_filename(attr);
			TEST_ASSERT_PTR_NOT_NULL(filename, "Attribute filename should not be NULL");

			DEBUG_PRINT("  INFO: First attribute: name='%s', filename='%s'\n",
				   name ? name : "NULL", filename ? filename : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_context_get_attr(test_ctx, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid index should return NULL");
}

TEST_FUNCTION(attr_find_operations)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	const struct iio_attr *attr = iio_context_find_attr(test_ctx, "nonexistent_attr");
	TEST_ASSERT_PTR_NULL(attr, "Finding nonexistent attribute should return NULL");

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_context_get_attr(test_ctx, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr = iio_context_find_attr(test_ctx, name);
				TEST_ASSERT_PTR_NOT_NULL(found_attr, "Finding existing attribute should succeed");
				TEST_ASSERT(found_attr == first_attr, "Found attribute should be same as original");
			}
		}
	}
}

TEST_FUNCTION(attr_raw_read_write)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	if (nb_attrs > 0) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, 0);
		if (attr) {
			char buffer[256];
			ssize_t ret = iio_attr_read_raw(attr, buffer, sizeof(buffer));

			if (ret >= 0) {
				TEST_ASSERT(ret < (ssize_t)sizeof(buffer), "Read should not exceed buffer size");
				DEBUG_PRINT("  INFO: Read %zd bytes from attribute\n", ret);
			} else {
				DEBUG_PRINT("  INFO: Attribute read failed with error %zd (may be expected)\n", ret);
			}

			const char *test_data = "test_value";
			ret = iio_attr_write_raw(attr, test_data, strlen(test_data));
			if (ret < 0) {
				DEBUG_PRINT("  INFO: Attribute write failed with error %zd (may be read-only)\n", ret);
			} else {
				DEBUG_PRINT("  INFO: Successfully wrote %zd bytes to attribute\n", ret);
			}
		}
	}
}

TEST_FUNCTION(attr_typed_read_write)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	bool bool_val;
	long long ll_val;
	double double_val;

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	if (nb_attrs > 0) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, 0);
		if (attr) {
			int ret;

			ret = iio_attr_read_bool(attr, &bool_val);
			if (ret == 0) {
				DEBUG_PRINT("  INFO: Successfully read bool value: %s\n", bool_val ? "true" : "false");
			} else {
				DEBUG_PRINT("  INFO: Bool read failed with error %d (may not be boolean)\n", ret);
			}

			ret = iio_attr_read_longlong(attr, &ll_val);
			if (ret == 0) {
				DEBUG_PRINT("  INFO: Successfully read long long value: %lld\n", ll_val);
			} else {
				DEBUG_PRINT("  INFO: Long long read failed with error %d (may not be numeric)\n", ret);
			}

			ret = iio_attr_read_double(attr, &double_val);
			if (ret == 0) {
				DEBUG_PRINT("  INFO: Successfully read double value: %f\n", double_val);
			} else {
				DEBUG_PRINT("  INFO: Double read failed with error %d (may not be numeric)\n", ret);
			}

			ret = iio_attr_write_bool(attr, true);
			if (ret < 0) {
				DEBUG_PRINT("  INFO: Bool write failed with error %d (may be read-only)\n", ret);
			}

			ret = iio_attr_write_longlong(attr, 12345);
			if (ret < 0) {
				DEBUG_PRINT("  INFO: Long long write failed with error %d (may be read-only)\n", ret);
			}

			ret = iio_attr_write_double(attr, 3.14159);
			if (ret < 0) {
				DEBUG_PRINT("  INFO: Double write failed with error %d (may be read-only)\n", ret);
			}

			ret = iio_attr_write_string(attr, "test_string");
			if (ret < 0) {
				DEBUG_PRINT("  INFO: String write failed with error %d (may be read-only)\n", ret);
			}
		}
	}
}

TEST_FUNCTION(attr_static_value)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	for (unsigned int i = 0; i < nb_attrs; i++) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, i);
		if (attr) {
			const char *static_val = iio_attr_get_static_value(attr);
			if (static_val) {
				DEBUG_PRINT("  INFO: Attribute %u has static value: '%s'\n", i, static_val);
				TEST_ASSERT_PTR_NOT_NULL(static_val, "Static value should not be NULL when present");
			}
		}
	}
}

TEST_FUNCTION(attr_device_operations)
{
	setup_test_context();

	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
	DEBUG_PRINT("  INFO: Found %u devices\n", nb_devices);

	for (unsigned int i = 0; i < nb_devices && i < 3; i++) {
		struct iio_device *dev = iio_context_get_device(test_ctx, i);
		if (dev) {
			unsigned int nb_attrs = iio_device_get_attrs_count(dev);
			DEBUG_PRINT("  INFO: Device %u has %u attributes\n", i, nb_attrs);

			if (nb_attrs > 0) {
				const struct iio_attr *attr = iio_device_get_attr(dev, 0);
				TEST_ASSERT_PTR_NOT_NULL(attr, "Device attribute should exist");

				if (attr) {
					const char *name = iio_attr_get_name(attr);
					const struct iio_attr *found = iio_device_find_attr(dev, name);
					TEST_ASSERT(found == attr, "Found device attribute should match original");
				}
			}
		}
	}
}

int main(void)
{
	DEBUG_PRINT("=== libiio Attribute Tests ===\n\n");

	RUN_TEST(attr_basic_operations);
	RUN_TEST(attr_find_operations);
	RUN_TEST(attr_raw_read_write);
	RUN_TEST(attr_typed_read_write);
	RUN_TEST(attr_static_value);
	RUN_TEST(attr_device_operations);

	cleanup_test_context();

	TEST_SUMMARY();
	return 0;
}
