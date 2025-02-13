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
#include <sys/socket.h>

static int log = 0;

void async_enable_log(void)
{
	//printf("Enabling log\n");
	if (!log)
		log = 1;
}

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

	if (log && len > 512)
		printf("[%lu]: Try to write a block (before lock = %zd)\n",
		       pthread_self(), len);

	pthread_mutex_lock(&pdata->aio_mutex[do_read]);

	if (log && len > 512)
		printf("[%lu]: Try to write a block (after lock)\n", pthread_self());

	ret = io_submit(pdata->aio_ctx[do_read], 1, ios);
	if (ret != 1) {
		pthread_mutex_unlock(&pdata->aio_mutex[do_read]);
		IIO_ERROR("Failed to submit IO operation: %zd\n", ret);
		return -EIO;
	}

	if (log && len > 512)
		printf("[%lu]: Try to write a block (after io_submit)\n", pthread_self());

	pfd[0].fd = pdata->aio_eventfd[do_read];
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;
	num_pfds = 2;

	do {
		if (log && len > 512)
			printf("[%lu]: Try to write a block (before poll)\n", pthread_self());
		poll_nointr(pfd, num_pfds);
		if (log && len > 512)
			printf("[%lu]: Try to write a block (after poll)\n", pthread_self());
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
			printf("ASYNC IO Got a STOP event\n");

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

void interpreter(struct iio_context *ctx, int fd_in, int fd_out,
		 bool is_socket, bool is_usb,
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

#if WITH_AIO
	for (i = 0; i < 2; i++) {
		pdata.aio_eventfd[i] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (pdata.aio_eventfd[i] < 0) {
			IIO_PERROR(errno, "Failed to create AIO eventfd");
			goto err_free_aio;
		}

		pdata.aio_ctx[i] = 0;
		ret = io_setup(16, &pdata.aio_ctx[i]);
		if (ret < 0) {
			IIO_PERROR(ret, "Failed to create AIO context");
			close(pdata.aio_eventfd[i]);
			goto err_free_aio;
		}

		pthread_mutex_init(&pdata.aio_mutex[i], NULL);
	}

	pdata.readfd = readfd_aio;
	pdata.writefd = writefd_aio;
#else
	pdata.readfd = readfd_io;
	pdata.writefd = writefd_io;
#endif

	if (WITH_IIOD_V0_COMPAT)
		ascii_interpreter(&pdata);

	if (pdata.binary)
		binary_parse(&pdata);

#if WITH_AIO
err_free_aio:
	for (i = 2; i > 0; i--) {
		io_destroy(pdata.aio_ctx[i - 1]);
		close(pdata.aio_eventfd[i - 1]);
		pthread_mutex_destroy(&pdata.aio_mutex[i - 1]);
	}
#endif
}
