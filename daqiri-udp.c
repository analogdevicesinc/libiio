// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * UDP sample mode for the DAQIRI backend: the server sends the samples of an
 * enabled buffer as UDP datagrams (iiod-udp-stream.h), which the DAQIRI
 * engine receives on one (port, queue). The buffer has an iiod connection of
 * its own, as the TCP path does, for OPEN_BUFFER, the setup command and
 * ENABLE/DISABLE_BUFFER; CLOSE_BUFFER goes on the management connection.
 *
 * Stream control, on the application's thread:
 *  - open: right after OPEN_BUFFER, one probing setup decides the data path.
 *    Refused (-ENOTSUP, -EINVAL, -EBUSY): with data=auto the buffer is a TCP
 *    buffer on the same connection, with data=udp the open fails with
 *    -ENOTSUP. Any other error fails the open, so do a payload that is not a
 *    whole number of samples (-EPROTO) and, on the socket engine, a source
 *    other than the YAML's remote_addr (-EADDRNOTAVAIL). The probe's stream
 *    is never used;
 *  - enable: a fresh setup, the same checks, the stream ID and payload size
 *    programmed into the tracker, the buffer's one server-side block created
 *    (the first time only: libiio does not enable a buffer that has no
 *    block; it is never enqueued), the poller started, then ENABLE_BUFFER. A
 *    setup outlives a failed ENABLE and would start a stream at the next one,
 *    so after any error between a successful setup and a successful ENABLE,
 *    DISABLE_BUFFER drops it before the error is returned. A refusal here
 *    fails the enable: the data path was settled at open;
 *  - disable: DISABLE_BUFFER, retried once after DQ_UDP_DISABLE_RETRY_MS if
 *    it fails (a board answers -ETIMEDOUT when its framer did not drain).
 *    When it fails twice the buffer is closed (CLOSE_BUFFER) and the error
 *    returned: the board may still be streaming. Otherwise the poller keeps
 *    draining the queue, since datagrams may still be on their way when the
 *    answer comes, until the queue has delivered nothing for
 *    DQ_UDP_DRAIN_IDLE_MS (DQ_UDP_DRAIN_MAX_MS at most), then stops.
 *
 * Receive path: one poller thread per open buffer drains the queue, runs
 * every datagram through the tracker (daqiri-udp-stream.h) and assembles the
 * payloads into the blocks the application queued, in order, unchanged: the
 * payload is the device's own sample layout, as iiod would send it over TCP.
 *
 * Raw queues (dpdk, ibverbs) hand over Ethernet frames: each is validated
 * down to UDP first (daqiri-l2.h); a rejected frame is counted (l2_rejected,
 * with the reason of the last one) and never parsed. A frame from another
 * source than the one the setup answered is foreign. With header-data split,
 * segment 0 must hold the Ethernet/IPv4/UDP headers and the datagram header,
 * segment 1 the rest of the payload; a frame cut anywhere else is malformed.
 *
 * Block states:
 *   FREE -enqueue-> QUEUED -poller-> FILLING -full-> DONE -dequeue-> FREE
 *   a gap or an overrun with lossy=0, or a stream failure:
 *   QUEUED/FILLING -> ERROR(err)
 *   cancel: QUEUED/FILLING -> CANCELLED
 * Dequeue returns 0 for DONE, err for ERROR and -ECANCELED for CANCELLED,
 * all three back to FREE; -EPERM for a FREE block. A pending block gives
 * -EBUSY without waiting, or -ETIMEDOUT after the timeout, still queued.
 *
 * A datagram that arrives with no block queued is dropped (dropped_no_block,
 * the host overrun path), and the assembler forgets the carry and pending
 * zeros it had for the stream: the next block starts at the next datagram.
 * The first drop after a delivered datagram is an overrun (overrun_events),
 * and the next datagram that finds a block treats it as a gap of unknown
 * size: with lossy=0 that block fails with -EIO and the datagram starts the
 * following one, with lossy=1 the block is flagged (blocks_with_gaps), not
 * zero-filled. Drops after a block failed for an overrun, with no datagram
 * delivered since, belong to the same overrun and fail nothing more, so a
 * consumer that queues one block at a time still gets data.
 * A gap too large to zero-fill (a resync) fails a block with -EIO whatever
 * lossy says: the current block, else the next queued one, else, with none
 * queued, the first block that a later datagram finds, which then also
 * reports the drops in between; the datagram after the loss starts the next
 * block.
 * A failure of the engine, of memory or of the stream's control fails the
 * stream: the current and queued blocks, and every block enqueued
 * afterwards, fail with that error until the buffer is disabled.
 *
 * Locking: v->lock covers the block states, the FIFO, the tracker, the
 * assembler and the counters. The poller holds it while it processes a
 * burst, never while it polls the engine or sleeps. Enable and cancel also
 * hold it to update stop (and enable to start the thread), so a cancel is
 * never undone by a concurrent enable. The stream control fields (setup,
 * pending, enabled, closed, the server-side block) belong to the thread
 * that enables, disables and closes the buffer.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* ppoll() */
#endif

#include <errno.h>
#include <iio/iio-debug.h>
#include <iio/iio.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "daqiri-l2.h"
#include "daqiri-private.h"
#include "daqiri-udp-stream.h"
#include "iiod-udp-stream.h"

/* Empty polls spun through after traffic before the poller sleeps */
#define DQ_UDP_SPIN 64

/* Bytes the assembler can carry over to the next block: one UDP payload */
#define DQ_UDP_CARRY_CAP 65536

#define DQ_UDP_PKTS_INIT 64

/* Stale bursts dropped at enable, at most (the sender may never pause) */
#define DQ_UDP_DRAIN_MAX 1024

/* Ethernet, one VLAN tag, IPv4 and UDP headers: segment 0 of a split frame
 * shorter than this cannot hold them, and a raw queue's buffer gives this
 * much of a frame to the headers when the setup asks for a payload size */
#define DQ_UDP_L2_HDR_MAX 46

/* IPv4 and UDP headers: what the IP MTU does not leave to the datagram */
#define DQ_UDP_IP_UDP_HDR 28

/* A DISABLE_BUFFER that failed is tried once more, this much later */
#define DQ_UDP_DISABLE_RETRY_MS 100

struct dq_udp_block {
	struct dq_udp_block *next;
	void *data;
	size_t size;
	size_t mapped; /* from dq_mem_alloc(), for dq_mem_free() */
	enum {
		DQ_BLK_FREE,
		DQ_BLK_QUEUED,
		DQ_BLK_FILLING,
		DQ_BLK_DONE,
		DQ_BLK_ERROR,
		DQ_BLK_CANCELLED,
	} state;
	int err;
};

struct dq_udp_buffer {
	struct iio_buffer_pdata *buf; /* its iiod connection, device and options */
	struct dq_rxq *q;
	bool raw; /* the queue hands over Ethernet frames */
	enum dq_mem mem;
	int gpu;
	const char *engine_kind;
	size_t sample_size;
	struct timespec poll_ts; /* idle wait: poll_us; zero never sleeps */

	/* Stream control: what every setup asks for; the socket engine's
	 * remote_addr (0: not checked); a setup succeeded that no ENABLE used;
	 * ENABLE succeeded and no DISABLE since; closed after DISABLE failed;
	 * the one server-side block */
	struct iiod_udp_stream_req req;
	uint32_t remote_ip;
	uint16_t remote_port;
	bool pending, enabled, closed;
	struct iio_block_pdata *srv_block;

	/* Receive buffer of the engine's socket as the kernel reported it at
	 * open, in bytes; 0: no socket (raw engines) or unknown */
	int rcvbuf;

