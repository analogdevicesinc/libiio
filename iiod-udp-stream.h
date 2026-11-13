/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * UDP sample stream: the IIOD_OP_UDP_STREAM_SETUP request and response, and
 * the wire format of their tails and of the datagram header. Everything on the
 * wire is in network byte order (big endian), although the rest of the binary
 * protocol is host-native. Libc only, so that the client, the server (iiod,
 * tinyiiod, Zephyr) and the host's receive path can all share it.
 */

#ifndef __IIOD_UDP_STREAM_H__
#define __IIOD_UDP_STREAM_H__

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* Command: cmd.dev = device index, cmd.code = buffer index, then the request
 * tail. Response: code = IIOD_UDP_STREAM_RESP_LEN and the response tail, or a
 * negative error code and no tail. */
#define IIOD_UDP_STREAM_REQ_LEN 8
#define IIOD_UDP_STREAM_RESP_LEN 12

/* The request and response of iiod_client_udp_stream_setup(), which
 * <iio/iiod-client.h> only declares. Fields are in host byte order. */
struct iiod_udp_stream_req {
	uint32_t dst_ip;      /* IPv4 address the datagrams are sent to */
	uint16_t dst_port;    /* UDP port they are sent to */
	uint16_t max_payload; /* Largest payload, in bytes, the host accepts */
};

struct iiod_udp_stream_resp {
	uint32_t stream_id;	/* Stream ID carried by every datagram */
	uint16_t payload_bytes; /* Payload size of every datagram */
	uint16_t src_port;	/* UDP port the datagrams come from */
	uint32_t src_ip;	/* IPv4 address they come from */
};

/* Each datagram: this header, then payload_length bytes of samples */
#define IIOD_UDP_STREAM_HDR_LEN 16
#define IIOD_UDP_STREAM_VERSION 1

struct iiod_udp_stream_hdr {
	uint8_t version; /* IIOD_UDP_STREAM_VERSION */
	uint8_t flags;	 /* 0, reserved */
	uint16_t payload_length;
	uint32_t stream_id;
	uint64_t sequence; /* 0 for the first datagram after an enable */
};

static inline void iiod_udp_stream_put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static inline void iiod_udp_stream_put32(uint8_t *p, uint32_t v)
{
	iiod_udp_stream_put16(p, (uint16_t)(v >> 16));
	iiod_udp_stream_put16(p + 2, (uint16_t)v);
}

static inline void iiod_udp_stream_put64(uint8_t *p, uint64_t v)
{
	iiod_udp_stream_put32(p, (uint32_t)(v >> 32));
	iiod_udp_stream_put32(p + 4, (uint32_t)v);
}

static inline uint16_t iiod_udp_stream_get16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t iiod_udp_stream_get32(const uint8_t *p)
{
	return ((uint32_t)iiod_udp_stream_get16(p) << 16) | iiod_udp_stream_get16(p + 2);
}

static inline uint64_t iiod_udp_stream_get64(const uint8_t *p)
{
	return ((uint64_t)iiod_udp_stream_get32(p) << 32) | iiod_udp_stream_get32(p + 4);
}

static inline void iiod_udp_stream_req_pack(
		const struct iiod_udp_stream_req *req, uint8_t out[IIOD_UDP_STREAM_REQ_LEN])
{
	iiod_udp_stream_put32(&out[0], req->dst_ip);
	iiod_udp_stream_put16(&out[4], req->dst_port);
	iiod_udp_stream_put16(&out[6], req->max_payload);
}

/* Any 8 bytes are a request; checking the values is the provider's job */
static inline int iiod_udp_stream_req_unpack(
		const uint8_t in[IIOD_UDP_STREAM_REQ_LEN], struct iiod_udp_stream_req *req)
{
	req->dst_ip = iiod_udp_stream_get32(&in[0]);
	req->dst_port = iiod_udp_stream_get16(&in[4]);
	req->max_payload = iiod_udp_stream_get16(&in[6]);

	return 0;
}

static inline void iiod_udp_stream_resp_pack(
		const struct iiod_udp_stream_resp *resp, uint8_t out[IIOD_UDP_STREAM_RESP_LEN])
{
	iiod_udp_stream_put32(&out[0], resp->stream_id);
	iiod_udp_stream_put16(&out[4], resp->payload_bytes);
	iiod_udp_stream_put16(&out[6], resp->src_port);
	iiod_udp_stream_put32(&out[8], resp->src_ip);
}

static inline int iiod_udp_stream_resp_unpack(
		const uint8_t in[IIOD_UDP_STREAM_RESP_LEN], struct iiod_udp_stream_resp *resp)
{
	resp->stream_id = iiod_udp_stream_get32(&in[0]);
	resp->payload_bytes = iiod_udp_stream_get16(&in[4]);
	resp->src_port = iiod_udp_stream_get16(&in[6]);
	resp->src_ip = iiod_udp_stream_get32(&in[8]);

	return 0;
}

static inline void iiod_udp_stream_hdr_pack(
		const struct iiod_udp_stream_hdr *hdr, uint8_t out[IIOD_UDP_STREAM_HDR_LEN])
{
	out[0] = hdr->version;
	out[1] = hdr->flags;
	iiod_udp_stream_put16(&out[2], hdr->payload_length);
	iiod_udp_stream_put32(&out[4], hdr->stream_id);
	iiod_udp_stream_put64(&out[8], hdr->sequence);
}

/* <in> is a datagram of <len> bytes. Returns -EBADMSG if it is shorter than
 * the header and -EPROTO if its version is not IIOD_UDP_STREAM_VERSION. The
 * payload length is not checked against <len>: that is the receiver's job. */
static inline int iiod_udp_stream_hdr_unpack(
		const uint8_t *in, size_t len, struct iiod_udp_stream_hdr *hdr)
{
	if (len < IIOD_UDP_STREAM_HDR_LEN)
		return -EBADMSG;
	if (in[0] != IIOD_UDP_STREAM_VERSION)
		return -EPROTO;

	hdr->version = in[0];
	hdr->flags = in[1];
	hdr->payload_length = iiod_udp_stream_get16(&in[2]);
	hdr->stream_id = iiod_udp_stream_get32(&in[4]);
	hdr->sequence = iiod_udp_stream_get64(&in[8]);

	return 0;
}

#endif /* __IIOD_UDP_STREAM_H__ */
