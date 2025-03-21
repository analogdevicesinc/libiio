// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"
#include <stdio.h>
#define _GNU_SOURCE
#include "local.h"

#include <errno.h>
#include <fcntl.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct iio_dmabuf_heap_data {
	uint64_t len;
	uint32_t fd;
	uint32_t fd_flags;
	uint64_t heap_flags;
};

#define IIO_DMA_HEAP_ALLOC		_IOWR('H', 0x0, struct iio_dmabuf_heap_data)

#define IIO_DMABUF_ATTACH_IOCTL		_IOW('i', 0x92, int)
#define IIO_DMABUF_DETACH_IOCTL		_IOW('i', 0x93, int)
#define IIO_DMABUF_ENQUEUE_IOCTL	_IOW('i', 0x94, struct iio_dmabuf)
#define IIO_DMABUF_SYNC_IOCTL		_IOW('b', 0, struct dma_buf_sync)

#define IIO_DMABUF_FLAG_CYCLIC		(1 << 0)

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)

struct iio_dmabuf {
	int32_t fd;
	uint32_t flags;
	uint64_t bytes_used;
};

struct dma_buf_sync {
	uint64_t flags;
};

static int enable_cpu_access(struct iio_block_pdata *pdata, bool enable)
{
	struct dma_buf_sync dbuf_sync = { 0 };
	int fd = (int)(intptr_t) pdata->pdata;

	dbuf_sync.flags = DMA_BUF_SYNC_RW;

	if (enable)
		dbuf_sync.flags |= DMA_BUF_SYNC_START;
	else
		dbuf_sync.flags |= DMA_BUF_SYNC_END;

	return ioctl_nointr(fd, IIO_DMABUF_SYNC_IOCTL, &dbuf_sync);
}

struct iio_block_pdata *
local_create_dmabuf(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_dmabuf_heap_data req = {
		.len = size,
		.fd_flags = O_CLOEXEC | O_RDWR,
	};
	struct iio_block_pdata *priv;
	int ret, fd, devfd = -1;

	printf("local create dma block\n");
	priv = zalloc(sizeof(*priv));
	if (!priv)
		return iio_ptr(-ENOMEM);

	switch (pdata->params->dma_allocator) {
	case IIO_DMA_ALLOCATOR_SYSTEM:
		devfd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC | O_NOFOLLOW); /* Flawfinder: ignore */
		break;
	case IIO_DMA_ALLOCATOR_CMA_LINUX:
		devfd = open("/dev/dma_heap/linux,cma", O_RDONLY | O_CLOEXEC | O_NOFOLLOW); /* Flawfinder: ignore */
		break;
	}
	if (devfd < 0) {
		ret = -errno;
		printf("Could not open /dev/dma_heap/system: %d\n", ret);
		/* If we're running on an old kernel, return -ENOSYS to mark
		 * the DMABUF interface as unavailable */
		if (ret == -ENOENT)
			ret = -ENOSYS;

		goto err_free_priv;
	}

	ret = ioctl(devfd, IIO_DMA_HEAP_ALLOC, &req);
	if (ret < 0) {
		ret = -errno;
		printf("Could not do IIO_DMA_HEAP_ALLOC: %d\n", ret);
		goto err_close_devfd;
	}

	fd = req.fd;

	*data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*data == MAP_FAILED) {
		ret = -errno;
		printf("Could not do mmap: %d\n", ret);
		goto err_close_fd;
	}

	priv->pdata = (void *)(intptr_t) fd;
	priv->data = *data;
	priv->size = size;
	priv->buf = pdata;
	priv->dequeued = true;

	/* The new block is dequeued by default, so enable CPU access */
	ret = enable_cpu_access(priv, true);
	if (ret)
		goto err_data_unmap;

	/* Attach DMABUF to the buffer */
	ret = ioctl(pdata->fd, IIO_DMABUF_ATTACH_IOCTL, &fd);
	if (ret) {
		ret = -errno;

		if (ret == -ENODEV) {
			/* If the ioctl is not available, return -ENOSYS to mark
			 * the DMABUF interface as unavailable */
			ret = -ENOSYS;
		}
		printf("Could not do IIO_DMABUF_ATTACH_IOCTL: %d\n", ret);
		goto err_data_unmap;
	}

	pdata->dmabuf_supported = true;

	close(devfd);

	return priv;

err_data_unmap:
	munmap(priv->data, priv->size);