	pthread_mutex_t lock;
	pthread_cond_t cond; /* a block left QUEUED/FILLING */
	int wake_fd;	     /* eventfd: wakes the sleeping poller */
	pthread_t thread;
	bool running;	     /* the thread exists; only enable/close touch it */
	atomic_bool stop;    /* tells the poller to return */
	atomic_ullong rx_ns; /* when the queue last delivered a burst, monotonic */

	bool cancelled;
	int fatal; /* stream failure; every block fails with it */

	struct dq_udp_block *head, *tail; /* QUEUED, in order */
	struct dq_udp_block *cur;	  /* FILLING */
	bool cur_gap;			  /* cur follows an unsized loss */
	bool delivered;			  /* the last datagram went into a block */
	bool overrun_pending;		  /* dropped datagrams the next block must report */
	bool resync_pending;		  /* a resync found no block: the next one fails */

	struct dq_udp_rx rx;
	struct dq_asm as;

	uint64_t dropped_no_block, overrun_events;
	uint64_t blocks_completed, blocks_failed, blocks_with_gaps;
	int last_error;

	/* Raw queues: where the stream comes from, from the setup */
	uint32_t src_ip;
	uint16_t src_port;
	uint64_t l2_rejected, vlan_packets, hds_packets;
	enum dq_l2_status l2_last; /* reason of the last rejection */

	/* Poller only */
	struct dq_pkt *pkts;
	unsigned int pkts_cap;
	uint8_t *split; /* DQ_UDP_CARRY_CAP: a split payload made contiguous */
};

enum dq_attr {
	DQ_ATTR_RX_PACKETS,
	DQ_ATTR_RX_BYTES,
	DQ_ATTR_RX_LOST_PACKETS,
	DQ_ATTR_RX_GAP_EVENTS,
	DQ_ATTR_RX_DROPPED_NO_BLOCK,
	DQ_ATTR_RX_OVERRUN_EVENTS,
	DQ_ATTR_RX_FOREIGN_PACKETS,
	DQ_ATTR_RX_MALFORMED,
	DQ_ATTR_RX_ZERO_FILLED_BYTES,
	DQ_ATTR_RX_LATE_PACKETS,
	DQ_ATTR_RX_DUPLICATE_PACKETS,
	DQ_ATTR_RX_L2_REJECTED,
	DQ_ATTR_RX_L2_LAST_REASON,
	DQ_ATTR_RX_VLAN_PACKETS,
	DQ_ATTR_RX_HDS_PACKETS,
	DQ_ATTR_BLOCKS_COMPLETED,
	DQ_ATTR_BLOCKS_FAILED,
	DQ_ATTR_BLOCKS_WITH_GAPS,
	DQ_ATTR_STREAM_ID,
	DQ_ATTR_LAST_ERROR,
	DQ_ATTR_LOSSY,
	DQ_ATTR_MEM_DOMAIN,
	DQ_ATTR_ENGINE_KIND,
	DQ_ATTR_DATA_PATH,
	/* Appended: the list is in this order, after the server's own */
	DQ_ATTR_RX_SOCKET_BUFFER,
	DQ_ATTR_COUNT,
};

const char *const dq_attr_names[] = {
	[DQ_ATTR_RX_PACKETS] = "daqiri_rx_packets",
	[DQ_ATTR_RX_BYTES] = "daqiri_rx_bytes",
	[DQ_ATTR_RX_LOST_PACKETS] = "daqiri_rx_lost_packets",
	[DQ_ATTR_RX_GAP_EVENTS] = "daqiri_rx_gap_events",
	[DQ_ATTR_RX_DROPPED_NO_BLOCK] = "daqiri_rx_dropped_no_block",
	[DQ_ATTR_RX_OVERRUN_EVENTS] = "daqiri_rx_overrun_events",
	[DQ_ATTR_RX_FOREIGN_PACKETS] = "daqiri_rx_foreign_packets",
	[DQ_ATTR_RX_MALFORMED] = "daqiri_rx_malformed",
	[DQ_ATTR_RX_ZERO_FILLED_BYTES] = "daqiri_rx_zero_filled_bytes",
	[DQ_ATTR_RX_LATE_PACKETS] = "daqiri_rx_late_packets",
	[DQ_ATTR_RX_DUPLICATE_PACKETS] = "daqiri_rx_duplicate_packets",
	[DQ_ATTR_RX_L2_REJECTED] = "daqiri_rx_l2_rejected",
	[DQ_ATTR_RX_L2_LAST_REASON] = "daqiri_rx_l2_last_reason",
	[DQ_ATTR_RX_VLAN_PACKETS] = "daqiri_rx_vlan_packets",
	[DQ_ATTR_RX_HDS_PACKETS] = "daqiri_rx_hds_packets",
	[DQ_ATTR_BLOCKS_COMPLETED] = "daqiri_blocks_completed",
	[DQ_ATTR_BLOCKS_FAILED] = "daqiri_blocks_failed",
	[DQ_ATTR_BLOCKS_WITH_GAPS] = "daqiri_blocks_with_gaps",
	[DQ_ATTR_STREAM_ID] = "daqiri_stream_id",
	[DQ_ATTR_LAST_ERROR] = "daqiri_last_error",
	[DQ_ATTR_LOSSY] = "daqiri_lossy",
	[DQ_ATTR_MEM_DOMAIN] = "daqiri_mem_domain",
	[DQ_ATTR_ENGINE_KIND] = "daqiri_engine_kind",
	[DQ_ATTR_DATA_PATH] = "daqiri_data_path",
	[DQ_ATTR_RX_SOCKET_BUFFER] = "daqiri_rx_socket_buffer",
};

const unsigned int dq_nb_attrs = DQ_ATTR_COUNT;

static unsigned long long dq_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}

static void dq_sleep_ms(unsigned int ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (long)(ms % 1000) * 1000000L,
	};

	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static void dq_print_addr(char *dst, size_t len, uint32_t ip, uint16_t port)
{
	snprintf(dst, len, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff,
			ip & 0xff, (unsigned int)port);
}

static void dq_udp_wake(struct dq_udp_buffer *v)
{
	uint64_t one = 1;

	if (write(v->wake_fd, &one, sizeof(one)) < 0) {
		/* Only fails when the counter is saturated: already awake */
	}
}

/* Idle: wait for the wake fd at most poll_us (microsecond resolution) */
static void dq_udp_sleep(struct dq_udp_buffer *v)
{
	struct pollfd pfd = { .fd = v->wake_fd, .events = POLLIN };
	uint64_t val;

	if (ppoll(&pfd, 1, &v->poll_ts, NULL) > 0 && (pfd.revents & POLLIN)) {
		if (read(v->wake_fd, &val, sizeof(val)) < 0) {
			/* Nothing to drain */
		}
	}
}

/* Finish the current block: DONE, or ERROR(err) */
static void dq_udp_complete(struct dq_udp_buffer *v, int err)
{
	struct dq_udp_block *b = v->cur;

	if (v->cur_gap || v->as.had_gap)
		v->blocks_with_gaps++;

	if (err) {
		b->state = DQ_BLK_ERROR;
		b->err = err;
		v->blocks_failed++;
		v->last_error = err;
	} else {
		b->state = DQ_BLK_DONE;
		v->blocks_completed++;
	}

	v->cur = NULL;
	pthread_cond_broadcast(&v->cond);
}

/* Make the next queued block current. Blocks that the carry or pending
 * zeros fill on their own complete here. False when none is queued. */
