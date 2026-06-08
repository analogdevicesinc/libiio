/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"
#include "vita49_packet.h"

#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define VITA49_TIMEOUT_MS 5000

struct iio_context_pdata {
	int fd;
	struct sockaddr_storage addr;
	socklen_t addrlen;
};


static struct iio_context *
vrt_create_context(const struct iio_context_params *params, const char *hostname)
{
	struct addrinfo hints, *res;
	struct iio_context_pdata *pdata;
	struct iio_context *ctx;
	char *host, *port_str, *tmp_host;
	int ret, fd;
	uint32_t buf[1024];
	struct timeval tv;

	fprintf(stderr, "vrt_create_context: hostname=%s\n", hostname);

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	tmp_host = iio_strdup(hostname);
	if (!tmp_host) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	host = tmp_host;
	port_str = strchr(host, ':');
	if (port_str) {
		*port_str = '\0';
		port_str++;
	} else {
		port_str = "1234"; /* Default VRT port */
	}

	fprintf(stderr, "vrt_create_context: host=%s port=%s\n", host, port_str);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	ret = getaddrinfo(host, port_str, &hints, &res);
	free(tmp_host);
	if (ret != 0) {
		fprintf(stderr, "vrt_create_context: getaddrinfo failed: %s\n", gai_strerror(ret));
		free(pdata);
		return iio_ptr(-EINVAL);
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "vrt_create_context: socket failed: %s\n", strerror(errno));
		freeaddrinfo(res);
		free(pdata);
		return iio_ptr(-errno);
	}

	pdata->fd = fd;
	memcpy(&pdata->addr, res->ai_addr, res->ai_addrlen);
	pdata->addrlen = res->ai_addrlen;
	freeaddrinfo(res);

	/* Bind to local port if we want to receive? Actually VRT is usually broadcast/multicast.
	 * For simplicity, let's assume we are receiving on the same port we target.
	 * Or just use the socket to receive if it was bound.
	 */
	struct sockaddr_in local_addr;
	memset(&local_addr, 0, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(atoi(port_str));
	local_addr.sin_addr.s_addr = INADDR_ANY;
	bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr));

	/* Set timeout for discovery */
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ctx = iio_context_create_from_backend(params, &iio_vrt_backend,
					      "VITA 49.2 VRT Backend", 0, 1, "");
	if (!ctx) {
		fprintf(stderr, "vrt_create_context: iio_context_create_from_backend failed\n");
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
		free(pdata);
		return NULL;
	}

	iio_context_set_pdata(ctx, pdata);

	/* Discovery loop */
	fprintf(stderr, "vrt_create_context: starting discovery loop\n");
	struct timeval start_time, current_time;
	gettimeofday(&start_time, NULL);

	while (1) {
		ssize_t received = recv(fd, buf, sizeof(buf), 0);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				fprintf(stderr, "vrt_create_context: discovery timeout\n");
				break;
			}
			fprintf(stderr, "vrt_create_context: recv failed: %s\n", strerror(errno));
			break;
		}

		struct vrt_packet pkt;
		if (vrt_parse_packet(buf, received / 4, &pkt) < 0) {
			fprintf(stderr, "vrt_create_context: Failed to parse VRT packet\n");
			continue;
		}

		fprintf(stderr, "vrt_create_context: received packet type %u\n", pkt.header.packet_type);

		if (pkt.header.packet_type == VRT_PKT_TYPE_IF_CONTEXT && pkt.has_stream_id) {
			uint32_t sid = pkt.stream_id;
			char sid_str[32];
			snprintf(sid_str, sizeof(sid_str), "vrt_device_%08x", sid);
			
			if (!iio_context_find_device(ctx, sid_str)) {
				fprintf(stderr, "vrt_create_context: discovered device %s\n", sid_str);
				struct iio_device *dev = iio_context_add_device(ctx, sid_str, sid_str, NULL);
				if (dev) {
					struct iio_data_format fmt;
					memset(&fmt, 0, sizeof(fmt));
					fmt.length = 16;
					fmt.bits = 16;
					fmt.is_signed = true;
					fmt.is_fully_defined = true;
					iio_device_add_channel(dev, 0, "voltage0", "voltage0_i", NULL, false, true, &fmt);
					iio_device_add_channel(dev, 1, "voltage1", "voltage0_q", NULL, false, true, &fmt);
				}
			}
		}

		gettimeofday(&current_time, NULL);
		long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + 
						  (current_time.tv_usec - start_time.tv_usec) / 1000;
		if (elapsed_ms >= 2000) {
			fprintf(stderr, "vrt_create_context: absolute discovery timeout\n");
			break;
		}
	}

	return ctx;
}

static void vrt_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	if (pdata->fd >= 0) {
#ifdef _WIN32
		closesocket(pdata->fd);
#else
		close(pdata->fd);
#endif
	}
}

static int vrt_get_version(const struct iio_context *ctx, unsigned int *major,
			   unsigned int *minor, char git_tag[8])
{
	*major = 0;
	*minor = 1;
	strncpy(git_tag, "v0.1", 8);
	return 0;
}

static const struct iio_backend_ops vrt_ops = {
	.create = vrt_create_context,
	.shutdown = vrt_shutdown,
	.get_version = vrt_get_version,
	/* TODO: Implement other ops */
};

const struct iio_backend iio_vrt_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "vrt",
	.uri_prefix = "vrt:",
	.ops = &vrt_ops,
	.default_timeout_ms = VITA49_TIMEOUT_MS,
};
