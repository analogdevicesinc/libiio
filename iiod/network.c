// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "../iio-config.h"
#include "debug.h"
#include "dns-sd.h"
#include "ops.h"
#include "thread-pool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#if WITH_ZSTD
#include <zstd.h>
#endif

#ifdef HAVE_IPV6
#define IP_ADDR_LEN (INET6_ADDRSTRLEN + 1 + IF_NAMESIZE)
#else
#define IP_ADDR_LEN (INET_ADDRSTRLEN + 1 + IF_NAMESIZE)
#endif

static const int dft_socket_flags = WITH_AIO ? 0 : SOCK_NONBLOCK;

static struct sockaddr_in sockaddr = {
	.sin_family = AF_INET,
};

#ifdef HAVE_IPV6
static struct sockaddr_in6 sockaddr6 = {
	.sin6_family = AF_INET6,
	.sin6_addr = IN6ADDR_ANY_INIT,
};
#endif /* HAVE_IPV6 */

struct network_pdata {
	struct thread_pool *pool;
	struct iio_context *ctx;
	const void *xml_zstd;
	size_t xml_zstd_len;
	uint16_t port;
};

struct client_data {
	int fd;
	struct iio_context *ctx;
	const void *xml_zstd;
	size_t xml_zstd_len;
};

static void client_thd(struct thread_pool *pool, void *d)
{
	struct client_data *cdata = d;

	interpreter(cdata->ctx, cdata->fd, cdata->fd, true, false, pool,
		    cdata->xml_zstd, cdata->xml_zstd_len);

	IIO_INFO("Client exited\n");
	close(cdata->fd);
	free(cdata);
}

