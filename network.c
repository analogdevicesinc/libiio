/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#include "debug.h"
#include "iio-private.h"

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define IIOD_PORT 30431

struct iio_context_pdata {
	int fd;
};

static pthread_mutex_t hostname_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t write_all(const void *src, size_t len, int fd)
{
	const void *ptr = src;
	while (len) {
		ssize_t ret = send(fd, ptr, len, 0);
		if (ret < 0)
			return -errno;
		ptr += ret;
		len -= ret;
	}
	return ptr - src;
}

static ssize_t read_all(void *dst, size_t len, int fd)
{
	void *ptr = dst;
	while (len) {
		ssize_t ret = recv(fd, ptr, len, 0);
		if (ret < 0)
			return -errno;
		ptr += ret;
		len -= ret;
	}
	return ptr - dst;
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;

	write_all("\r\nEXIT\r\n", sizeof("\r\nEXIT\r\n") - 1, pdata->fd);
	close(pdata->fd);
	free(pdata);
}

static struct iio_backend_ops network_ops = {
	.shutdown = network_shutdown,
};

static struct iio_context * get_context(int fd)
{
	struct iio_context *ctx;
	unsigned int i;
	unsigned long xml_len;
	char *xml, *ptr, buf[1024];

	DEBUG("Writing PRINT command\n");
	ssize_t ret = write_all("PRINT\r\n", sizeof("PRINT\r\n") - 1, fd);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to send PRINT command: %s\n", buf);
		return NULL;
	}

	DEBUG("Reading response...\n");
	for (i = 0; i < sizeof(buf) - 1; i++) {
		ssize_t ret = read_all(buf + i, 1, fd);
		if (ret < 0) {
			char buf[1024];
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read response to PRINT: %s\n", buf);
			return NULL;
		}

		if (buf[i] == '\n')
			break;
	}

	buf[i] = '\0';
	xml_len = strtoul(buf, &ptr, 10);
	if (ptr == buf) {
		ERROR("Unrecognized response from server\n");
		return NULL;
	}

	DEBUG("Server returned a XML string of length %lu\n", xml_len);

	xml = malloc(xml_len);
	if (!xml) {
		ERROR("Unable to allocate data\n");
		return NULL;
	}

	DEBUG("Reading XML string...\n");
	read_all(xml, xml_len, fd);

	DEBUG("Creating context from XML...\n");
	ctx = iio_create_xml_context_mem(xml, xml_len);
	free(xml);
	return ctx;
}

struct iio_context * iio_create_network_context(const char *host)
{
	struct hostent *ent;
	struct sockaddr_in serv;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	int fd;

	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(IIOD_PORT);

	/* gethostbyname is not reentrant, and gethostbyname_r
	 * is a GNU extension. So we use a mutex to prevent races */
	pthread_mutex_lock(&hostname_lock);
	ent = gethostbyname(host);
	if (!ent) {
		ERROR("Unable to create network context: %s\n",
				strerror(h_errno));
		pthread_mutex_unlock(&hostname_lock);
		return NULL;
	} else if (!ent->h_addr_list[0]) {
		ERROR("Unable to find an IP for host %s\n", host);
		pthread_mutex_unlock(&hostname_lock);
		return NULL;
	} else {
		memcpy(&serv.sin_addr.s_addr,
				ent->h_addr_list[0], ent->h_length);
		pthread_mutex_unlock(&hostname_lock);
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		ERROR("Unable to open socket\n");
		return NULL;
	}

	if (connect(fd, (struct sockaddr *) &serv, sizeof(serv)) < 0) {
		ERROR("Unable to connect\n");
		goto err_close_socket;
	}

	pdata = calloc(1, sizeof(*pdata));
	if (!pdata) {
		ERROR("Unable to allocate memory\n");
		goto err_close_socket;
	}

	pdata->fd = fd;

	DEBUG("Creating context...\n");
	ctx = get_context(fd);
	if (!ctx)
		goto err_free_pdata;

	/* Override the name and low-level functions of the XML context
	 * with those corresponding to the network context */
	ctx->name = "network";
	ctx->ops = &network_ops;
	ctx->pdata = pdata;

	return ctx;

err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
	return NULL;
}