static bool dq_udp_next(struct dq_udp_buffer *v)
{
	struct dq_udp_block *b;

	while ((b = v->head)) {
		v->head = b->next;
		if (!v->head)
			v->tail = NULL;
		b->next = NULL;

		b->state = DQ_BLK_FILLING;
		v->cur = b;
		v->cur_gap = false;
		dq_asm_attach(&v->as, b->data, b->size);
		if (!dq_asm_done(&v->as))
			return true;

		dq_udp_complete(v, 0);
	}

	return false;
}

/* Fail the current and the queued blocks, and those enqueued later */
static void dq_udp_fail_stream(struct dq_udp_buffer *v, int err)
{
	struct dq_udp_block *b;

	v->fatal = err;
	if (v->cur)
		dq_udp_complete(v, err);

	while ((b = v->head)) {
		v->head = b->next;
		b->next = NULL;
		b->state = DQ_BLK_ERROR;
		b->err = err;
		v->blocks_failed++;
	}
	v->tail = NULL;
	v->last_error = err;

	dq_asm_reset(&v->as);
	pthread_cond_broadcast(&v->cond);
}

/* No block for a datagram: drop it. The first drop after a delivered one
 * starts an overrun, which the next block reports. */
static void dq_udp_drop(struct dq_udp_buffer *v)
{
	v->dropped_no_block++;
	if (v->delivered) {
		v->overrun_events++;
		v->overrun_pending = true;
		v->delivered = false;
	}

	/* The next block starts with the next datagram */
	dq_asm_reset(&v->as);
}

/* Assemble one payload that follows <gap> lost datagrams */
static void dq_udp_deliver(
		struct dq_udp_buffer *v, const uint8_t *payload, size_t len, uint32_t gap)
{
	bool done, flag = false;
	int ret;

	for (;;) {
		if (!v->cur && !dq_udp_next(v)) {
			dq_udp_drop(v);
			return;
		}

		if (v->resync_pending) {
			/* A gap too large to fill came while no block was
			 * queued: this block fails for it whatever lossy says,
			 * and reports the overrun of those drops as well. The
			 * datagram starts the next one. */
			v->resync_pending = false;
			v->overrun_pending = false;
			v->cur_gap = true;
			dq_udp_complete(v, -EIO);
			dq_asm_reset(&v->as);
			gap = 0;
			continue;
		}

		if (v->overrun_pending) {
			/* Datagrams were dropped since the last delivered one:
			 * a gap of unknown size. Fail the block like a sized
			 * gap, or with lossy=1 flag it: there is nothing to
			 * fill. */
			v->overrun_pending = false;
			if (v->as.lossy)
				flag = true;
			else if (!gap)
				gap = 1;
		}

		if (flag)
			v->cur_gap = true;

		ret = dq_asm_push(&v->as, payload, len, gap, &done);
		if (ret >= 0) {
			v->delivered = true;
			if (done)
				dq_udp_complete(v, 0);
			return;
		}

		if (ret == -ENOSPC) {
			/* Not expected: a full block never stays current */
			dq_udp_complete(v, 0);
			continue;
		}

		dq_udp_complete(v, ret);
		if (ret != -EIO) {
			/* -ENOBUFS: a payload larger than block plus carry */
			dq_asm_reset(&v->as);
			return;
		}

		/* Gap with lossy=0: this datagram starts the next block */
		gap = 0;
	}
}

/*
 * One datagram into the stream of v, which the caller locks (the poller).
 * The first hdr_len bytes are at hdr; with header-data split payload_len
 * more follow at payload, otherwise payload_len is 0.
 */
static void dq_udp_packet_segs(struct dq_udp_buffer *v, const uint8_t *hdr, uint32_t hdr_len,
		const uint8_t *payload, uint32_t payload_len)
{
	struct dq_udp_data out;
	const uint8_t *data;
	size_t head;
	int ret;

	ret = dq_udp_rx_header(&v->rx, hdr, hdr_len, (size_t)hdr_len + payload_len, &out);
	if (ret == DQ_UDP_SKIP || v->fatal)
		return;

	if (out.resync) {
		/* Too large to fill: resync on this datagram. A block fails as
		 * with lossy=0: the current one, or with none current (the gap
		 * fell on a block boundary) the next one, attached empty after
		 * dropping what the assembler owed, or with none queued either
		 * the first one a later datagram finds (resync_pending). */
		if (!v->cur) {
			dq_asm_reset(&v->as);
			dq_udp_next(v);
		}
		if (v->cur) {
			v->cur_gap = true;
			dq_udp_complete(v, -EIO);
			/* That block reports a pending overrun as well */
			v->overrun_pending = false;
		} else {
			v->resync_pending = true;
		}
		dq_asm_reset(&v->as);
		out.gap = 0;
	}

	data = out.payload;
	if (payload_len) {
		/* The payload bytes in hdr, then those in the second segment,
		 * pushed as one payload: the assembler sizes a lossy zero-fill
		 * from the length pushed. */
		head = hdr_len - IIOD_UDP_STREAM_HDR_LEN;
		if (!head) {
			data = payload;
		} else {
			if (!v->split) {
				v->split = malloc(DQ_UDP_CARRY_CAP);
				if (!v->split) {
					dq_udp_fail_stream(v, -ENOMEM);
					return;
				}
			}
			memcpy(v->split, out.payload, head);
			memcpy(v->split + head, payload, out.len - head);
			data = v->split;
		}
	}

	/* A gap this large was a resync above: it fits */
	dq_udp_deliver(v, data, out.len, (uint32_t)out.gap);
}

/* One frame of a raw queue: Ethernet, at most one VLAN tag, IPv4, UDP and
 * the datagram, segment 0 then, with header-data split, segment 1 */
static void dq_udp_raw_packet(struct dq_udp_buffer *v, const struct dq_pkt *pkt)
{
	uint32_t frame_len = pkt->len, hdr_len;
	enum dq_l2_status st;
	struct dq_l2_info l2;

	if (pkt->payload_len) {
		/* The headers are all in segment 0, the lengths they give
		 * cover both segments. The parser reads no further than the
		 * headers, which a segment 0 this long holds. */
		if (pkt->len < DQ_UDP_L2_HDR_MAX) {
			v->rx.c.malformed++;
			return;
		}
		frame_len += pkt->payload_len;
	}

	st = dq_l2_parse(pkt->data, frame_len, &l2);
	if (st != DQ_L2_OK) {
		v->l2_rejected++;
		v->l2_last = st;
		return;
	}
	if (l2.vlan_id)
		v->vlan_packets++;
	if (pkt->payload_len)
		v->hds_packets++;

	/* Not from where the setup said the stream comes from */
	if ((v->src_ip && l2.ip_src != v->src_ip) || (v->src_port && l2.udp_src != v->src_port)) {
		v->rx.c.foreign++;
		return;
	}

	/* l2.payload_len is the UDP payload: no Ethernet padding, and with
	 * header-data split it spans both segments */
	hdr_len = pkt->len - l2.payload_off;
	if (hdr_len > l2.payload_len)
		hdr_len = l2.payload_len;
	dq_udp_packet_segs(v, pkt->data + l2.payload_off, hdr_len, pkt->payload,
			l2.payload_len - hdr_len);
}

