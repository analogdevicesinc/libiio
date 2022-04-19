// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil
 */

#include "debug.h"
#include "iio-config.h"
#include "iio-private.h"
#include "network.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int set_blocking_mode(int fd, bool blocking)
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

#if WITH_NETWORK_EVENTFD
#include <sys/eventfd.h>

static int create_cancel_fd(struct iiod_client_pdata *io_ctx)
{
	io_ctx->cancel_fd[0] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (io_ctx->cancel_fd[0] < 0)
		return -errno;
	return 0;
}

#else /* WITH_NETWORK_EVENTFD */

static int create_cancel_fd(struct iiod_client_pdata *io_ctx)
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
#endif /* WITH_NETWORK_EVENTFD */

void cleanup_cancel(struct iiod_client_pdata *io_ctx)
{
	close(io_ctx->cancel_fd[0]);
	if (!WITH_NETWORK_EVENTFD)
		close(io_ctx->cancel_fd[1]);
}

int setup_cancel(struct iiod_client_pdata *io_ctx)
{
	return create_cancel_fd(io_ctx);
}

#define CANCEL_WR_FD (!WITH_NETWORK_EVENTFD)

void do_cancel(struct iiod_client_pdata *io_ctx)
{
	uint64_t event = 1;
	int ret;

	ret = write(io_ctx->cancel_fd[CANCEL_WR_FD], &event, sizeof(event));
	if (ret == -1) {
		/* If this happens something went very seriously wrong */
		char err_str[1024];
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Unable to signal cancellation event: %s\n", err_str);
	}
}

int wait_cancellable(struct iiod_client_pdata *io_ctx, bool read)
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

int network_get_error(void)
{
	return -errno;
}

bool network_should_retry(int err)
{
	return err == -EAGAIN;
}

bool network_is_interrupted(int err)
{
	return err == -EINTR;
}

bool network_connect_in_progress(int err)
{
	return err == -EINPROGRESS;
}


/* Use it if available */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

int do_create_socket(const struct addrinfo *addrinfo)
{
	int fd;

	fd = socket(addrinfo->ai_family, addrinfo->ai_socktype | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	return fd;
}

int set_socket_timeout(int fd, unsigned int timeout)
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
