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

// This set of unit tests is to exercise the packet generation and parsing
// correctness of the VITA 49.2 subsystem.

#define TESTS_DEBUG
#include "test_framework.h"
#include <vita49_2/vita49_2_packet_types.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

#define TIME_PACKET_LENGTH sizeof(TIME_PACKET)/sizeof(TIME_PACKET[0])

// Hard-coded packets to compare against
// Signal Time Data Packet
uint32_t TIME_PACKET[] = 
{
	// Header
	(	(0x1 << 28) 	| 	/* Packet Type -> 1 = Signal Data with Stream ID */
		(0xA << 24) 	| 	/* Class ID included, Trailer not include, Not a V49.0 Packet, Signal Time Data Packet */
		(0x6 << 20) 	| 	/* UTC Integer Time and Real-Time (Picoseconds) Fractional Time*/
		(0xD << 16) 	| 	/* Packet Count = 13 (modulo 15) */
		(0x0009) 			/* Packet Size = 9 */
	),

	// Stream ID
	0x0001,

	// Class ID
	// First Word
	(
		(0x00 << 24) 	| 	/* Pad Bit Count = 0 */
		(OUI)		   		/* Organizationally Unique Identifier, defined in vita49_2_packet_elements.h */
	),
	// Second Word
	(
		// See my "VITA 49.2 Information Structures" document
		(0x0001 << 16) 	| 	/* Information Class Code */
		(0x0001)
	),

	// Integer Timestamp
	0x00FFDDEE,

	// Fractional Timestamp
	0x00001111,
	0x22223333,

	// Data Payload
	0x000F000D,				/* I/Q Sample One */
	0x11110001,				/* I/Q Sample Two */

	// TODO: Add testing for the trailer once we've added that logic to the parsing and generation functions
};


TEST_FUNCTION (test_vita49_2_parse_data_packet)
{
	// Translating to network order
	uint32_t time_packet_data[TIME_PACKET_LENGTH];
	for (uint8_t i = 0; i < TIME_PACKET_LENGTH; i++)
		time_packet_data[i] = htonl(TIME_PACKET[i]);

	struct vita49_2_data_packet data_packet;
	
	
	// Checking that the parser returns properly
	TEST_ASSERT_EQ(vita49_2_parse_data_packet(time_packet_data, TIME_PACKET_LENGTH, &data_packet), 0, "Valid packet should result in the parser returning 0");

	
	// Checking that each field was parsed properly
	size_t field_offset = 0;
	
	// Header
	// Header Word Overall
	uint32_t parsed_header_word;
	memcpy(&parsed_header_word, &data_packet.prologue.header, sizeof(parsed_header_word));
	TEST_ASSERT_EQ(parsed_header_word, TIME_PACKET[field_offset], "Header should match");

	// Testing a header subfield to ensure byte order and fields were handled properly
	TEST_ASSERT_EQ(data_packet.prologue.header.packet_count, ((TIME_PACKET[0] >> 16) & 0x000F), "Packet Type subfield should match");
	field_offset++;

	// Stream ID
	TEST_ASSERT_EQ(data_packet.prologue.has_stream_id, 1, "Stream ID should be present");
	TEST_ASSERT_EQ(data_packet.prologue.stream_id, TIME_PACKET[field_offset], "Stream ID should match");
	field_offset++;

	// Class ID
	TEST_ASSERT_EQ(data_packet.prologue.has_class_id, 1, "Class ID should be present");
	
	uint32_t class_id_first_word, class_id_second_word;
	memcpy(&class_id_first_word, &data_packet.prologue.class_id.lower_word, sizeof(class_id_first_word));
	memcpy(&class_id_second_word, &data_packet.prologue.class_id.upper_word, sizeof(class_id_second_word));

	TEST_ASSERT_EQ(class_id_first_word, TIME_PACKET[field_offset], "Class ID first word should match");
	field_offset++;
	TEST_ASSERT_EQ(class_id_second_word, TIME_PACKET[field_offset], "Class ID second word should match");
	field_offset++;

	// Integer Timestamp
	TEST_ASSERT_EQ(data_packet.prologue.has_timestamp_int, 1, "Integer Timestamp should be present");
	TEST_ASSERT_EQ(data_packet.prologue.timestamp_int, TIME_PACKET[field_offset], "Integer Timestamp should match");
	field_offset++;

	// Fractional Timestamp
	TEST_ASSERT_EQ(data_packet.prologue.has_timestamp_frac, 1, "Fractional Timestamp should be present");
	
	// According to Figure 5.1.4-1 from the VITA 49.2 full spec document, the first word is the most significant 32 bits
	uint32_t timestamp_frac_lower, timestamp_frac_upper;
	memcpy(&timestamp_frac_upper, &TIME_PACKET[field_offset], sizeof(timestamp_frac_upper));
	field_offset++;
	memcpy(&timestamp_frac_lower, &TIME_PACKET[field_offset], sizeof(timestamp_frac_lower));
	field_offset++;
	
	uint64_t timestamp_frac = ((uint64_t)(timestamp_frac_upper) << 32) | timestamp_frac_lower;
	TEST_ASSERT_EQ(data_packet.prologue.timestamp_frac, timestamp_frac, "Fractional Timestamp should match");

	// Data Payload
	TEST_ASSERT_EQ(data_packet.payload_num_words, 2, "2 samples should be present");
	
	uint32_t sample_1, sample_2;
	memcpy(&sample_1, &data_packet.payload[0], sizeof(sample_1));
	memcpy(&sample_2, &data_packet.payload[1], sizeof(sample_2));

	TEST_ASSERT_EQ(sample_1, TIME_PACKET[field_offset], "Sample 1 should match");
	field_offset++;
	TEST_ASSERT_EQ(sample_2, TIME_PACKET[field_offset], "Sample 2 should match");
}

