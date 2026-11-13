// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for dq_udp_rx_header(), the UDP sample mode receive entry point
 * for a datagram that header-data split (HDS) cut in two: the 16-byte header
 * must be in the first part, the total length must be the header and the
 * stream's payload size. Uses the udp-valid.bin golden vector (a 16-byte
 * header and 1440 payload bytes).
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daqiri-udp-stream.h"
#include "test_framework.h"

#define HDR IIOD_UDP_STREAM_HDR_LEN
#define PAYLOAD 1440u
#define STREAM_ID 0xC0FFEE01u /* gen_golden.py UDP_STREAM_ID */

static struct dq_udp_rx ref_rx, rx;

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

/* An exactly sized heap copy of the first n bytes: ASan catches over-reads */
static uint8_t *head(const uint8_t *p, size_t n)
{
	uint8_t *h = malloc(n ? n : 1);

	if (h)
		memcpy(h, p, n);
	return h;
}

static void init_rx(struct dq_udp_rx *r)
{
	dq_udp_rx_init(r);
	dq_udp_rx_start(r, STREAM_ID, PAYLOAD);
}

TEST_FUNCTION(split_boundaries)
{
	static const size_t bounds[] = { 0, 10, 15, 16, 17, 70, 0 /* len */ };
	struct dq_udp_data ref, out, sentinel;
	size_t len = 0, i, b;
	uint8_t *dgram = load("udp-valid.bin", &len), *hdr;
	char msg[128];
	int ret;

	TEST_ASSERT_PTR_NOT_NULL(dgram, "load udp-valid.bin");
	if (!dgram)
		return;
	TEST_ASSERT_EQ(len, HDR + PAYLOAD, "vector length");

	/* The single-buffer parse every split is compared with */
	init_rx(&ref_rx);
	ret = dq_udp_rx_datagram(&ref_rx, dgram, len, &ref);
	TEST_ASSERT_EQ(ret, DQ_UDP_DATA, "reference: DATA");
	TEST_ASSERT_EQ(ref.len, PAYLOAD, "reference: payload length");
	TEST_ASSERT(ref.payload == dgram + HDR, "reference: payload after the header");

	memset(&sentinel, 0x5a, sizeof(sentinel));

	for (i = 0; i < sizeof(bounds) / sizeof(bounds[0]); i++) {
		b = bounds[i] ? bounds[i] : len;
		hdr = head(dgram, b);
		TEST_ASSERT_PTR_NOT_NULL(hdr, "alloc header part");
		if (!hdr)
			break;

		init_rx(&rx);
		out = sentinel;
		ret = dq_udp_rx_header(&rx, hdr, b, len, &out);

		if (b < HDR) {
			/* The header is cut: nothing can be read from it */
			snprintf(msg, sizeof(msg), "split at %zu: SKIP", b);
			TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, msg);
			snprintf(msg, sizeof(msg), "split at %zu: out untouched", b);
			TEST_ASSERT(!memcmp(&out, &sentinel, sizeof(out)), msg);
			snprintf(msg, sizeof(msg), "split at %zu: malformed, nothing else", b);
			TEST_ASSERT(rx.c.malformed == 1 && !rx.c.packets && !rx.c.foreign, msg);
			snprintf(msg, sizeof(msg), "split at %zu: no sequence state", b);
			TEST_ASSERT(rx.expected == 0, msg);
		} else {
			snprintf(msg, sizeof(msg), "split at %zu: DATA", b);
			TEST_ASSERT_EQ(ret, DQ_UDP_DATA, msg);
			snprintf(msg, sizeof(msg), "split at %zu: payload length", b);
			TEST_ASSERT_EQ(out.len, ref.len, msg);
			snprintf(msg, sizeof(msg), "split at %zu: payload offset", b);
			TEST_ASSERT(out.payload == hdr + HDR, msg);
			snprintf(msg, sizeof(msg), "split at %zu: sequence and gap", b);
			TEST_ASSERT(out.sequence == ref.sequence && out.gap == ref.gap &&
							out.resync == ref.resync,
					msg);
			snprintf(msg, sizeof(msg), "split at %zu: sequence state", b);
			TEST_ASSERT(rx.expected == ref_rx.expected, msg);
			snprintf(msg, sizeof(msg), "split at %zu: counters", b);
			TEST_ASSERT(!memcmp(&rx.c, &ref_rx.c, sizeof(rx.c)), msg);
		}
		free(hdr);
	}

	free(dgram);
}

/* The total length must be the header and exactly the payload size */
TEST_FUNCTION(total_length)
{
	struct dq_udp_data out;
	size_t len = 0;
	uint8_t *dgram = load("udp-valid.bin", &len), *hdr;
	int ret;

	TEST_ASSERT_PTR_NOT_NULL(dgram, "load udp-valid.bin");
	if (!dgram)
		return;
	hdr = head(dgram, 70);
	TEST_ASSERT_PTR_NOT_NULL(hdr, "alloc header part");
	if (!hdr) {
		free(dgram);
		return;
	}

	init_rx(&rx);
	ret = dq_udp_rx_header(&rx, hdr, 70, len + 4, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "split, 4 bytes more than the payload size: SKIP");
	ret = dq_udp_rx_header(&rx, hdr, 70, len - 4, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "split, 4 bytes less than the payload size: SKIP");
	TEST_ASSERT_EQ(rx.c.malformed, 2, "size mismatch: malformed");
	TEST_ASSERT_EQ(rx.c.packets, 0, "size mismatch: nothing accepted");
	TEST_ASSERT(rx.expected == 0, "size mismatch: no sequence state");

	/* Unsplit, the same rule: dq_udp_rx_datagram() is the wrapper */
	init_rx(&rx);
	ret = dq_udp_rx_header(&rx, dgram, len - 4, len - 4, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_SKIP, "unsplit, truncated: SKIP");
	TEST_ASSERT_EQ(rx.c.malformed, 1, "unsplit, truncated: malformed");
	ret = dq_udp_rx_header(&rx, dgram, len, len, &out);
	TEST_ASSERT_EQ(ret, DQ_UDP_DATA, "unsplit, exact: DATA");

	free(hdr);
	free(dgram);
}

int main(void)
{
	RUN_TEST(split_boundaries);
	RUN_TEST(total_length);
	TEST_SUMMARY();
}
