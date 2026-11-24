// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for the 64-bit sequence tracker of the UDP sample mode receive
 * side (daqiri-udp-stream.c): datagrams in order, gaps, a gap too large to
 * zero-fill, late and duplicate datagrams, other streams, other versions and
 * lengths, and the restart of the sequence after a new setup. Datagrams are
 * built in memory with the header of iiod-udp-stream.h.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daqiri-udp-stream.h"
#include "test_framework.h"

#define STREAM_ID 0x5eed0001u
#define OTHER_ID 0x5eed0002u
#define PAYLOAD 64u
#define DGRAM_LEN (IIOD_UDP_STREAM_HDR_LEN + PAYLOAD)

static struct dq_udp_rx rx;
static struct dq_udp_data out;
static uint8_t dgram[DGRAM_LEN + 16];

/* A datagram of <stream_id> with sequence number <seq>; its payload bytes
 * all hold the low byte of <seq>. Returns its length. */
static size_t mk(uint32_t stream_id, uint64_t seq)
{
	const struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.payload_length = PAYLOAD,
		.stream_id = stream_id,
		.sequence = seq,
	};

	iiod_udp_stream_hdr_pack(&hdr, dgram);
	memset(dgram + IIOD_UDP_STREAM_HDR_LEN, (int)(seq & 0xff), PAYLOAD);

	return DGRAM_LEN;
}

static int feed(uint64_t seq)
{
	return dq_udp_rx_datagram(&rx, dgram, mk(STREAM_ID, seq), &out);
}

static void start(void)
{
	dq_udp_rx_init(&rx);
	dq_udp_rx_start(&rx, STREAM_ID, PAYLOAD);
}

/* <ret> is what the last feed returned */
static void expect_data(int ret, uint64_t seq, uint64_t gap, const char *what)
{
	char msg[160];

	snprintf(msg, sizeof(msg), "%s: accepted", what);
	TEST_ASSERT_EQ(ret, DQ_UDP_DATA, msg);
	if (ret != DQ_UDP_DATA)
		return;

	snprintf(msg, sizeof(msg), "%s: sequence %llu, gap %llu", what, (unsigned long long)seq,
			(unsigned long long)gap);
	TEST_ASSERT(out.sequence == seq && out.gap == gap, msg);
	snprintf(msg, sizeof(msg), "%s: payload", what);
	TEST_ASSERT(out.payload == dgram + IIOD_UDP_STREAM_HDR_LEN && out.len == PAYLOAD &&
					out.payload[0] == (uint8_t)(seq & 0xff),
			msg);
	snprintf(msg, sizeof(msg), "%s: resync only past the fill cap", what);
	TEST_ASSERT(out.resync == (gap > DQ_UDP_MAX_FILL), msg);
}

TEST_FUNCTION(in_order)
{
	uint64_t s;
	int ret;

	start();
	for (s = 0; s < 10; s++) {
		ret = feed(s);
		expect_data(ret, s, 0, "in order");
	}

	TEST_ASSERT_EQ(rx.c.packets, 10, "10 datagrams");
	TEST_ASSERT_EQ(rx.c.bytes, 10 * PAYLOAD, "their payload bytes");
	TEST_ASSERT_EQ(rx.c.lost_packets + rx.c.gap_events + rx.c.late + rx.c.duplicates +
					rx.c.foreign + rx.c.malformed,
			0, "nothing else counted");
	TEST_ASSERT_EQ(rx.expected, 10, "11th expected next");
}

TEST_FUNCTION(gap_of_one)
{
	int ret;

	start();
	feed(0);
	feed(1);
	ret = feed(3);
	expect_data(ret, 3, 1, "2 lost");
	TEST_ASSERT_EQ(rx.c.lost_packets, 1, "1 lost");
	TEST_ASSERT_EQ(rx.c.gap_events, 1, "1 gap");
	TEST_ASSERT_EQ(rx.c.packets, 3, "3 accepted");
}

TEST_FUNCTION(gap_of_1000)
{
	int ret;

	start();
	feed(0);
	ret = feed(1001);
	expect_data(ret, 1001, 1000, "1000 lost");
	TEST_ASSERT_EQ(rx.c.lost_packets, 1000, "1000 lost");
	TEST_ASSERT_EQ(rx.c.gap_events, 1, "one gap");

	ret = feed(1002);
	expect_data(ret, 1002, 0, "the stream goes on");
}

