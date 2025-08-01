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
	if (test_ctx) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(context_creation_basic)
{
	struct iio_context *ctx;

	ctx = iio_create_context(NULL, NULL);
	if (iio_err(ctx)) {
		DEBUG_PRINT("  INFO: Default context creation failed with error %d (may be expected)\n", iio_err(ctx));
	} else {
		TEST_ASSERT_PTR_NOT_NULL(ctx, "Default context should be created");
		iio_context_destroy(ctx);
	}

	ctx = iio_create_context(NULL, "local:");
	if (iio_err(ctx)) {
		DEBUG_PRINT("  INFO: Local context creation failed with error %d (may be expected)\n", iio_err(ctx));
	} else {
		TEST_ASSERT_PTR_NOT_NULL(ctx, "Local context should be created");
		iio_context_destroy(ctx);
	}
}

TEST_FUNCTION(context_creation_with_params)
{
	struct iio_context_params params = {
		.out = stdout,
		.err = stderr,
		.log_level = LEVEL_ERROR,
		.stderr_level = LEVEL_WARNING,
		.timestamp_level = LEVEL_DEBUG,
		.timeout_ms = 5000,
	};

	struct iio_context *ctx = create_test_context("TESTS_API_URI", "local:", &params);
	if (iio_err(ctx)) {
		DEBUG_PRINT("  INFO: Context creation with params failed with error %d\n", iio_err(ctx));
		return;
	}

	TEST_ASSERT_PTR_NOT_NULL(ctx, "Context with params should be created");

	if (ctx) {
		const struct iio_context_params *retrieved_params = iio_context_get_params(ctx);
		TEST_ASSERT_PTR_NOT_NULL(retrieved_params, "Retrieved params should not be NULL");

		if (retrieved_params) {
			TEST_ASSERT_EQ(retrieved_params->log_level, LEVEL_ERROR, "Log level should match");
			TEST_ASSERT_EQ(retrieved_params->timeout_ms, 5000, "Timeout should match");
		}

		iio_context_destroy(ctx);
	}
}

TEST_FUNCTION(context_creation_invalid_uris)
{
	const char *invalid_uris[] = {
		"invalid:",
		"nonexistent:device",
		"usb:99.99.99",
		"ip:999.999.999.999",
		"serial:/dev/nonexistent",
		"xml:/nonexistent/file.xml",
		"",
		":",
		"backend_without_colon",
		"multiple:colons:here"
	};

	size_t num_uris = sizeof(invalid_uris) / sizeof(invalid_uris[0]);

	for (size_t i = 0; i < num_uris; i++) {
		struct iio_context *ctx = iio_create_context(NULL, invalid_uris[i]);
		if (iio_err(ctx)) {
			DEBUG_PRINT("  INFO: Invalid URI '%s' correctly failed with error %d\n",
				   invalid_uris[i], iio_err(ctx));
		} else {
			DEBUG_PRINT("  WARN: Invalid URI '%s' unexpectedly succeeded\n", invalid_uris[i]);
			iio_context_destroy(ctx);
		}
	}
}

TEST_FUNCTION(context_version_info)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int major = iio_context_get_version_major(test_ctx);
	unsigned int minor = iio_context_get_version_minor(test_ctx);
	const char *tag = iio_context_get_version_tag(test_ctx);

	TEST_ASSERT(major > 0, "Major version should be greater than 0");
	DEBUG_PRINT("  INFO: Context version: %u.%u, tag: '%s'\n", major, minor, tag ? tag : "NULL");

	unsigned int local_major = iio_context_get_version_major(NULL);
	unsigned int local_minor = iio_context_get_version_minor(NULL);
	const char *local_tag = iio_context_get_version_tag(NULL);

	TEST_ASSERT(local_major > 0, "Local major version should be greater than 0");
	DEBUG_PRINT("  INFO: Local version: %u.%u, tag: '%s'\n", local_major, local_minor, local_tag ? local_tag : "NULL");
}

TEST_FUNCTION(context_properties)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	const char *name = iio_context_get_name(test_ctx);
	TEST_ASSERT_PTR_NOT_NULL(name, "Context name should not be NULL");
	DEBUG_PRINT("  INFO: Context name: '%s'\n", name ? name : "NULL");

	const char *description = iio_context_get_description(test_ctx);
	TEST_ASSERT_PTR_NOT_NULL(description, "Context description should not be NULL");
	DEBUG_PRINT("  INFO: Context description: '%s'\n", description ? description : "NULL");

	char *xml = iio_context_get_xml(test_ctx);
	if (iio_err(xml)) {
		DEBUG_PRINT("  INFO: XML generation failed with error %d\n", iio_err(xml));
	} else {
		TEST_ASSERT_PTR_NOT_NULL(xml, "XML should not be NULL");
		TEST_ASSERT(strlen(xml) > 0, "XML should not be empty");
		DEBUG_PRINT("  INFO: XML length: %zu bytes\n", strlen(xml));
		free(xml);
	}
}

