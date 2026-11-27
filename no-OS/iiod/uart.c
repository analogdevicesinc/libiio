/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <no_os_print_log.h>
#include <no_os_uart.h>
#include <no_os_util.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

#include "iio_device.h"
#include "noos_iiod.h"

/* Size of a binary iiod_command header */
#define IIOD_UART_HDR_LEN 8

/*
 * The uart has no disconnect, so after a v1 client leaves the next client
 * still meets the binary parser. A v0 client opens with "PRINT\r\n" (7
 * bytes) or "ZPRINT\r\n" (8): a text line that arrives while a header is
 * awaited ends the binary session and is handed to the ASCII parser.
 */
static uint8_t iiod_uart_replay[IIOD_UART_HDR_LEN];
static size_t iiod_uart_replay_len, iiod_uart_replay_pos;

/**
 * @brief Tell whether a partial header is a whole v0 command line.
 * @param buf - Bytes received so far.
 * @param len - Number of bytes in buf.
 * @return true if buf is printable text ending in a newline.
 */
static bool iiod_uart_is_v0_line(const uint8_t *buf, size_t len)
{
	size_t i;

	if (len < 2 || buf[len - 1] != '\n')
		return false;

	/* Answered by the responder itself */
	if (!memcmp(buf, "BINARY\r\n", no_os_min(len, IIOD_UART_HDR_LEN)))
		return false;

	for (i = 0; i < len; i++)
		if ((buf[i] < ' ' || buf[i] > '~') && buf[i] != '\r' && buf[i] != '\n')
			return false;

	return true;
}

/*
 * Blocking read for the interpreter; a v0 text command seen during a v1
 * session is kept and replayed to the ASCII parser.
 */
static ssize_t iiod_uart_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct no_os_uart_desc *uart = (struct no_os_uart_desc *)pdata;
	uint32_t total = 0;
	int32_t ret;

	if (iiod_uart_replay_pos < iiod_uart_replay_len) {
		size_t n;

		n = no_os_min(size, iiod_uart_replay_len - iiod_uart_replay_pos);
		memcpy(buf, iiod_uart_replay + iiod_uart_replay_pos, n);
		iiod_uart_replay_pos += n;

		return (ssize_t)n;
	}

	while (total < size) {
		ret = no_os_uart_read(uart, (uint8_t *)buf + total, (uint32_t)(size - total));
		if (ret < 0) {
			if (ret == -EAGAIN)
				continue;
			return ret;
		}

		total += ret;

		/* Only the binary parser asks for a whole header at once */
		if (size == IIOD_UART_HDR_LEN && iiod_uart_is_v0_line(buf, total)) {
			memcpy(iiod_uart_replay, buf, total);
			iiod_uart_replay_len = total;
			iiod_uart_replay_pos = 0;

			return -EPIPE;
		}
	}

	return (ssize_t)size;
}

/* Write a whole reply to the uart. */
static ssize_t iiod_uart_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct no_os_uart_desc *uart = (struct no_os_uart_desc *)pdata;
	int32_t ret;

	ret = no_os_uart_write(uart, (const uint8_t *)buf, (uint32_t)size);
	if (ret < 0)
		return ret;

	return (ssize_t)size;
}

/* Build the context XML once, then serve clients one after another forever. */
int iiod_uart_run(struct no_os_uart_desc *uart_desc)
{
	struct iio_context_params ctx_params = { 0 };
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	ret = iiod_init();
	if (ret < 0) {
		pr_err("iiod_init failed: %d\n", ret);
		return ret;
	}

	ctx = iio_create_context(&ctx_params, "no-os:");
	ret = iio_err(ctx);
	if (ret) {
		pr_err("iio_create_context failed: %d\n", ret);
		goto cleanup_iiod;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		ret = -ENOMEM;
		goto destroy_ctx;
	}

	xml_len = strlen(xml) + 1;

	while (1) {
		ret = iiod_interpreter(ctx, (struct iiod_pdata *)uart_desc, iiod_uart_read,
				iiod_uart_write, xml, xml_len);
		if (ret)
			pr_debug("session ended: %d\n", ret);
	}

	free(xml);
destroy_ctx:
	iio_context_destroy(ctx);
cleanup_iiod:
	iiod_cleanup();

	return ret;
}

/*
 * Entry point of the uart build: register the project's devices, open its
 * uart and run IIOD on it.
 */
int noos_iiod_run(const struct noos_iiod_param *param)
{
	struct no_os_uart_init_param uart_ip;
	struct no_os_uart_desc *uart_desc;
	int ret;

	if (!param || !param->uart)
		return -EINVAL;

	/* The read loop polls with -EAGAIN, which needs asynchronous rx. */
	uart_ip = *param->uart;
	uart_ip.asynchronous_rx = true;

	noos_iiod_register(param);

	ret = no_os_uart_init(&uart_desc, &uart_ip);
	if (ret)
		return ret;

	ret = iiod_uart_run(uart_desc);

	no_os_uart_remove(uart_desc);

	return ret;
}
