/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include "test_framework.h"
#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_external_test(const char *test_name)
{
	char command[256];
	snprintf(command, sizeof(command), "./%s", test_name);

	DEBUG_PRINT("=== Running %s ===\n", test_name);
	int result = system(command);

	if (result == 0) {
		DEBUG_PRINT("✓ %s PASSED\n\n", test_name);
		return 0;
	} else {
		DEBUG_PRINT("✗ %s FAILED (exit code: %d)\n\n", test_name, result);
		return 1;
	}
}

TEST_FUNCTION(library_version_info)
{
	unsigned int major = iio_context_get_version_major(NULL);
	unsigned int minor = iio_context_get_version_minor(NULL);
	const char *tag = iio_context_get_version_tag(NULL);

	DEBUG_PRINT("  INFO: libiio version %u.%u, tag: '%s'\n", major, minor, tag ? tag : "NULL");
	TEST_ASSERT(major > 0, "Library major version should be > 0");
}

TEST_FUNCTION(backend_availability)
{
	unsigned int backend_count = iio_get_builtin_backends_count();
	DEBUG_PRINT("  INFO: %u built-in backends available\n", backend_count);

	for (unsigned int i = 0; i < backend_count; i++) {
		const char *backend = iio_get_builtin_backend(i);
		bool available = iio_has_backend(NULL, backend);
		DEBUG_PRINT("  INFO: Backend '%s': %s\n",
			   backend ? backend : "NULL", available ? "Available" : "Not available");
	}

	TEST_ASSERT(backend_count > 0, "At least one backend should be available");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Comprehensive Test Suite ===\n\n");
	DEBUG_PRINT("Running integrated tests first...\n\n");

	RUN_TEST(library_version_info);
	RUN_TEST(backend_availability);

	DEBUG_PRINT("\n=== Running Individual Test Suites ===\n\n");

	const char *test_suites[] = {
		"test_error_handling",
		"test_toplevel",
		"test_scan",
		"test_context",
		"test_attr",
		"test_device",
		"test_channel",
		"test_buffer",
		"test_hwmon",
		"test_events",
		"test_lowlevel",
		"test_typed_attr"
	};

	int num_suites = sizeof(test_suites) / sizeof(test_suites[0]);
	int passed = 0;
	int failed = 0;

	for (int i = 0; i < num_suites; i++) {
		if (run_external_test(test_suites[i]) == 0) {
			passed++;
		} else {
			failed++;
		}
	}

	DEBUG_PRINT("=== Final Test Suite Summary ===\n");
	DEBUG_PRINT("Test suites run: %d\n", num_suites);
	DEBUG_PRINT("Test suites passed: %d\n", passed);
	DEBUG_PRINT("Test suites failed: %d\n", failed);
	DEBUG_PRINT("Success rate: %.1f%%\n", num_suites > 0 ? (100.0 * passed / num_suites) : 0.0);

	if (failed == 0) {
		DEBUG_PRINT("\n ALL API TESTS PASSED! \n");
		exit(EXIT_SUCCESS);
	} else {
		DEBUG_PRINT("\n %d API TEST FAILED \n", failed);
		exit(EXIT_FAILURE);
	}
}
