/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include "test_framework.h"
#include <iio/iio.h>
#include <errno.h>
#include <string.h>

TEST_FUNCTION(strerror_functionality)
{
	char buffer[256];

	iio_strerror(0, buffer, sizeof(buffer));
	TEST_ASSERT(strlen(buffer) > 0, "Error string for 0 should not be empty");
	DEBUG_PRINT("  INFO: Error 0: '%s'\n", buffer);

	iio_strerror(-EINVAL, buffer, sizeof(buffer));
	TEST_ASSERT(strlen(buffer) > 0, "Error string for -EINVAL should not be empty");
	DEBUG_PRINT("  INFO: Error -EINVAL: '%s'\n", buffer);

	iio_strerror(EINVAL, buffer, sizeof(buffer));
	TEST_ASSERT(strlen(buffer) > 0, "Error string for EINVAL should not be empty");
	DEBUG_PRINT("  INFO: Error EINVAL: '%s'\n", buffer);

	iio_strerror(-ENODEV, buffer, sizeof(buffer));
	TEST_ASSERT(strlen(buffer) > 0, "Error string for -ENODEV should not be empty");
	DEBUG_PRINT("  INFO: Error -ENODEV: '%s'\n", buffer);

	iio_strerror(12345, buffer, sizeof(buffer));
	TEST_ASSERT(strlen(buffer) > 0, "Error string for unknown error should not be empty");
	DEBUG_PRINT("  INFO: Error 12345: '%s'\n", buffer);
}

TEST_FUNCTION(strerror_buffer_sizes)
{
	char small_buffer[10];
	char large_buffer[1000];

	iio_strerror(-EINVAL, small_buffer, sizeof(small_buffer));
	TEST_ASSERT(strlen(small_buffer) < sizeof(small_buffer), "Small buffer should not overflow");
	TEST_ASSERT(small_buffer[sizeof(small_buffer) - 1] == '\0', "Small buffer should be null-terminated");

	iio_strerror(-EINVAL, large_buffer, sizeof(large_buffer));
	TEST_ASSERT(strlen(large_buffer) < sizeof(large_buffer), "Large buffer should not overflow");
	TEST_ASSERT(large_buffer[sizeof(large_buffer) - 1] == '\0', "Large buffer should be null-terminated");

	iio_strerror(-EINVAL, small_buffer, 1);
	TEST_ASSERT(small_buffer[0] == '\0', "Buffer of size 1 should contain only null terminator");

	iio_strerror(-EINVAL, small_buffer, 0);
}

TEST_FUNCTION(has_backend_functionality)
{
	const char *common_backends[] = {
		"local",
		"usb",
		"ip",
		"network",
		"serial",
		"xml"
	};

	size_t num_backends = sizeof(common_backends) / sizeof(common_backends[0]);

	for (size_t i = 0; i < num_backends; i++) {
		bool has_backend = iio_has_backend(NULL, common_backends[i]);
		DEBUG_PRINT("  INFO: Backend '%s' availability: %s\n",
			   common_backends[i], has_backend ? "YES" : "NO");
	}

	bool has_nonexistent = iio_has_backend(NULL, "nonexistent_backend");
	TEST_ASSERT(!has_nonexistent, "Nonexistent backend should not be available");

	bool has_empty = iio_has_backend(NULL, "");
	TEST_ASSERT(!has_empty, "Empty backend name should not be available");

	// Note: Passing NULL backend name causes segfault in libiio - this is expected behavior
	// The API requires a valid backend name string
}

TEST_FUNCTION(has_backend_with_params)
{
	struct iio_context_params params = {
		.out = stdout,
		.err = stderr,
		.log_level = LEVEL_ERROR,
		.stderr_level = LEVEL_WARNING,
		.timestamp_level = LEVEL_DEBUG,
		.timeout_ms = 1000,
	};

	bool has_local_no_params = iio_has_backend(NULL, "local");
	bool has_local_with_params = iio_has_backend(&params, "local");

	TEST_ASSERT(has_local_no_params == has_local_with_params,
		   "Backend availability should be consistent with/without params");

	DEBUG_PRINT("  INFO: Local backend with params: %s\n",
		   has_local_with_params ? "YES" : "NO");
}

TEST_FUNCTION(builtin_backends_count)
{
	unsigned int count = iio_get_builtin_backends_count();
	TEST_ASSERT(count > 0, "Should have at least one built-in backend");
	DEBUG_PRINT("  INFO: Found %u built-in backends\n", count);

	for (unsigned int i = 0; i < count && i < 10; i++) {
		const char *backend_name = iio_get_builtin_backend(i);
		TEST_ASSERT_PTR_NOT_NULL(backend_name, "Built-in backend name should not be NULL");

		if (backend_name) {
			DEBUG_PRINT("  INFO: Built-in backend %u: '%s'\n", i, backend_name);

			bool has_backend = iio_has_backend(NULL, backend_name);
			DEBUG_PRINT("    Availability check: %s\n", has_backend ? "YES" : "NO");
		}
	}
}

TEST_FUNCTION(builtin_backends_invalid_index)
{
	unsigned int count = iio_get_builtin_backends_count();

	const char *invalid_backend = iio_get_builtin_backend(count);
	TEST_ASSERT_PTR_NULL(invalid_backend, "Invalid index should return NULL");

	invalid_backend = iio_get_builtin_backend(count + 100);
	TEST_ASSERT_PTR_NULL(invalid_backend, "Large invalid index should return NULL");

	invalid_backend = iio_get_builtin_backend(UINT_MAX);
	TEST_ASSERT_PTR_NULL(invalid_backend, "UINT_MAX index should return NULL");
}

TEST_FUNCTION(builtin_backends_consistency)
{
	unsigned int count = iio_get_builtin_backends_count();

	for (unsigned int i = 0; i < count; i++) {
		const char *backend_name = iio_get_builtin_backend(i);
		if (backend_name) {
			bool has_backend = iio_has_backend(NULL, backend_name);
			if (!has_backend) {
				DEBUG_PRINT("  WARN: Built-in backend '%s' reports as not available\n", backend_name);
			}
		}
	}
}

TEST_FUNCTION(backend_name_validation)
{
	const char *test_names[] = {
		"",
		" ",
		"local ",
		" local",
		"LOCAL",
		"Local",
		"usb:device",
		"ip:192.168.1.1",
		"serial:/dev/ttyUSB0",
		"very_long_backend_name_that_probably_does_not_exist_but_we_test_anyway",
		"backend-with-dashes",
		"backend_with_underscores",
		"backend123",
		"123backend",
		"backend with spaces",
		"\t\n\r",
		"backend\x00hidden"
	};

	size_t num_names = sizeof(test_names) / sizeof(test_names[0]);

	for (size_t i = 0; i < num_names; i++) {
		bool has_backend = iio_has_backend(NULL, test_names[i]);
		DEBUG_PRINT("  INFO: Backend name test '%s': %s\n",
			   test_names[i], has_backend ? "YES" : "NO");
	}
}

int main(void)
{
	DEBUG_PRINT("=== libiio Top-level Functions Tests ===\n\n");

	RUN_TEST(strerror_functionality);
	RUN_TEST(strerror_buffer_sizes);
	RUN_TEST(has_backend_functionality);
	RUN_TEST(has_backend_with_params);
	RUN_TEST(builtin_backends_count);
	RUN_TEST(builtin_backends_invalid_index);
	RUN_TEST(builtin_backends_consistency);
	RUN_TEST(backend_name_validation);

	TEST_SUMMARY();
	return 0;
}
