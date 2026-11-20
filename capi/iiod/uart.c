/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <capi_time.h>
#include <capi_uart.h>
#include <errno.h>
#include <iiod_uart.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

/*
 * Received bytes go from the UART interrupt into a
 * ring buffer, so they are kept while the interpreter is busy (sending a
 * block, filling one from a driver) instead of overrunning the UART's own
 * FIFO. A platform whose capi_uart has no interrupts (register_callback or
 * set_irq_rx fails) is served by polling the receive FIFO instead. Either
 * way the application's idle function runs while no byte is waiting.
 * Transmit is blocking. UART errors reach libiio as -EIO.
 */

/* A power of two; the ring is single producer (the interrupt), single
 * consumer (the interpreter). */
#ifndef IIOD_UART_RX_BUF_SIZE
#define IIOD_UART_RX_BUF_SIZE 256
#endif

/*
 * What a client sends first. It is watched for in everything received, at
 * any position: a client gone mid-command leaves part of it behind, and the
 * responder, reading 8-byte command headers, would see the next client's
 * handshake only where it falls on a header boundary. Bytes that may be its
 * start are held back until it is clear whether they are, so nothing of a
 * handshake completes what the previous client left behind.
 */
#define IIOD_UART_HANDSHAKE "BINARY\r\n"
#define IIOD_UART_HANDSHAKE_LEN (sizeof(IIOD_UART_HANDSHAKE) - 1)

/* How long held bytes wait for the rest of a handshake. A command can end
 * in the first bytes of one; its client waits for the answer, so they go
 * to the responder once nothing more arrives. Far longer than the bytes of
 * one handshake are apart, far shorter than a new client takes to start. */
#ifndef IIOD_UART_HOLD_US
#define IIOD_UART_HOLD_US 10000
#endif

struct iiod_pdata {
	struct capi_uart_handle *uart;
	void (*idle)(void);
	bool rx_irq;
	/* Dropping received bytes up to the next handshake. */
	bool resync;
	/* Received bytes held back: the first <matched> of a handshake. */
	size_t matched;
	/* When the last of them arrived. */
	uint64_t matched_us;
	/* Held bytes that turned out not to be a handshake: the first <held>
	 * of one, <released> of them handed on so far. */
	size_t held;
	size_t released;
	/* The byte that ended the match, to be looked at once they are. */
	bool has_next;
	uint8_t next;
	/* How much of the last handshake received was handed to the
	 * responder: less than all of it while a session has to start
	 * with it. */
	size_t replayed;
};

static struct iiod_pdata iiod_uart;

