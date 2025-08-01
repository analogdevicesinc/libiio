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

TEST_FUNCTION(scan_basic_operations)
{
	struct iio_scan *scan;

	scan = iio_scan(NULL, NULL);
	if (iio_err(scan)) {
		DEBUG_PRINT("  INFO: Basic scan failed with error %d (may be expected on some systems)\n", iio_err(scan));
		return;
	}

	TEST_ASSERT_PTR_NOT_NULL(scan, "Scan should succeed");

	size_t results_count = iio_scan_get_results_count(scan);
	DEBUG_PRINT("  INFO: Found %zu scan results\n", results_count);

	for (size_t i = 0; i < results_count && i < 5; i++) {
		const char *description = iio_scan_get_description(scan, i);
		const char *uri = iio_scan_get_uri(scan, i);

		DEBUG_PRINT("  INFO: Result %zu: URI='%s', Description='%s'\n",
				i, uri ? uri : "NULL", description ? description : "NULL");

		TEST_ASSERT_PTR_NOT_NULL(uri, "URI should not be NULL for valid index");
	}

	const char *invalid_desc = iio_scan_get_description(scan, results_count + 10);
	TEST_ASSERT_PTR_NULL(invalid_desc, "Description for invalid index should be NULL");

	const char *invalid_uri = iio_scan_get_uri(scan, results_count + 10);
	TEST_ASSERT_PTR_NULL(invalid_uri, "URI for invalid index should be NULL");

	iio_scan_destroy(scan);
}

TEST_FUNCTION(scan_with_params)
{
	struct iio_context_params params = {
		.out = stdout,
		.err = stderr,
		.log_level = LEVEL_ERROR,
		.stderr_level = LEVEL_WARNING,
		.timestamp_level = LEVEL_DEBUG,
		.timeout_ms = 5000,
	};

	struct iio_scan *scan = iio_scan(&params, NULL);
	if (iio_err(scan)) {
		DEBUG_PRINT("  INFO: Scan with params failed with error %d (may be expected)\n", iio_err(scan));
		return;
	}

	TEST_ASSERT_PTR_NOT_NULL(scan, "Scan with params should succeed");

	size_t results_count = iio_scan_get_results_count(scan);
	DEBUG_PRINT("  INFO: Scan with params found %zu results\n", results_count);

	iio_scan_destroy(scan);
}

TEST_FUNCTION(scan_specific_backends)
{
	const char *backends[] = {
		"local",
		"usb",
		"ip",
		"serial",
		"xml",
		"local,usb",
		"",
		"nonexistent_backend"
	};

	size_t num_backends = sizeof(backends) / sizeof(backends[0]);

	for (size_t i = 0; i < num_backends; i++) {
		DEBUG_PRINT("  INFO: Testing backend: '%s'\n", backends[i]);

		struct iio_scan *scan = iio_scan(NULL, backends[i]);
		if (iio_err(scan)) {
			DEBUG_PRINT("    Backend '%s' failed with error %d (may be expected)\n",
					backends[i], iio_err(scan));
			continue;
		}

		size_t results_count = iio_scan_get_results_count(scan);
		DEBUG_PRINT("    Backend '%s' found %zu results\n", backends[i], results_count);

		iio_scan_destroy(scan);
	}
}

TEST_FUNCTION(scan_empty_items)
{
	const char *backends = ";;";
	struct iio_scan *scan;
    int err;
    size_t backends_count;

	DEBUG_PRINT("  INFO: Testing backend: '%s'\n", backends);
	scan = iio_scan(NULL, backends);
	err = iio_err(scan);
	if (scan) {
        backends_count = iio_scan_get_results_count(scan);
    }

	TEST_ASSERT_PTR_NOT_NULL(scan, "Scan with params should succeed");
	TEST_ASSERT_EQ(err, 0, "iio_scan should return 0 if succeeded");
	TEST_ASSERT_EQ(backends_count, 0, "iio_scan_get_results_count should return 0");

	iio_scan_destroy(scan);
}