// static void fill_context_packet(uint32_t *buf)
// {
// 	struct vrt_header hdr_s;
// 	memset(&hdr_s, 0, sizeof(hdr_s));
// 	hdr_s.packet_type = VRT_PKT_TYPE_IF_CONTEXT;
// 	hdr_s.has_class_id = 1;
// 	hdr_s.packet_size_words = 10;
	
// 	buf[0] = htonl(*(uint32_t *)&hdr_s);
// 	buf[1] = htonl(0x12345678); /* Stream ID */
// 	buf[2] = htonl(0x0012A200); /* OUI */
// 	buf[3] = htonl(0x00000001); /* Class Code */

// 	/* Context Indicator Field 0 */
// 	buf[4] = htonl((1 << 21) | (1 << 30)); /* Sample Rate & Bandwidth */
// 	buf[5] = htonl(0x40000000); /* sample rate placeholder */
// 	buf[6] = htonl(0x00000000);
// 	buf[7] = htonl(0x40000000); /* bandwidth placeholder */
// 	buf[8] = htonl(0x00000000);
// 	buf[9] = htonl(0x00000000); /* padding basically, since t=0 */
// }

// TEST_FUNCTION(test_vrt_parse_packet_basic)
// {
// 	uint32_t packet[10];
// 	fill_context_packet(packet);

// 	struct vrt_packet pkt;
// 	int ret = vrt_parse_packet(packet, 10, &pkt);
// 	TEST_ASSERT_EQ(ret, 0, "Packet should parse successfully");
// 	TEST_ASSERT_EQ(pkt.header.packet_type, VRT_PKT_TYPE_IF_CONTEXT, "Should be IF_CONTEXT");
// 	TEST_ASSERT_EQ(pkt.has_stream_id, true, "Should have stream ID");
// 	TEST_ASSERT_EQ(pkt.stream_id, 0x12345678, "Stream ID should match");
// 	TEST_ASSERT_EQ(pkt.has_class_id, true, "Should have class ID");
// 	uint64_t expected_class = ((uint64_t)0x0012A200 << 32) | 0x00000001;
// 	TEST_ASSERT(pkt.class_id == expected_class, "Class ID should match");
// 	TEST_ASSERT_EQ(pkt.has_timestamp_int, false, "Should not have TSI");
// 	TEST_ASSERT_EQ(pkt.has_timestamp_frac, false, "Should not have TSF");
// 	TEST_ASSERT_EQ(pkt.has_trailer, false, "Should not have trailer");
// 	TEST_ASSERT_EQ(pkt.payload_words, 6, "Payload should be 6 words");
// }

// TEST_FUNCTION(test_vrt_generate_packet_basic)
// {
// 	struct vrt_packet pkt;
// 	memset(&pkt, 0, sizeof(pkt));
	
// 	pkt.header.packet_type = VRT_PKT_TYPE_IF_DATA_WITH_SID;
// 	pkt.header.has_class_id = 0;
// 	pkt.header.has_trailer = 1;
// 	pkt.header.ts_integer_format = VRT_TSI_UTC;
// 	pkt.header.ts_fractional_format = VRT_TSF_REAL_TIME;
	
// 	pkt.stream_id = 0x87654321;
// 	pkt.timestamp_int = 1000000;
// 	pkt.timestamp_frac = 2000000;
	
// 	uint32_t payload[2] = {0xDEADBEEF, 0xCAFEBABE};
// 	pkt.payload = payload;
// 	pkt.payload_words = 2;
	
// 	pkt.trailer.context_packet_count_enable = 1;
	
// 	uint32_t buffer[10];
// 	ssize_t words = vrt_generate_packet(&pkt, buffer, 10);
	
// 	TEST_ASSERT_EQ(words, 8, "Should generate 8 words");
	
// 	struct vrt_packet pkt2;
// 	int ret = vrt_parse_packet(buffer, words, &pkt2);
// 	TEST_ASSERT_EQ(ret, 0, "Should parse generated packet");
// 	TEST_ASSERT_EQ(pkt2.stream_id, 0x87654321, "Stream ID matches");
// 	TEST_ASSERT_EQ(pkt2.timestamp_int, 1000000, "TSI matches");
// 	TEST_ASSERT_EQ(pkt2.timestamp_frac, 2000000, "TSF matches");
// 	TEST_ASSERT_EQ(pkt2.payload_words, 2, "Payload size matches");
// 	TEST_ASSERT_EQ(pkt2.payload[0], 0xDEADBEEF, "Payload[0] matches");
// 	TEST_ASSERT_EQ(pkt2.payload[1], 0xCAFEBABE, "Payload[1] matches");
// 	TEST_ASSERT_EQ(pkt2.trailer.context_packet_count_enable, 1, "Trailer e matches");
// }

int main(void)
{
	RUN_TEST(test_vita49_2_parse_data_packet);
	// RUN_TEST(test_vrt_generate_packet_basic);
	TEST_SUMMARY();
	return 0;
}