static void dq_udp_burst(struct dq_udp_buffer *v, const struct dq_burst *b)
{
	struct dq_pkt *pkts;
	unsigned int i, n;

	if (b->nb_pkts > v->pkts_cap) {
		pkts = realloc(v->pkts, b->nb_pkts * sizeof(*pkts));
		if (!pkts) {
			pthread_mutex_lock(&v->lock);
			if (!v->fatal)
				dq_udp_fail_stream(v, -ENOMEM);
			pthread_mutex_unlock(&v->lock);
			return;
		}
		v->pkts = pkts;
		v->pkts_cap = b->nb_pkts;
	}

	n = dq_burst_pkts(b, v->pkts, v->pkts_cap);

	pthread_mutex_lock(&v->lock);
	for (i = 0; i < n; i++) {
		if (b->raw)
			dq_udp_raw_packet(v, &v->pkts[i]);
		else
			dq_udp_packet_segs(v, v->pkts[i].data, v->pkts[i].len, NULL, 0);
	}
	pthread_mutex_unlock(&v->lock);
}

static void *dq_udp_poller(void *d)
{
	struct dq_udp_buffer *v = d;
	unsigned int idle = 0;
	struct dq_burst b;
	int ret;

	/* The default 50 us timer slack would double a 50 us idle wait */
	prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);

	while (!atomic_load(&v->stop)) {
		ret = dq_rxq_poll(v->q, &b);
		if (ret == -EAGAIN) {
			/* Spin a little after traffic, then sleep; idle stays
			 * at the limit until the next burst */
			if (idle < DQ_UDP_SPIN)
				idle++;
			else
				dq_udp_sleep(v);
			continue;
		}

		/* The process is exiting: no more engine calls */
		if (ret == -ESHUTDOWN)
			break;

		idle = 0;
		if (ret < 0) {
			pthread_mutex_lock(&v->lock);
			if (!v->fatal)
				dq_udp_fail_stream(v, ret);
			pthread_mutex_unlock(&v->lock);
			dq_udp_sleep(v);
			continue;
		}

		atomic_store(&v->rx_ns, dq_now_ns());
		dq_udp_burst(v, &b);
		dq_burst_free(&b);
	}

	return NULL;
}

static struct dq_udp_buffer *dq_udp_new(struct iio_buffer_pdata *buf, size_t sample_size)
{
	const struct dq_opts *o = &buf->ctx->opts;
	struct dq_udp_buffer *v;
	pthread_condattr_t attr;
	int ret;

	v = calloc(1, sizeof(*v));
	if (!v)
		return iio_ptr(-ENOMEM);

	v->buf = buf;
	v->q = buf->rxq;
	v->mem = o->mem;
	v->gpu = o->gpu;
	v->engine_kind = buf->ctx->engine_kind;
	v->sample_size = sample_size;
	v->poll_ts.tv_sec = (time_t)(o->poll_us / 1000000);
	v->poll_ts.tv_nsec = (long)(o->poll_us % 1000000) * 1000L;
	atomic_init(&v->stop, true);
	atomic_init(&v->rx_ns, 0ULL);
	dq_udp_rx_init(&v->rx);

	/* The device's own sample layout: copied as it comes, never swapped */
	ret = dq_asm_init(&v->as, DQ_SWAP_NONE, o->lossy, DQ_UDP_CARRY_CAP);
	if (ret)
		goto err_free;

	v->pkts = calloc(DQ_UDP_PKTS_INIT, sizeof(*v->pkts));
	if (!v->pkts) {
		ret = -ENOMEM;
		goto err_asm;
	}
	v->pkts_cap = DQ_UDP_PKTS_INIT;

	v->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (v->wake_fd < 0) {
		ret = -errno;
		goto err_pkts;
	}

	ret = -pthread_mutex_init(&v->lock, NULL);
	if (ret)
		goto err_close;

	/* Dequeue timeouts are durations: wait on the monotonic clock */
	ret = -pthread_condattr_init(&attr);
	if (!ret) {
		ret = -pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
		if (!ret)
			ret = -pthread_cond_init(&v->cond, &attr);
		pthread_condattr_destroy(&attr);
	}
	if (ret)
		goto err_mutex;

	return v;

err_mutex:
	pthread_mutex_destroy(&v->lock);
err_close:
	close(v->wake_fd);
err_pkts:
	free(v->pkts);
err_asm:
	dq_asm_fini(&v->as);
err_free:
	free(v);
	return iio_ptr(ret);
}

static void dq_udp_free(struct dq_udp_buffer *v)
{
	pthread_cond_destroy(&v->cond);
	pthread_mutex_destroy(&v->lock);
	close(v->wake_fd);
	free(v->split);
	free(v->pkts);
	dq_asm_fini(&v->as);
	free(v);
}

/* Stop and join the poller */
static void dq_udp_stop(struct dq_udp_buffer *v)
{
	atomic_store(&v->stop, true);
	dq_udp_wake(v);

	if (v->running) {
		pthread_join(v->thread, NULL);
		v->running = false;
	}
}

/* A new stream from <resp>: program the tracker, drop what queued up while
 * disabled, start the poller */
static int dq_udp_rx_begin(struct dq_udp_buffer *v, const struct iiod_udp_stream_resp *resp)
{
	struct dq_burst b;
	unsigned int i;
	int ret;

	pthread_mutex_lock(&v->lock);
	if (v->cancelled) {
		pthread_mutex_unlock(&v->lock);
		return -ECANCELED;
	}

	/* The tracker expects sequence 0 of the new stream ID (counters
	 * kept) and the assembler forgets the old position */
	dq_udp_rx_start(&v->rx, resp->stream_id, resp->payload_bytes);
	dq_asm_reset(&v->as);
	v->delivered = false;
	v->overrun_pending = false;
	v->resync_pending = false;
	v->fatal = 0;
	/* The socket engine filters on the source itself (remote_addr) */
	if (v->raw) {
		v->src_ip = resp->src_ip;
		v->src_port = resp->src_port;
	}
	pthread_mutex_unlock(&v->lock);

	/* Whatever queued up while disabled is stale */
	for (i = 0; i < DQ_UDP_DRAIN_MAX && !dq_rxq_poll(v->q, &b); i++)
		dq_burst_free(&b);

	/* Under the lock: a cancel that came first is seen here, and one that
	 * comes later sets stop after this, so it is never overwritten */
	pthread_mutex_lock(&v->lock);
	if (v->cancelled) {
		ret = -ECANCELED;
	} else {
		atomic_store(&v->stop, false);
		ret = -pthread_create(&v->thread, NULL, dq_udp_poller, v);
		if (ret)
			atomic_store(&v->stop, true);
		else
			v->running = true;
	}
	pthread_mutex_unlock(&v->lock);

	return ret;
}

/* Stop the poller; a partly filled block starts over from the next datagram */
static void dq_udp_rx_end(struct dq_udp_buffer *v)
{
	dq_udp_stop(v);

	pthread_mutex_lock(&v->lock);
	if (v->cur) {
		v->cur->state = DQ_BLK_QUEUED;
		v->cur->next = v->head;
		v->head = v->cur;
		if (!v->tail)
			v->tail = v->cur;
		v->cur = NULL;
	}
	v->fatal = 0;
	pthread_mutex_unlock(&v->lock);
}

/* A setup outlives a failed ENABLE: without a DISABLE, the next ENABLE would
 * start a stream nobody receives */
static void dq_udp_drop_setup(struct dq_udp_buffer *v)
{
	int ret;

	if (!v->pending)
		return;

	v->pending = false;
	ret = iiod_client_enable_buffer(v->buf->iiod.pdata, 0, false, false);
	if (ret)
		dev_perror(v->buf->dev, ret, "Unable to drop the pending UDP stream setup");
}

/* What the server answered must fit the request, the channels and, on the
 * socket engine, where the engine receives from */
