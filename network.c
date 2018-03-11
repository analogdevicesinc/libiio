/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2015 Analog Devices, Inc.
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

#include "iio-config.h"
#include "iio-private.h"
#include "iio-lock.h"
#include "iiod-client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close(s) closesocket(s)

/* winsock2.h defines ERROR, we don't want that */
#undef ERROR

#else /* _WIN32 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* _WIN32 */

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

struct iio_network_io_context {
	int fd;

	/* Only buffer IO contexts can be cancelled. */
	bool cancellable;
	bool cancelled;
#if defined(_WIN32)
	WSAEVENT events[2];
#elif defined(WITH_NETWORK_EVENTFD)
	int cancel_fd[1]; /* eventfd */
#else
	int cancel_fd[2]; /* pipe */
#endif
	unsigned int timeout_ms;
};

struct iio_context_pdata {
	struct iio_network_io_context io_ctx;
	struct addrinfo *addrinfo;
	struct iio_mutex *lock;
	struct iiod_client *iiod_client;
	bool msg_trunc_supported;
};

struct iio_device_pdata {
	struct iio_network_io_context io_ctx;
#ifdef WITH_NETWORK_GET_BUFFER
	int memfd;
	void *mmap_addr;
	size_t mmap_len;
#endif
	bool wait_for_err_code, is_cyclic, is_tx;
	struct iio_mutex *lock;
};

#ifdef _WIN32

static int set_blocking_mode(int s, bool blocking)
{
	unsigned long nonblock;
	int ret;

	nonblock = blocking ? 0 : 1;

	ret = ioctlsocket(s, FIONBIO, &nonblock);
	if (ret == SOCKET_ERROR) {
		ret = -WSAGetLastError();
		return ret;
	}

	return 0;
}

static int setup_cancel(struct iio_network_io_context *io_ctx)
{
	io_ctx->events[0] = WSACreateEvent();
	if (io_ctx->events[0] == WSA_INVALID_EVENT)
		return -ENOMEM; /* Pretty much the only error that can happen */

	io_ctx->events[1] = WSACreateEvent();
	if (io_ctx->events[1] == WSA_INVALID_EVENT) {
		WSACloseEvent(io_ctx->events[0]);
		return -ENOMEM;
	}

	return 0;
}

static void cleanup_cancel(struct iio_network_io_context *io_ctx)
{
	WSACloseEvent(io_ctx->events[0]);
	WSACloseEvent(io_ctx->events[1]);
}

static void do_cancel(struct iio_network_io_context *io_ctx)
{
	WSASetEvent(io_ctx->events[1]);
}

static int wait_cancellable(struct iio_network_io_context *io_ctx, bool read)
{
	long wsa_events = FD_CLOSE;
	DWORD ret;

	if (!io_ctx->cancellable)
		return 0;

	if (read)
		wsa_events |= FD_READ;
	else
		wsa_events |= FD_WRITE;

	WSAEventSelect(io_ctx->fd, NULL, 0);
	WSAResetEvent(io_ctx->events[0]);
	WSAEventSelect(io_ctx->fd, io_ctx->events[0], wsa_events);

	ret = WSAWaitForMultipleEvents(2, io_ctx->events, FALSE,
		WSA_INFINITE, FALSE);

	if (ret == WSA_WAIT_EVENT_0 + 1)
		return -EBADF;

	return 0;
}

static int network_get_error(void)
{
	return -WSAGetLastError();
}

static bool network_should_retry(int err)
{
	return err == -WSAEWOULDBLOCK || err == -WSAETIMEDOUT;
}

static bool network_is_interrupted(int err)
{
	return false;
}

static bool network_connect_in_progress(int err)
{
	return err == -WSAEWOULDBLOCK;
}

#define NETWORK_ERR_TIMEOUT WSAETIMEDOUT

#else

static int set_blocking_mode(int fd, bool blocking)
{
	int ret = fcntl(fd, F_GETFL, 0);
	if (ret < 0)
		return -errno;

	if (blocking)
		ret &= ~O_NONBLOCK;
	else
		ret |= O_NONBLOCK;

	ret = fcntl(fd, F_SETFL, ret);
	return ret < 0 ? -errno : 0;
}

#include <poll.h>

#if defined(WITH_NETWORK_EVENTFD)

#include <sys/eventfd.h>

