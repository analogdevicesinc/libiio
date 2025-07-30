/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include "test_framework.h"
#include <iio/iio.h>
#include <errno.h>

TEST_FUNCTION(iio_ptr_encoding)
{
	void *ptr;
	
	ptr = iio_ptr(-EINVAL);
	TEST_ASSERT(ptr != NULL, "iio_ptr should return non-NULL for negative errno");
	
	ptr = iio_ptr(-ENODEV);
	TEST_ASSERT(ptr != NULL, "iio_ptr should return non-NULL for -ENODEV");
	
	ptr = iio_ptr(-1);
	TEST_ASSERT(ptr != NULL, "iio_ptr should return non-NULL for -1");
	
	ptr = iio_ptr(-4095);
	TEST_ASSERT(ptr != NULL, "iio_ptr should return non-NULL for -4095 (max errno)");
}

TEST_FUNCTION(iio_err_decoding)
{
	void *ptr;
	int err;
	
	ptr = iio_ptr(-EINVAL);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, -EINVAL, "iio_err should decode -EINVAL correctly");
	
	ptr = iio_ptr(-ENODEV);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, -ENODEV, "iio_err should decode -ENODEV correctly");
	
	ptr = iio_ptr(-1);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, -1, "iio_err should decode -1 correctly");
	
	ptr = iio_ptr(-4095);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, -4095, "iio_err should decode max errno correctly");
	
	char valid_ptr[] = "test";
	err = iio_err(valid_ptr);
	TEST_ASSERT_EQ(err, 0, "iio_err should return 0 for valid pointer");
	
	err = iio_err(NULL);
	TEST_ASSERT_EQ(err, 0, "iio_err should return 0 for NULL pointer");
}

TEST_FUNCTION(iio_err_cast)
{
	void *original_ptr, *cast_ptr;
	
	original_ptr = iio_ptr(-EINVAL);
	cast_ptr = iio_err_cast(original_ptr);
	
	TEST_ASSERT(cast_ptr == original_ptr, "iio_err_cast should return same pointer");
	TEST_ASSERT_EQ(iio_err(cast_ptr), -EINVAL, "Cast pointer should decode to same error");
}

TEST_FUNCTION(error_roundtrip)
{
	int original_errors[] = {-EINVAL, -ENODEV, -ENOMEM, -EBUSY, -EPERM, -1, -4095};
	size_t num_errors = sizeof(original_errors) / sizeof(original_errors[0]);
	
	for (size_t i = 0; i < num_errors; i++) {
		void *ptr = iio_ptr(original_errors[i]);
		int decoded = iio_err(ptr);
		TEST_ASSERT_EQ(decoded, original_errors[i], "Error roundtrip should preserve error code");
	}
}

TEST_FUNCTION(error_range_limits)
{
	void *ptr;
	int err;
	
	ptr = iio_ptr(-4096);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, 0, "iio_err should return 0 for errno outside valid range");
	
	ptr = iio_ptr(0);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, 0, "iio_err should return 0 for positive zero value");
	
	ptr = iio_ptr(1);
	err = iio_err(ptr);
	TEST_ASSERT_EQ(err, 0, "iio_err should return 0 for positive value");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Error Handling Tests ===\n\n");
	
	RUN_TEST(iio_ptr_encoding);
	RUN_TEST(iio_err_decoding);
	RUN_TEST(iio_err_cast);
	RUN_TEST(error_roundtrip);
	RUN_TEST(error_range_limits);
	
	TEST_SUMMARY();
	return 0;
}