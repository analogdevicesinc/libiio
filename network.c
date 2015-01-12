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

#ifndef HAVE_PTHREAD
#define HAVE_PTHREAD 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
/* Override the default version of Windows supported by MinGW.
 * This is required to use the function inet_ntop. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#define close(s) closesocket(s)

/* winsock2.h defines ERROR, we don't want that */
#undef ERROR

#else /* _WIN32 */
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* _WIN32 */

#if HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-common/error.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#endif

#include "debug.h"

#define DEFAULT_TIMEOUT_MS 5000

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define IIOD_PORT 30431
#define IIOD_PORT_STR STRINGIFY(IIOD_PORT)

struct iio_context_pdata {
	int fd;
#if HAVE_PTHREAD
	pthread_mutex_t lock;
#endif
};

#ifdef HAVE_AVAHI
struct avahi_discovery_data {
	AvahiSimplePoll *poll;
	AvahiAddress *address;
	uint16_t *port;
	bool found, resolved;
};

static void __avahi_resolver_cb(AvahiServiceResolver *resolver,
		__notused AvahiIfIndex iface, __notused AvahiProtocol proto,
		__notused AvahiResolverEvent event, __notused const char *name,
		__notused const char *type, __notused const char *domain,
		__notused const char *host_name, const AvahiAddress *address,
		uint16_t port, __notused AvahiStringList *txt,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct avahi_discovery_data *ddata = (struct avahi_discovery_data *) d;

	memcpy(ddata->address, address, sizeof(*address));
	*ddata->port = port;
	ddata->resolved = true;
	avahi_service_resolver_free(resolver);
}

static void __avahi_browser_cb(AvahiServiceBrowser *browser,
		AvahiIfIndex iface, AvahiProtocol proto,
		AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct avahi_discovery_data *ddata = (struct avahi_discovery_data *) d;
	struct AvahiClient *client = avahi_service_browser_get_client(browser);

	switch (event) {
	default:
	case AVAHI_BROWSER_NEW:
		ddata->found = !!avahi_service_resolver_new(client, iface,
				proto, name, type, domain,
				AVAHI_PROTO_UNSPEC, 0,
				__avahi_resolver_cb, d);
		break;
	case AVAHI_BROWSER_ALL_FOR_NOW:
		if (ddata->found) {
			while (!ddata->resolved) {
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 4000000;
				nanosleep(&ts, NULL);
			}
		}
	case AVAHI_BROWSER_FAILURE: /* fall-through */
		avahi_simple_poll_quit(ddata->poll);
	case AVAHI_BROWSER_CACHE_EXHAUSTED: /* fall-through */
		break;
	}
}

static int discover_host(AvahiAddress *addr, uint16_t *port)
{
	struct avahi_discovery_data ddata;
	int ret = 0;
	AvahiClient *client;
	AvahiServiceBrowser *browser;
	AvahiSimplePoll *poll = avahi_simple_poll_new();
	if (!poll)
		return -ENOMEM;

	client = avahi_client_new(avahi_simple_poll_get(poll),
			0, NULL, NULL, &ret);
	if (!client) {
		ERROR("Unable to start ZeroConf client :%s\n",
				avahi_strerror(ret));
		goto err_free_poll;
	}

	memset(&ddata, 0, sizeof(ddata));
	ddata.poll = poll;
	ddata.address = addr;
	ddata.port = port;

	browser = avahi_service_browser_new(client,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			"_iio._tcp", NULL, 0, __avahi_browser_cb, &ddata);
	if (!browser) {
		ret = avahi_client_errno(client);
		ERROR("Unable to create ZeroConf browser: %s\n",
				avahi_strerror(ret));
		goto err_free_client;
	}

	DEBUG("Trying to discover host\n");
	avahi_simple_poll_loop(poll);

	if (!ddata.found)
		ret = ENXIO;

	avahi_service_browser_free(browser);
err_free_client:
	avahi_client_free(client);
err_free_poll:
	avahi_simple_poll_free(poll);
	return -ret; /* we want a negative error code */
}
#endif /* HAVE_AVAHI */

static void network_lock(struct iio_context_pdata *pdata)
{
#if HAVE_PTHREAD
	pthread_mutex_lock(&pdata->lock);
#endif
}

static void network_unlock(struct iio_context_pdata *pdata)
{
#if HAVE_PTHREAD
	pthread_mutex_unlock(&pdata->lock);
#endif
}