static int create_cancel_fd(struct iio_network_io_context *io_ctx)
{
	io_ctx->cancel_fd[0] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (io_ctx->cancel_fd[0] < 0)
		return -errno;
	return 0;
}

static void cleanup_cancel(struct iio_network_io_context *io_ctx)
{
	close(io_ctx->cancel_fd[0]);
}

#define CANCEL_WR_FD 0

#else

static int create_cancel_fd(struct iio_network_io_context *io_ctx)
{
	int ret;

#ifdef HAS_PIPE2
	ret = pipe2(io_ctx->cancel_fd, O_CLOEXEC | O_NONBLOCK);
	if (ret < 0 && errno != ENOSYS) /* If ENOSYS try pipe() */
		return -errno;
#endif
	ret = pipe(io_ctx->cancel_fd);
	if (ret < 0)
		return -errno;
	ret = set_blocking_mode(io_ctx->cancel_fd[0], false);
	if (ret < 0)
		goto err_close;
	ret = set_blocking_mode(io_ctx->cancel_fd[1], false);
	if (ret < 0)
		goto err_close;

	return 0;
err_close:
	close(io_ctx->cancel_fd[0]);
	close(io_ctx->cancel_fd[1]);
	return ret;
}

static void cleanup_cancel(struct iio_network_io_context *io_ctx)
{
	close(io_ctx->cancel_fd[0]);
	close(io_ctx->cancel_fd[1]);
}

#define CANCEL_WR_FD 1

#endif

static int setup_cancel(struct iio_network_io_context *io_ctx)
{
	int ret;

	ret = set_blocking_mode(io_ctx->fd, false);
	if (ret)
		return ret;

	return create_cancel_fd(io_ctx);
}

static void do_cancel(struct iio_network_io_context *io_ctx)
{
	uint64_t event = 1;
	int ret;

	ret = write(io_ctx->cancel_fd[CANCEL_WR_FD], &event, sizeof(event));
	if (ret == -1) {
		/* If this happens something went very seriously wrong */
		char err_str[1024];
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to signal cancellation event: %s\n", err_str);
	}
}

static int wait_cancellable(struct iio_network_io_context *io_ctx, bool read)
{
	struct pollfd pfd[2];
	int ret;

	if (!io_ctx->cancellable)
		return 0;

	memset(pfd, 0, sizeof(pfd));

	pfd[0].fd = io_ctx->fd;
	if (read)
		pfd[0].events = POLLIN;
	else
		pfd[0].events = POLLOUT;
	pfd[1].fd = io_ctx->cancel_fd[0];
	pfd[1].events = POLLIN;

	do {
		int timeout_ms;

		if (io_ctx->timeout_ms > 0)
			timeout_ms = (int) io_ctx->timeout_ms;
		else
			timeout_ms = -1;

		do {
			ret = poll(pfd, 2, timeout_ms);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1)
			return -errno;
		if (!ret)
			return -EPIPE;

		if (pfd[1].revents & POLLIN)
			return -EBADF;
	} while (!(pfd[0].revents & (pfd[0].events | POLLERR | POLLHUP)));

	return 0;
}

static int network_get_error(void)
{
	return -errno;
}

static bool network_should_retry(int err)
{
	return err == -EAGAIN;
}

static bool network_is_interrupted(int err)
{
	return err == -EINTR;
}

static bool network_connect_in_progress(int err)
{
	return err == -EINPROGRESS;
}

#define NETWORK_ERR_TIMEOUT ETIMEDOUT

#endif

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
		/* fall-through */
	case AVAHI_BROWSER_FAILURE:
		avahi_simple_poll_quit(ddata->poll);
		/* fall-through */
	case AVAHI_BROWSER_CACHE_EXHAUSTED:
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

