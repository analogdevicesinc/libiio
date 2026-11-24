// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Receive side of UDP sample mode: datagram header checks, the 64-bit
 * sequence tracker and the end of the drain; see daqiri-udp-stream.h.
 */

#include <string.h>

#include "daqiri-udp-stream.h"

void dq_udp_rx_init(struct dq_udp_rx *rx)
{
	memset(rx, 0, sizeof(*rx));
}

void dq_udp_rx_start(struct dq_udp_rx *rx, uint32_t stream_id, uint16_t payload_bytes)
{
	struct dq_udp_counters c = rx->c;

	memset(rx, 0, sizeof(*rx));
	rx->c = c;
	rx->started = true;
	rx->stream_id = stream_id;
	rx->payload_bytes = payload_bytes;
}

/* s < expected: inside the window, and remembered there */
static bool dq_udp_in_window(const struct dq_udp_rx *rx, uint64_t s)
{
	return rx->expected - s <= DQ_UDP_WINDOW;
}

static void dq_udp_remember(struct dq_udp_rx *rx, uint64_t s)
{
	rx->seen[s % DQ_UDP_WINDOW] = s + 1;
}

int dq_udp_rx_header(struct dq_udp_rx *rx, const uint8_t *hdr, size_t hdr_len, size_t total_len,
		struct dq_udp_data *out)
{
	struct iiod_udp_stream_hdr h;
	uint64_t s, gap;

	if (hdr_len > total_len)
		hdr_len = total_len;

	/* -EBADMSG (short, or cut by the split) or -EPROTO (version) */
	if (iiod_udp_stream_hdr_unpack(hdr, hdr_len, &h)) {
		rx->c.malformed++;
		return DQ_UDP_SKIP;
	}

	if (!rx->started || h.stream_id != rx->stream_id) {
		rx->c.foreign++;
		return DQ_UDP_SKIP;
	}

	if (h.payload_length != rx->payload_bytes ||
			total_len != (size_t)IIOD_UDP_STREAM_HDR_LEN + rx->payload_bytes) {
		rx->c.malformed++;
		return DQ_UDP_SKIP;
	}

	s = h.sequence;
	if (s < rx->expected) {
		if (dq_udp_in_window(rx, s) && rx->seen[s % DQ_UDP_WINDOW] == s + 1) {
			rx->c.duplicates++;
		} else {
			/* It did arrive, just too late to deliver */
			rx->c.late++;
			if (rx->c.lost_packets)
				rx->c.lost_packets--;
			if (dq_udp_in_window(rx, s))
				dq_udp_remember(rx, s);
		}
		return DQ_UDP_SKIP;
	}

	gap = s - rx->expected;
	if (gap) {
		rx->c.gap_events++;
		rx->c.lost_packets += gap;
	}

	dq_udp_remember(rx, s);
	rx->expected = s + 1;
	rx->c.packets++;
	rx->c.bytes += rx->payload_bytes;

	out->payload = hdr + IIOD_UDP_STREAM_HDR_LEN;
	out->len = rx->payload_bytes;
	out->sequence = s;
	out->gap = gap;
	out->resync = gap > DQ_UDP_MAX_FILL;
	return DQ_UDP_DATA;
}

int dq_udp_rx_datagram(
		struct dq_udp_rx *rx, const uint8_t *dgram, size_t len, struct dq_udp_data *out)
{
	return dq_udp_rx_header(rx, dgram, len, len, out);
}

bool dq_udp_drain_over(uint64_t start, uint64_t last, uint64_t now)
{
	const uint64_t idle = DQ_UDP_DRAIN_IDLE_MS * 1000000ULL;
	const uint64_t cap = DQ_UDP_DRAIN_MAX_MS * 1000000ULL;
	uint64_t quiet_from = last > start ? last : start;

	if (now - start >= cap)
		return true;

	/* A burst later than now is news, not a wrapped quiet time */
	return now >= quiet_from && now - quiet_from >= idle;
}

uint64_t dq_udp_rcvbuf_target(uint64_t num_bufs, uint64_t buf_size)
{
	if (buf_size && num_bufs > DQ_UDP_RCVBUF_MAX / buf_size)
		return DQ_UDP_RCVBUF_MAX;
	if (num_bufs * buf_size < DQ_UDP_RCVBUF_MIN)
		return DQ_UDP_RCVBUF_MIN;

	return num_bufs * buf_size;
}
