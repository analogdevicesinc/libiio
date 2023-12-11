// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "../iio-config.h"
#include "debug.h"
#include "ops.h"
#include "thread-pool.h"

#include <poll.h>
#if WITH_AIO
#include <pthread.h>
#include <sys/eventfd.h>
#endif

#if WITH_AIO
static ssize_t async_io(struct parser_pdata *pdata, void *buf, size_t len,
	bool do_read)
{
	ssize_t ret;
	struct pollfd pfd[2];
	unsigned int num_pfds;
	struct iocb iocb;
	struct iocb *ios[1];
	struct io_event e[1];

	ios[0] = &iocb;

	if (do_read)
		io_prep_pread(&iocb, pdata->fd_in, buf, len, 0);
	else
		io_prep_pwrite(&iocb, pdata->fd_out, buf, len, 0);

	io_set_eventfd(&iocb, pdata->aio_eventfd[do_read]);

	pthread_mutex_lock(&pdata->aio_mutex[do_read]);

	ret = io_submit(pdata->aio_ctx[do_read], 1, ios);
	if (ret != 1) {
		pthread_mutex_unlock(&pdata->aio_mutex[do_read]);
		IIO_ERROR("Failed to submit IO operation: %zd\n", ret);
		return -EIO;
	}

	pfd[0].fd = pdata->aio_eventfd[do_read];
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;
	num_pfds = 2;

	do {
		poll_nointr(pfd, num_pfds);

		if (pfd[0].revents & POLLIN) {
			uint64_t event;
			ret = read(pdata->aio_eventfd[do_read],
						&event, sizeof(event));
			if (ret != sizeof(event)) {
				IIO_ERROR("Failed to read from eventfd: %d\n", -errno);
				ret = -EIO;
				break;
			}

			ret = io_getevents(pdata->aio_ctx[do_read], 0, 1, e, NULL);
			if (ret != 1) {
				IIO_ERROR("Failed to read IO events: %zd\n", ret);
				ret = -EIO;
				break;
			} else {
				ret = (long)e[0].res;
			}
		} else if ((num_pfds > 1 && pfd[1].revents & POLLIN)) {
			/* Got a STOP event to abort this whole session */
			ret = io_cancel(pdata->aio_ctx[do_read], &iocb, e);
			if (ret != -EINPROGRESS && ret != -EINVAL) {
				IIO_ERROR("Failed to cancel IO transfer: %zd\n", ret);
				ret = -EIO;
				break;
			}
			/* It should not be long now until we get the cancellation event */
			num_pfds = 1;
		}
	} while (!(pfd[0].revents & POLLIN));

	pthread_mutex_unlock(&pdata->aio_mutex[do_read]);

	/* Got STOP event, treat it as EOF */
	if (num_pfds == 1)
		return 0;

	return ret;
}

#define MAX_AIO_REQ_SIZE (1024 * 1024)

static ssize_t readfd_aio(struct parser_pdata *pdata, void *dest, size_t len)
{
	if (len > MAX_AIO_REQ_SIZE)
		len = MAX_AIO_REQ_SIZE;
	return async_io(pdata, dest, len, true);
}

static ssize_t writefd_aio(struct parser_pdata *pdata, const void *dest,
		size_t len)
{
	if (len > MAX_AIO_REQ_SIZE)
		len = MAX_AIO_REQ_SIZE;
	return async_io(pdata, (void *)dest, len, false);
}
#endif /* WITH_AIO */

static ssize_t readfd_io(struct parser_pdata *pdata, void *dest, size_t len)
{
	ssize_t ret;
	struct pollfd pfd[2];

	pfd[0].fd = pdata->fd_in;
	pfd[0].events = POLLIN | POLLRDHUP;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	do {
		poll_nointr(pfd, 2);

		/* Got STOP event, or client closed the socket: treat it as EOF */
		if (pfd[1].revents & POLLIN || pfd[0].revents & POLLRDHUP)
			return 0;
		if (pfd[0].revents & POLLERR)
			return -EIO;
		if (!(pfd[0].revents & POLLIN))
			continue;

		do {
			if (pdata->fd_in_is_socket)
				ret = recv(pdata->fd_in, dest, len, MSG_NOSIGNAL);
			else
				ret = read(pdata->fd_in, dest, len);
		} while (ret == -1 && errno == EINTR);

		if (ret != -1 || errno != EAGAIN)
			break;
	} while (true);

	if (ret == -1)
		return -errno;

	return ret;
}

