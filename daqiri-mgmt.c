// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Plain TCP transport to iiod for the DAQIRI backend: the same job network.c
 * does for ip:, with poll() timeouts and an eventfd to cancel blocked I/O.
 */

#include <errno.h>
#include <iio/iio-debug.h>
#include <iio/iiod-client.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "daqiri-private.h"

static int64_t dq_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* <timeout_ms> < 0 waits forever, 0 does not wait. */
static int64_t dq_deadline(int timeout_ms)
{
	return timeout_ms < 0 ? -1 : dq_now_ms() + timeout_ms;
}

static int dq_remaining(int64_t deadline)
{
	int64_t left;

	if (deadline < 0)
		return -1;

	left = deadline - dq_now_ms();
	if (left <= 0)
		return 0;

	return left > INT32_MAX ? INT32_MAX : (int)left;
}

/*
 * Wait until <fd> is ready for <events>, the deadline passes (-ETIMEDOUT) or
 * the connection is cancelled (-EBADF). The eventfd is never drained, so once
 * cancelled every waiter, now or later, sees it.
 */
static int dq_wait(int fd, int cancel_fd, short events, int64_t deadline)
{
	struct pollfd pfd[2] = {
		{ .fd = fd, .events = events },
		{ .fd = cancel_fd, .events = POLLIN },
	};
	int ret;

	for (;;) {
		ret = poll(pfd, cancel_fd >= 0 ? 2 : 1, dq_remaining(deadline));
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!ret)
			return -ETIMEDOUT;

		if (pfd[1].revents & POLLIN)
			return -EBADF;
		if (pfd[0].revents & POLLNVAL)
			return -EBADF;

		/* A hung-up socket cannot be written; let a read see the EOF. */
		if (events == POLLOUT && (pfd[0].revents & (POLLHUP | POLLERR)))
			return -EPIPE;
		if (pfd[0].revents & (events | POLLHUP | POLLERR))
			return 0;
	}
}

static ssize_t dq_mgmt_io(struct dq_mgmt *m, void *data, size_t len, int timeout_ms, bool wr)
{
	int64_t deadline = dq_deadline(timeout_ms);
	ssize_t ret;
	int err;

	if (!len)
		return 0;

	for (;;) {
		err = dq_wait(m->fd, m->cancel_fd, wr ? POLLOUT : POLLIN, deadline);
		if (err)
			return err;

		if (wr)
			ret = send(m->fd, data, len, MSG_NOSIGNAL);
		else
			ret = recv(m->fd, data, len, 0);
		if (ret > 0)
			return ret;
		if (!ret)
			return -EPIPE;
		if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
			return -errno;
	}
}

static ssize_t dq_mgmt_read(struct iiod_client_pdata *desc, char *dst, size_t len, int timeout_ms)
{
	return dq_mgmt_io((struct dq_mgmt *)desc, dst, len, timeout_ms, false);
}

static ssize_t dq_mgmt_write(
		struct iiod_client_pdata *desc, const char *src, size_t len, int timeout_ms)
{
	return dq_mgmt_io((struct dq_mgmt *)desc, (void *)src, len, timeout_ms, true);
}

/*
 * Latch the eventfd: every wait on the connection, now or later, returns
 * -EBADF. The socket is left alone, as ip: leaves it (network-unix.c): a
 * shutdown() would make the server tear down the connection's buffers by
 * itself while the close of a buffer still frees its block and closes it on
 * the management connection. dq_mgmt_close() closes the socket afterwards.
 */
static void dq_mgmt_cancel(struct iiod_client_pdata *desc)
{
	struct dq_mgmt *m = (struct dq_mgmt *)desc;
	uint64_t one = 1;

	if (m->cancel_fd >= 0 && write(m->cancel_fd, &one, sizeof(one)) < 0)
		prm_perror(m->params, -errno, "Unable to signal cancellation");
}

const struct iiod_client_ops dq_mgmt_ops = {
	.write = dq_mgmt_write,
	.read = dq_mgmt_read,
	.read_line = NULL,
	.cancel = dq_mgmt_cancel,
};

static int dq_connect_one(const struct addrinfo *ai, int64_t deadline)
{
	socklen_t len = sizeof(int);
	int fd, ret, error, yes = 1;

	fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, ai->ai_protocol);
	if (fd < 0)
		return -errno;

	if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
		if (errno != EINPROGRESS) {
			ret = -errno;
			goto err_close;
		}

		ret = dq_wait(fd, -1, POLLOUT, deadline);
		/* POLLOUT|POLLERR both mean "done"; SO_ERROR says how it went */
		if (ret && ret != -EPIPE)
			goto err_close;

		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			ret = -errno;
			goto err_close;
		}
		if (error) {
			ret = -error;
			goto err_close;
		}
	}

	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
		ret = -errno;
		goto err_close;
	}

	return fd;

err_close:
	close(fd);
	return ret;
}

int dq_mgmt_connect(struct dq_mgmt *m, const char *host, const char *port, int timeout_ms)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	int64_t deadline = dq_deadline(timeout_ms);
	struct addrinfo *res, *ai;
	int ret, fd = -ENOENT;

	m->fd = -1;
	m->cancel_fd = -1;

	ret = getaddrinfo(host, port, &hints, &res);
	if (ret) {
		prm_err(m->params, "Unable to find host %s: %s\n", host, gai_strerror(ret));
		return ret == EAI_SYSTEM ? -errno : -ENOENT;
	}

	/* "localhost" may resolve to ::1 first while iiod only listens on IPv4 */
	for (ai = res; ai; ai = ai->ai_next) {
		fd = dq_connect_one(ai, deadline);
		if (fd >= 0 || fd == -ETIMEDOUT)
			break;
	}
	freeaddrinfo(res);

	if (fd < 0) {
		prm_perror(m->params, fd, "Unable to connect to %s:%s", host, port);
		return fd;
	}

	m->cancel_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (m->cancel_fd < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	m->fd = fd;

	return 0;
}

void dq_mgmt_close(struct dq_mgmt *m)
{
	if (m->fd >= 0)
		close(m->fd);
	if (m->cancel_fd >= 0)
		close(m->cancel_fd);

	m->fd = -1;
	m->cancel_fd = -1;
}