static void network_main(struct thread_pool *pool, void *d)
{
	struct network_pdata *pdata = d;
	uint16_t port = pdata->port;
	int ret, fd = -1, yes = 1,
	    keepalive_time = 10,
	    keepalive_intvl = 10,
	    keepalive_probes = 6;
	struct pollfd pfd[2];
	bool ipv6;

	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

#ifdef HAVE_IPV6
	sockaddr6.sin6_port = htons(port);

	fd = socket(AF_INET6, SOCK_STREAM | dft_socket_flags, 0);
#endif
	ipv6 = (fd >= 0);
	if (!ipv6)
		fd = socket(AF_INET, SOCK_STREAM | dft_socket_flags, 0);
	if (fd < 0) {
		IIO_PERROR(errno, "Unable to create socket");
		return;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	if (ret < 0)
		IIO_PERROR(errno, "Failed to set SO_REUSEADDR");

#ifdef HAVE_IPV6
	if (ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr6,
				sizeof(sockaddr6));
#endif
	if (!ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
	if (ret < 0) {
		IIO_PERROR(errno, "Bind failed");
		goto err_close_socket;
	}

	/* if port == 0, the OS will return something in the ephemeral port range
	 * which we need to find, to pass to avahi
	 */
	if (!port) {
		struct sockaddr_in sin;
		socklen_t len = sizeof(sin);
		if (getsockname(fd, (struct sockaddr *)&sin, &len) == -1) {
			IIO_PERROR(errno, "getsockname failed");
			goto err_close_socket;
		}
		port = ntohs(sin.sin_port);
		/* we don't use sockaddr or sockaddr6 anymore, so ignore those */
	}

	if (ipv6)
		IIO_INFO("IPv6 support enabled\n");

	if (listen(fd, 16) < 0) {
		IIO_PERROR(errno, "Unable to mark as passive socket");
		goto err_close_socket;
	}

	if (HAVE_AVAHI)
		start_avahi(pdata->pool, port);

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	while (true) {
		struct client_data *cdata;
#ifdef HAVE_IPV6
		struct sockaddr_in6 caddr;
#else
		struct sockaddr_in caddr;
#endif

		socklen_t addr_len = sizeof(caddr);
		int new;

		poll_nointr(pfd, 2);

		if (pfd[1].revents & POLLIN) /* STOP event */
			break;

		new = accept4(fd, (struct sockaddr *) &caddr, &addr_len,
			      dft_socket_flags);
		if (new == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;

			IIO_PERROR(errno, "Failed to create connection socket");
			continue;
		}

		cdata = malloc(sizeof(*cdata));
		if (!cdata) {
			IIO_WARNING("Unable to allocate memory for client\n");
			close(new);
			continue;
		}

		/* Configure the socket to send keep-alive packets every 10s,
		 * and disconnect the client if no reply was received for one
		 * minute. */
		ret = setsockopt(new, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
		if (ret < 0)
			IIO_PERROR(errno, "setsockopt SO_KEEPALIVE");

		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes,
				sizeof(keepalive_probes));
		if (ret < 0)
			IIO_PERROR(errno, "setsockopt TCP_KEEPCNT");

		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time,
				sizeof(keepalive_time));
		if (ret < 0)
			IIO_PERROR(errno, "setsockopt TCP_KEEPIDLE");

		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl,
				sizeof(keepalive_intvl));
		if (ret < 0)
			IIO_PERROR(errno, "setsockopt TCP_KEEPINTVL");

		ret = setsockopt(new, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
		if (ret < 0)
			IIO_PERROR(errno, "setsockopt TCP_NODELAY");

		cdata->fd = new;
		cdata->ctx = pdata->ctx;
		cdata->xml_zstd = pdata->xml_zstd;
		cdata->xml_zstd_len = pdata->xml_zstd_len;

		if (iiod_params.log_level >= LEVEL_INFO) {
			struct sockaddr_in *caddr4 = (struct sockaddr_in *)&caddr;
			char ipaddr[IP_ADDR_LEN];
			int zone = 0;
			void *addr;
			char *ptr;

			if (!ipv6 || caddr4->sin_family == AF_INET) {
				addr = &caddr4->sin_addr;
#ifdef HAVE_IPV6
			} else {
				addr = &caddr.sin6_addr;
				zone = caddr.sin6_scope_id;
#endif
			}

			if (!inet_ntop(caddr4->sin_family, addr, ipaddr, sizeof(ipaddr) - 1)) {
				IIO_PERROR(errno, "Error during inet_ntop");
			} else {
				ipaddr[IP_ADDR_LEN - 1] = '\0';

				if (zone) {
					ptr = &ipaddr[strnlen(ipaddr, IP_ADDR_LEN)];

					if (if_indextoname(zone, ptr + 1))
						*ptr = '%';
				}

				if (!strncmp(ipaddr, "::ffff:", sizeof("::ffff:") - 1))
					ptr = &ipaddr[sizeof("::ffff:") - 1];
				else
					ptr = ipaddr;

				IIO_INFO("New client connected from %s\n", ptr);
			}
		}

		ret = thread_pool_add_thread(pdata->pool, client_thd, cdata, "net_client_thd");
		if (ret) {
			IIO_PERROR(ret, "Failed to create new client thread");
			close(new);
			free(cdata);
		}
	}

	IIO_DEBUG("Cleaning up\n");
	if (HAVE_AVAHI)
		stop_avahi();

err_close_socket:
	close(fd);
	thread_pool_stop_and_wait(pdata->pool);
	thread_pool_destroy(pdata->pool);
	free(pdata);
}

int start_network_daemon(struct iio_context *ctx,
			 struct thread_pool *pool, const void *xml_zstd,
			 size_t xml_zstd_len, uint16_t port)
{
	struct network_pdata *pdata;
	int err;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return -ENOMEM;

	pdata->ctx = ctx;
	pdata->xml_zstd = xml_zstd;
	pdata->xml_zstd_len = xml_zstd_len;
	pdata->port = port;

	pdata->pool = thread_pool_new();
	if (!pdata->pool) {
		err = -errno;
		goto err_free_pdata;
	}

	err = thread_pool_add_thread(pool, network_main, pdata, "network_main_thd");
	if (!err)
		return 0;

	thread_pool_destroy(pdata->pool);
err_free_pdata:
	free(pdata);
	return err;
}
