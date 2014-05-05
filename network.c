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

#include "iio-private.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close(s) closesocket(s)

/* winsock2.h defines ERROR, we don't want that */
#undef ERROR

#else /* _WIN32 */
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* _WIN32 */

#include "debug.h"

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define IIOD_PORT 30431
#define IIOD_PORT_STR STRINGIFY(IIOD_PORT)

struct iio_context_pdata {
	int fd;
	pthread_mutex_t lock;
};

static ssize_t write_all(const void *src, size_t len, int fd)
{
	uintptr_t ptr = (uintptr_t) src;
	while (len) {
		ssize_t ret = send(fd, (const void *) ptr, len, 0);
		if (ret < 0)
			return -errno;
		ptr += ret;
		len -= ret;
	}
	return ptr - (uintptr_t) src;
}

static ssize_t read_all(void *dst, size_t len, int fd)
{
	uintptr_t ptr = (uintptr_t) dst;
	while (len) {
		ssize_t ret = recv(fd, (void *) ptr, len, 0);
		if (ret < 0)
			return -errno;
		ptr += ret;
		len -= ret;
	}
	return ptr - (uintptr_t) dst;
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

		/* Skip the eventual first few carriage returns.
		 * Also stop when a dot is found (for parsing floats) */
		if (buf[i] != '\n' && buf[i] != '.')
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

#if LOG_LEVEL >= DEBUG_L
	if (resp < 0) {
		char buf[1024];
		strerror_r(-resp, buf, sizeof(buf));
		DEBUG("Server returned an error: %s\n", buf);
	}
#endif

	return resp;
}

static int network_open(const struct iio_device *dev,
		size_t samples_count, uint32_t *mask, size_t nb)
{
	char buf[1024], *ptr;
	unsigned int i;
	int ret;

	if (nb != dev->words)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "OPEN %s %lu ",
			dev->id, (unsigned long) samples_count);
	ptr = buf + strlen(buf);

	for (i = nb; i > 0; i--) {
		snprintf(ptr, (ptr - buf) + i * 8, "%08x", mask[i - 1]);
		ptr += 8;
	}
	strcpy(ptr, "\r\n");

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);

	if (ret < 0) {
		return ret;
	} else {
		memcpy(dev->mask, mask, nb * sizeof(*mask));
		return 0;
	}
}

static int network_close(const struct iio_device *dev)
{
	int ret;
	char buf[1024];
	snprintf(buf, sizeof(buf), "CLOSE %s\r\n", dev->id);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);

	return ret;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	uintptr_t ptr = (uintptr_t) dst;
	int fd = dev->ctx->pdata->fd;
	ssize_t ret, read = 0;
	char buf[1024];
	bool read_mask = true;

	if (!len || words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
			dev->id, (unsigned long) len);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = write_command(buf, fd);
	if (ret < 0) {
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
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
			pthread_mutex_unlock(&dev->ctx->pdata->lock);
			return read ? read : ret;
		}

		if (read_len < 0) {
			strerror_r(-read_len, buf, sizeof(buf));
			ERROR("Server returned an error: %s\n", buf);
			pthread_mutex_unlock(&dev->ctx->pdata->lock);
			return read ? read : read_len;
		} else if (read_len == 0) {
			break;
		}

		DEBUG("Bytes to read: %li\n", read_len);

		if (read_mask) {
			DEBUG("Reading mask\n");
			buf[8] = '\0';
			for (i = words; i > 0; i--) {
				ret = read_all(buf, 8, fd);
				if (ret < 0)
					break;
				sscanf(buf, "%08x", &mask[i - 1]);
				DEBUG("mask[%i] = 0x%x\n", i - 1, mask[i - 1]);
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
			pthread_mutex_unlock(&dev->ctx->pdata->lock);
			return read ? read : ret;
		}

		ret = read_all((void *) ptr, read_len, fd);
		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read response to READ: %s\n", buf);
			pthread_mutex_unlock(&dev->ctx->pdata->lock);
			return read ? read : ret;
		}

		ptr += ret;
		read += ret;
		len -= read_len;
	} while (len);

	pthread_mutex_unlock(&dev->ctx->pdata->lock);
	return read;
}

static ssize_t network_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	int fd = dev->ctx->pdata->fd;
	ssize_t ret;
	char buf[1024];
	long resp;

	snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
			dev->id, (unsigned long) len);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = (ssize_t) exec_command(buf, fd);
	if (ret < 0)
		goto err_unlock;

	ret = write_all(src, len, fd);
	if (ret < 0)
		goto err_unlock;

	ret = read_integer(fd, &resp);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);

	if (ret < 0)
		return ret;
	return (ssize_t) resp;

err_unlock:
	pthread_mutex_unlock(&dev->ctx->pdata->lock);
	return ret;
}

