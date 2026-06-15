/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 * 
 * Contributors:
 * 		- Praveen Perera <praveen.perera@analog.com>
 */

#define TESTS_DEBUG
#include "test_framework.h"
#include "../../vita49_packet.c"
#include <stdint.h>
#include <string.h>

static void fill_context_packet(uint32_t *buf)
{
	struct vrt_header hdr_s;
	memset(&hdr_s, 0, sizeof(hdr_s));
	hdr_s.packet_type = VRT_PKT_TYPE_IF_CONTEXT;
	hdr_s.has_class_id = 1;
	hdr_s.packet_size_words = 10;
	
	buf[0] = htonl(*(uint32_t *)&hdr_s);
	buf[1] = htonl(0x12345678); /* Stream ID */
	buf[2] = htonl(0x0012A200); /* OUI */
	buf[3] = htonl(0x00000001); /* Class Code */

	/* Context Indicator Field 0 */
	buf[4] = htonl((1 << 21) | (1 << 30)); /* Sample Rate & Bandwidth */
	buf[5] = htonl(0x40000000); /* sample rate placeholder */
	buf[6] = htonl(0x00000000);
	buf[7] = htonl(0x40000000); /* bandwidth placeholder */
	buf[8] = htonl(0x00000000);
	buf[9] = htonl(0x00000000); /* padding basically, since t=0 */
}

TEST_FUNCTION(test_vrt_parse_packet_basic)
{
	uint32_t packet[10];
	fill_context_packet(packet);

	struct vrt_packet pkt;
	int ret = vrt_parse_packet(packet, 10, &pkt);
	TEST_ASSERT_EQ(ret, 0, "Packet should parse successfully");
	TEST_ASSERT_EQ(pkt.header.packet_type, VRT_PKT_TYPE_IF_CONTEXT, "Should be IF_CONTEXT");
	TEST_ASSERT_EQ(pkt.has_stream_id, true, "Should have stream ID");
	TEST_ASSERT_EQ(pkt.stream_id, 0x12345678, "Stream ID should match");
	TEST_ASSERT_EQ(pkt.has_class_id, true, "Should have class ID");
	uint64_t expected_class = ((uint64_t)0x0012A200 << 32) | 0x00000001;
	TEST_ASSERT(pkt.class_id == expected_class, "Class ID should match");
	TEST_ASSERT_EQ(pkt.has_timestamp_int, false, "Should not have TSI");
	TEST_ASSERT_EQ(pkt.has_timestamp_frac, false, "Should not have TSF");
	TEST_ASSERT_EQ(pkt.has_trailer, false, "Should not have trailer");
	TEST_ASSERT_EQ(pkt.payload_words, 6, "Payload should be 6 words");
}

TEST_FUNCTION(test_vrt_generate_packet_basic)
{
	struct vrt_packet pkt;
	memset(&pkt, 0, sizeof(pkt));
	
	pkt.header.packet_type = VRT_PKT_TYPE_IF_DATA_WITH_SID;
	pkt.header.has_class_id = 0;
	pkt.header.has_trailer = 1;
	pkt.header.ts_integer_format = VRT_TSI_UTC;
	pkt.header.ts_fractional_format = VRT_TSF_REAL_TIME;
	
	pkt.stream_id = 0x87654321;
	pkt.timestamp_int = 1000000;
	pkt.timestamp_frac = 2000000;
	
	uint32_t payload[2] = {0xDEADBEEF, 0xCAFEBABE};
	pkt.payload = payload;
	pkt.payload_words = 2;
	
	pkt.trailer.context_packet_count_enable = 1;
	
	uint32_t buffer[10];
	ssize_t words = vrt_generate_packet(&pkt, buffer, 10);
	
	TEST_ASSERT_EQ(words, 8, "Should generate 8 words");
	
	struct vrt_packet pkt2;
	int ret = vrt_parse_packet(buffer, words, &pkt2);
	TEST_ASSERT_EQ(ret, 0, "Should parse generated packet");
	TEST_ASSERT_EQ(pkt2.stream_id, 0x87654321, "Stream ID matches");
	TEST_ASSERT_EQ(pkt2.timestamp_int, 1000000, "TSI matches");
	TEST_ASSERT_EQ(pkt2.timestamp_frac, 2000000, "TSF matches");
	TEST_ASSERT_EQ(pkt2.payload_words, 2, "Payload size matches");
	TEST_ASSERT_EQ(pkt2.payload[0], 0xDEADBEEF, "Payload[0] matches");
	TEST_ASSERT_EQ(pkt2.payload[1], 0xCAFEBABE, "Payload[1] matches");
	TEST_ASSERT_EQ(pkt2.trailer.context_packet_count_enable, 1, "Trailer e matches");
}

int main(void)
{
	RUN_TEST(test_vrt_parse_packet_basic);
	RUN_TEST(test_vrt_generate_packet_basic);
	TEST_SUMMARY();
	return 0;
}