static ssize_t network_recv(struct iio_network_io_context *io_ctx,
		void *data, size_t len, int flags)
{
	ssize_t ret;
	int err;

	while (1) {
		ret = wait_cancellable(io_ctx, true);
		if (ret < 0)
			return ret;

		ret = recv(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (network_should_retry(err)) {
			if (io_ctx->cancellable)
				continue;
			else
				return -EPIPE;
		} else if (!network_is_interrupted(err)) {
			return (ssize_t) err;
		}
	}
	return ret;
}

static ssize_t network_send(struct iio_network_io_context *io_ctx,
		const void *data, size_t len, int flags)
{
	ssize_t ret;
	int err;

	while (1) {
		ret = wait_cancellable(io_ctx, false);
		if (ret < 0)
			return ret;

		ret = send(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (network_should_retry(err)) {
			if (io_ctx->cancellable)
				continue;
			else
				return -EPIPE;
		} else if (!network_is_interrupted(err)) {
			return (ssize_t) err;
		}
	}

	return ret;
}

static ssize_t write_all(struct iio_network_io_context *io_ctx,
		const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;
	while (len) {
		ssize_t ret = network_send(io_ctx, (const void *) ptr, len, 0);
		if (ret < 0)
			return ret;
		ptr += ret;
		len -= ret;
	}
	return (ssize_t)(ptr - (uintptr_t) src);
}

static ssize_t write_command(struct iio_network_io_context *io_ctx,
		const char *cmd)
{
	ssize_t ret;

	DEBUG("Writing command: %s\n", cmd);
	ret = write_all(io_ctx, cmd, strlen(cmd));
	if (ret < 0) {
		char buf[1024];
		iio_strerror(-ret, buf, sizeof(buf));
		ERROR("Unable to send command: %s\n", buf);
	}
	return ret;
}

static void network_cancel(const struct iio_device *dev)
{
	struct iio_device_pdata *ppdata = dev->pdata;

	do_cancel(&ppdata->io_ctx);

	ppdata->io_ctx.cancelled = true;
}

#ifndef _WIN32

/* Use it if available */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

static int do_create_socket(const struct addrinfo *addrinfo)
{
	int fd;

	fd = socket(addrinfo->ai_family, addrinfo->ai_socktype | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	return fd;
}

static int set_socket_timeout(int fd, unsigned int timeout)
{
	struct timeval tv;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0 ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
				&tv, sizeof(tv)) < 0)
		return -errno;
	else
		return 0;
}
#else

/* Use it if available */
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0
#endif

static int do_create_socket(const struct addrinfo *addrinfo)
{
	SOCKET s;

	s = WSASocketW(addrinfo->ai_family, addrinfo->ai_socktype, 0, NULL, 0,
		WSA_FLAG_NO_HANDLE_INHERIT | WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET)
		return -WSAGetLastError();

	return (int) s;
}

static int set_socket_timeout(int fd, unsigned int timeout)
{
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
				(const char *) &timeout, sizeof(timeout)) < 0 ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
				(const char *) &timeout, sizeof(timeout)) < 0)
		return -WSAGetLastError();
	else
		return 0;
}
#endif /* !_WIN32 */

/* The purpose of this function is to provide a version of connect()
 * that does not ignore timeouts... */
static int do_connect(int fd, const struct addrinfo *addrinfo,
	unsigned int timeout)
{
	int ret, error;
	socklen_t len;
#ifdef _WIN32
	struct timeval tv;
	struct timeval *ptv;
	fd_set set;
#else
	struct pollfd pfd;
#endif

	ret = set_blocking_mode(fd, false);
	if (ret < 0)
		return ret;

	ret = connect(fd, addrinfo->ai_addr, (int) addrinfo->ai_addrlen);
	if (ret < 0) {
		ret = network_get_error();
		if (!network_connect_in_progress(ret))
			return ret;
	}

#ifdef _WIN32
	FD_ZERO(&set);
	FD_SET(fd, &set);

	if (timeout != 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		ptv = &tv;
	} else {
		ptv = NULL;
	}

	ret = select(fd + 1, NULL, &set, &set, ptv);
#else
	pfd.fd = fd;
	pfd.events = POLLOUT | POLLERR;
	pfd.revents = 0;

	do {
		ret = poll(&pfd, 1, timeout);
	} while (ret == -1 && errno == EINTR);
#endif

	if (ret < 0)
		return network_get_error();

	if (ret == 0)
		return -NETWORK_ERR_TIMEOUT;

	/* Verify that we don't have an error */
	len = sizeof(error);
	ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &len);
	if(ret < 0)
		return network_get_error();

	if (error)
		return -error;

	ret = set_blocking_mode(fd, true);
	if (ret < 0)
		return ret;

	return 0;
}

static int create_socket(const struct addrinfo *addrinfo, unsigned int timeout)
{
	int ret, fd, yes = 1;

	fd = do_create_socket(addrinfo);
	if (fd < 0)
		return fd;

	ret = do_connect(fd, addrinfo, timeout);
	if (ret < 0) {
		close(fd);
		return ret;
	}

	set_socket_timeout(fd, timeout);
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				(const char *) &yes, sizeof(yes)) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	return fd;
}

