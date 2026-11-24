// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for the end of the drain after DISABLE_BUFFER
 * (dq_udp_drain_over(), daqiri-udp-stream.c): over once the queue has been
 * quiet for 20 ms since the answer or its last burst, whichever came later,
 * and 500 ms after the answer at most. A burst stored after the drain read
 * the clock is later than "now": news, not 2^64 ns of quiet. Times are made
 * up, so the table holds however the host schedules threads.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "daqiri-udp-stream.h"
#include "test_framework.h"

#define MS 1000000ULL
/* The answer to DISABLE_BUFFER, some time after boot */
#define T0 (5000 * MS)

#define ON false
#define OVER true

static void expect(uint64_t start, uint64_t last, uint64_t now, bool over, const char *what)
{
	TEST_ASSERT(dq_udp_drain_over(start, last, now) == over, what);
}

TEST_FUNCTION(quiet_since_answer)
{
	expect(T0, 0, T0, ON, "no burst, at the answer: on");
	expect(T0, 0, T0 + 19 * MS, ON, "no burst, 19 ms after the answer: on");
	expect(T0, 0, T0 + 20 * MS, OVER, "no burst, 20 ms after the answer: over");
}

TEST_FUNCTION(burst_before_answer)
{
	/* The stream's bursts before DISABLE, or an earlier enable's */
	expect(T0, T0 - 50 * MS, T0 + 19 * MS, ON,
			"last burst 50 ms before the answer, 19 ms after it: on");
	expect(T0, T0 - 50 * MS, T0 + 20 * MS, OVER,
			"last burst 50 ms before the answer, 20 ms after it: over");
}

TEST_FUNCTION(quiet_since_burst)
{
	expect(T0, T0 + 10 * MS, T0 + 20 * MS, ON,
			"burst 10 ms after the answer, 20 ms after it: on");
	expect(T0, T0 + 10 * MS, T0 + 29 * MS, ON, "burst 10 ms after the answer, 19 ms quiet: on");
	expect(T0, T0 + 10 * MS, T0 + 30 * MS, OVER,
			"burst 10 ms after the answer, 20 ms quiet: over");
	expect(T0, T0 + 300 * MS, T0 + 319 * MS, ON, "burst at 300 ms, 19 ms quiet: on");
	expect(T0, T0 + 300 * MS, T0 + 320 * MS, OVER, "burst at 300 ms, 20 ms quiet: over");
}

TEST_FUNCTION(cap)
{
	expect(T0, T0 + 480 * MS, T0 + 499 * MS, ON,
			"burst at 480 ms, 499 ms after the answer: on");
	expect(T0, T0 + 490 * MS, T0 + 500 * MS, OVER,
			"burst at 490 ms, 500 ms after the answer: over, 10 ms quiet or not");
	expect(T0, T0 + 500 * MS - 1, T0 + 500 * MS, OVER,
			"burst 1 ns before 500 ms, at 500 ms: over");
}

TEST_FUNCTION(burst_after_clock_read)
{
	/* The poller stored its burst between the drain's read of the clock
	 * and its read of the burst time */
	expect(T0, T0 + 5 * MS + 1, T0 + 5 * MS, ON, "burst 1 ns after now, 5 ms in: on");
	expect(T0, T0 + 100 * MS + 1000, T0 + 100 * MS, ON, "burst 1 us after now, 100 ms in: on");
	expect(T0, T0 + 1, T0, ON, "burst 1 ns after now, at the answer: on");
	expect(T0, T0 + 500 * MS + 1, T0 + 500 * MS, OVER, "burst 1 ns after now, at 500 ms: over");
}

int main(void)
{
	RUN_TEST(quiet_since_answer);
	RUN_TEST(burst_before_answer);
	RUN_TEST(quiet_since_burst);
	RUN_TEST(cap);
	RUN_TEST(burst_after_clock_read);
	TEST_SUMMARY();
}