/* The fill cap: 1024 lost datagrams can still be zero-filled, 1025 not */
TEST_FUNCTION(gap_past_fill_cap)
{
	int ret;

	start();
	feed(0);
	ret = feed(1 + DQ_UDP_MAX_FILL);
	expect_data(ret, 1 + DQ_UDP_MAX_FILL, DQ_UDP_MAX_FILL, "1024 lost");
	TEST_ASSERT(!out.resync, "a gap of 1024 is filled");

	ret = feed(2 + 2 * DQ_UDP_MAX_FILL + 1);
	expect_data(ret, 2 + 2 * DQ_UDP_MAX_FILL + 1, DQ_UDP_MAX_FILL + 1, "1025 lost");
	TEST_ASSERT(out.resync, "a gap of 1025 is a resync (-EIO, even with lossy=1)");
	TEST_ASSERT_EQ(rx.c.lost_packets, 2 * DQ_UDP_MAX_FILL + 1, "both gaps counted lost");
	TEST_ASSERT_EQ(rx.c.gap_events, 2, "2 gaps");
}

/* Datagrams before the first one that arrives are lost too */
TEST_FUNCTION(first_not_zero)
{
	int ret;

	start();
	ret = feed(5);
	expect_data(ret, 5, 5, "0 to 4 lost");
	TEST_ASSERT_EQ(rx.c.lost_packets, 5, "5 lost");
}

TEST_FUNCTION(late)
{
	int ret;

	start();
	feed(0);
	feed(1);
	feed(3);
	ret = feed(2);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "2 after 3: dropped");
	TEST_ASSERT_EQ(rx.c.late, 1, "counted late");
	TEST_ASSERT_EQ(rx.c.lost_packets, 0, "it arrived after all: the loss is taken back");
	TEST_ASSERT_EQ(rx.c.gap_events, 1, "the gap stays counted");
	TEST_ASSERT_EQ(rx.c.packets, 3, "not delivered");
	TEST_ASSERT_EQ(rx.expected, 4, "4 still expected next");

	/* A second copy of the late datagram is a duplicate, not another
	 * loss taken back */
	ret = feed(2);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "2 again: dropped");
	TEST_ASSERT_EQ(rx.c.duplicates, 1, "counted duplicate");
	TEST_ASSERT_EQ(rx.c.late, 1, "not late again");

	ret = feed(4);
	expect_data(ret, 4, 0, "the stream goes on");
}

/* A late datagram never takes the loss count below 0 */
TEST_FUNCTION(late_floor)
{
	start();
	feed(0);
	feed(2);
	feed(1);
	TEST_ASSERT_EQ(rx.c.lost_packets, 0, "1 lost, then back");

	/* Older than the window: whether it arrived is not known any more.
	 * It counts as late. */
	start();
	feed(0);
	feed(1 + 2 * DQ_UDP_WINDOW);
	TEST_ASSERT_EQ(rx.c.lost_packets, 2 * DQ_UDP_WINDOW, "2048 lost");
	feed(0);
	TEST_ASSERT_EQ(rx.c.late, 1, "0, outside the window: late");
	TEST_ASSERT_EQ(rx.c.duplicates, 0, "not a duplicate");
	TEST_ASSERT_EQ(rx.c.lost_packets, 2 * DQ_UDP_WINDOW - 1, "one loss taken back");
}

/* Inside the window, after a gap: late if it never came, else a duplicate */
TEST_FUNCTION(window)
{
	int ret;

	start();
	feed(0);
	feed(10);
	feed(1 + DQ_UDP_WINDOW);

	/* 9 was lost in the first gap: 1017 below the expected 1026, inside
	 * the window */
	ret = feed(9);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "9: dropped");
	TEST_ASSERT_EQ(rx.c.late, 1, "9: late");

	/* 10 did arrive */
	ret = feed(10);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "10 again: dropped");
	TEST_ASSERT_EQ(rx.c.duplicates, 1, "10 again: duplicate");
	TEST_ASSERT_EQ(rx.c.late, 1, "10 again: not late");
}

TEST_FUNCTION(duplicate)
{
	int ret;

	start();
	feed(0);
	feed(1);
	ret = feed(1);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "1 twice: dropped");
	TEST_ASSERT_EQ(rx.c.duplicates, 1, "the last one again: duplicate");

	feed(2);
	ret = feed(0);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "0 again: dropped");
	TEST_ASSERT_EQ(rx.c.duplicates, 2, "an older one again: duplicate");
	TEST_ASSERT_EQ(rx.c.late, 0, "nothing late");
	TEST_ASSERT_EQ(rx.c.lost_packets, 0, "nothing lost");
	TEST_ASSERT_EQ(rx.c.packets, 3, "3 accepted");

	ret = feed(3);
	expect_data(ret, 3, 0, "the stream goes on");
}

