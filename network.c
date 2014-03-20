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
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define IIOD_PORT 30431
#define IIOD_PORT_STR STRINGIFY(IIOD_PORT)

struct iio_context_pdata {
	int fd;
};

struct iio_device_pdata {
	uint32_t *mask;
	size_t nb;
};

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

static int read_integer(int fd, long *val)
{
	unsigned int i;
	char buf[1024], *ptr;
	ssize_t ret;
	bool found = false;

	for (i = 0; i < sizeof(buf) - 1; i++) {
		ret = read_all(buf + i, 1, fd);
		if (ret < 0)
			return (int) ret;

		/* Skip the eventual first few carriage returns */
		if (buf[i] != '\n')
			found = true;
		else if (found)
			break;
	}

	buf[i] = '\0';
	ret = (ssize_t) strtol(buf, &ptr, 10);
	if (ptr == buf)
		return -EINVAL;
	*val = (long) ret;
	return 0;
}

static ssize_t write_command(const char *cmd, int fd)
{
	ssize_t ret;

	DEBUG("Writing command: %s\n", cmd);
	ret = write_all(cmd, strlen(cmd), fd);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to send command: %s\n", buf);
	}
	return ret;
}

static long exec_command(const char *cmd, int fd)
{
	long resp;
	ssize_t ret = write_command(cmd, fd);
	if (ret < 0)
		return (long) ret;

	DEBUG("Reading response\n");
	ret = read_integer(fd, &resp);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response: %s\n", buf);
		return (long) ret;
	}

	if (resp < 0) {
		char buf[1024];
		strerror_r(-resp, buf, sizeof(buf));
		ERROR("Server returned an error: %s\n", buf);
	}
	return resp;
}

static int network_open(const struct iio_device *dev, uint32_t *mask, size_t nb)
{
	struct iio_device_pdata *pdata = dev->pdata;
	char buf[1024], *ptr;
	uint32_t *mask_copy;
	unsigned int i;
	int ret;

	if (nb != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	mask_copy = malloc(nb * sizeof(*mask_copy));
	if (!mask_copy)
		return -ENOMEM;

	snprintf(buf, sizeof(buf), "OPEN %s ", dev->id);
	ptr = buf + strlen(buf);

	for (i = nb; i > 0; i--) {
		sprintf(ptr, "%08x", mask[i - 1]);
		ptr += 8;
	}
	strcpy(ptr, "\r\n");

	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	if (ret < 0) {
		free(mask_copy);
		return ret;
	} else {
		memcpy(mask_copy, mask, nb * sizeof(*mask));
		pdata->mask = mask_copy;
		pdata->nb = nb;
		return 0;
	}
}

static int network_close(const struct iio_device *dev)
{
	char buf[1024];
	int ret;

	snprintf(buf, sizeof(buf), "CLOSE %s\r\n", dev->id);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	if (ret == 0) {
		struct iio_device_pdata *pdata = dev->pdata;
		free(pdata->mask);
		pdata->mask = NULL;
	}
	return ret;
}

static ssize_t copy_sample_if_enabled(const struct iio_channel *chn,
		void *buf, void *d)
{
	struct iio_device_pdata *pdata = chn->dev->pdata;
	unsigned int i, len = chn->format.length / 8;
	char *dst = *(char **) d, *prev = dst;
	const char *src = buf;

	if (chn->format.length < 8 || chn->index < 0)
		return -EINVAL;
	else if (!TEST_BIT(pdata->mask, chn->index))
		return 0;

	if ((uintptr_t) dst % len)
		dst += len - ((uintptr_t) dst % len);

	for (i = 0; i < len; i++)
		dst[i] = src[i];
	dst += len;

	*(char **) d = dst;
	return dst - prev;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int fd = dev->ctx->pdata->fd;
	ssize_t ret, read = 0, nb_words = (dev->nb_channels + 31) / 32;
	char buf[1024];
	uint32_t *mask;
	bool read_mask = true, demux = false;

	if (!len)
		return -EINVAL;

	mask = malloc(nb_words * sizeof(*mask));
	if (!mask)
		return -ENOMEM;

	snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
			dev->id, (unsigned long) len);
	ret = write_command(buf, fd);
	if (ret < 0) {
		free(mask);
		return ret;
	}

	do {
		unsigned int i;
		long read_len;

		DEBUG("Reading READ response\n");
		ret = read_integer(fd, &read_len);
		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read response to READ: %s\n", buf);
			free(mask);
			return read ?: ret;
		}

		if (read_len < 0) {
			strerror_r(-read_len, buf, sizeof(buf));
			ERROR("Server returned an error: %s\n", buf);
			free(mask);
			return read ?: read_len;
		} else if (read_len == 0) {
			break;
		}

		DEBUG("Bytes to read: %li\n", read_len);

		if (read_mask) {
			DEBUG("Reading mask\n");
			buf[8] = '\0';
			for (i = nb_words; i > 0; i--) {
				ret = read_all(buf, 8, fd);
				if (ret < 0)
					break;
				sscanf(buf, "%08x", &mask[i - 1]);
				DEBUG("mask[%i] = 0x%x\n", i - 1, mask[i - 1]);
				demux |= pdata->mask[i - 1] ^ mask[i - 1];
			}
			read_mask = false;
		}

		if (ret > 0) {
			char c;
			ret = recv(fd, &c, 1, 0);
			if (ret > 0 && c != '\n')
				ret = -EIO;
		}

		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read mask: %s\n", buf);
			free(mask);
			return read ?: ret;
		}

		/* TODO(pcercuei): actually use the mask for something and
		 * demultiplex the received stream to extract the channels
		 * we're interested in */

		ret = read_all(dst, read_len, fd);
		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read response to READ: %s\n", buf);
			free(mask);
			return read ?: ret;
		}

		if (demux) {
			void *demux_dst = dst;
			DEBUG("Demultiplex data (%li bytes)\n", (long) ret);
			ret = iio_device_process_samples(dev,
					mask, nb_words, dst, ret,
					copy_sample_if_enabled, &demux_dst);

			if (ret < 0) {
				strerror_r(-ret, buf, sizeof(buf));
				ERROR("Unable to demux channels: %s\n", buf);
				free(mask);
				return read ?: ret;
			}
			DEBUG("Data demultiplexed: %li bytes\n", (long) ret);
		}

		dst += ret;
		read += ret;
		len -= read_len;
	} while (len);

	free(mask);
	return read;
}