static int network_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	struct iio_device_pdata *ppdata = dev->pdata;
	int ret = -EBUSY;

	iio_mutex_lock(ppdata->lock);
	if (ppdata->io_ctx.fd >= 0)
		goto out_mutex_unlock;

	ret = create_socket(pdata->addrinfo, DEFAULT_TIMEOUT_MS);
	if (ret < 0)
		goto out_mutex_unlock;

	ppdata->io_ctx.fd = ret;
	ppdata->io_ctx.cancelled = false;
	ppdata->io_ctx.cancellable = false;
	ppdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;

	ret = iiod_client_open_unlocked(pdata->iiod_client,
			&ppdata->io_ctx, dev, samples_count, cyclic);
	if (ret < 0)
		goto err_close_socket;

	ret = setup_cancel(&ppdata->io_ctx);
	if (ret < 0)
		goto err_close_socket;

	set_socket_timeout(ppdata->io_ctx.fd, pdata->io_ctx.timeout_ms);

	ppdata->io_ctx.timeout_ms = pdata->io_ctx.timeout_ms;
	ppdata->io_ctx.cancellable = true;
	ppdata->is_tx = iio_device_is_tx(dev);
	ppdata->is_cyclic = cyclic;
	ppdata->wait_for_err_code = false;
#ifdef WITH_NETWORK_GET_BUFFER
	ppdata->mmap_len = samples_count * iio_device_get_sample_size(dev);
#endif

	iio_mutex_unlock(ppdata->lock);

	return 0;

err_close_socket:
	close(ppdata->io_ctx.fd);
	ppdata->io_ctx.fd = -1;
out_mutex_unlock:
	iio_mutex_unlock(ppdata->lock);
	return ret;
}

static int network_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBADF;

	iio_mutex_lock(pdata->lock);

	if (pdata->io_ctx.fd >= 0) {
		if (!pdata->io_ctx.cancelled) {
			ret = iiod_client_close_unlocked(
					dev->ctx->pdata->iiod_client,
					&pdata->io_ctx, dev);

			write_command(&pdata->io_ctx, "\r\nEXIT\r\n");
		} else {
			ret = 0;
		}

		cleanup_cancel(&pdata->io_ctx);
		close(pdata->io_ctx.fd);
		pdata->io_ctx.fd = -1;
	}

#ifdef WITH_NETWORK_GET_BUFFER
	if (pdata->memfd >= 0)
		close(pdata->memfd);
	pdata->memfd = -1;

	if (pdata->mmap_addr) {
		munmap(pdata->mmap_addr, pdata->mmap_len);
		pdata->mmap_addr = NULL;
	}
#endif

	iio_mutex_unlock(pdata->lock);
	return ret;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_read_unlocked(dev->ctx->pdata->iiod_client,
			&pdata->io_ctx, dev, dst, len, mask, words);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t network_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_write_unlocked(dev->ctx->pdata->iiod_client,
			&pdata->io_ctx, dev, src, len);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

#ifdef WITH_NETWORK_GET_BUFFER

static ssize_t read_all(struct iio_network_io_context *io_ctx,
		void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;
	while (len) {
		ssize_t ret = network_recv(io_ctx, (void *) ptr, len, 0);
		if (ret < 0)
			return ret;
		ptr += ret;
		len -= ret;
	}
	return (ssize_t)(ptr - (uintptr_t) dst);
}