static ssize_t write_all(const void *src, size_t len, int fd)
{
	uintptr_t ptr = (uintptr_t) src;
	while (len) {
		ssize_t ret = send(fd, (const void *) ptr, len, 0);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -errno;
		}
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
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (ret == 0)
			return -EPIPE;
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

static int network_open(const struct iio_device *dev, size_t samples_count,
		uint32_t *mask, size_t nb, bool cyclic)
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

	strcpy(ptr, cyclic ? " CYCLIC\r\n" : "\r\n");

	network_lock(dev->ctx->pdata);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	network_unlock(dev->ctx->pdata);

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

	network_lock(dev->ctx->pdata);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	network_unlock(dev->ctx->pdata);

	return ret;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	uintptr_t ptr = (uintptr_t) dst;
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	int fd = pdata->fd;
	ssize_t ret, read = 0;
	char buf[1024];
	bool read_mask = true;

	if (!len || words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
			dev->id, (unsigned long) len);

	network_lock(pdata);
	ret = write_command(buf, fd);
	if (ret < 0) {
		network_unlock(pdata);
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
			network_unlock(pdata);
			return read ? read : ret;
		}

		if (read_len < 0) {
			strerror_r(-read_len, buf, sizeof(buf));
			ERROR("Server returned an error: %s\n", buf);
			network_unlock(pdata);
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
			ret = read_all(&c, 1, fd);
			if (ret > 0 && c != '\n')
				ret = -EIO;
		}

		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read mask: %s\n", buf);
			network_unlock(pdata);
			return read ? read : ret;
		}

		ret = read_all((void *) ptr, read_len, fd);
		if (ret < 0) {
			strerror_r(-ret, buf, sizeof(buf));
			ERROR("Unable to read response to READ: %s\n", buf);
			network_unlock(pdata);
			return read ? read : ret;
		}

		ptr += ret;
		read += ret;
		len -= read_len;
	} while (len);

	network_unlock(pdata);
	return read;
}

static ssize_t do_write(struct iio_context_pdata *pdata, bool attr,
		const char *command, const void *src, size_t len)
{
	int fd = pdata->fd;
	ssize_t ret;
	long resp;

	network_lock(pdata);
	if (attr)
		ret = (ssize_t) write_command(command, fd);
	else
		ret = (ssize_t) exec_command(command, fd);
	if (ret < 0)
		goto err_unlock;

	ret = write_all(src, len, fd);
	if (ret < 0)
		goto err_unlock;

	ret = read_integer(fd, &resp);
	network_unlock(pdata);

	if (ret < 0)
		return ret;
	return (ssize_t) resp;

err_unlock:
	network_unlock(pdata);
	return ret;
}

static ssize_t network_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
			dev->id, (unsigned long) len);
	return do_write(dev->ctx->pdata, false, buf, src, len);
}

static ssize_t network_read_attr_helper(const struct iio_device *dev,
		const struct iio_channel *chn, const char *attr, char *dst,
		size_t len, bool is_debug)
{
	long read_len;
	ssize_t ret;
	char buf[1024];
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	int fd = pdata->fd;
	const char *id = dev->id;

	if (chn)
		snprintf(buf, sizeof(buf), "READ %s %s %s %s\r\n", id,
				chn->is_output ? "OUTPUT" : "INPUT",
				chn->id, attr ? attr : "");
	else if (is_debug)
		snprintf(buf, sizeof(buf), "READ %s DEBUG %s\r\n",
				id, attr ? attr : "");
	else
		snprintf(buf, sizeof(buf), "READ %s %s\r\n",
				id, attr ? attr : "");

	network_lock(pdata);
	read_len = exec_command(buf, fd);
	if (read_len < 0) {
		network_unlock(pdata);
		return (ssize_t) read_len;
	}

	if ((unsigned long) read_len > len) {
		ERROR("Value returned by server is too large\n");
		network_unlock(pdata);
		return -EIO;
	}

	ret = read_all(dst, read_len, fd);
	network_unlock(pdata);

	if (ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response to READ: %s\n", buf);
		return ret;
	}

	return read_len;
}

static ssize_t network_write_attr_helper(const struct iio_device *dev,
		const struct iio_channel *chn, const char *attr,
		const char *src, size_t len, bool is_debug)
{
	char buf[1024];
	const char *id = dev->id;

	if (chn)
		snprintf(buf, sizeof(buf), "WRITE %s %s %s %s %lu\r\n",
				id, chn->is_output ? "OUTPUT" : "INPUT",
				chn->id, attr ? attr : "", (unsigned long) len);
	else if (is_debug)
		snprintf(buf, sizeof(buf), "WRITE %s DEBUG %s %lu\r\n",
				id, attr ? attr : "", (unsigned long) len);
	else
		snprintf(buf, sizeof(buf), "WRITE %s %s %lu\r\n",
				id, attr ? attr : "", (unsigned long) len);
	return do_write(dev->ctx->pdata, true, buf, src, len);
}