static int dq_udp_check_resp(struct iio_buffer_pdata *buf, const struct iiod_udp_stream_req *req,
		const struct iiod_udp_stream_resp *resp, size_t sample_size, uint32_t remote_ip,
		uint16_t remote_port)
{
	char from[32], yaml[32];

	if (!resp->payload_bytes || resp->payload_bytes > req->max_payload ||
			resp->payload_bytes % sample_size) {
		dev_err(buf->dev,
				"UDP stream: %u-byte payloads for %zu-byte samples, at most %u asked\n",
				(unsigned int)resp->payload_bytes, sample_size,
				(unsigned int)req->max_payload);
		return -EPROTO;
	}

	if ((remote_ip && resp->src_ip != remote_ip) ||
			(remote_port && resp->src_port != remote_port)) {
		dq_print_addr(from, sizeof(from), resp->src_ip, resp->src_port);
		dq_print_addr(yaml, sizeof(yaml), remote_ip, remote_port);
		dev_err(buf->dev,
				"UDP stream: the server sends from %s, but the DAQIRI socket engine only receives from remote_addr %s of %s\n",
				from, yaml, buf->ctx->opts.yaml);
		return -EADDRNOTAVAIL;
	}

	return 0;
}

/* The local IPv4 address of the management connection, host order, or 0 */
static uint32_t dq_udp_control_ipv4(const struct iio_context_pdata *pdata)
{
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);
	const struct sockaddr_in6 *sin6;
	uint32_t ip;

	if (getsockname(pdata->mgmt.fd, (struct sockaddr *)&ss, &len))
		return 0;

	if (ss.ss_family == AF_INET)
		return ntohl(((const struct sockaddr_in *)&ss)->sin_addr.s_addr);

	/* An IPv4-mapped IPv6 address */
	sin6 = (const struct sockaddr_in6 *)&ss;
	if (ss.ss_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		memcpy(&ip, &sin6->sin6_addr.s6_addr[12], sizeof(ip));
		return ntohl(ip);
	}

	return 0;
}

/* What every setup of the buffer asks for: the queue's UDP endpoint and the
 * largest payload its buffers, the interface and the socket engine take */
static int dq_udp_request(struct iio_buffer_pdata *buf, int port, struct iiod_udp_stream_req *req,
		uint32_t *remote_ip, uint16_t *remote_port, bool *raw)
{
	struct iio_context_pdata *pdata = buf->ctx;
	const struct dq_opts *o = &pdata->opts;
	struct dq_rxq_limits lim;
	uint16_t local_port;
	uint32_t local_ip;
	size_t max;
	int ret;

	ret = dq_engine_udp_endpoint(pdata->engine, port, o->rxq, &local_ip, &local_port, remote_ip,
			remote_port);
	if (!ret)
		ret = dq_engine_rx_limits(pdata->engine, port, o->rxq, &lim);
	if (ret) {
		dev_perror(buf->dev, ret, "No UDP endpoint for queue %d in %s", o->rxq, o->yaml);
		return ret;
	}

	if (!local_port) {
		dev_err(buf->dev, "%s gives no UDP port for queue %d\n", o->yaml, o->rxq);
		return -EINVAL;
	}

	/* Precedence: udp_dst=, the engine's address, the control connection's */
	req->dst_ip = o->udp_dst ? o->udp_dst : local_ip;
	if (!req->dst_ip)
		req->dst_ip = dq_udp_control_ipv4(pdata);
	if (!req->dst_ip) {
		dev_err(buf->dev, "No IPv4 address to receive the UDP stream on: set udp_dst=\n");
		return -EADDRNOTAVAIL;
	}
	req->dst_port = local_port;

	/* A raw queue's buffer holds the whole frame, a VLAN tag maybe */
	max = lim.buf_size;
	if (lim.raw)
		max = max > DQ_UDP_L2_HDR_MAX ? max - DQ_UDP_L2_HDR_MAX : 0;
	if (lim.max_udp_payload && max > lim.max_udp_payload)
		max = lim.max_udp_payload;
	/* No IP fragments */
	if (lim.mtu > DQ_UDP_IP_UDP_HDR && max > lim.mtu - DQ_UDP_IP_UDP_HDR)
		max = lim.mtu - DQ_UDP_IP_UDP_HDR;
	max = max > IIOD_UDP_STREAM_HDR_LEN ? max - IIOD_UDP_STREAM_HDR_LEN : 0;
	if (max > UINT16_MAX)
		max = UINT16_MAX;
	if (max < 16) {
		dev_err(buf->dev,
				"The packet buffers of queue %d are too small for UDP sample mode\n",
				o->rxq);
		return -EINVAL;
	}
	req->max_payload = (uint16_t)max;

	/* Only the socket engine pins the source, by connecting to it */
	*raw = lim.raw;
	if (lim.raw) {
		*remote_ip = 0;
		*remote_port = 0;
	}

	return 0;
}

/* Whether this process may force a receive buffer past net.core.rmem_max
 * (SO_RCVBUFFORCE): tried on a socket of its own, since CAP_NET_ADMIN counts
 * only in the initial user namespace, and DAQIRI logs a failed setsockopt()
 * as an error */
static bool dq_can_force_rcvbuf(void)
{
	int fd, val = 4096;
	bool ok;

	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;
	ok = !setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &val, sizeof(val));
	close(fd);

	return ok;
}

/* The receive buffer of the queue's socket as the kernel reports it, into
 * *got; logs a failure */
static int dq_udp_read_rcvbuf(struct dq_udp_buffer *v, int *got)
{
	char err[256];
	int ret;

	ret = dq_rxq_get_rcvbuf(v->q, got);
	if (ret) {
		iio_strerror(-ret, err, sizeof(err));
		dev_warn(v->buf->dev, "Unable to read the UDP receive buffer of queue %d: %s\n",
				v->buf->ctx->opts.rxq, err);
	}

	return ret;
}

/*
 * Socket engine: size the receive buffer of the queue's socket, which DAQIRI
 * leaves at net.core.rmem_default (some 16 jumbo datagrams), before the first
 * enable, and keep what the kernel made of it in v->rcvbuf. rcvbuf= asks for
 * that many bytes; without it num_bufs * buf_size of the queue, from 4 to
 * 64 MiB, so that the kernel holds about as many datagrams as the engine's
 * queue when the consumer falls behind. rcvbuf=0 asks for nothing and looks
 * nothing up: the kernel's default stays, and v->rcvbuf 0. SO_RCVBUF first,
 * which net.core.rmem_max caps; when the kernel granted less than asked and
 * the process may force it, SO_RCVBUFFORCE. The kernel reports twice what it
 * granted, so that is what is compared with twice the target. Never fails
 * the open: an unsized socket still receives, only it drops sooner, and the
 * warnings say so.
 */
