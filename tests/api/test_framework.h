/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;

#ifdef TESTS_DEBUG
#define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) do {} while (0)
#endif

#define TEST_ASSERT(condition, message) \
	do { \
		test_count++; \
		if (condition) { \
			test_passed++; \
			DEBUG_PRINT("  PASS: %s\n", message); \
		} else { \
			test_failed++; \
			DEBUG_PRINT("  FAIL: %s\n", message); \
		} \
	} while (0)

#define TEST_ASSERT_EQ(actual, expected, message) \
	do { \
		test_count++; \
		if ((actual) == (expected)) { \
			test_passed++; \
			DEBUG_PRINT("  PASS: %s (got %ld, expected %ld)\n", message, (long)(actual), (long)(expected)); \
		} else { \
			test_failed++; \
			DEBUG_PRINT("  FAIL: %s (got %ld, expected %ld)\n", message, (long)(actual), (long)(expected)); \
		} \
	} while (0)

#define TEST_ASSERT_PTR_NOT_NULL(ptr, message) \
	do { \
		test_count++; \
		if ((ptr) != NULL) { \
			test_passed++; \
			DEBUG_PRINT("  PASS: %s (ptr is not NULL)\n", message); \
		} else { \
			test_failed++; \
			DEBUG_PRINT("  FAIL: %s (ptr is NULL)\n", message); \
		} \
	} while (0)

#define TEST_ASSERT_PTR_NULL(ptr, message) \
	do { \
		test_count++; \
		if ((ptr) == NULL) { \
			test_passed++; \
			DEBUG_PRINT("  PASS: %s (ptr is NULL)\n", message); \
		} else { \
			test_failed++; \
			DEBUG_PRINT("  FAIL: %s (ptr is not NULL)\n", message); \
		} \
	} while (0)

#define TEST_ASSERT_STR_EQ(actual, expected, message) \
	do { \
		test_count++; \
		if (strcmp((actual), (expected)) == 0) { \
			test_passed++; \
			DEBUG_PRINT("  PASS: %s (got '%s', expected '%s')\n", message, (actual), (expected)); \
		} else { \
			test_failed++; \
			DEBUG_PRINT("  FAIL: %s (got '%s', expected '%s')\n", message, (actual), (expected)); \
		} \
	} while (0)

#define TEST_FUNCTION(name) \
	static void test_##name(void); \
	static void test_##name(void)

#define RUN_TEST(name) \
	do { \
		DEBUG_PRINT("Running test: %s\n", #name); \
		test_##name(); \
		DEBUG_PRINT("\n"); \
	} while (0)

#define TEST_SUMMARY() \
	do { \
		DEBUG_PRINT("=== TEST SUMMARY ===\n"); \
		DEBUG_PRINT("Total tests: %d\n", test_count); \
		DEBUG_PRINT("Passed: %d\n", test_passed); \
		DEBUG_PRINT("Failed: %d\n", test_failed); \
		DEBUG_PRINT("Success rate: %.1f%%\n", test_count > 0 ? (100.0 * test_passed / test_count) : 0.0); \
		if (test_failed > 0) { \
			exit(EXIT_FAILURE); \
		} else { \
			exit(EXIT_SUCCESS); \
		} \
	} while (0)

#endif /* TEST_FRAMEWORK_H */