TEST_FUNCTION(foreign)
{
	int ret;

	start();
	feed(0);
	ret = dq_udp_rx_datagram(&rx, dgram, mk(OTHER_ID, 1), &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "another stream ID: dropped");
	TEST_ASSERT_EQ(rx.c.foreign, 1, "counted foreign");
	TEST_ASSERT_EQ(rx.expected, 1, "no sequence state");

	ret = feed(1);
	expect_data(ret, 1, 0, "the stream goes on");

	/* No stream programmed yet: nothing is this stream's */
	dq_udp_rx_init(&rx);
	ret = feed(0);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "no stream: dropped");
	TEST_ASSERT_EQ(rx.c.foreign, 1, "no stream: foreign");
}

TEST_FUNCTION(version)
{
	int ret;

	start();
	feed(0);
	mk(STREAM_ID, 1);
	dgram[0] = 2;
	ret = dq_udp_rx_datagram(&rx, dgram, DGRAM_LEN, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "version 2: dropped");
	TEST_ASSERT_EQ(rx.c.malformed, 1, "version 2: malformed");
	TEST_ASSERT_EQ(rx.expected, 1, "no sequence state");

	/* The one it was not: lost */
	ret = feed(2);
	expect_data(ret, 2, 1, "the next one reports the skipped one lost");
}

TEST_FUNCTION(length)
{
	struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.payload_length = PAYLOAD - 16,
		.stream_id = STREAM_ID,
		.sequence = 1,
	};
	int ret;

	start();
	feed(0);

	/* payload_length is not the stream's payload size, though consistent */
	iiod_udp_stream_hdr_pack(&hdr, dgram);
	ret = dq_udp_rx_datagram(&rx, dgram, DGRAM_LEN - 16, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "other payload_length: dropped");
	TEST_ASSERT_EQ(rx.c.malformed, 1, "other payload_length: malformed");

	/* The right payload_length, a datagram longer or shorter than that */
	mk(STREAM_ID, 1);
	ret = dq_udp_rx_datagram(&rx, dgram, DGRAM_LEN + 16, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "16 bytes too long: dropped");
	ret = dq_udp_rx_datagram(&rx, dgram, DGRAM_LEN - 1, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "1 byte too short: dropped");
	ret = dq_udp_rx_datagram(&rx, dgram, IIOD_UDP_STREAM_HDR_LEN - 1, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "shorter than a header: dropped");
	TEST_ASSERT_EQ(rx.c.malformed, 4, "all malformed");
	TEST_ASSERT_EQ(rx.c.packets, 1, "none accepted");
	TEST_ASSERT_EQ(rx.expected, 1, "no sequence state");
}

/* Reserved for later: not checked */
TEST_FUNCTION(flags)
{
	int ret;

	start();
	mk(STREAM_ID, 0);
	dgram[1] = 0xff;
	ret = dq_udp_rx_datagram(&rx, dgram, DGRAM_LEN, &out);
	expect_data(ret, 0, 0, "flags 0xff");
}

/* Each enable has a setup of its own: the sequence starts over at 0, under a
 * new stream ID, and the counters go on */
TEST_FUNCTION(restart)
{
	int ret;

	start();
	feed(0);
	feed(1);
	feed(3);

	dq_udp_rx_start(&rx, STREAM_ID, PAYLOAD);
	ret = feed(0);
	expect_data(ret, 0, 0, "0 after the restart: not a duplicate");
	ret = feed(1);
	expect_data(ret, 1, 0, "1 after the restart");
	TEST_ASSERT_EQ(rx.c.packets, 5, "counters kept: 5 accepted");
	TEST_ASSERT_EQ(rx.c.lost_packets, 1, "counters kept: 1 lost");
	TEST_ASSERT_EQ(rx.c.duplicates, 0, "no duplicate");

	/* And a new stream ID: the old one is foreign now */
	dq_udp_rx_start(&rx, OTHER_ID, PAYLOAD);
	ret = feed(2);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "the previous stream ID: dropped");
	TEST_ASSERT_EQ(rx.c.foreign, 1, "the previous stream ID: foreign");
	ret = dq_udp_rx_datagram(&rx, dgram, mk(OTHER_ID, 0), &out);
	expect_data(ret, 0, 0, "the new stream ID from 0");
}

int main(void)
{
	RUN_TEST(in_order);
	RUN_TEST(gap_of_one);
	RUN_TEST(gap_of_1000);
	RUN_TEST(gap_past_fill_cap);
	RUN_TEST(first_not_zero);
	RUN_TEST(late);
	RUN_TEST(late_floor);
	RUN_TEST(window);
	RUN_TEST(duplicate);
	RUN_TEST(foreign);
	RUN_TEST(version);
	RUN_TEST(length);
	RUN_TEST(flags);
	RUN_TEST(restart);
	TEST_SUMMARY();
}