static void dq_udp_size_rcvbuf(struct dq_udp_buffer *v, int port)
{
	struct iio_buffer_pdata *buf = v->buf;
	const struct dq_opts *o = &buf->ctx->opts;
	struct dq_rxq_limits lim;
	uint64_t target = 0;
	char err[256];
	int ret, got;

	if (v->raw || !o->rcvbuf)
		return;

	if (o->rcvbuf > 0) {
		target = (uint64_t)o->rcvbuf;
	} else {
		ret = dq_engine_rx_limits(buf->ctx->engine, port, o->rxq, &lim);
		if (ret) {
			dev_perror(buf->dev, ret, "No packet buffers for queue %d", o->rxq);
			return;
		}
		target = dq_udp_rcvbuf_target(lim.num_bufs, lim.buf_size);
	}
	if (target > INT_MAX)
		target = INT_MAX;

	ret = dq_rxq_set_rcvbuf(v->q, (int)target, false);
	if (ret == -EADDRINUSE) {
		dev_warn(buf->dev,
				"The UDP receive buffer of queue %d is left as it is: another interface of %s has the same local_addr, and DAQIRI cannot tell their sockets apart\n",
				o->rxq, o->yaml);
	} else if (ret) {
		iio_strerror(-ret, err, sizeof(err));
		dev_warn(buf->dev,
				"Unable to set the UDP receive buffer of queue %d to %" PRIu64
				" bytes: %s\n",
				o->rxq, target, err);
	}

	if (dq_udp_read_rcvbuf(v, &got))
		return;

	/* Granted less than asked: force it where the process may */
	if (!ret && (uint64_t)got < 2 * target && dq_can_force_rcvbuf() &&
			!dq_rxq_set_rcvbuf(v->q, (int)target, true) && dq_udp_read_rcvbuf(v, &got))
		return;

	v->rcvbuf = got;

	if (!ret && (uint64_t)got < 2 * target)
		dev_warn(buf->dev,
				"The kernel granted %d bytes of the %" PRIu64
				" asked for as the UDP receive buffer of queue %d: datagrams may be lost when the consumer falls behind. "
				"Raise net.core.rmem_max on this host, or set rcvbuf= (0 leaves the kernel's default)\n",
				got / 2, target, o->rxq);
}

static bool dq_udp_refused(int err)
{
	return err == -ENOTSUP || err == -EINVAL || err == -EBUSY;
}

int dq_udp_open_buffer(struct iio_buffer_pdata *buf, struct iio_channels_mask *mask)
{
	struct iio_context_pdata *pdata = buf->ctx;
	const struct dq_opts *o = &pdata->opts;
	struct iiod_udp_stream_resp resp;
	struct iiod_udp_stream_req req;
	struct dq_udp_buffer *v;
	uint16_t remote_port;
	uint32_t remote_ip;
	char err[256];
	bool raw;
	ssize_t ss;
	int port, ret;

	if (o->mem == DQ_MEM_DEVICE) {
		dev_err(buf->dev, "mem=device is not supported in this version\n");
		return -ENOTSUP;
	}

	ss = iio_device_get_sample_size(buf->dev, mask);
	if (ss <= 0)
		return ss < 0 ? (int)ss : -EINVAL;

	pthread_mutex_lock(&pdata->lock);
	ret = pdata->engine ? 0 : dq_engine_acquire(o->yaml, &pdata->engine, err, sizeof(err));
	pthread_mutex_unlock(&pdata->lock);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to start the DAQIRI engine");
		dev_err(buf->dev, "%s: %s\n", o->yaml, err);
		return ret;
	}

	ret = dq_engine_get_port(pdata->engine, o->iface, &port);
	if (ret) {
		dev_err(buf->dev, "No interface '%s' in %s\n", o->iface, o->yaml);
		return ret;
	}

	ret = dq_rxq_open(pdata->engine, port, o->rxq, &buf->rxq);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to open RX queue");
		return ret;
	}

	ret = dq_udp_request(buf, port, &req, &remote_ip, &remote_port, &raw);
	if (ret)
		goto err_close_rxq;

	/* Logs its own errors */
	ret = dq_iiod_open_buffer(buf, mask);
	if (ret)
		goto err_close_rxq;

	/* The probe: can the server stream this buffer? Its stream is never
	 * used, every enable gets a setup of its own */
	ret = iiod_client_udp_stream_setup(buf->iiod.pdata, &req, &resp);
	if (dq_udp_refused(ret) && o->data == DQ_DATA_AUTO) {
		dev_info(buf->dev, "UDP sample mode refused (%d): the buffer streams over TCP\n",
				ret);
		dq_rxq_close(buf->rxq);
		buf->rxq = NULL;
		buf->path = DQ_PATH_TCP;
		return 0;
	}
	if (dq_udp_refused(ret)) {
		dev_perror(buf->dev, ret, "The server refused UDP sample mode for this buffer");
		ret = -ENOTSUP;
		goto err_close_iiod;
	}
	if (ret) {
		dev_perror(buf->dev, ret, "UDP sample mode setup failed");
		goto err_close_iiod;
	}

	v = dq_udp_new(buf, (size_t)ss);
	ret = iio_err(v);
	if (ret)
		goto err_drop_setup;

	v->raw = raw;
	v->req = req;
	v->remote_ip = remote_ip;
	v->remote_port = remote_port;
	v->pending = true;

	ret = dq_udp_check_resp(buf, &req, &resp, (size_t)ss, remote_ip, remote_port);
	if (ret) {
		dq_udp_drop_setup(v);
		dq_udp_free(v);
		goto err_close_iiod;
	}

	dq_udp_size_rcvbuf(v, port);

	buf->udp = v;
	buf->path = DQ_PATH_UDP;

	return 0;

err_drop_setup:
	/* There is no v to remember the pending setup: drop it here */
	iiod_client_enable_buffer(buf->iiod.pdata, 0, false, false);
err_close_iiod:
	dq_iiod_close_buffer(buf);
err_close_rxq:
	dq_rxq_close(buf->rxq);
	buf->rxq = NULL;
	return ret;
}

static int dq_udp_enable(struct dq_udp_buffer *v, size_t nb_samples)
{
	struct iio_buffer_pdata *buf = v->buf;
	struct iiod_udp_stream_resp resp;
	struct iio_block_pdata *block;
	bool cancelled;
	void *data;
	int ret;

	if (v->closed)
		return -EBADF;

	pthread_mutex_lock(&v->lock);
	cancelled = v->cancelled;
	pthread_mutex_unlock(&v->lock);
	if (cancelled)
		return -ECANCELED;
	if (v->enabled)
		return 0;

	/* A fresh stream for every enable; a refusal here is an error, the
	 * data path was settled at open */
	ret = iiod_client_udp_stream_setup(buf->iiod.pdata, &v->req, &resp);
	if (ret) {
		dev_perror(buf->dev, ret, "UDP sample mode setup failed");
		goto err_drop_setup;
	}
	v->pending = true;

	ret = dq_udp_check_resp(buf, &v->req, &resp, v->sample_size, v->remote_ip, v->remote_port);
	if (ret)
		goto err_drop_setup;

	/* libiio does not enable a buffer that has no block: one, never
	 * enqueued, for the buffer's lifetime */
	if (!v->srv_block) {
		block = iiod_client_create_block(buf->iiod.pdata, resp.payload_bytes, &data);
		ret = iio_err(block);
		if (ret) {
			dev_perror(buf->dev, ret, "Unable to create the server-side block");
			goto err_drop_setup;
		}
		v->srv_block = block;
	}

	ret = dq_udp_rx_begin(v, &resp);
	if (ret)
		goto err_drop_setup;

	ret = iiod_client_enable_buffer(buf->iiod.pdata, nb_samples, true, false);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to enable the buffer");
		dq_udp_rx_end(v);
		goto err_drop_setup;
	}

	v->pending = false;
	v->enabled = true;

	return 0;

err_drop_setup:
	dq_udp_drop_setup(v);
	return ret;
}

/* After a successful DISABLE_BUFFER: let the poller go on until the queue
 * has delivered nothing for DQ_UDP_DRAIN_IDLE_MS since the answer or its last
 * burst, whichever came later, but not past DQ_UDP_DRAIN_MAX_MS */
