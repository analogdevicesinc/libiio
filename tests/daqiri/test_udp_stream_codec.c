// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Unit test for the UDP sample stream codec (iiod-udp-stream.h): the setup
 * request and response tails of IIOD_OP_UDP_STREAM_SETUP and the datagram
 * header, all in network byte order on the wire.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "../../iiod-udp-stream.h"
#include "test_framework.h"

/* Every byte of a field differs from its neighbours, so a swap shows. */
static const uint8_t req_wire[IIOD_UDP_STREAM_REQ_LEN] = {
	/* dst_ip 192.0.2.17, dst_port 4991, max_payload 8960 */
	0xc0, 0x00, 0x02, 0x11, 0x13, 0x7f, 0x23, 0x00
};

static const uint8_t resp_wire[IIOD_UDP_STREAM_RESP_LEN] = {
	/* stream_id 0x12345678, payload_bytes 8944, src_port 25102 */
	0x12, 0x34, 0x56, 0x78, 0x22, 0xf0, 0x62, 0x0e,
	/* src_ip 198.51.100.7 */
	0xc6, 0x33, 0x64, 0x07
};

static const uint8_t hdr_wire[IIOD_UDP_STREAM_HDR_LEN] = {
	/* version 1, flags 0, payload_length 8944, stream_id 0xdeadbeef */
	0x01, 0x00, 0x22, 0xf0, 0xde, 0xad, 0xbe, 0xef,
	/* sequence 0x0102030405060708 */
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
};

TEST_FUNCTION(lengths)
{
	TEST_ASSERT_EQ(IIOD_UDP_STREAM_REQ_LEN, 8, "request tail length");
	TEST_ASSERT_EQ(IIOD_UDP_STREAM_RESP_LEN, 12, "response tail length");
	TEST_ASSERT_EQ(IIOD_UDP_STREAM_HDR_LEN, 16, "datagram header length");
	TEST_ASSERT_EQ(IIOD_UDP_STREAM_VERSION, 1, "datagram version");
}

TEST_FUNCTION(req)
{
	const struct iiod_udp_stream_req req = {
		.dst_ip = 0xc0000211,
		.dst_port = 4991,
		.max_payload = 8960,
	};
	struct iiod_udp_stream_req out;
	uint8_t buf[IIOD_UDP_STREAM_REQ_LEN];

	memset(buf, 0xaa, sizeof(buf));
	iiod_udp_stream_req_pack(&req, buf);
	TEST_ASSERT(!memcmp(buf, req_wire, sizeof(buf)), "request packs to network order");

	memset(&out, 0xaa, sizeof(out));
	TEST_ASSERT_EQ(iiod_udp_stream_req_unpack(req_wire, &out), 0, "request unpacks");
	TEST_ASSERT_EQ(out.dst_ip, req.dst_ip, "dst_ip");
	TEST_ASSERT_EQ(out.dst_port, req.dst_port, "dst_port");
	TEST_ASSERT_EQ(out.max_payload, req.max_payload, "max_payload");
}

TEST_FUNCTION(resp)
{
	const struct iiod_udp_stream_resp resp = {
		.stream_id = 0x12345678,
		.payload_bytes = 8944,
		.src_port = 25102,
		.src_ip = 0xc6336407,
	};
	struct iiod_udp_stream_resp out;
	uint8_t buf[IIOD_UDP_STREAM_RESP_LEN];

	memset(buf, 0xaa, sizeof(buf));
	iiod_udp_stream_resp_pack(&resp, buf);
	TEST_ASSERT(!memcmp(buf, resp_wire, sizeof(buf)), "response packs to network order");

	memset(&out, 0xaa, sizeof(out));
	TEST_ASSERT_EQ(iiod_udp_stream_resp_unpack(resp_wire, &out), 0, "response unpacks");
	TEST_ASSERT_EQ(out.stream_id, resp.stream_id, "stream_id");
	TEST_ASSERT_EQ(out.payload_bytes, resp.payload_bytes, "payload_bytes");
	TEST_ASSERT_EQ(out.src_port, resp.src_port, "src_port");
	TEST_ASSERT_EQ(out.src_ip, resp.src_ip, "src_ip");
}