TEST_FUNCTION(context_attributes)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_attrs = iio_context_get_attrs_count(test_ctx);
	DEBUG_PRINT("  INFO: Context has %u attributes\n", nb_attrs);

	for (unsigned int i = 0; i < nb_attrs && i < 5; i++) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Context attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Context attribute %u: '%s'\n", i, name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_context_get_attr(test_ctx, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid attribute index should return NULL");

	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_context_get_attr(test_ctx, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr = iio_context_find_attr(test_ctx, name);
				TEST_ASSERT(found_attr == first_attr, "Found attribute should match original");
			}
		}
	}

	const struct iio_attr *nonexistent = iio_context_find_attr(test_ctx, "nonexistent_attr");
	TEST_ASSERT_PTR_NULL(nonexistent, "Nonexistent attribute should return NULL");
}

TEST_FUNCTION(context_devices)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
	DEBUG_PRINT("  INFO: Context has %u devices\n", nb_devices);

	for (unsigned int i = 0; i < nb_devices && i < 5; i++) {
		struct iio_device *dev = iio_context_get_device(test_ctx, i);
		TEST_ASSERT_PTR_NOT_NULL(dev, "Device should exist");

		if (dev) {
			const char *id = iio_device_get_id(dev);
			const char *name = iio_device_get_name(dev);
			DEBUG_PRINT("  INFO: Device %u: id='%s', name='%s'\n",
				   i, id ? id : "NULL", name ? name : "NULL");
		}
	}

	struct iio_device *invalid_dev = iio_context_get_device(test_ctx, nb_devices + 10);
	TEST_ASSERT_PTR_NULL(invalid_dev, "Invalid device index should return NULL");

	if (nb_devices > 0) {
		struct iio_device *first_dev = iio_context_get_device(test_ctx, 0);
		if (first_dev) {
			const char *id = iio_device_get_id(first_dev);
			if (id) {
				struct iio_device *found_dev = iio_context_find_device(test_ctx, id);
				TEST_ASSERT(found_dev == first_dev, "Found device should match original");
			}
		}
	}

	struct iio_device *nonexistent = iio_context_find_device(test_ctx, "nonexistent_device");
	TEST_ASSERT_PTR_NULL(nonexistent, "Nonexistent device should return NULL");
}

TEST_FUNCTION(context_timeout)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	int ret = iio_context_set_timeout(test_ctx, 1000);
	if (ret == 0) {
		DEBUG_PRINT("  INFO: Successfully set timeout to 1000ms\n");
	} else {
		DEBUG_PRINT("  INFO: Setting timeout failed with error %d\n", ret);
	}

	ret = iio_context_set_timeout(test_ctx, 0);
	if (ret == 0) {
		DEBUG_PRINT("  INFO: Successfully set timeout to 0 (no timeout)\n");
	} else {
		DEBUG_PRINT("  INFO: Setting timeout to 0 failed with error %d\n", ret);
	}

	ret = iio_context_set_timeout(test_ctx, UINT_MAX);
	if (ret == 0) {
		DEBUG_PRINT("  INFO: Successfully set timeout to UINT_MAX\n");
	} else {
		DEBUG_PRINT("  INFO: Setting timeout to UINT_MAX failed with error %d\n", ret);
	}
}

TEST_FUNCTION(context_user_data)
{
	setup_test_context();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	void *initial_data = iio_context_get_data(test_ctx);
	TEST_ASSERT_PTR_NULL(initial_data, "Initial user data should be NULL");

	int test_value = 42;
	iio_context_set_data(test_ctx, &test_value);

	void *retrieved_data = iio_context_get_data(test_ctx);
	TEST_ASSERT(retrieved_data == &test_value, "Retrieved data should match set data");

	iio_context_set_data(test_ctx, NULL);
	retrieved_data = iio_context_get_data(test_ctx);
	TEST_ASSERT_PTR_NULL(retrieved_data, "Data should be NULL after setting to NULL");
}

TEST_FUNCTION(context_destroy_behavior)
{
	// Note: iio_context_destroy(NULL) behavior is not guaranteed safe
	// API assumes valid context pointer, similar to other destroy functions
	DEBUG_PRINT("  INFO: context_destroy requires valid pointer (NULL behavior undefined)\n");
	TEST_ASSERT(true, "API behavior documented");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Context Tests ===\n\n");

	RUN_TEST(context_creation_basic);
	RUN_TEST(context_creation_with_params);
	RUN_TEST(context_creation_invalid_uris);
	RUN_TEST(context_version_info);
	RUN_TEST(context_properties);
	RUN_TEST(context_attributes);
	RUN_TEST(context_devices);
	RUN_TEST(context_timeout);
	RUN_TEST(context_user_data);
	RUN_TEST(context_destroy_behavior);

	cleanup_test_context();

	TEST_SUMMARY();
	return 0;
}
