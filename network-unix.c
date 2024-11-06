// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil
 */

#include "iio-config.h"
#include "network.h"

#include <errno.h>
#include <fcntl.h>
#include <iio/iio-debug.h>
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
		prm_perror(io_ctx->params, -errno,
			   "Unable to signal cancellation event");
	}
}

int wait_cancellable(struct iiod_client_pdata *io_ctx,
		     bool read, unsigned int timeout_ms)
{
	int timeout = timeout_ms > 0 ? (int) timeout_ms : -1;
	struct pollfd pfd[2];
	int ret;

	memset(pfd, 0, sizeof(pfd));

	pfd[0].fd = io_ctx->fd;
	if (read)
		pfd[0].events = POLLIN;
	else
		pfd[0].events = POLLOUT;
	pfd[1].fd = io_ctx->cancel_fd[0];
	pfd[1].events = POLLIN;

	do {
		do {
			ret = poll(pfd, 2, timeout);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1)
			return -errno;
		if (!ret)
			return -ETIMEDOUT;

		if (pfd[1].revents & POLLIN)
			return -EBADF;
	} while (!(pfd[0].revents & (pfd[0].events | POLLERR | POLLHUP)));

	/* If we get POLLHUP when writing, return -EPIPE, otherwise send() will
	 * get a SIGPIPE. When reading, recv() will return 0 once all bytes have
	 * been read from the input stream and won't send a SIGPIPE. */
	if (!read && (pfd[0].revents & POLLHUP))
		return -EPIPE;

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

int do_select(int fd, unsigned int timeout)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = fd;
	pfd.events = POLLOUT | POLLERR;
	pfd.revents = 0;

	do {
		ret = poll(&pfd, 1, timeout);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0)
		return -errno;

	if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}
