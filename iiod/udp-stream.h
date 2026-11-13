/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Server side of the UDP sample stream. A provider registered here answers
 * IIOD_OP_UDP_STREAM_SETUP and sends the samples of an enabled buffer as UDP
 * datagrams. Without one, the command is answered with -ENOTSUP, which is what
 * iiod, tinyiiod and Zephyr do by default.
 */

#ifndef __IIOD_SERVER_UDP_STREAM_H__
#define __IIOD_SERVER_UDP_STREAM_H__

#include "../iiod-udp-stream.h"

/* See ops.h. A provider keeps its per-buffer state in entry->udp_priv. */
struct buffer_entry;

/*
 * All four callbacks are mandatory. The responder calls them as follows,
 * keeping two flags per buffer: a pending setup (succeeded, not yet used by an
 * enable) and an active stream (start() succeeded, stop() not yet called).
 *
 * - SETUP: answered -EBUSY while a stream is active. Otherwise setup() runs,
 *   and a success makes the setup pending. A setup may be repeated before the
 *   enable; if a later one fails, the earlier success stays pending.
 * - ENABLE: once the buffer is enabled, a pending setup is used up and
 *   start() runs; its success makes the stream active. Without a new setup,
 *   a later ENABLE starts nothing, and start() never runs while active.
 * - DISABLE: stop() runs first if a stream is active; any pending setup is
 *   dropped.
 * - CLOSE_BUFFER, or loss of the connection: stop() if a stream is active,
 *   then release() if entry->udp_priv is set.
 *
 * Each runs on the thread serving the connection that sent the command: for
 * CLOSE_BUFFER, that is usually the context's connection, not the buffer's.
 * On connection teardown, stop() and release() run with the buffer list
 * locked and must not call back into the responder.
 */
struct iiod_udp_stream_ops {
	/* Check <req>, set the stream up and fill <resp> (zeroed on entry).
	 * Returns 0 or a negative error code, which is what the client gets. */
	int (*setup)(void *priv, struct buffer_entry *entry, const struct iiod_udp_stream_req *req,
			struct iiod_udp_stream_resp *resp);

	/* Start sending. A negative return fails the enable, which leaves the
	 * buffer enabled but no stream active: start() must clean up after itself,
	 * as stop() will not be called, and the client must send a new setup
	 * before the next enable. */
	int (*start)(void *priv, struct buffer_entry *entry);

	/* Stop sending. */
	void (*stop)(void *priv, struct buffer_entry *entry);

	/* Free entry->udp_priv. The stream, if any, is already stopped. */
	void (*release)(void *priv, struct buffer_entry *entry);
};

/* Register the provider, with <priv> passed back to every call; NULL removes
 * it. Call it before the server takes connections. */
void iiod_udp_stream_set_ops(const struct iiod_udp_stream_ops *ops, void *priv);

#endif /* __IIOD_SERVER_UDP_STREAM_H__ */
