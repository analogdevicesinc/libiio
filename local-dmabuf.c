// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

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

#include <linux/memfd.h>

struct iio_udmabuf_create {
	uint32_t memfd;
	uint32_t flags;
	uint64_t _;
	uint64_t size;
};

#define IIO_UDMABUF_CREATE		_IOW('u', 0x42, struct iio_udmabuf_create)

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

static void * local_mmap(void *addr, size_t length, int prot, int flags,
			 int fd, off_t offset, bool hugetlb)
{
	void *map = MAP_FAILED;

	if (hugetlb) {
		map = mmap(addr, length, prot,
			   flags | MAP_HUGETLB | (21 << MAP_HUGE_SHIFT),
			   fd, offset);
	}

	if (map == MAP_FAILED)
		map = mmap(addr, length, prot, flags, fd, offset);

	return map;
}

struct iio_block_pdata *
local_create_dmabuf(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_udmabuf_create req;
	struct iio_block_pdata *priv;
	size_t page_size, mem_size;
	int ret, fd, devfd, memfd;
	int memfd_flags = MFD_ALLOW_SEALING;
	bool hugetlb = size >= 0x200000;

	if (hugetlb)
		memfd_flags |= MFD_HUGETLB | MFD_HUGE_2MB;

	priv = zalloc(sizeof(*priv));
	if (!priv)
		return iio_ptr(-ENOMEM);

	page_size = sysconf(_SC_PAGESIZE);
	mem_size = (size + page_size - 1) / page_size * page_size;

	devfd = open("/dev/udmabuf", O_RDWR | O_NOFOLLOW);
	if (devfd < 0) {
		ret = -errno;

		/* If we're running on an old kernel, return -ENOSYS to mark
		 * the DMABUF interface as unavailable */
		if (ret == -ENOENT)
			ret = -ENOSYS;

		goto err_free_priv;
	}

	memfd = syscall(__NR_memfd_create, "/libiio-udmabuf", memfd_flags);
	if (memfd < 0 && hugetlb) {
		/* Try again, without requesting huge pages this time. */
		memfd = syscall(__NR_memfd_create, "/libiio-udmabuf",
				MFD_ALLOW_SEALING);
		hugetlb = false;
	}
	if (memfd < 0) {
		ret = -errno;
		goto err_close_devfd;
	}

	if (fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
		ret = -errno;
		goto err_close_devfd;
	}

	if (ftruncate(memfd, mem_size) < 0) {
		ret = -errno;
		goto err_close_memfd;
	}

	memset(&req, 0, sizeof(req));

	req.memfd = memfd;
	req.size = mem_size;

	fd = ioctl(devfd, IIO_UDMABUF_CREATE, &req);
	if (fd < 0) {
		ret = -errno;
		goto err_close_memfd;
	}

	*data = local_mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			   fd, 0, hugetlb);
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

	/* The new block is dequeued by default, so enable CPU access */
	ret = enable_cpu_access(priv, true);
	if (ret)
		goto err_data_unmap;

	/* Attach DMABUF to the buffer */
	ret = ioctl(pdata->fd, IIO_DMABUF_ATTACH_IOCTL, &fd);
	if (ret) {
		ret = -errno;

		if (ret == -ENOTTY) {
			/* If the ioctl is not available, return -ENOSYS to mark
			 * the DMABUF interface as unavailable */
			ret = -ENOSYS;
		}

		goto err_data_unmap;
	}

	close(memfd);
	close(devfd);

	return priv;

err_data_unmap:
	munmap(priv->data, priv->size);
err_close_fd:
	close(fd);
err_close_memfd:
	close(memfd);
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

int local_dmabuf_get_fd(struct iio_block_pdata *pdata)
{
	return (int)(intptr_t) pdata->pdata;
}

int local_enqueue_dmabuf(struct iio_block_pdata *pdata,
			 size_t bytes_used, bool cyclic)
{
	struct iio_dmabuf dmabuf;
	int ret, fd = (int)(intptr_t) pdata->pdata;

	if (!pdata->dequeued)
		return -EPERM;

	if (bytes_used > pdata->size || bytes_used == 0)
		return -EINVAL;

	dmabuf.fd = fd;
	dmabuf.flags = 0;
	dmabuf.bytes_used = bytes_used;

	if (cyclic)
		dmabuf.flags |= IIO_DMABUF_FLAG_CYCLIC;

	if (!pdata->cpu_access_disabled) {
		/* Disable CPU access to last block */
		ret = enable_cpu_access(pdata, false);
		if (ret)
			return ret;
	}

	ret = ioctl_nointr(pdata->buf->fd, IIO_DMABUF_ENQUEUE_IOCTL, &dmabuf);
	if (ret) {
		dev_perror(pdata->buf->dev, ret, "Unable to enqueue DMABUF");
		return ret;
	}

	pdata->dequeued = false;

	return 0;
}

int local_dequeue_dmabuf(struct iio_block_pdata *pdata, bool nonblock)
{
	struct iio_buffer_pdata *buf_pdata = pdata->buf;
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

	if (!pdata->cpu_access_disabled) {
		/* Enable CPU access to new block */
		ret = enable_cpu_access(pdata, true);
		if (ret < 0)
			return ret;
	}

	pdata->dequeued = true;

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
