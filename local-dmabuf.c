// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "local.h"

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define IIO_DMABUF_ALLOC_IOCTL		_IOW('i', 0x92, struct iio_dmabuf_req)
#define IIO_DMABUF_ENQUEUE_IOCTL	_IOW('i', 0x93, struct iio_dmabuf)
#define IIO_DMABUF_SYNC_IOCTL		_IOW('b', 0, struct dma_buf_sync)

#define IIO_DMABUF_FLAG_CYCLIC		(1 << 0)

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)

struct iio_dmabuf_req {
	uint64_t size;
	uint64_t resv;
};

struct iio_dmabuf {
	int32_t fd;
	uint32_t flags;
	uint64_t bytes_used;
};

struct dma_buf_sync {
	uint64_t flags;
};

struct iio_block_pdata *
local_create_dmabuf(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_block_pdata *priv;
	struct iio_dmabuf_req req;
	int ret, fd;

	priv = zalloc(sizeof(*priv));
	if (!priv)
		return iio_ptr(-ENOMEM);

	req.size = size;
	req.resv = 0;

	ret = ioctl_nointr(pdata->fd, IIO_DMABUF_ALLOC_IOCTL, &req);

	/* If we get -ENODEV or -EINVAL errors here, the ioctl is wrong and the
	 * high-speed DMABUF interface is not supported. */
	if (ret == -ENODEV || ret == -EINVAL || ret == -ENOTTY)
		ret = -ENOSYS;
	if (ret < 0)
		goto err_free_priv;

	fd = ret;

	*data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*data == MAP_FAILED) {
		ret = -errno;
		goto err_close_fd;
	}

	priv->pdata = (void *)(intptr_t) fd;
	priv->data = *data;
	priv->size = size;
	priv->buf = pdata;
	priv->dequeued = true;
	pdata->dmabuf_supported = true;

	return priv;

err_close_fd:
	close(fd);
err_free_priv:
	free(priv);
	return iio_ptr(ret);
}

void local_free_dmabuf(struct iio_block_pdata *pdata)
{
	int fd = (int)(intptr_t) pdata->pdata;

	munmap(pdata->data, pdata->size);
	close(fd);
	free(pdata);
}

int local_enqueue_dmabuf(struct iio_block_pdata *pdata,
			 size_t bytes_used, bool cyclic)
{
	struct dma_buf_sync dbuf_sync;
	struct iio_dmabuf dmabuf;
	int ret, fd = (int)(intptr_t) pdata->pdata;

	if (!pdata->dequeued)
		return -EPERM;

	dmabuf.fd = fd;
	dmabuf.flags = 0;
	dmabuf.bytes_used = bytes_used;

	if (cyclic)
	      dmabuf.flags |= IIO_DMABUF_FLAG_CYCLIC;

	dbuf_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

	/* Disable CPU access to last block */
	ret = ioctl_nointr(fd, IIO_DMABUF_SYNC_IOCTL, &dbuf_sync);
	if (ret)
		return ret;

	ret = ioctl_nointr(pdata->buf->fd, IIO_DMABUF_ENQUEUE_IOCTL, &dmabuf);
	if (ret)
		return ret;

	pdata->dequeued = false;

	return 0;
}

int local_dequeue_dmabuf(struct iio_block_pdata *pdata, bool nonblock)
{
	struct iio_buffer_pdata *buf_pdata = pdata->buf;
	struct dma_buf_sync dbuf_sync;
	struct timespec start, *time_ptr = NULL;
	int ret, fd = (int)(intptr_t) pdata->pdata;

	if (pdata->dequeued)
		return -EPERM;

	if (!nonblock) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		time_ptr = &start;
	}

	ret = buffer_check_ready(buf_pdata, fd, POLLOUT, time_ptr);
	if (ret < 0)
		return ret;

	dbuf_sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

	/* Enable CPU access to new block */
	ret = ioctl_nointr(fd, IIO_DMABUF_SYNC_IOCTL, &dbuf_sync);
	if (ret < 0)
		return ret;

	pdata->dequeued = true;

	return 0;
}