err_close_fd:
	close(fd);
err_close_devfd:
	close(devfd);
err_free_priv:
	free(priv);
	return iio_ptr(ret);
}

void local_free_dmabuf(struct iio_block_pdata *pdata)
{
	int ret, fd = (int)(intptr_t) pdata->pdata;

	ret = ioctl(pdata->buf->fd, IIO_DMABUF_DETACH_IOCTL, &fd);
	if (ret)
		dev_perror(pdata->buf->dev, ret, "Unable to detach DMABUF");

	munmap(pdata->data, pdata->size);
	close(fd);
	free(pdata);
}

int local_share_dmabuf(struct iio_buffer_pdata *pdata, struct iio_block_pdata *block)
{
	int ret, fd = local_dmabuf_get_fd(block);

	/* make sure nothing is happening on this block */
	if (!block->dequeued)
		return -EPERM;

	printf("local_share_dmabuf (cpu=%d)\n", block->cpu_access_disabled);
	/* Attach DMABUF to the buffer */
	ret = ioctl(pdata->fd, IIO_DMABUF_ATTACH_IOCTL, &fd);
	if (ret)
		return -errno;

	pdata->dmabuf_supported = true;

	return 0;
}

void local_unshare_dmabuf(struct iio_buffer_pdata *pdata, struct iio_block_pdata *block)
{
	int ret, fd = local_dmabuf_get_fd(block);

	/* make sure nothing is happening on this block */
	if (!block->dequeued) {
		dev_err(pdata->dev, "Block is still in use, cannot unshare DMABUF\n");
		return;
	}

	printf("local_unshare_dmabuf (cpu=%d)\n", block->cpu_access_disabled);
	/* Attach DMABUF to the buffer */
	ret = ioctl(pdata->fd, IIO_DMABUF_DETACH_IOCTL, &fd);
	if (ret)
		dev_perror(pdata->dev, ret, "Unable to unshare DMABUF");
}

int local_dmabuf_get_fd(struct iio_block_pdata *pdata)
{
	return (int)(intptr_t) pdata->pdata;
}

int local_enqueue_dmabuf_to_buf(struct iio_buffer_pdata *pdata, struct iio_block_pdata *block,
				size_t bytes_used, bool cyclic)
{
	struct iio_dmabuf dmabuf;
	int ret, fd = local_dmabuf_get_fd(block);

	if (!block->dequeued)
		return -EPERM;

	if (bytes_used > block->size || bytes_used == 0) {
		dev_err(pdata->dev, "Invalid bytes_used (%zu) (sz=%d)\n", bytes_used,
			(int) pdata->size);
		return -EINVAL;
	}

	dmabuf.fd = fd;
	dmabuf.flags = 0;
	dmabuf.bytes_used = bytes_used;

	if (cyclic)
		dmabuf.flags |= IIO_DMABUF_FLAG_CYCLIC;

	if (!block->cpu_access_disabled) {
		/* Disable CPU access to last block */
		ret = enable_cpu_access(block, false);
		if (ret)
			return ret;
	}

	ret = ioctl_nointr(pdata->fd, IIO_DMABUF_ENQUEUE_IOCTL, &dmabuf);
	if (ret) {
		dev_perror(pdata->dev, ret, "Unable to enqueue DMABUF");
		return ret;
	}

	block->dequeued = false;

	return 0;
}

int local_dequeue_dmabuf_from_buf(struct iio_buffer_pdata *pdata, struct iio_block_pdata *block,
				  bool nonblock)
{
	struct timespec start, *time_ptr = NULL;
	int ret, fd = local_dmabuf_get_fd(block);

	if (block->dequeued)
		return -EPERM;

	if (!nonblock) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		time_ptr = &start;
	}

	ret = buffer_check_ready(pdata, fd, POLLOUT, time_ptr);
	if (ret < 0)
		return ret;

	if (!block->cpu_access_disabled) {
		/* Enable CPU access to new block */
		ret = enable_cpu_access(block, true);
		if (ret < 0)
			return ret;
	}

	block->dequeued = true;

	return 0;
}

int local_dmabuf_disable_cpu_access(struct iio_block_pdata *pdata, bool disable)
{
	int ret;

	if (pdata->dequeued) {
		ret = enable_cpu_access(pdata, !disable);
		if (ret)
			return ret;
	}

	pdata->cpu_access_disabled = disable;

	return 0;
}