static ssize_t network_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	if (attr && ((is_debug && !iio_device_find_debug_attr(dev, attr)) ||
			(!is_debug && !iio_device_find_attr(dev, attr))))
		return -ENOENT;

	return network_read_attr_helper(dev, NULL, attr, dst, len, is_debug);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, bool is_debug)
{
	if (attr && ((is_debug && !iio_device_find_debug_attr(dev, attr)) ||
			(!is_debug && !iio_device_find_attr(dev, attr))))
		return -ENOENT;

	return network_write_attr_helper(dev, NULL, attr, src, len, is_debug);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	if (attr && !iio_channel_find_attr(chn, attr))
		return -ENOENT;

	return network_read_attr_helper(chn->dev, chn, attr, dst, len, false);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	if (attr && !iio_channel_find_attr(chn, attr))
		return -ENOENT;

	return network_write_attr_helper(chn->dev, chn, attr, src, len, false);
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

	network_lock(dev->ctx->pdata);
	resp = exec_command(buf, pdata->fd);
	if (resp < 0) {
		network_unlock(pdata);
		return (int) resp;
	} else if (resp == 0) {
		*trigger = NULL;
		network_unlock(pdata);
		return 0;
	} else if ((unsigned long) resp > sizeof(buf)) {
		ERROR("Value returned by server is too large\n");
		network_unlock(pdata);
		return -EIO;
	}

	ret = read_all(buf, resp, pdata->fd);
	network_unlock(pdata);

	if (ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to read response to GETTRIG: %s\n", buf);
		return ret;
	}

	if (buf[0] == '\0') {
		*trigger = NULL;
		return 0;
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

	network_lock(dev->ctx->pdata);
	ret = (int) exec_command(buf, dev->ctx->pdata->fd);
	network_unlock(dev->ctx->pdata);
	return ret;
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int i;

	network_lock(pdata);
	write_command("\r\nEXIT\r\n", pdata->fd);
	close(pdata->fd);
	network_unlock(pdata);

#if HAVE_PTHREAD
	/* XXX(pcercuei): is this safe? */
	pthread_mutex_destroy(&pdata->lock);
#endif
	free(pdata);

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (dev->pdata)
			free(dev->pdata);
	}
}

static int network_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	struct iio_context_pdata *pdata = ctx->pdata;
	long maj, min;
	int ret;

	network_lock(pdata);
	ret = (int) write_command("VERSION\r\n", pdata->fd);
	if (ret < 0)
		goto err_unlock;

	ret = read_integer(pdata->fd, &maj);
	if (!ret)
		ret = read_integer(pdata->fd, &min);
	if (!ret) {
		char tag[8];
		tag[7] = '\0';

		ret = read_all(tag, sizeof(tag) - 1, pdata->fd);
		if (ret < 0)
			goto err_unlock;

		if (major)
			*major = (unsigned int) maj;
		if (minor)
			*minor = (unsigned int) min;
		if (git_tag)
			strncpy(git_tag, tag, 8);
	}

	ret = 0;
err_unlock:
	network_unlock(pdata);
	return ret;
}

static unsigned int calculate_remote_timeout(unsigned int timeout)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout / 2;
}

static int set_socket_timeout(int fd, unsigned int timeout)
{
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
				&timeout, sizeof(timeout)) < 0 ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
				&timeout, sizeof(timeout)) < 0)
		return -errno;
	else
		return 0;
}

static int set_remote_timeout(struct iio_context *ctx, unsigned int timeout)
{
	char buf[1024];
	int ret;

	snprintf(buf, sizeof(buf), "TIMEOUT %u\r\n", timeout);
	network_lock(ctx->pdata);
	ret = (int) exec_command(buf, ctx->pdata->fd);
	network_unlock(ctx->pdata);
	return ret;
}

static int network_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	int ret = set_socket_timeout(ctx->pdata->fd, timeout);
	if (!ret) {
		timeout = calculate_remote_timeout(timeout);
		ret = set_remote_timeout(ctx, timeout);
	}
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		WARNING("Unable to set R/W timeout: %s\n", buf);
	} else {
		ctx->rw_timeout_ms = timeout;
	}
	return ret;
}

static struct iio_context * network_clone(const struct iio_context *ctx)
{
	return iio_create_network_context(ctx->description);
}

static struct iio_backend_ops network_ops = {
	.clone = network_clone,
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
	.set_timeout = network_set_timeout,
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

	if (ctx)
		ctx->xml = xml;
	else
		free(xml);
	return ctx;
}

#ifndef _WIN32
/* The purpose of this function is to provide a version of connect()
 * that does not ignore timeouts... */
