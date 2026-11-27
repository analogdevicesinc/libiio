/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef NO_OS_LWIP_NETWORKING

#include <errno.h>
#include <no_os_alloc.h>
#include <no_os_delay.h>
#include <no_os_print_log.h>
#include <stdlib.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

#include "iio_device.h"
#include "lwip_socket.h"
#include "noos_iiod.h"
#include "tcp_socket.h"

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
static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf, size_t size);

/* True if the lwIP socket behind sock is still connected. */
static bool net_sock_live(const struct tcp_socket_desc *sock)
{
	if (!sock || sock->id >= NO_OS_MAX_SOCKETS)
		return false;

	return g_server.lwip->sockets[sock->id].state == SOCKET_CONNECTED;
}

/* Run lwIP a few rounds so pending packets (e.g. a FIN) get processed. */
static void net_drain(struct lwip_network_desc *lwip)
{
	unsigned int i;

	for (i = 0; i < NET_DRAIN_STEPS; i++)
		no_os_lwip_step(lwip, NULL);
}

/*
 * Blocking TCP read for the interpreter; a client idle for
 * NET_READ_TIMEOUT_S is dropped so the next one can connect.
 */
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

		ret = socket_recv(np->client, dst + total, (uint32_t)(size - total));

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

			if (now.s > deadline.s || (now.s == deadline.s && now.us >= deadline.us))
				return -ETIMEDOUT;
		} else {
			return -EIO;
		}
	}

	return (ssize_t)total;
}

/* Send the whole reply, running lwIP while the TCP window is full. */
static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	const uint8_t *src = (const uint8_t *)buf;
	struct no_os_time deadline;
	size_t total = 0;
	int32_t ret;

	deadline = no_os_get_time();
	deadline.s += NET_WRITE_TIMEOUT_S;

	while (total < size) {
		ret = socket_send(np->client, src + total, (uint32_t)(size - total));

		if (ret > 0) {
			total += ret;
			deadline = no_os_get_time();
			deadline.s += NET_WRITE_TIMEOUT_S;
		} else if (ret == 0) {
			struct no_os_time now = no_os_get_time();

			if (now.s > deadline.s || (now.s == deadline.s && now.us >= deadline.us))
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

/* Free a finished client's socket and let lwIP flush the close. */
static void net_client_close(struct tcp_socket_desc *sock, int reason)
{
	if (net_sock_live(sock))
		socket_remove(sock);
	else
		no_os_free(sock);

	pr_info("IIOD: client disconnected (%d)\n", reason);

	net_drain(g_server.lwip);
}

/*
 * TCP server on port 30431: accept one client at a time and run the
 * interpreter for it.
 */
int iiod_network_run(struct lwip_network_desc *lwip_desc)
{
	struct iio_context_params ctx_params = { 0 };
	struct tcp_socket_init_param tcp_ip = { .max_buff_size = 0 };
	struct tcp_socket_desc *server_socket;
	struct tcp_socket_desc *client_socket;
	struct iiod_net_pdata np;
	struct iio_context *ctx;
	char *xml = NULL;
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
		iiod_cleanup();
		return ret;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		iio_context_destroy(ctx);
		iiod_cleanup();
		return -ENOMEM;
	}

	xml_len = strlen(xml) + 1;

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

		ret = iiod_interpreter(ctx, (struct iiod_pdata *)&np, iiod_net_read, iiod_net_write,
				xml, xml_len);

		net_client_close(client_socket, ret);
	}

err_server:
	socket_remove(server_socket);
err_ctx:
	free(xml);
	iio_context_destroy(ctx);
	iiod_cleanup();
	return ret;
}

/*
 * Entry point of the network build: register the project's devices, bring
 * up lwIP with its MAC/PHY settings, then serve.
 */
int noos_iiod_run(const struct noos_iiod_param *param)
{
	struct lwip_network_desc *lwip_desc;
	int ret;

	if (!param || !param->lwip)
		return -EINVAL;

	noos_iiod_register(param);

	ret = no_os_lwip_init(&lwip_desc, param->lwip);
	if (ret) {
		pr_err("lwIP init failed: %d\n", ret);
		return ret;
	}

	ret = iiod_network_run(lwip_desc);
	no_os_lwip_remove(lwip_desc);

	return ret;
}

#endif /* NO_OS_LWIP_NETWORKING */
