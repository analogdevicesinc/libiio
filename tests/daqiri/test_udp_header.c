// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for the datagram header checks of the UDP sample mode receive
 * side (daqiri-udp-stream.c) against the udp-*.bin golden vectors generated
 * by gen_golden.py: what iiod_udp_stream_hdr_unpack() makes of each, and
 * whether a stream programmed with DQ_UDP_GOLDEN_STREAM_ID and
 * DQ_UDP_GOLDEN_PAYLOAD_BYTES accepts it, or counts it malformed or foreign.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daqiri-udp-stream.h"
#include "test_framework.h"

enum dq_udp_expect {
	DQ_UDP_EXPECT_DATA,
	DQ_UDP_EXPECT_MALFORMED,
	DQ_UDP_EXPECT_FOREIGN,
};

struct dq_udp_golden {
	const char *name;
	int unpack;
	uint8_t version, flags;
	uint16_t payload_length;
	uint32_t stream_id;
	uint64_t sequence;
	size_t len;
	enum dq_udp_expect expect;
};

#define DQ_UDP_GOLDEN
#include "../resources/daqiri/expected.h"

#define N_GOLDEN (sizeof(udp_golden) / sizeof(udp_golden[0]))

/* Returns a malloc'd buffer of exactly the file size (never zero-sized). */
static uint8_t *load(const char *name, size_t *len)
{
	char path[1024];
	uint8_t *buf;
	long sz;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", TESTS_DAQIRI_DIR, name);
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(sz ? sz : 1);
	if (buf && fread(buf, 1, sz, f) != (size_t)sz) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	*len = sz;
	return buf;
}

static void check_unpack(const struct dq_udp_golden *g, const uint8_t *buf, size_t len)
{
	struct iiod_udp_stream_hdr hdr;
	char msg[128];
	int ret;

	memset(&hdr, 0, sizeof(hdr));
	ret = iiod_udp_stream_hdr_unpack(buf, len, &hdr);
	snprintf(msg, sizeof(msg), "%s: unpack", g->name);
	TEST_ASSERT_EQ(ret, g->unpack, msg);
	if (ret)
		return;

	snprintf(msg, sizeof(msg), "%s: header fields", g->name);
	TEST_ASSERT(hdr.version == g->version && hdr.flags == g->flags &&
					hdr.payload_length == g->payload_length &&
					hdr.stream_id == g->stream_id &&
					hdr.sequence == g->sequence,
			msg);
}

static void check_rx(const struct dq_udp_golden *g, const uint8_t *buf, size_t len)
{
	static struct dq_udp_rx rx;
	struct dq_udp_data out, sentinel;
	char msg[128];
	int ret;

	dq_udp_rx_init(&rx);
	dq_udp_rx_start(&rx, DQ_UDP_GOLDEN_STREAM_ID, DQ_UDP_GOLDEN_PAYLOAD_BYTES);

	memset(&sentinel, 0x5a, sizeof(sentinel));
	out = sentinel;
	ret = dq_udp_rx_datagram(&rx, buf, len, &out);

	switch (g->expect) {
	case DQ_UDP_EXPECT_DATA:
		snprintf(msg, sizeof(msg), "%s: accepted", g->name);
		TEST_ASSERT_EQ(ret, DQ_UDP_DATA, msg);
		snprintf(msg, sizeof(msg), "%s: payload right after the header", g->name);
		TEST_ASSERT(out.payload == buf + IIOD_UDP_STREAM_HDR_LEN &&
						out.len == DQ_UDP_GOLDEN_PAYLOAD_BYTES,
				msg);
		snprintf(msg, sizeof(msg), "%s: sequence number", g->name);
		TEST_ASSERT(out.sequence == g->sequence, msg);
		snprintf(msg, sizeof(msg), "%s: counted as a datagram", g->name);
		TEST_ASSERT(rx.c.packets == 1 && rx.c.bytes == DQ_UDP_GOLDEN_PAYLOAD_BYTES &&
						!rx.c.malformed && !rx.c.foreign,
				msg);
		break;
	case DQ_UDP_EXPECT_MALFORMED:
		snprintf(msg, sizeof(msg), "%s: skipped", g->name);
		TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, msg);
		snprintf(msg, sizeof(msg), "%s: counted malformed, nothing else", g->name);
		TEST_ASSERT(rx.c.malformed == 1 && !rx.c.foreign && !rx.c.packets, msg);
		break;
	case DQ_UDP_EXPECT_FOREIGN:
		snprintf(msg, sizeof(msg), "%s: skipped", g->name);
		TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, msg);
		snprintf(msg, sizeof(msg), "%s: counted foreign, nothing else", g->name);
		TEST_ASSERT(rx.c.foreign == 1 && !rx.c.malformed && !rx.c.packets, msg);
		break;
	}

	if (ret != DQ_UDP_DATA) {
		snprintf(msg, sizeof(msg), "%s: out untouched", g->name);
		TEST_ASSERT(!memcmp(&out, &sentinel, sizeof(out)), msg);
		snprintf(msg, sizeof(msg), "%s: no sequence state", g->name);
		TEST_ASSERT(rx.expected == 0, msg);
	}
}

TEST_FUNCTION(golden_vectors)
{
	size_t i, len;
	uint8_t *buf;

	TEST_ASSERT_EQ(N_GOLDEN, 7, "vector count");

	for (i = 0; i < N_GOLDEN; i++) {
		len = 0;
		buf = load(udp_golden[i].name, &len);
		TEST_ASSERT_PTR_NOT_NULL(buf, udp_golden[i].name);
		if (!buf)
			continue;

		/* Exactly sized heap buffers: ASan catches any over-read */
		TEST_ASSERT_EQ(len, udp_golden[i].len, udp_golden[i].name);
		check_unpack(&udp_golden[i], buf, len);
		check_rx(&udp_golden[i], buf, len);
		free(buf);
	}
}

/* The payload is ramp16 in little-endian words, as iio_daqiri_bench --check
 * ramp reads host-order int16 I/Q pairs: sample n has I = n & 0x7FFF and
 * Q = (0x8000 - n) & 0xFFFF */
TEST_FUNCTION(payload_is_ramp16_le)
{
	const uint8_t *p;
	size_t len = 0, n, bad = 0;
	uint8_t *buf = load("udp-valid.bin", &len);
	uint16_t i, q;

	TEST_ASSERT_PTR_NOT_NULL(buf, "load udp-valid.bin");
	if (!buf)
		return;

	p = buf + IIOD_UDP_STREAM_HDR_LEN;
	for (n = 0; n < DQ_UDP_GOLDEN_PAYLOAD_BYTES / 4; n++) {
		i = (uint16_t)(p[4 * n] | p[4 * n + 1] << 8);
		q = (uint16_t)(p[4 * n + 2] | p[4 * n + 3] << 8);
		bad += i != (uint16_t)(n & 0x7FFF) || q != (uint16_t)((0x8000 - n) & 0xFFFF);
	}

	TEST_ASSERT_EQ(bad, 0, "udp-valid.bin carries ramp16, little-endian");
	free(buf);
}

int main(void)
{
	RUN_TEST(golden_vectors);
	RUN_TEST(payload_is_ramp16_le);
	TEST_SUMMARY();
}