TEST_FUNCTION(hdr)
{
	const struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.flags = 0,
		.payload_length = 8944,
		.stream_id = 0xdeadbeef,
		.sequence = 0x0102030405060708ull,
	};
	struct iiod_udp_stream_hdr out;
	uint8_t buf[IIOD_UDP_STREAM_HDR_LEN];

	memset(buf, 0xaa, sizeof(buf));
	iiod_udp_stream_hdr_pack(&hdr, buf);
	TEST_ASSERT(!memcmp(buf, hdr_wire, sizeof(buf)), "header packs to network order");

	memset(&out, 0xaa, sizeof(out));
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(hdr_wire, sizeof(hdr_wire), &out), 0,
			"header unpacks");
	TEST_ASSERT_EQ(out.version, hdr.version, "version");
	TEST_ASSERT_EQ(out.flags, hdr.flags, "flags");
	TEST_ASSERT_EQ(out.payload_length, hdr.payload_length, "payload_length");
	TEST_ASSERT_EQ(out.stream_id, hdr.stream_id, "stream_id");
	TEST_ASSERT(out.sequence == hdr.sequence, "sequence");
}

/* All-ones fields catch a value truncated or sign-extended on the way. */
TEST_FUNCTION(round_trip_extremes)
{
	const struct iiod_udp_stream_req req = { 0xffffffff, 0xffff, 0xffff };
	const struct iiod_udp_stream_resp resp = { 0xffffffff, 0xffff, 0xffff, 0xffffffff };
	const struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.flags = 0xff,
		.payload_length = 0xffff,
		.stream_id = 0xffffffff,
		.sequence = UINT64_MAX,
	};
	struct iiod_udp_stream_req req_out;
	struct iiod_udp_stream_resp resp_out;
	struct iiod_udp_stream_hdr hdr_out;
	uint8_t buf[IIOD_UDP_STREAM_HDR_LEN];

	iiod_udp_stream_req_pack(&req, buf);
	TEST_ASSERT_EQ(iiod_udp_stream_req_unpack(buf, &req_out), 0, "request round trip");
	TEST_ASSERT(!memcmp(&req_out, &req, sizeof(req)), "request fields survive");

	iiod_udp_stream_resp_pack(&resp, buf);
	TEST_ASSERT_EQ(iiod_udp_stream_resp_unpack(buf, &resp_out), 0, "response round trip");
	TEST_ASSERT(!memcmp(&resp_out, &resp, sizeof(resp)), "response fields survive");

	iiod_udp_stream_hdr_pack(&hdr, buf);
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(buf, sizeof(buf), &hdr_out), 0,
			"header round trip");
	TEST_ASSERT_EQ(hdr_out.flags, 0xff, "flags survive");
	TEST_ASSERT_EQ(hdr_out.payload_length, 0xffff, "payload_length survives");
	TEST_ASSERT(hdr_out.stream_id == 0xffffffff, "stream_id survives");
	TEST_ASSERT(hdr_out.sequence == UINT64_MAX, "sequence survives");
}

TEST_FUNCTION(hdr_short)
{
	struct iiod_udp_stream_hdr out;
	uint8_t datagram[IIOD_UDP_STREAM_HDR_LEN + 4] = { 0 };

	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(hdr_wire, 0, &out), -EBADMSG, "empty");
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(hdr_wire, IIOD_UDP_STREAM_HDR_LEN - 1, &out),
			-EBADMSG, "one byte short");

	/* A short datagram is short whatever its version byte says */
	datagram[0] = IIOD_UDP_STREAM_VERSION + 1;
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(datagram, IIOD_UDP_STREAM_HDR_LEN - 1, &out),
			-EBADMSG, "short beats version");

	/* The header is followed by the payload */
	memcpy(datagram, hdr_wire, sizeof(hdr_wire));
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(datagram, sizeof(datagram), &out), 0,
			"header with payload");
	TEST_ASSERT(out.sequence == 0x0102030405060708ull, "sequence with payload");
}

TEST_FUNCTION(hdr_version)
{
	struct iiod_udp_stream_hdr out;
	uint8_t buf[IIOD_UDP_STREAM_HDR_LEN];

	memcpy(buf, hdr_wire, sizeof(buf));

	buf[0] = 0;
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(buf, sizeof(buf), &out), -EPROTO, "version 0");
	buf[0] = 2;
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(buf, sizeof(buf), &out), -EPROTO, "version 2");
	buf[0] = 0xff;
	TEST_ASSERT_EQ(iiod_udp_stream_hdr_unpack(buf, sizeof(buf), &out), -EPROTO, "version 255");
}

int main(void)
{
	RUN_TEST(lengths);
	RUN_TEST(req);
	RUN_TEST(resp);
	RUN_TEST(hdr);
	RUN_TEST(round_trip_extremes);
	RUN_TEST(hdr_short);
	RUN_TEST(hdr_version);
	TEST_SUMMARY();
}
