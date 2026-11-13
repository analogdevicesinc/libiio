/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Receive side of UDP sample mode for the DAQIRI backend: checks the header
 * of every datagram (iiod-udp-stream.h) against the stream that the setup
 * response described, and tracks the 64-bit sequence number. Free of libiio,
 * DAQIRI and CUDA dependencies so it can be unit tested on its own.
 *
 * Before each enable, dq_udp_rx_start() programs the stream from the setup
 * response: its stream ID and payload size, and sequence number 0 expected
 * first. Per datagram, in order:
 *  - shorter than the header (also: a header cut by header-data split), or a
 *    version other than IIOD_UDP_STREAM_VERSION: malformed++, SKIP;
 *  - no stream programmed, or another stream ID: foreign++, SKIP;
 *  - a payload_length other than the stream's payload size, or a datagram
 *    that is not exactly the header and that payload long: malformed++, SKIP;
 *  - its sequence number s against e, the one expected next:
 *    - s == e: accepted;
 *    - s > e: accepted after a gap of s - e datagrams that never arrived:
 *      gap_events++, lost_packets += s - e. A gap of more than
 *      DQ_UDP_MAX_FILL is too large to zero-fill: out->resync is set;
 *    - s < e: s arrived before (it is the last accepted one, or an older
 *      one): duplicates++, SKIP. Otherwise it is late: it was counted lost
 *      when the gap was seen, and it is still not delivered, the stream has
 *      moved on. late++, lost_packets-- (not below 0), SKIP.
 *    Accepted: packets++, bytes += payload size, DATA.
 * Whether s arrived before is known for the DQ_UDP_WINDOW sequence numbers
 * below e, which covers every gap that can still be zero-filled; an older s
 * counts as late. A late datagram is remembered as well, so that a second
 * copy of it is a duplicate.
 * A skipped datagram changes no sequence state: when it was the expected
 * one, the next accepted datagram reports it as lost.
 * The flags byte is reserved and not checked.
 *
 * It also decides when the drain after DISABLE_BUFFER is over
 * (dq_udp_drain_over()), and how large a receive buffer to ask for
 * (dq_udp_rcvbuf_target()).
 */

#ifndef DAQIRI_UDP_STREAM_H
#define DAQIRI_UDP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iiod-udp-stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sequence numbers below the expected one whose arrival is remembered */
#define DQ_UDP_WINDOW 1024

/* The largest gap, in datagrams, that lossy reception zero-fills; a larger
 * one fails a block (-EIO) even with lossy=1 */
#define DQ_UDP_MAX_FILL 1024

struct dq_udp_counters {
	uint64_t packets;      /* accepted datagrams (DQ_UDP_DATA) */
	uint64_t bytes;	       /* payload bytes of accepted datagrams */
	uint64_t lost_packets; /* sequence numbers skipped, less late arrivals */
	uint64_t gap_events;   /* gaps, however many datagrams each */
	uint64_t foreign, malformed, duplicates, late;
};

struct dq_udp_rx {
	bool started; /* a stream is programmed */
	uint32_t stream_id;
	uint16_t payload_bytes;
	uint64_t expected; /* sequence number expected next */
	/* s + 1 at index s % DQ_UDP_WINDOW once s arrived; 0: nothing yet */
	uint64_t seen[DQ_UDP_WINDOW];
	struct dq_udp_counters c;
};

struct dq_udp_data {
	const uint8_t *payload; /* in the datagram, right after the header */
	size_t len;		/* the stream's payload size */
	uint64_t sequence;
	uint64_t gap; /* datagrams lost right before this one */
	bool resync;  /* gap > DQ_UDP_MAX_FILL */
};

#define DQ_UDP_DATA 0 /* out is valid */
#define DQ_UDP_SKIP 1 /* counted, nothing to copy */

/* No stream, every counter 0 */
void dq_udp_rx_init(struct dq_udp_rx *rx);

/*
 * A new stream, from a setup response: sequence number 0 is expected next and
 * nothing has arrived yet. The counters are kept.
 */
void dq_udp_rx_start(struct dq_udp_rx *rx, uint32_t stream_id, uint16_t payload_bytes);

/*
 * Process one datagram of total_len bytes of which only the first hdr_len, at
 * hdr, are contiguous: header-data split put the rest elsewhere (hdr_len is
 * at most total_len). The header is read from hdr alone. Returns DQ_UDP_DATA
 * with out filled in, out->payload pointing at byte 16 of hdr even when hdr
 * ends there, or DQ_UDP_SKIP (counted, out untouched).
 */
int dq_udp_rx_header(struct dq_udp_rx *rx, const uint8_t *hdr, size_t hdr_len, size_t total_len,
		struct dq_udp_data *out);

/* One datagram of len bytes at dgram: dq_udp_rx_header(rx, dgram, len, len, out) */
int dq_udp_rx_datagram(
		struct dq_udp_rx *rx, const uint8_t *dgram, size_t len, struct dq_udp_data *out);

/* After DISABLE_BUFFER the poller still receives what is on its way: until
 * the queue has delivered nothing for this long, */
#define DQ_UDP_DRAIN_IDLE_MS 20
/* or this long after the answer at most */
#define DQ_UDP_DRAIN_MAX_MS 500

/*
 * Whether the drain is over at <now>, the answer to DISABLE_BUFFER having
 * come at <start> (no later than <now>) and the queue's last burst at <last>
 * (0: none yet), all in nanoseconds of one monotonic clock: once the queue
 * has been quiet for DQ_UDP_DRAIN_IDLE_MS since the later of <start> and
 * <last>, or DQ_UDP_DRAIN_MAX_MS after <start> whatever comes. A <last> later
 * than <now>, a burst stored after the clock was read, is not quiet.
 */
bool dq_udp_drain_over(uint64_t start, uint64_t last, uint64_t now);

/* The receive buffer asked for a socket engine queue's socket by default */
#define DQ_UDP_RCVBUF_MIN ((uint64_t)4 << 20)
#define DQ_UDP_RCVBUF_MAX ((uint64_t)64 << 20)

/*
 * The bytes of the queue's packet buffers, <num_bufs> * <buf_size> of its
 * memory region, at least DQ_UDP_RCVBUF_MIN and at most DQ_UDP_RCVBUF_MAX
 * (also when the product does not fit 64 bits): the kernel then holds as many
 * datagrams as the engine does when the consumer falls behind.
 */
uint64_t dq_udp_rcvbuf_target(uint64_t num_bufs, uint64_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* DAQIRI_UDP_STREAM_H */