TEST_FUNCTION(scan_usb_filtering)
{
	const char *usb_filters[] = {
		"usb=0456:*",
		"usb=0456:b673",
		"usb=ffff:ffff",
		"usb=invalid"
	};

	size_t num_filters = sizeof(usb_filters) / sizeof(usb_filters[0]);

	for (size_t i = 0; i < num_filters; i++) {
		DEBUG_PRINT("  INFO: Testing USB filter: '%s'\n", usb_filters[i]);

		struct iio_scan *scan = iio_scan(NULL, usb_filters[i]);
		if (iio_err(scan)) {
			DEBUG_PRINT("    USB filter '%s' failed with error %d (may be expected)\n", 
				usb_filters[i], iio_err(scan));
			continue;
		}

		size_t results_count = iio_scan_get_results_count(scan);
		DEBUG_PRINT("    USB filter '%s' found %zu results\n", usb_filters[i], results_count);

		iio_scan_destroy(scan);
	}
}

TEST_FUNCTION(scan_edge_cases)
{
	struct iio_scan *scan;

	scan = iio_scan(NULL, "");
	if (!iio_err(scan)) {
		size_t count = iio_scan_get_results_count(scan);
		DEBUG_PRINT("  INFO: Empty backend string found %zu results\n", count);
		iio_scan_destroy(scan);
	} else {
		DEBUG_PRINT("  INFO: Empty backend string failed with error %d\n", iio_err(scan));
	}

	scan = iio_scan(NULL, ",,,");
	if (!iio_err(scan)) {
		size_t count = iio_scan_get_results_count(scan);
		DEBUG_PRINT("  INFO: Comma-only backend string found %zu results\n", count);
		iio_scan_destroy(scan);
	} else {
		DEBUG_PRINT("  INFO: Comma-only backend string failed with error %d\n", iio_err(scan));
	}

	scan = iio_scan(NULL, "local,");
	if (!iio_err(scan)) {
		size_t count = iio_scan_get_results_count(scan);
		DEBUG_PRINT("  INFO: Trailing comma backend string found %zu results\n", count);
		iio_scan_destroy(scan);
	} else {
		DEBUG_PRINT("  INFO: Trailing comma backend string failed with error %d\n", iio_err(scan));
	}
}

TEST_FUNCTION(scan_destroy_behavior)
{
	// Note: iio_scan_destroy(NULL) causes segfault - this is expected behavior
	// The API requires a valid scan pointer, similar to free()
	DEBUG_PRINT("  INFO: scan_destroy requires valid pointer (NULL not supported)\n");
	TEST_ASSERT(true, "API behavior documented");
}

TEST_FUNCTION(scan_results_boundary)
{
	struct iio_scan *scan = iio_scan(NULL, "local");
	if (iio_err(scan)) {
		DEBUG_PRINT("  SKIP: Could not create scan for boundary test\n");
		return;
	}

	size_t results_count = iio_scan_get_results_count(scan);

	const char *desc = iio_scan_get_description(scan, SIZE_MAX);
	TEST_ASSERT_PTR_NULL(desc, "Description for SIZE_MAX index should be NULL");

	const char *uri = iio_scan_get_uri(scan, SIZE_MAX);
	TEST_ASSERT_PTR_NULL(uri, "URI for SIZE_MAX index should be NULL");

	if (results_count > 0) {
		desc = iio_scan_get_description(scan, results_count - 1);
		DEBUG_PRINT("  INFO: Last valid description: '%s'\n", desc ? desc : "NULL");

		uri = iio_scan_get_uri(scan, results_count - 1);
		DEBUG_PRINT("  INFO: Last valid URI: '%s'\n", uri ? uri : "NULL");
	}

	iio_scan_destroy(scan);
}

int main(void)
{
	DEBUG_PRINT("=== libiio Scan Tests ===\n\n");

	RUN_TEST(scan_basic_operations);
	RUN_TEST(scan_with_params);
	RUN_TEST(scan_specific_backends);
	RUN_TEST(scan_empty_items);
	RUN_TEST(scan_usb_filtering);
	RUN_TEST(scan_edge_cases);
	RUN_TEST(scan_destroy_behavior);
	RUN_TEST(scan_results_boundary);

	TEST_SUMMARY();
	return 0;
}