static void dq_udp_drain(struct dq_udp_buffer *v)
{
	unsigned long long start = dq_now_ns(), now, last;

	for (;;) {
		/* The burst time first, the clock then: a burst stored in
		 * between is no later than now */
		last = atomic_load(&v->rx_ns);
		now = dq_now_ns();
		if (dq_udp_drain_over(start, last, now))
			return;

		dq_sleep_ms(1);
	}
}

/* After DISABLE_BUFFER failed twice: the board may still be streaming, so
 * the buffer is closed rather than left half disabled */
static void dq_udp_close_server(struct dq_udp_buffer *v, int err)
{
	struct iio_buffer_pdata *buf = v->buf;

	pthread_mutex_lock(&v->lock);
	dq_udp_fail_stream(v, err);
	pthread_mutex_unlock(&v->lock);

	/* FREE_BLOCK needs the buffer that CLOSE_BUFFER frees */
	if (v->srv_block)
		iiod_client_free_block(v->srv_block);
	v->srv_block = NULL;
	iiod_client_close_buffer(buf->iiod.pdata);
	buf->iiod.pdata = NULL;

	v->closed = true;
	v->enabled = false;
}

static int dq_udp_disable(struct dq_udp_buffer *v)
{
	struct iio_buffer_pdata *buf = v->buf;
	bool was_enabled = v->enabled, cancelled;
	int ret;

	/* The failed disable that closed it said so already */
	if (v->closed)
		return 0;

	/* A cancel left nothing to send DISABLE_BUFFER on: the stream ends
	 * with CLOSE_BUFFER */
	pthread_mutex_lock(&v->lock);
	cancelled = v->cancelled;
	pthread_mutex_unlock(&v->lock);
	if (cancelled) {
		dq_udp_stop(v);
		v->pending = false;
		v->enabled = false;
		return 0;
	}

	ret = iiod_client_enable_buffer(buf->iiod.pdata, 0, false, false);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to disable the buffer, trying again");
		dq_sleep_ms(DQ_UDP_DISABLE_RETRY_MS);
		ret = iiod_client_enable_buffer(buf->iiod.pdata, 0, false, false);
	}

	/* Either way, DISABLE_BUFFER or CLOSE_BUFFER dropped any setup */
	v->pending = false;

	if (ret) {
		dev_perror(buf->dev, ret, "Unable to disable the buffer: closing it");
		dq_udp_stop(v);
		dq_udp_close_server(v, ret);
		return ret;
	}

	/* The board completes the datagram in progress before it answers;
	 * datagrams may still be on their way */
	if (was_enabled)
		dq_udp_drain(v);

	dq_udp_rx_end(v);
	v->enabled = false;

	return 0;
}

int dq_udp_enable_buffer(struct iio_buffer_pdata *buf, size_t nb_samples, bool enable)
{
	return enable ? dq_udp_enable(buf->udp, nb_samples) : dq_udp_disable(buf->udp);
}

static void dq_udp_cancel(struct dq_udp_buffer *v)
{
	struct dq_udp_block *b;

	pthread_mutex_lock(&v->lock);
	v->cancelled = true;

	if (v->cur) {
		v->cur->state = DQ_BLK_CANCELLED;
		v->cur = NULL;
	}
	while ((b = v->head)) {
		v->head = b->next;
		b->next = NULL;
		b->state = DQ_BLK_CANCELLED;
	}
	v->tail = NULL;
	dq_asm_reset(&v->as);
	v->delivered = false;
	v->overrun_pending = false;
	v->resync_pending = false;
	/* Under the lock, so that a concurrent enable cannot clear it */
	atomic_store(&v->stop, true);

	pthread_cond_broadcast(&v->cond);
	pthread_mutex_unlock(&v->lock);

	dq_udp_wake(v);
}

/* Wakes whatever is blocked on the buffer: dequeues, and the stream control
 * on its connection, which stays unusable afterwards; the stream ends with
 * CLOSE_BUFFER */
void dq_udp_cancel_buffer(struct iio_buffer_pdata *buf)
{
	dq_udp_cancel(buf->udp);
	dq_mgmt_ops.cancel((struct iiod_client_pdata *)&buf->iiod.io);
}

void dq_udp_close_buffer(struct iio_buffer_pdata *buf)
{
	struct dq_udp_buffer *v = buf->udp;

	dq_udp_cancel(v);
	dq_udp_stop(v);

	/* FREE_BLOCK needs the buffer that CLOSE_BUFFER frees */
	if (v->srv_block)
		iiod_client_free_block(v->srv_block);
	dq_iiod_close_buffer(buf);

	dq_udp_free(v);
	buf->udp = NULL;

	dq_rxq_close(buf->rxq);
	buf->rxq = NULL;
}

struct dq_udp_block *dq_udp_block_new(struct dq_udp_buffer *v, size_t size, void **data)
{
	struct dq_udp_block *b;
	int ret;

	/* Blocks hold whole samples: the assembler never splits one */
	if (!size || size % v->sample_size)
		return iio_ptr(-EINVAL);

	b = calloc(1, sizeof(*b));
	if (!b)
		return iio_ptr(-ENOMEM);

	ret = dq_mem_alloc(v->mem, v->gpu, size, &b->data, &b->mapped);
	if (ret) {
		free(b);
		return iio_ptr(ret);
	}

	b->size = size;
	b->state = DQ_BLK_FREE;
	*data = b->data;

	return b;
}

void dq_udp_block_free(struct dq_udp_buffer *v, struct dq_udp_block *b)
{
	struct dq_udp_block **p;

	pthread_mutex_lock(&v->lock);
	if (b == v->cur) {
		/* The poller must not write into it any more */
		v->cur = NULL;
		dq_asm_reset(&v->as);
	} else if (b->state == DQ_BLK_QUEUED) {
		for (p = &v->head; *p; p = &(*p)->next) {
			if (*p == b) {
				*p = b->next;
				break;
			}
		}
		for (v->tail = v->head; v->tail && v->tail->next; v->tail = v->tail->next)
			;
	}
	pthread_mutex_unlock(&v->lock);

	dq_mem_free(v->mem, b->data, b->mapped);
	free(b);
}

int dq_udp_enqueue(struct dq_udp_buffer *v, struct dq_udp_block *b, size_t bytes_used)
{
	int ret = 0;

	pthread_mutex_lock(&v->lock);

	if (b->state != DQ_BLK_FREE) {
		ret = -EPERM;
	} else if (bytes_used != b->size) {
		/* RX blocks are always filled completely */
		ret = -EINVAL;
	} else if (v->cancelled) {
		ret = -ECANCELED;
	} else if (v->fatal) {
		b->state = DQ_BLK_ERROR;
		b->err = v->fatal;
		v->blocks_failed++;
	} else {
		b->state = DQ_BLK_QUEUED;
		b->next = NULL;
		if (v->tail)
			v->tail->next = b;
		else
			v->head = b;
		v->tail = b;
	}

	pthread_mutex_unlock(&v->lock);

	return ret;
}

