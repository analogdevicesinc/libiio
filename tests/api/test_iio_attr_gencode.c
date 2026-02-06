/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include "test_framework.h"
#include "test_helpers.h"
#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

static const char* get_test_uri(void)
{
	if (!test_ctx) return "local:";
	const struct iio_attr *uri_attr = iio_context_find_attr(test_ctx, "uri");
	return uri_attr ? iio_attr_get_static_value(uri_attr) : "local:";
}

static void cleanup_test_context(void)
{
	if (test_ctx && !iio_err(test_ctx)) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

static void cleanup_generated_files(void)
{
	system("rm -rf generated_files/");
}

static int create_generated_files_dir(void)
{
	struct stat st = {0};
	if (stat("generated_files", &st) == -1) {
		if (mkdir("generated_files", 0755) != 0) {
			DEBUG_PRINT("  ERROR: Failed to create directory 'generated_files'\n");
			return -1;
		}
	}
	return 0;
}

static void generate_code(const char *test_name, const char *iio_args, const char *file_base)
{
	if (create_generated_files_dir() != 0) {
		return;
	}

	const char *uri = get_test_uri();
	char cmd[512];

	/* Test C generation */
	snprintf(cmd, sizeof(cmd), "iio_attr -u %s %s -g generated_files/%s.c", uri, iio_args, file_base);

	int c_ret = system(cmd);
	TEST_ASSERT_EQ(c_ret, 0, "C generation should succeed");
	if (c_ret != 0) {
		DEBUG_PRINT("  ERROR: Failed to generate C code for %s\n", test_name);
		return;
	} else {
		DEBUG_PRINT("  INFO: Successfully generated C code for %s at generated_files/%s.c\n", test_name, file_base);
	}

	/* Test Python generation */
	snprintf(cmd, sizeof(cmd), "iio_attr -u %s %s -g generated_files/%s.py", uri, iio_args, file_base);
	int py_ret = system(cmd);
	TEST_ASSERT_EQ(py_ret, 0, "Python generation should succeed");
	if (py_ret != 0) {
		DEBUG_PRINT("  ERROR: Failed to generate Python code for %s\n", test_name);
		return;
	} else {
		DEBUG_PRINT("  INFO: Successfully generated Python code for %s at generated_files/%s.py\n", test_name, file_base);
	}
}

static void build_and_run_code(const char *test_name, const char *file_base)
{
	char cmd[512];

	/* Test C code building */
	snprintf(cmd, sizeof(cmd), "gcc -o generated_files/%s_test generated_files/%s.c -liio", file_base, file_base);
	int c_ret = system(cmd);
	TEST_ASSERT_EQ(c_ret, 0, "Generated C code should compile");
	if (c_ret != 0) {
		DEBUG_PRINT("  ERROR: Failed to compile generated C code for %s\n", test_name);

	} else {
		DEBUG_PRINT("  INFO: Successfully compiled generated C code for %s\n", test_name);
		struct stat exe_stat;

		snprintf(cmd, sizeof(cmd), "./generated_files/%s_test", file_base);
		int run_ret = system(cmd);
		TEST_ASSERT_EQ(run_ret, 0, "Generated C test should run successfully");
	}
}

TEST_FUNCTION(iio_attr_code_generation)
{
	setup_test_context();
	if ( !test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	bool found_context = false, found_device = false, found_channel = false, found_debug = false;
	char args[256];

	unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
	unsigned int nb_ctx_attrs = iio_context_get_attrs_count(test_ctx);

	/* Test context attributes */
	if (nb_ctx_attrs > 0 && !found_context) {
		const struct iio_attr *attr = iio_context_get_attr(test_ctx, 0);
		if (attr) {
			const char *attr_name = iio_attr_get_name(attr);
			if (attr_name) {
				snprintf(args, sizeof(args), "-C %s", attr_name);
				DEBUG_PRINT("  INFO: Found context attribute: %s\n", attr_name);
				found_context = true;
				generate_code("context", args, "test_context");
				build_and_run_code("context", "test_context");
			}
		}
	}

	for (unsigned int i = 0; i < nb_devices &&
		!(found_context && found_device && found_channel && found_debug); i++) {
		struct iio_device *dev = iio_context_get_device(test_ctx, i);
		TEST_ASSERT_PTR_NOT_NULL(dev, "Device should exist");
		const char *dev_name = iio_device_get_name(dev);
		if (!dev_name) dev_name = iio_device_get_id(dev);
		if (!dev_name) continue;

		/* Test device attributes */
		unsigned int nb_device_attr = iio_device_get_attrs_count(dev);
		if (nb_device_attr > 0 && !found_device) {
			const struct iio_attr *attr = iio_device_get_attr(dev, 0);
			if (attr) {
				const char *attr_name = iio_attr_get_name(attr);
				if (attr_name) {
					DEBUG_PRINT("  INFO: Found device attributegot: Device:%s, Attr:%s\n", dev_name, attr_name);
					snprintf(args, sizeof(args), "-d %s %s", dev_name, attr_name);
					found_device = true;
					generate_code("device", args, "test_device");
					build_and_run_code("device", "test_device");
				}
			}
		}
		/* Test debug attributes */
		unsigned int nb_debug_attr = iio_device_get_debug_attrs_count(dev);
		if (nb_debug_attr > 0 && !found_debug) {
			const struct iio_attr *attr = iio_device_get_debug_attr(dev, 0);
			if (attr) {
				const char *attr_name = iio_attr_get_name(attr);
				if (attr_name) {
					DEBUG_PRINT("  INFO: Found debug attribute: Device:%s, Attr:%s\n", dev_name, attr_name);
					snprintf(args, sizeof(args), "-D %s %s", dev_name, attr_name);
					found_debug = true;
					generate_code("debug", args, "test_debug");
					build_and_run_code("debug", "test_debug");
				}
			}
		}

		/* Test channel attributes */
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		if (nb_channels > 0 && !found_channel) {
			for (unsigned int j = 0; j < nb_channels; j++) {
				struct iio_channel *ch = iio_device_get_channel(dev, j);
				TEST_ASSERT_PTR_NOT_NULL(ch, "Channel should exist");
				unsigned int nb_channels_attr = iio_channel_get_attrs_count(ch);

				if (nb_channels_attr > 0) {
					const char *ch_name = iio_channel_get_name(ch);
					if (!ch_name) ch_name = iio_channel_get_id(ch);
					if (!ch_name) continue;

					const struct iio_attr *attr = iio_channel_get_attr(ch, 0);
					if (attr) {
						const char *attr_name = iio_attr_get_name(attr);
						if (attr_name) {
							DEBUG_PRINT("  INFO: Found channel attribute: Device:%s, Channel:%s, Attr:%s\n", dev_name, ch_name, attr_name);
							snprintf(args, sizeof(args), "-c %s %s %s", dev_name, ch_name, attr_name);
							found_channel = true;
							generate_code("channel", args, "test_channel");
							build_and_run_code("channel", "test_channel");
							break;
						}
					}
				}
			}
		}
	}

	if (!found_context || !found_device || !found_channel || !found_debug) {
		DEBUG_PRINT("  SKIP: Some attribute types not found - gencode not fully tested\n");
	}
}

int main(void)
{
	RUN_TEST(iio_attr_code_generation);
	cleanup_test_context();
	cleanup_generated_files();
	TEST_SUMMARY();
	return 0;
}