static int read_integer(struct iio_network_io_context *io_ctx, long *val)
{
	unsigned int i;
	char buf[1024], *ptr;
	ssize_t ret;
	bool found = false;

	for (i = 0; i < sizeof(buf) - 1; i++) {
		ret = read_all(io_ctx, buf + i, 1);
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

static ssize_t network_read_mask(struct iio_network_io_context *io_ctx,
		uint32_t *mask, size_t words)
{
	long read_len;
	ssize_t ret;

	ret = read_integer(io_ctx, &read_len);
	if (ret < 0)
		return ret;

	if (read_len > 0 && mask) {
		size_t i;
		char buf[9];

		buf[8] = '\0';
		DEBUG("Reading mask\n");

		for (i = words; i > 0; i--) {
			ret = read_all(io_ctx, buf, 8);
			if (ret < 0)
				return ret;

			sscanf(buf, "%08x", &mask[i - 1]);
			DEBUG("mask[%lu] = 0x%x\n",
					(unsigned long)(i - 1), mask[i - 1]);
		}
	}

	if (read_len > 0) {
		char c;
		ssize_t nb = read_all(io_ctx, &c, 1);
		if (nb > 0 && c != '\n')
			read_len = -EIO;
	}

	return (ssize_t) read_len;
}

static ssize_t read_error_code(struct iio_network_io_context *io_ctx)
{
	/*
	 * The server returns two integer codes.
	 * The first one is returned right after the WRITEBUF command is issued,
	 * and corresponds to the error code returned when the server attempted
	 * to open the device.
	 * If zero, a second error code is returned, that corresponds (if positive)
	 * to the number of bytes written.
	 *
	 * To speed up things, we delay error reporting. We just send out the
	 * data without reading the error code that the server gives us, because
	 * the answer will take too much time. If an error occured, it will be
	 * reported by the next call to iio_buffer_push().
	 */

	unsigned int i;
	long resp = 0;

	for (i = 0; i < 2; i++) {
		ssize_t ret = read_integer(io_ctx, &resp);
		if (ret < 0)
			return ret;
		if (resp < 0)
			return (ssize_t) resp;
	}

	return (ssize_t) resp;
}

static ssize_t write_rwbuf_command(const struct iio_device *dev,
		const char *cmd)
{
	struct iio_device_pdata *pdata = dev->pdata;

	if (pdata->wait_for_err_code) {
		ssize_t ret = read_error_code(&pdata->io_ctx);

		pdata->wait_for_err_code = false;
		if (ret < 0)
			return ret;
	}

	return write_command(&pdata->io_ctx, cmd);
}

static ssize_t network_do_splice(struct iio_device_pdata *pdata, size_t len,
		bool read)
{
	int pipefd[2];
	int fd_in, fd_out;
	ssize_t ret, read_len = len, write_len = 0;

	ret = (ssize_t) pipe2(pipefd, O_CLOEXEC);
	if (ret < 0)
		return -errno;

	if (read) {
	    fd_in = pdata->io_ctx.fd;
	    fd_out = pdata->memfd;
	} else {
	    fd_in = pdata->memfd;
	    fd_out = pdata->io_ctx.fd;
	}

	do {
		ret = wait_cancellable(&pdata->io_ctx, read);
		if (ret < 0)
			goto err_close_pipe;

		if (read_len) {
			/*
			 * SPLICE_F_NONBLOCK is just here to avoid a deadlock when
			 * splicing from a socket. As the socket is not in
			 * non-blocking mode, it should never return -EAGAIN.
			 * TODO(pcercuei): Find why it locks...
			 * */
			ret = splice(fd_in, NULL, pipefd[1], NULL, read_len,
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
			if (!ret)
				ret = -EIO;
			if (ret < 0 && errno != EAGAIN) {
				ret = -errno;
				goto err_close_pipe;
			} else if (ret > 0) {
				write_len += ret;
				read_len -= ret;
			}
		}

		if (write_len) {
			ret = splice(pipefd[0], NULL, fd_out, NULL, write_len,
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
			if (!ret)
				ret = -EIO;
			if (ret < 0 && errno != EAGAIN) {
				ret = -errno;
				goto err_close_pipe;
			} else if (ret > 0) {
				write_len -= ret;
			}
		}

	} while (write_len || read_len);

err_close_pipe:
	close(pipefd[0]);
	close(pipefd[1]);
	return ret < 0 ? ret : len;
}

static ssize_t network_get_buffer(const struct iio_device *dev,
		void **addr_ptr, size_t bytes_used,
		uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret, read = 0;
	int memfd;

	if (pdata->is_cyclic)
		return -ENOSYS;

	/* We check early that the temporary file can be created, so that we can
	 * return -ENOSYS in case it fails, which will indicate that the
	 * high-speed interface is not available.
	 *
	 * O_TMPFILE -> Linux 3.11.
	 * TODO: use memfd_create (Linux 3.17) */
	memfd = open(P_tmpdir, O_RDWR | O_TMPFILE | O_EXCL | O_CLOEXEC, S_IRWXU);
	if (memfd < 0)
		return -ENOSYS;

	if (!addr_ptr || words != (dev->nb_channels + 31) / 32) {
		close(memfd);
		return -EINVAL;
	}

	if (pdata->mmap_addr)
		munmap(pdata->mmap_addr, pdata->mmap_len);

	if (pdata->mmap_addr && pdata->is_tx) {
		char buf[1024];

		iio_snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
				dev->id, (unsigned long) bytes_used);

		iio_mutex_lock(pdata->lock);

		ret = write_rwbuf_command(dev, buf);
		if (ret < 0)
			goto err_close_memfd;

		ret = network_do_splice(pdata, bytes_used, false);
		if (ret < 0)
			goto err_close_memfd;

		pdata->wait_for_err_code = true;
		iio_mutex_unlock(pdata->lock);
	}

	if (pdata->memfd >= 0)
		close(pdata->memfd);

	pdata->memfd = memfd;

	ret = (ssize_t) ftruncate(pdata->memfd, pdata->mmap_len);
	if (ret < 0) {
		ret = -errno;
		ERROR("Unable to truncate temp file: %zi\n", -ret);
		return ret;
	}

	if (!pdata->is_tx) {
		char buf[1024];
		size_t len = pdata->mmap_len;

		iio_snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
				dev->id, (unsigned long) len);

		iio_mutex_lock(pdata->lock);
		ret = write_rwbuf_command(dev, buf);
		if (ret < 0)
			goto err_unlock;

		do {
			ret = network_read_mask(&pdata->io_ctx, mask, words);
			if (!ret)
				break;
			if (ret < 0)
				goto err_unlock;

			mask = NULL; /* We read the mask only once */

			ret = network_do_splice(pdata, ret, true);
			if (ret < 0)
				goto err_unlock;

			read += ret;
			len -= ret;
		} while (len);

		iio_mutex_unlock(pdata->lock);
	}

	pdata->mmap_addr = mmap(NULL, pdata->mmap_len,
			PROT_READ | PROT_WRITE, MAP_SHARED, pdata->memfd, 0);
	if (pdata->mmap_addr == MAP_FAILED) {
		pdata->mmap_addr = NULL;
		ret = -errno;
		ERROR("Unable to mmap: %zi\n", -ret);
		return ret;
	}

	*addr_ptr = pdata->mmap_addr;
	return read ? read : (ssize_t) bytes_used;

err_close_memfd:
	close(memfd);
err_unlock:
	iio_mutex_unlock(pdata->lock);
	return ret;
}
#endif

static ssize_t network_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr, dst, len, type);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr, src, len, type);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr, dst, len, false);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr, src, len, false);
}