static void dq_udp_deadline(struct timespec *ts, int timeout_ms)
{
	clock_gettime(CLOCK_MONOTONIC, ts);
	ts->tv_sec += timeout_ms / 1000;
	ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

/* timeout_ms is the resolved context timeout: < 0 waits forever, 0 does
 * not wait (IIO_TIMEOUT_NONBLOCK) */
int dq_udp_dequeue(struct dq_udp_buffer *v, struct dq_udp_block *b, bool nonblock, int timeout_ms)
{
	bool timed_out = false;
	struct timespec ts;
	int ret;

	if (timeout_ms > 0)
		dq_udp_deadline(&ts, timeout_ms);

	pthread_mutex_lock(&v->lock);

	for (;;) {
		switch (b->state) {
		case DQ_BLK_FREE:
			ret = -EPERM;
			goto out_unlock;
		case DQ_BLK_DONE:
			ret = 0;
			b->state = DQ_BLK_FREE;
			goto out_unlock;
		case DQ_BLK_ERROR:
			ret = b->err;
			b->state = DQ_BLK_FREE;
			goto out_unlock;
		case DQ_BLK_CANCELLED:
			ret = -ECANCELED;
			b->state = DQ_BLK_FREE;
			goto out_unlock;
		default:
			break;
		}

		if (nonblock) {
			ret = -EBUSY;
			goto out_unlock;
		}
		if (timed_out || !timeout_ms) {
			ret = -ETIMEDOUT;
			goto out_unlock;
		}

		if (timeout_ms < 0)
			pthread_cond_wait(&v->cond, &v->lock);
		else
			timed_out = pthread_cond_timedwait(&v->cond, &v->lock, &ts) == ETIMEDOUT;
	}

out_unlock:
	pthread_mutex_unlock(&v->lock);

	return ret;
}

int dq_attr_index(const char *name)
{
	unsigned int i;

	for (i = 0; i < dq_nb_attrs; i++)
		if (!strcmp(name, dq_attr_names[i]))
			return (int)i;

	return -ENOENT;
}

static ssize_t dq_attr_print(char *dst, size_t len, int n)
{
	if (n < 0)
		return -EIO;
	if ((size_t)n >= len)
		return -ENOSPC;

	return (ssize_t)n + 1;
}

/* What a buffer that streams over TCP, or none at all yet, reads: its data
 * path, the options it would start with, and zeros */
ssize_t dq_attr_read_default(const struct iio_context_pdata *pdata, enum dq_path path,
		const char *name, char *dst, size_t len)
{
	int idx = dq_attr_index(name);
	const char *val = "0";

	if (idx < 0)
		return idx;

	switch (idx) {
	case DQ_ATTR_DATA_PATH:
		val = path == DQ_PATH_UDP ? "udp" : "tcp";
		break;
	case DQ_ATTR_MEM_DOMAIN:
		val = dq_mem_name(pdata->opts.mem);
		break;
	case DQ_ATTR_ENGINE_KIND:
	case DQ_ATTR_RX_L2_LAST_REASON:
		val = "none";
		break;
	case DQ_ATTR_LOSSY:
		val = pdata->opts.lossy ? "1" : "0";
		break;
	default:
		break;
	}

	return dq_attr_print(dst, len, snprintf(dst, len, "%s", val));
}

ssize_t dq_udp_read_attr(struct dq_udp_buffer *v, const char *name, char *dst, size_t len)
{
	struct dq_udp_counters c;
	uint64_t u = 0, zero_filled, dropped, overruns, completed, failed, with_gaps;
	uint64_t l2_rejected, vlan, hds;
	int idx = dq_attr_index(name), last_error, lossy, n;
	enum dq_l2_status l2_last;
	bool have_stream;
	uint32_t sid;

	if (idx < 0)
		return idx;

	pthread_mutex_lock(&v->lock);
	c = v->rx.c;
	zero_filled = v->as.zero_filled;
	dropped = v->dropped_no_block;
	overruns = v->overrun_events;
	completed = v->blocks_completed;
	failed = v->blocks_failed;
	with_gaps = v->blocks_with_gaps;
	last_error = v->last_error;
	l2_rejected = v->l2_rejected;
	l2_last = v->l2_last;
	vlan = v->vlan_packets;
	hds = v->hds_packets;
	lossy = v->as.lossy;
	have_stream = v->rx.started;
	sid = v->rx.stream_id;
	pthread_mutex_unlock(&v->lock);

	switch (idx) {
	case DQ_ATTR_RX_PACKETS:
		u = c.packets;
		break;
	case DQ_ATTR_RX_BYTES:
		u = c.bytes;
		break;
	case DQ_ATTR_RX_LOST_PACKETS:
		u = c.lost_packets;
		break;
	case DQ_ATTR_RX_GAP_EVENTS:
		u = c.gap_events;
		break;
	case DQ_ATTR_RX_DROPPED_NO_BLOCK:
		u = dropped;
		break;
	case DQ_ATTR_RX_OVERRUN_EVENTS:
		u = overruns;
		break;
	case DQ_ATTR_RX_FOREIGN_PACKETS:
		u = c.foreign;
		break;
	case DQ_ATTR_RX_MALFORMED:
		u = c.malformed;
		break;
	case DQ_ATTR_RX_ZERO_FILLED_BYTES:
		u = zero_filled;
		break;
	case DQ_ATTR_RX_LATE_PACKETS:
		u = c.late;
		break;
	case DQ_ATTR_RX_DUPLICATE_PACKETS:
		u = c.duplicates;
		break;
	case DQ_ATTR_RX_L2_REJECTED:
		u = l2_rejected;
		break;
	case DQ_ATTR_RX_L2_LAST_REASON:
		n = snprintf(dst, len, "%s", l2_rejected ? dq_l2_status_name(l2_last) : "none");
		return dq_attr_print(dst, len, n);
	case DQ_ATTR_RX_VLAN_PACKETS:
		u = vlan;
		break;
	case DQ_ATTR_RX_HDS_PACKETS:
		u = hds;
		break;
	case DQ_ATTR_BLOCKS_COMPLETED:
		u = completed;
		break;
	case DQ_ATTR_BLOCKS_FAILED:
		u = failed;
		break;
	case DQ_ATTR_BLOCKS_WITH_GAPS:
		u = with_gaps;
		break;
	case DQ_ATTR_STREAM_ID:
		/* From the setup of the last enable; the probe's is never used */
		n = have_stream ? snprintf(dst, len, "0x%08" PRIx32, sid) : snprintf(dst, len, "0");
		return dq_attr_print(dst, len, n);
	case DQ_ATTR_LAST_ERROR:
		return dq_attr_print(dst, len, snprintf(dst, len, "%d", last_error));
	case DQ_ATTR_LOSSY:
		return dq_attr_print(dst, len, snprintf(dst, len, "%d", lossy));
	case DQ_ATTR_MEM_DOMAIN:
		return dq_attr_print(dst, len, snprintf(dst, len, "%s", dq_mem_name(v->mem)));
	case DQ_ATTR_ENGINE_KIND:
		return dq_attr_print(dst, len, snprintf(dst, len, "%s", v->engine_kind));
	case DQ_ATTR_RX_SOCKET_BUFFER:
		return dq_attr_print(dst, len, snprintf(dst, len, "%d", v->rcvbuf));
	case DQ_ATTR_DATA_PATH:
	default:
		return dq_attr_print(dst, len, snprintf(dst, len, "udp"));
	}

	return dq_attr_print(dst, len, snprintf(dst, len, "%" PRIu64, u));
}

int dq_udp_write_attr(struct dq_udp_buffer *v, const char *name, const char *src, size_t len)
{
	bool lossy;

	if (strcmp(name, dq_attr_names[DQ_ATTR_LOSSY]))
		return dq_attr_index(name) < 0 ? -ENOENT : -EPERM;

	/* "0" or "1", optionally followed by a newline and/or a NUL */
	if (!len || (src[0] != '0' && src[0] != '1') ||
			(len > 1 && src[1] != '\0' && src[1] != '\n') ||
			(len > 2 && src[1] == '\n' && src[2] != '\0'))
		return -EINVAL;
	lossy = src[0] == '1';

	pthread_mutex_lock(&v->lock);
	v->as.lossy = lossy;
	pthread_mutex_unlock(&v->lock);

	return 0;
}