static int do_connect(int fd, const struct sockaddr *addr,
		socklen_t addrlen, struct timeval *timeout)
{
	int ret, flags, error;
	socklen_t len;
	fd_set set;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	ret = fcntl(fd, F_GETFL, 0);
	if (ret < 0)
		return -errno;

	flags = ret;

	ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (ret < 0)
		return -errno;

	ret = connect(fd, addr, addrlen);
	if (ret < 0 && errno != EINPROGRESS) {
		ret = -errno;
		goto end;
	}

	ret = select(fd + 1, &set, &set, NULL, timeout);
	if (ret < 0) {
		ret = -errno;
		goto end;
	}
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto end;
	}

	/* Verify that we don't have an error */
	len = sizeof(error);
	ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
	if(ret < 0) {
		ret = -errno;
		goto end;
	}
	if (error) {
		ret = -error;
		goto end;
	}

end:
	/* Restore blocking mode */
	fcntl(fd, F_SETFL, flags);
	return ret;
}
#endif

struct iio_context * network_create_context(const char *host)
{
	struct addrinfo hints, *res;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	struct timeval timeout;
	unsigned int i, len;
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

#ifdef HAVE_AVAHI
	if (!host) {
		char addr_str[AVAHI_ADDRESS_STR_MAX];
		char port_str[6];
		AvahiAddress address;
		uint16_t port = IIOD_PORT;

		memset(&address, 0, sizeof(address));

		ret = discover_host(&address, &port);
		if (ret < 0) {
			ERROR("Unable to find host: %s\n", strerror(-ret));
			return NULL;
		}

		avahi_address_snprint(addr_str, sizeof(addr_str), &address);
		snprintf(port_str, sizeof(port_str), "%hu", port);
		ret = getaddrinfo(addr_str, port_str, &hints, &res);
	} else
#endif
	{
		ret = getaddrinfo(host, IIOD_PORT_STR, &hints, &res);
	}

	if (ret) {
		ERROR("Unable to find host: %s\n", gai_strerror(ret));
		return NULL;
	}

	fd = socket(res->ai_family, res->ai_socktype, 0);
	if (fd < 0) {
		ERROR("Unable to open socket\n");
		return NULL;
	}

	timeout.tv_sec = DEFAULT_TIMEOUT_MS / 1000;
	timeout.tv_usec = (DEFAULT_TIMEOUT_MS % 1000) * 1000;

#ifndef _WIN32
	ret = do_connect(fd, res->ai_addr, res->ai_addrlen, &timeout);
#else
	ret = connect(fd, res->ai_addr, res->ai_addrlen);
#endif
	if (ret < 0) {
		ERROR("Unable to connect\n");
		goto err_close_socket;
	}

	set_socket_timeout(fd, DEFAULT_TIMEOUT_MS);

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

#ifdef HAVE_IPV6
	len = INET6_ADDRSTRLEN + IF_NAMESIZE + 2;
#else
	len = INET_ADDRSTRLEN + 1;
#endif
	ctx->description = malloc(len);
	if (!ctx->description) {
		ERROR("Unable to allocate memory\n");
		goto err_network_shutdown;
	}

	ctx->description[0] = '\0';

#ifdef HAVE_IPV6
	if (res->ai_family == AF_INET6) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *) res->ai_addr;
		char *ptr;
		inet_ntop(AF_INET6, &in->sin6_addr,
				ctx->description, INET6_ADDRSTRLEN);

		ptr = if_indextoname(in->sin6_scope_id, ctx->description +
				strlen(ctx->description) + 1);
		if (!ptr) {
			ERROR("Unable to lookup interface of IPv6 address\n");
			goto err_network_shutdown;
		}

		*(ptr - 1) = '%';
	}
#endif
	if (res->ai_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *) res->ai_addr;
		inet_ntop(AF_INET, &in->sin_addr,
				ctx->description, INET_ADDRSTRLEN);
	}

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		uint32_t *mask = NULL;

		dev->words = (dev->nb_channels + 31) / 32;
		if (dev->words) {
			mask = calloc(dev->words, sizeof(*mask));
			if (!mask) {
				ERROR("Unable to allocate memory\n");
				goto err_network_shutdown;
			}
		}

		dev->mask = mask;
	}

	iio_context_init(ctx);

#if HAVE_PTHREAD
	ret = pthread_mutex_init(&pdata->lock, NULL);
	if (ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		ERROR("Unable to initialize mutex: %s\n", buf);
		goto err_network_shutdown;
	}
#endif

	set_remote_timeout(ctx, calculate_remote_timeout(DEFAULT_TIMEOUT_MS));
	return ctx;

err_network_shutdown:
	iio_context_destroy(ctx);
	return NULL;
err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
	return NULL;
}
