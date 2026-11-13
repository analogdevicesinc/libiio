// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for the receive buffer that the backend asks for a socket engine
 * queue's socket by default (dq_udp_rcvbuf_target(), daqiri-udp-stream.c):
 * the bytes of the queue's packet buffers, num_bufs * buf_size, at least
 * 4 MiB and at most 64 MiB. A product that does not fit 64 bits is the cap.
 */

#include <stdint.h>

#include "daqiri-udp-stream.h"
#include "test_framework.h"

#define MIB ((uint64_t)1 << 20)

TEST_FUNCTION(limits)
{
	TEST_ASSERT_EQ(DQ_UDP_RCVBUF_MIN, 4 * MIB, "at least 4 MiB");
	TEST_ASSERT_EQ(DQ_UDP_RCVBUF_MAX, 64 * MIB, "at most 64 MiB");
}

TEST_FUNCTION(floor)
{
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(0, 0), 4 * MIB, "no buffers: 4 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(256, 2048), 4 * MIB, "256 x 2048 (512 KiB): 4 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(64, 9216), 4 * MIB, "64 x 9216 (576 KiB): 4 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(512, 8192), 4 * MIB, "512 x 8192: exactly 4 MiB");
}

TEST_FUNCTION(product)
{
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(4097, 1024), 4097 * 1024, "4 MiB + 1 KiB: as is");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(4096, 9216), 4096 * 9216,
			"4096 x 9216 (36 MiB): as is");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(8192, 8192), 64 * MIB, "8192 x 8192: exactly 64 MiB");
}

TEST_FUNCTION(cap)
{
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(8193, 8192), 64 * MIB, "64 MiB + 8 KiB: 64 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(16384, 9216), 64 * MIB, "16384 x 9216: 64 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(UINT64_MAX, 2), 64 * MIB, "overflow: 64 MiB");
	TEST_ASSERT_EQ(dq_udp_rcvbuf_target(1ULL << 32, 1ULL << 32), 64 * MIB, "2^64: 64 MiB");
}

int main(void)
{
	RUN_TEST(limits);
	RUN_TEST(floor);
	RUN_TEST(product);
	RUN_TEST(cap);
	TEST_SUMMARY();
}