static ssize_t network_read_attr_helper(int fd, const char *id,
		const char *chn, const char *attr, char *dst, size_t len)
{
	long read_len;
	ssize_t ret;
	char buf[1024];

	if (chn)
		snprintf(buf, sizeof(buf), "READ %s %s %s\r\n", id, chn, attr);
	else
		snprintf(buf, sizeof(buf), "READ %s %s\r\n", id, attr);
	read_len = exec_command(buf, fd);
	if (read_len < 0)
		return (ssize_t) read_len;

	if (read_len > len) {
		ERROR("Value returned by server is too large\n");
		return -EIO;
	}

	ret = read_all(dst, read_len, fd);
	if (ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response to READ: %s\n", buf);
		return ret;
	}

	dst[read_len] = '\0';
	return read_len + 1;
}

static ssize_t network_write_attr_helper(int fd, const char *id,
		const char *chn, const char *attr, const char *src)
{
	char buf[1024];

	if (chn)
		snprintf(buf, sizeof(buf), "WRITE %s %s %s %s\r\n",
				id, chn, attr, src);
	else
		snprintf(buf, sizeof(buf), "WRITE %s %s %s\r\n", id, attr, src);
	return (ssize_t) exec_command(buf, fd);
}

static ssize_t network_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	return network_read_attr_helper(dev->ctx->pdata->fd, dev->id,
			NULL, attr, dst, len);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src)
{
	return network_write_attr_helper(dev->ctx->pdata->fd, dev->id,
			NULL, attr, src);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	return network_read_attr_helper(chn->dev->ctx->pdata->fd, chn->dev->id,
			chn->id, attr, dst, len);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	return network_write_attr_helper(chn->dev->ctx->pdata->fd, chn->dev->id,
			chn->id, attr, src);
}

static int network_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	unsigned int i;
	char buf[1024];
	ssize_t ret;
	long resp;

	snprintf(buf, sizeof(buf), "GETTRIG %s\r\n", dev->id);
	resp = exec_command(buf, pdata->fd);
	if (resp < 0) {
		return (int) resp;
	} else if (resp == 0) {
		*trigger = NULL;
		return 0;
	} else if (resp > sizeof(buf)) {
		ERROR("Value returned by server is too large\n");
		return -EIO;
	}

	ret = read_all(buf, resp, pdata->fd);
	if (ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response to GETTRIG: %s\n", buf);
		return ret;
	}

	for (i = 0; i < dev->ctx->nb_devices; i++) {
		struct iio_device *cur = dev->ctx->devices[i];
		if (iio_device_is_trigger(cur) &&
				!strncmp(cur->name, buf, resp)) {
			*trigger = cur;
			return 0;
		}
	}

	return -ENXIO;
}

static int network_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	char buf[1024];
	if (trigger)
		snprintf(buf, sizeof(buf), "SETTRIG %s %s\r\n",
				dev->id, trigger->id);
	else
		snprintf(buf, sizeof(buf), "SETTRIG %s\r\n", dev->id);
	return (int) exec_command(buf, dev->ctx->pdata->fd);
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int i;

	write_command("\r\nEXIT\r\n", pdata->fd);
	close(pdata->fd);
	free(pdata);

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (dev->pdata)
			free(dev->pdata);
	}
}

static struct iio_backend_ops network_ops = {
	.open = network_open,
	.close = network_close,
	.read = network_read,
	.read_device_attr = network_read_dev_attr,
	.write_device_attr = network_write_dev_attr,
	.read_channel_attr = network_read_chn_attr,
	.write_channel_attr = network_write_chn_attr,
	.get_trigger = network_get_trigger,
	.set_trigger = network_set_trigger,
	.shutdown = network_shutdown,
};

static struct iio_context * get_context(int fd)
{
	struct iio_context *ctx;
	char *xml;
	long xml_len = exec_command("PRINT\r\n", fd);
	if (xml_len < 0)
		return NULL;

	DEBUG("Server returned a XML string of length %li\n", xml_len);
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
	struct addrinfo hints, *res;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	unsigned int i;
	int fd, ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(host, IIOD_PORT_STR, &hints, &res);
	if (ret) {
		ERROR("Unable to find IP address for host %s: %s\n",
				host, gai_strerror(ret));
		return NULL;
	}

	fd = socket(res->ai_family, res->ai_socktype, 0);
	if (fd < 0) {
		ERROR("Unable to open socket\n");
		return NULL;
	}

	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
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

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		struct iio_device_pdata *d = calloc(1, sizeof(*d));
		if (!d)
			goto err_network_shutdown;
		else
			dev->pdata = d;
	}

	/* Override the name and low-level functions of the XML context
	 * with those corresponding to the network context */
	ctx->name = "network";
	ctx->ops = &network_ops;
	ctx->pdata = pdata;

	iio_context_init_channels(ctx);
	return ctx;

err_network_shutdown:
	network_shutdown(ctx);
	iio_context_destroy(ctx);
err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
	return NULL;
}