static ssize_t writefd_io(struct parser_pdata *pdata, const void *src, size_t len)
{
	ssize_t ret;
	struct pollfd pfd[2];

	pfd[0].fd = pdata->fd_out;
	pfd[0].events = POLLOUT;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	do {
		poll_nointr(pfd, 2);

		/* Got STOP event, or client closed the socket: treat it as EOF */
		if (pfd[1].revents & POLLIN || pfd[0].revents & POLLHUP)
			return 0;
		if (pfd[0].revents & POLLERR)
			return -EIO;
		if (!(pfd[0].revents & POLLOUT))
			continue;

		do {
			if (pdata->fd_out_is_socket)
				ret = send(pdata->fd_out, src, len, MSG_NOSIGNAL);
			else
				ret = write(pdata->fd_out, src, len);
		} while (ret == -1 && errno == EINTR);

		if (ret != -1 || errno != EAGAIN)
			break;
	} while (true);

	if (ret == -1)
		return -errno;

	return ret;
}

ssize_t write_all(struct parser_pdata *pdata, const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;

	while (len) {
		ssize_t ret = pdata->writefd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) src;
}

ssize_t read_all(struct parser_pdata *pdata, void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;

	while (len) {
		ssize_t ret = pdata->readfd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) dst;
}

void interpreter(struct iio_context *ctx, int fd_in, int fd_out,
		 bool is_socket, bool is_usb, bool use_aio,
		 struct thread_pool *pool, const void *xml_zstd,
		 size_t xml_zstd_len)
{
	struct parser_pdata pdata = { 0 };
	unsigned int i;
	int ret;

	pdata.ctx = ctx;
	pdata.fd_in = fd_in;
	pdata.fd_out = fd_out;
	pdata.pool = pool;
	pdata.binary = !WITH_IIOD_V0_COMPAT;

	pdata.xml_zstd = xml_zstd;
	pdata.xml_zstd_len = xml_zstd_len;

	pdata.fd_in_is_socket = is_socket;
	pdata.fd_out_is_socket = is_socket;
	pdata.is_usb = is_usb;

	SLIST_INIT(&pdata.thdlist_head);

	if (use_aio) {
		/* Note: if WITH_AIO is not defined, use_aio is always false.
		 * We ensure that in iiod.c. */
#if WITH_AIO
		char err_str[1024];

		for (i = 0; i < 2; i++) {
			pdata.aio_eventfd[i] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
			if (pdata.aio_eventfd[i] < 0) {
				iio_strerror(errno, err_str, sizeof(err_str));
				IIO_ERROR("Failed to create AIO eventfd: %s\n", err_str);
				goto err_free_aio;
			}

			pdata.aio_ctx[i] = 0;
			ret = io_setup(1, &pdata.aio_ctx[i]);
			if (ret < 0) {
				iio_strerror(-ret, err_str, sizeof(err_str));
				IIO_ERROR("Failed to create AIO context: %s\n", err_str);
				close(pdata.aio_eventfd[i]);
				goto err_free_aio;
			}

			pthread_mutex_init(&pdata.aio_mutex[i], NULL);
		}

		pdata.readfd = readfd_aio;
		pdata.writefd = writefd_aio;
#endif
	} else {
		pdata.readfd = readfd_io;
		pdata.writefd = writefd_io;
	}

	if (WITH_IIOD_V0_COMPAT)
		ascii_interpreter(&pdata);

	if (pdata.binary)
		binary_parse(&pdata);

#if WITH_AIO
	i = use_aio ? 2 : 0;

err_free_aio:
	for (; i > 0; i--) {
		io_destroy(pdata.aio_ctx[i - 1]);
		close(pdata.aio_eventfd[i - 1]);
		pthread_mutex_destroy(&pdata.aio_mutex[i - 1]);
	}
#endif
}