static int network_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_get_trigger(pdata->iiod_client,
			&pdata->io_ctx, dev, trigger);
}

static int network_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_set_trigger(pdata->iiod_client,
			&pdata->io_ctx, dev, trigger);
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int i;

	iio_mutex_lock(pdata->lock);
	write_command(&pdata->io_ctx, "\r\nEXIT\r\n");
	close(pdata->io_ctx.fd);
	iio_mutex_unlock(pdata->lock);

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		struct iio_device_pdata *dpdata = dev->pdata;

		if (dpdata) {
			network_close(dev);
			iio_mutex_destroy(dpdata->lock);
			free(dpdata);
		}
	}

	iiod_client_destroy(pdata->iiod_client);
	iio_mutex_destroy(pdata->lock);
	freeaddrinfo(pdata->addrinfo);
	free(pdata);
}

static int network_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	return iiod_client_get_version(ctx->pdata->iiod_client,
			&ctx->pdata->io_ctx, major, minor, git_tag);
}

static unsigned int calculate_remote_timeout(unsigned int timeout)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout / 2;
}

static int network_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	int ret, fd = pdata->io_ctx.fd;

	ret = set_socket_timeout(fd, timeout);
	if (!ret) {
		unsigned int remote_timeout = calculate_remote_timeout(timeout);

		ret = iiod_client_set_timeout(pdata->iiod_client,
			&pdata->io_ctx, remote_timeout);
		if (!ret)
			pdata->io_ctx.timeout_ms = timeout;
	}
	if (ret < 0) {
		char buf[1024];
		iio_strerror(-ret, buf, sizeof(buf));
		WARNING("Unable to set R/W timeout: %s\n", buf);
	}
	return ret;
}

static int network_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
			 &pdata->io_ctx, dev, nb_blocks);
}

static struct iio_context * network_clone(const struct iio_context *ctx)
{
	const char *addr = iio_context_get_attr_value(ctx, "ip,ip-addr");

