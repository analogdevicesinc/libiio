/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef NO_OS_LWIP_NETWORKING

#include <string.h>
#include <errno.h>
#include <no_os_uart.h>
#include <no_os_print_log.h>
#include <no_os_alloc.h>
#include <no_os_delay.h>
#include <tinyiiod/tinyiiod.h>
#include "lwip_socket.h"
#include "tcp_socket.h"
#include "parameters.h"
#include "iio_device.h"

#define IIOD_PORT 30431
#define NET_WRITE_TIMEOUT_S 5
#define NET_READ_TIMEOUT_S 3
#define NET_DRAIN_STEPS 16

struct net_server {
	struct tcp_socket_desc *server_socket;
	struct lwip_network_desc *lwip;
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
};

static struct net_server g_server;

struct iiod_net_pdata {
	struct tcp_socket_desc *client;
	struct lwip_network_desc *lwip;
	struct net_server *server;
};

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size);
static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size);

static bool net_sock_live(const struct tcp_socket_desc *sock)
{
	if (!sock || sock->id >= NO_OS_MAX_SOCKETS)
		return false;

	return g_server.lwip->sockets[sock->id].state == SOCKET_CONNECTED;
}

static void net_drain(struct lwip_network_desc *lwip)
{
	unsigned int i;

	for (i = 0; i < NET_DRAIN_STEPS; i++)
		no_os_lwip_step(lwip, NULL);
}

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	uint8_t *dst = (uint8_t *)buf;
	struct no_os_time deadline;
	size_t total = 0;
	int32_t ret;

	deadline = no_os_get_time();
	deadline.s += NET_READ_TIMEOUT_S;

	while (total < size) {
		ret = no_os_lwip_step(np->lwip, NULL);
		if (ret)
			return -EIO;

		ret = socket_recv(np->client, dst + total,
				  (uint32_t)(size - total));

		if (ret > 0) {
			total += ret;
			deadline = no_os_get_time();
			deadline.s += NET_READ_TIMEOUT_S;
		} else if (ret == 0) {
			/*
			 * The interpreter needs a blocking read: -EAGAIN would
			 * end the session. An idle client is dropped after
			 * NET_READ_TIMEOUT_S so the next one can be accepted.
			 */
			struct no_os_time now = no_os_get_time();

			if (now.s > deadline.s ||
			    (now.s == deadline.s && now.us >= deadline.us))
				return -ETIMEDOUT;
		} else {
			return -EIO;
		}
	}

	return (ssize_t)total;
}

static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	const uint8_t *src = (const uint8_t *)buf;
	struct no_os_time deadline;
	size_t total = 0;
	int32_t ret;

	deadline = no_os_get_time();
	deadline.s += NET_WRITE_TIMEOUT_S;

	while (total < size) {
		ret = socket_send(np->client, src + total,
				  (uint32_t)(size - total));

		if (ret > 0) {
			total += ret;
			deadline = no_os_get_time();
			deadline.s += NET_WRITE_TIMEOUT_S;
		} else if (ret == 0) {
			struct no_os_time now = no_os_get_time();

			if (now.s > deadline.s ||
			    (now.s == deadline.s && now.us >= deadline.us))
				return -ETIMEDOUT;

			ret = no_os_lwip_step(np->lwip, NULL);
			if (ret)
				return -EIO;
		} else {
			return -EIO;
		}
	}

	no_os_lwip_step(np->lwip, NULL);

	return (ssize_t)total;
}

static void net_client_close(struct tcp_socket_desc *sock, int reason)
{
	if (net_sock_live(sock))
		socket_remove(sock);
	else
		no_os_free(sock);

	pr_info("IIOD: client disconnected (%d)\n", reason);

	net_drain(g_server.lwip);
}

int iiod_network_run(struct lwip_network_desc *lwip_desc)
{
	struct iio_context_params ctx_params = {0};
	struct tcp_socket_init_param tcp_ip = { .max_buff_size = 0 };
	struct tcp_socket_desc *server_socket;
	struct tcp_socket_desc *client_socket;
	struct iiod_net_pdata np;
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
	if (iio_err(ctx)) {
		pr_err("iio_create_context failed\n");
		iiod_cleanup();
		return -1;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		iio_context_destroy(ctx);
		iiod_cleanup();
		return -1;
	}

	xml_len = strlen(xml) + 1; /* Flawfinder: ignore */

	tcp_ip.net = &lwip_desc->no_os_net;

	ret = socket_init(&server_socket, &tcp_ip);
	if (ret) {
		pr_err("socket_init failed: %d\n", ret);
		goto err_ctx;
	}

	ret = socket_bind(server_socket, IIOD_PORT);
	if (ret) {
		pr_err("socket_bind failed: %d\n", ret);
		goto err_server;
	}

	ret = socket_listen(server_socket, MAX_BACKLOG);
	if (ret) {
		pr_err("socket_listen failed: %d\n", ret);
		goto err_server;
	}

	g_server.server_socket = server_socket;
	g_server.lwip = lwip_desc;
	g_server.ctx = ctx;
	g_server.xml = xml;
	g_server.xml_len = xml_len;

	pr_info("IIOD: listening on port %d\n", IIOD_PORT);

	/*
	 * One client at a time: the interpreter blocks until the client goes
	 * away or stays idle past NET_READ_TIMEOUT_S.
	 */
	while (1) {
		ret = socket_accept(server_socket, &client_socket);
		if (ret == -EAGAIN) {
			no_os_lwip_step(lwip_desc, NULL);
			continue;
		}
		if (ret) {
			pr_err("socket_accept failed: %d\n", ret);
			break;
		}

		pr_info("IIOD: client connected\n");

		np.client = client_socket;
		np.lwip = lwip_desc;
		np.server = &g_server;

		ret = iiod_interpreter(ctx, (struct iiod_pdata *)&np,
				       iiod_net_read, iiod_net_write,
				       xml, xml_len);

		net_client_close(client_socket, ret);
	}

err_server:
	socket_remove(server_socket);
err_ctx:
	iio_context_destroy(ctx);
	iiod_cleanup();
	return ret;
}

int noos_iiod_run(void)
{
	struct lwip_network_desc *lwip_desc;
	struct no_os_uart_desc *console;
	struct no_os_uart_init_param console_ip = {
		.device_id = UART_DEVICE_ID,
		.baud_rate = UART_BAUDRATE,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = UART_OPS,
		.extra = UART_EXTRA,
	};
	struct noos_net_config cfg = {
		.lwip_ops = NET_LWIP_OPS,
		.mac_param = NET_MAC_PARAM,
		.mac_addr = NET_MAC_ADDR,
	};
	struct lwip_network_param lwip_param = {
		.platform_ops = (const struct no_os_lwip_ops *)cfg.lwip_ops,
		.mac_param = cfg.mac_param,
	};
	int ret;

	ret = no_os_uart_init(&console, &console_ip);
	if (ret)
		return ret;

	no_os_uart_stdio(console);

	memcpy(lwip_param.hwaddr, cfg.mac_addr, 6); /* Flawfinder: ignore */

	ret = no_os_lwip_init(&lwip_desc, &lwip_param);
	if (ret) {
		pr_err("lwIP init failed: %d\n", ret);
		goto err_console;
	}

	ret = iiod_network_run(lwip_desc);
	no_os_lwip_remove(lwip_desc);

err_console:
	no_os_uart_remove(console);

	return ret;
}

#endif /* NO_OS_LWIP_NETWORKING */