static uint8_t rx_buf[IIOD_UART_RX_BUF_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t rx_dropped;

static void iiod_uart_irq(enum capi_uart_async_event event, void *arg, int event_extra)
{
	struct capi_uart_handle *uart = arg;
	uint32_t head;
	uint8_t byte;

	(void)event_extra;

	if (event != CAPI_UART_EVENT_INTERRUPT)
		return;

	while (capi_uart_read_byte(uart, &byte)) {
		head = rx_head;

		if (head - rx_tail == IIOD_UART_RX_BUF_SIZE) {
			/* Full: drop it. */
			rx_dropped++;
			continue;
		}

		rx_buf[head % IIOD_UART_RX_BUF_SIZE] = byte;
		rx_head = head + 1;
	}
}

static void iiod_uart_idle(const struct iiod_pdata *pdata)
{
	if (pdata->idle)
		pdata->idle();
}

/* One received byte, from the ring (interrupt-driven receive) or the receive
 * FIFO (polled): 1 if there was one, 0 if not. */
static int iiod_uart_rx_byte(struct iiod_pdata *pdata, uint8_t *byte)
{
	uint32_t tail = rx_tail;
	uint16_t count = 0;

	if (pdata->rx_irq) {
		if (rx_head == tail)
			return 0;

		*byte = rx_buf[tail % IIOD_UART_RX_BUF_SIZE];
		rx_tail = tail + 1;

		return 1;
	}

	if (capi_uart_get_rx_fifo_count(pdata->uart, &count))
		return -EIO;
	if (!count)
		return 0;

	if (capi_uart_receive(pdata->uart, byte, 1))
		return -EIO;

	return 1;
}

/* Held bytes waited long enough (or there is no clock to tell). */
static bool iiod_uart_hold_expired(const struct iiod_pdata *pdata)
{
	uint64_t now;

	if (capi_uptime(&now))
		return true;

	return now - pdata->matched_us >= IIOD_UART_HOLD_US;
}

/*
 * What was received, at least one byte, without the bytes held back. 0 when
 * a handshake completed: then the next session has to start with it.
 */
static ssize_t iiod_uart_recv(struct iiod_pdata *pdata, void *buf, size_t size)
{
	uint8_t *dst = buf;
	size_t len = 0;
	uint8_t byte;
	int ret;

	while (len < size) {
		if (pdata->released < pdata->held) {
			dst[len++] = (uint8_t)IIOD_UART_HANDSHAKE[pdata->released++];
			continue;
		}

		pdata->held = 0;
		pdata->released = 0;

		if (pdata->has_next) {
			byte = pdata->next;
			pdata->has_next = false;
		} else {
			ret = iiod_uart_rx_byte(pdata, &byte);
			if (ret < 0)
				return ret;

			if (!ret) {
				if (len)
					break;

				if (pdata->matched && iiod_uart_hold_expired(pdata)) {
					pdata->held = pdata->matched;
					pdata->matched = 0;
				} else {
					iiod_uart_idle(pdata);
				}

				continue;
			}
		}

		if (byte == (uint8_t)IIOD_UART_HANDSHAKE[pdata->matched]) {
			if (++pdata->matched < IIOD_UART_HANDSHAKE_LEN) {
				if (capi_uptime(&pdata->matched_us))
					pdata->matched_us = 0;
				continue;
			}

			pdata->matched = 0;
			pdata->replayed = 0;

			return 0;
		}

		if (pdata->matched) {
			/* No handshake: what was held goes on first. */
			pdata->held = pdata->matched;
			pdata->matched = 0;
			pdata->next = byte;
			pdata->has_next = true;
			continue;
		}

		dst[len++] = byte;
	}

	return len;
}

static bool iiod_uart_handshake_pending(const struct iiod_pdata *pdata)
{
	return pdata->replayed < IIOD_UART_HANDSHAKE_LEN;
}

static ssize_t iiod_uart_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	ssize_t ret;

	/* After a failed session: drop everything up to a handshake. */
	while (pdata->resync) {
		ret = iiod_uart_recv(pdata, buf, size);
		if (ret < 0)
			return ret;

		pdata->resync = ret > 0;
	}

	/* A session starts with the handshake that opened it. */
	if (iiod_uart_handshake_pending(pdata)) {
		if (size > IIOD_UART_HANDSHAKE_LEN - pdata->replayed)
			size = IIOD_UART_HANDSHAKE_LEN - pdata->replayed;

		memcpy(buf, IIOD_UART_HANDSHAKE + pdata->replayed, size);
		pdata->replayed += size;

		return size;
	}

	ret = iiod_uart_recv(pdata, buf, size);

	/* A new client: end this session, so the responder frees what the
	 * previous client left open and runs nothing of what it left behind;
	 * the next one starts with this handshake. */
	if (!ret)
		return -EPIPE;

	return ret;
}

static ssize_t iiod_uart_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	/* capi_uart_transmit() does not take a const buffer. */
	if (capi_uart_transmit(pdata->uart, (uint8_t *)buf, size))
		return -EIO;

	return size;
}

/* Drop whatever was received before, then start receiving. */
static void iiod_uart_rx_start(struct iiod_pdata *pdata)
{
	if (pdata->rx_irq)
		capi_uart_set_irq_rx(pdata->uart, false);

	/* Bytes left from before (or from reset) would offset every command
	 * header that follows. */
	capi_uart_flush_rx_fifo(pdata->uart);
	rx_head = 0;
	rx_tail = 0;

	if (pdata->rx_irq && capi_uart_set_irq_rx(pdata->uart, true))
		pdata->rx_irq = false;
}

int iiod_uart_run(struct capi_uart_handle *uart, void (*idle)(void))
{
	struct iio_context_params ctx_params = { 0 };
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	if (!uart)
		return -EINVAL;

	ret = iiod_init();
	if (ret < 0)
		return ret;

	ctx = iio_create_context(&ctx_params, "capi:");
	if (iio_err(ctx)) {
		ret = iio_err(ctx);
		goto err_cleanup;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		ret = -ENOMEM;
		iio_context_destroy(ctx);
		goto err_cleanup;
	}

	xml_len = strlen(xml) + 1;

	iiod_uart = (struct iiod_pdata){
		.uart = uart,
		.idle = idle,
		.rx_irq = !capi_uart_register_callback(uart, iiod_uart_irq, uart),
		.resync = true,
		.replayed = IIOD_UART_HANDSHAKE_LEN,
	};

	iiod_uart_rx_start(&iiod_uart);

	/*
	 * One session per client. The interpreter returns when a new
	 * client's handshake ended the session, or when a command failed
	 * (a UART error, bytes that are no command); either way the
	 * responder has freed what was left open. A new client is served
	 * right away, starting with its handshake; after a failure, the
	 * next session starts at the next handshake.
	 */
	for (;;) {
		iiod_interpreter(ctx, &iiod_uart, iiod_uart_read, iiod_uart_write, xml, xml_len);

		if (iiod_uart_handshake_pending(&iiod_uart)) {
			/* All of it, also if a failure cut a replay short. */
			iiod_uart.replayed = 0;
			continue;
		}

		iiod_uart.resync = true;
	}

err_cleanup:
	iiod_cleanup();

	return ret;
}