	return iio_create_network_context(addr);
}

static const struct iio_backend_ops network_ops = {
	.clone = network_clone,
	.open = network_open,
	.close = network_close,
	.read = network_read,
	.write = network_write,
#ifdef WITH_NETWORK_GET_BUFFER
	.get_buffer = network_get_buffer,
#endif
	.read_device_attr = network_read_dev_attr,
	.write_device_attr = network_write_dev_attr,
	.read_channel_attr = network_read_chn_attr,
	.write_channel_attr = network_write_chn_attr,
	.get_trigger = network_get_trigger,
	.set_trigger = network_set_trigger,
	.shutdown = network_shutdown,
	.get_version = network_get_version,
	.set_timeout = network_set_timeout,
	.set_kernel_buffers_count = network_set_kernel_buffers_count,

	.cancel = network_cancel,
};

static ssize_t network_write_data(struct iio_context_pdata *pdata,
		void *io_data, const char *src, size_t len)
{
	struct iio_network_io_context *io_ctx = io_data;

	return network_send(io_ctx, src, len, 0);
}

static ssize_t network_read_data(struct iio_context_pdata *pdata,
		void *io_data, char *dst, size_t len)
{
	struct iio_network_io_context *io_ctx = io_data;

	return network_recv(io_ctx, dst, len, 0);
}

static ssize_t network_read_line(struct iio_context_pdata *pdata,
		void *io_data, char *dst, size_t len)
{
	bool found = false;
	size_t i;
#ifdef __linux__
	struct iio_network_io_context *io_ctx = io_data;
	ssize_t ret;
	size_t bytes_read = 0;

	do {
		size_t to_trunc;

		ret = network_recv(io_ctx, dst, len, MSG_PEEK);
		if (ret < 0)
			return ret;

		/* Lookup for the trailing \n */
		for (i = 0; i < (size_t) ret && dst[i] != '\n'; i++);
		found = i < (size_t) ret;

		len -= ret;
		dst += ret;

		if (found)
			to_trunc = i + 1;
		else
			to_trunc = (size_t) ret;

		/* Advance the read offset to the byte following the \n if
		 * found, or after the last charater read otherwise */
		if (pdata->msg_trunc_supported)
			ret = network_recv(io_ctx, NULL, to_trunc, MSG_TRUNC);
		else
			ret = network_recv(io_ctx, dst - ret, to_trunc, 0);
		if (ret < 0)
			return ret;

		bytes_read += to_trunc;
	} while (!found && len);

	if (!found)
		return -EIO;
	else
		return bytes_read;
#else
	for (i = 0; i < len - 1; i++) {
		ssize_t ret = network_read_data(pdata, io_data, dst + i, 1);

		if (ret < 0)
			return ret;

		if (dst[i] != '\n')
			found = true;
		else if (found)
			break;
	}

	if (!found || i == len - 1)
		return -EIO;

	return (ssize_t) i + 1;
#endif
}

static const struct iiod_client_ops network_iiod_client_ops = {
	.write = network_write_data,
	.read = network_read_data,
	.read_line = network_read_line,
};

#ifdef __linux__
/*
 * As of build 16299, Windows Subsystem for Linux presents a Linux API but
 * without support for MSG_TRUNC. Since WSL allows running native Linux
 * applications this is not something that can be detected at compile time. If
 * we want to support WSL we have to have a runtime workaround.
 */
static bool msg_trunc_supported(struct iio_network_io_context *io_ctx)
{
	int ret;

	ret = network_recv(io_ctx, NULL, 0, MSG_TRUNC | MSG_DONTWAIT);

	return ret != -EFAULT && ret != -EINVAL;
}
#else
static bool msg_trunc_supported(struct iio_network_io_context *io_ctx)
{
	return false;
}
#endif

struct iio_context * network_create_context(const char *host)
{
	struct addrinfo hints, *res;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	size_t i, len;
	int fd, ret;
	char *description;
#ifdef _WIN32
	WSADATA wsaData;

	ret = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (ret < 0) {
		ERROR("WSAStartup failed with error %i\n", ret);
		errno = -ret;
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
			char buf[1024];
			iio_strerror(-ret, buf, sizeof(buf));
			DEBUG("Unable to find host: %s\n", buf);
			errno = -ret;
			return NULL;
		}

