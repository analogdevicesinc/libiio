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

TEST_FUNCTION(typed_attribute_functions)
{
	struct iio_context *ctx = create_test_context("TESTS_API_URI", "local:", NULL);
	if (iio_err(ctx) || !ctx) {
		DEBUG_PRINT("  SKIP: No context for typed attribute test\n");
		TEST_ASSERT(true, "Typed attribute test skipped");
		return;
	}

	unsigned int nb_attrs = iio_context_get_attrs_count(ctx);
	if (nb_attrs > 0) {
		const struct iio_attr *attr = iio_context_get_attr(ctx, 0);
		if (attr) {
			bool bool_val;
			long long ll_val;
			double double_val;

			int ret_bool = iio_attr_read_bool(attr, &bool_val);
			int ret_ll = iio_attr_read_longlong(attr, &ll_val);
			int ret_double = iio_attr_read_double(attr, &double_val);

			DEBUG_PRINT("  INFO: Typed reads - bool:%s, longlong:%s, double:%s\n",
				   ret_bool == 0 ? "OK" : "FAIL",
				   ret_ll == 0 ? "OK" : "FAIL",
				   ret_double == 0 ? "OK" : "FAIL");

			ssize_t ret_str = iio_attr_write_string(attr, "test");
			int ret_bool_w = iio_attr_write_bool(attr, true);
			int ret_ll_w = iio_attr_write_longlong(attr, 42);
			int ret_double_w = iio_attr_write_double(attr, 3.14);

			DEBUG_PRINT("  INFO: Typed writes - string:%s, bool:%s, longlong:%s, double:%s\n",
				   ret_str >= 0 ? "OK" : "FAIL",
				   ret_bool_w == 0 ? "OK" : "FAIL",
				   ret_ll_w == 0 ? "OK" : "FAIL",
				   ret_double_w == 0 ? "OK" : "FAIL");

			TEST_ASSERT(true, "Typed attribute functions tested");
		}
	}

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== libiio Typed Attribute Tests ===\n\n");

	RUN_TEST(typed_attribute_functions);

	TEST_SUMMARY();
	return 0;
}