static ssize_t network_read_attr_helper(const struct iio_device *dev,
		const char *chn, const char *attr, char *dst,
		size_t len, bool is_debug)
{
	long read_len;
	ssize_t ret;
	char buf[1024];
	int fd = dev->ctx->pdata->fd;
	const char *id = dev->id;

	if (chn)
		snprintf(buf, sizeof(buf), "READ %s %s %s\r\n", id, chn, attr);
	else if (is_debug)
		snprintf(buf, sizeof(buf), "READ %s debug %s\r\n", id, attr);
	else
		snprintf(buf, sizeof(buf), "READ %s %s\r\n", id, attr);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	read_len = exec_command(buf, fd);
	if (read_len < 0) {
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
		return (ssize_t) read_len;
	}

	if ((unsigned long) read_len > len) {
		ERROR("Value returned by server is too large\n");
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
		return -EIO;
	}

	ret = read_all(dst, read_len, fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);

	if (ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response to READ: %s\n", buf);
		return ret;
	}

	dst[read_len] = '\0';
	return read_len + 1;
}

static ssize_t network_write_attr_helper(const struct iio_device *dev,
		const char *chn, const char *attr, const char *src,
		bool is_debug)
{
	char buf[1024];
	ssize_t ret;
	int fd = dev->ctx->pdata->fd;
	const char *id = dev->id;

	if (chn)
		snprintf(buf, sizeof(buf), "WRITE %s %s %s %s\r\n",
				id, chn, attr, src);
	else if (is_debug)
		snprintf(buf, sizeof(buf), "WRITE %s debug %s %s\r\n",
				id, attr, src);
	else
		snprintf(buf, sizeof(buf), "WRITE %s %s %s\r\n", id, attr, src);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = (ssize_t) exec_command(buf, fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);
	return ret;
}

static ssize_t network_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	return network_read_attr_helper(dev, NULL, attr, dst, len, is_debug);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, bool is_debug)
{
	return network_write_attr_helper(dev, NULL, attr, src, is_debug);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	return network_read_attr_helper(chn->dev,
			chn->id, attr, dst, len, false);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	return network_write_attr_helper(chn->dev, chn->id, attr, src, false);
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

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	resp = exec_command(buf, pdata->fd);
	if (resp < 0) {
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
		return (int) resp;
	} else if (resp == 0) {
		*trigger = NULL;
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
		return 0;
	} else if ((unsigned long) resp > sizeof(buf)) {
		ERROR("Value returned by server is too large\n");
		pthread_mutex_unlock(&dev->ctx->pdata->lock);
		return -EIO;
	}

	ret = read_all(buf, resp, pdata->fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);

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
	int ret;
	char buf[1024];
	if (trigger)
		snprintf(buf, sizeof(buf), "SETTRIG %s %s\r\n",
				dev->id, trigger->id);
	else
		snprintf(buf, sizeof(buf), "SETTRIG %s\r\n", dev->id);

	pthread_mutex_lock(&dev->ctx->pdata->lock);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	pthread_mutex_unlock(&dev->ctx->pdata->lock);
	return ret;
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int i;

	pthread_mutex_lock(&pdata->lock);
	write_command("\r\nEXIT\r\n", pdata->fd);
	close(pdata->fd);
	pthread_mutex_unlock(&pdata->lock);

	/* XXX(pcercuei): is this safe? */
	pthread_mutex_destroy(&pdata->lock);
	free(pdata);

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (dev->pdata)
			free(dev->pdata);
	}
}

static int network_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	long maj, min;
	int ret;

	pthread_mutex_lock(&pdata->lock);
	ret = (int) write_command("VERSION\r\n", pdata->fd);
	if (ret < 0)
		goto err_unlock;

	ret = read_integer(pdata->fd, &maj);
	if (!ret)
		ret = read_integer(pdata->fd, &min);
	if (!ret) {
		*major = (unsigned int) maj;
		*minor = (unsigned int) min;
	}

err_unlock:
	pthread_mutex_unlock(&pdata->lock);
	return ret;
}

static struct iio_backend_ops network_ops = {
	.open = network_open,
	.close = network_close,
	.read = network_read,
	.write = network_write,
	.read_device_attr = network_read_dev_attr,
	.write_device_attr = network_write_dev_attr,
	.read_channel_attr = network_read_chn_attr,
	.write_channel_attr = network_write_chn_attr,
	.get_trigger = network_get_trigger,
	.set_trigger = network_set_trigger,
	.shutdown = network_shutdown,
	.get_version = network_get_version,
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
#ifdef _WIN32
	WSADATA wsaData;

	ret = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (ret < 0) {
		ERROR("WSAStartup failed with error %i\n", ret);
		return NULL;
	}
#endif

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
		uint32_t *mask = NULL;

		dev->words = (dev->nb_channels + 31) / 32;
		if (dev->words) {
			mask = calloc(dev->words, sizeof(*mask));
			if (!mask)
				goto err_network_shutdown;
		}

		dev->mask = mask;
	}


	/* Override the name and low-level functions of the XML context
	 * with those corresponding to the network context */
	ctx->name = "network";
	ctx->ops = &network_ops;
	ctx->pdata = pdata;

	ret = iio_context_init(ctx);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to initialize context: %s\n", buf);
		goto err_network_shutdown;
	}

	ret = pthread_mutex_init(&pdata->lock, NULL);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to initialize mutex: %s\n", buf);
		goto err_network_shutdown;
	}

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