		avahi_address_snprint(addr_str, sizeof(addr_str), &address);
		iio_snprintf(port_str, sizeof(port_str), "%hu", port);
		ret = getaddrinfo(addr_str, port_str, &hints, &res);
	} else
#endif
	{
		ret = getaddrinfo(host, IIOD_PORT_STR, &hints, &res);
	}

	if (ret) {
		ERROR("Unable to find host: %s\n", gai_strerror(ret));
#ifndef _WIN32
		if (ret != EAI_SYSTEM)
			errno = -ret;
#endif
		return NULL;
	}

	fd = create_socket(res, DEFAULT_TIMEOUT_MS);
	if (fd < 0) {
		errno = -fd;
		goto err_free_addrinfo;
	}

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		errno = ENOMEM;
		goto err_close_socket;
	}

	pdata->io_ctx.fd = fd;
	pdata->addrinfo = res;
	pdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;

	pdata->lock = iio_mutex_create();
	if (!pdata->lock) {
		errno = ENOMEM;
		goto err_free_pdata;
	}

	pdata->iiod_client = iiod_client_new(pdata, pdata->lock,
			&network_iiod_client_ops);

	pdata->msg_trunc_supported = msg_trunc_supported(&pdata->io_ctx);
	if (pdata->msg_trunc_supported)
		DEBUG("MSG_TRUNC is supported\n");
	else
		DEBUG("MSG_TRUNC is NOT supported\n");

	if (!pdata->iiod_client)
		goto err_destroy_mutex;

	DEBUG("Creating context...\n");
	ctx = iiod_client_create_context(pdata->iiod_client, &pdata->io_ctx);
	if (!ctx)
		goto err_destroy_iiod_client;

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

	description = malloc(len);
	if (!description) {
		ret = -ENOMEM;
		goto err_network_shutdown;
	}

	description[0] = '\0';

#ifdef HAVE_IPV6
	if (res->ai_family == AF_INET6) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *) res->ai_addr;
		char *ptr;
		inet_ntop(AF_INET6, &in->sin6_addr,
				description, INET6_ADDRSTRLEN);

		ptr = if_indextoname(in->sin6_scope_id, description +
				strlen(description) + 1);
		if (!ptr) {
			ret = -errno;
			ERROR("Unable to lookup interface of IPv6 address\n");
			goto err_free_description;
		}

		*(ptr - 1) = '%';
	}
#endif
	if (res->ai_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *) res->ai_addr;
#if (!_WIN32 || _WIN32_WINNT >= 0x600)
		inet_ntop(AF_INET, &in->sin_addr, description, INET_ADDRSTRLEN);
#else
		char *tmp = inet_ntoa(in->sin_addr);
		strncpy(description, tmp, len);
#endif
	}

	ret = iio_context_add_attr(ctx, "ip,ip-addr", description);
	if (ret < 0)
		goto err_free_description;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		dev->pdata = zalloc(sizeof(*dev->pdata));
		if (!dev->pdata) {
			ret = -ENOMEM;
			goto err_free_description;
		}

		dev->pdata->io_ctx.fd = -1;
		dev->pdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;
#ifdef WITH_NETWORK_GET_BUFFER
		dev->pdata->memfd = -1;
#endif

		dev->pdata->lock = iio_mutex_create();
		if (!dev->pdata->lock) {
			ret = -ENOMEM;
			goto err_free_description;
		}
	}

	if (ctx->description) {
		size_t desc_len = strlen(description);
		size_t new_size = desc_len + strlen(ctx->description) + 2;
		char *ptr, *new_description = realloc(description, new_size);
		if (!new_description) {
			ret = -ENOMEM;
			goto err_free_description;
		}

		ptr = strrchr(new_description, '\0');
		iio_snprintf(ptr, new_size - desc_len, " %s", ctx->description);
		free(ctx->description);

		ctx->description = new_description;
	} else {
		ctx->description = description;
	}

	iiod_client_set_timeout(pdata->iiod_client, &pdata->io_ctx,
			calculate_remote_timeout(DEFAULT_TIMEOUT_MS));
	return ctx;

err_free_description:
	free(description);
err_network_shutdown:
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_destroy_iiod_client:
	iiod_client_destroy(pdata->iiod_client);
err_destroy_mutex:
	iio_mutex_destroy(pdata->lock);
err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
err_free_addrinfo:
	freeaddrinfo(res);
	return NULL;
}
