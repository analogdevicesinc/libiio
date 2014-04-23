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

#include "debug.h"
#include "iio-private.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#define NB_BLOCKS 4

#define BLOCK_ALLOC_IOCTL   _IOWR('i', 0xa0, struct block_alloc_req)
#define BLOCK_FREE_IOCTL      _IO('i', 0xa1)
#define BLOCK_QUERY_IOCTL   _IOWR('i', 0xa2, struct block)
#define BLOCK_ENQUEUE_IOCTL _IOWR('i', 0xa3, struct block)
#define BLOCK_DEQUEUE_IOCTL _IOWR('i', 0xa4, struct block)

struct block_alloc_req {
	uint32_t type,
		 size,
		 count,
		 id;
};

struct block {
	uint32_t id,
		 size,
		 bytes_used,
		 type,
		 flags,
		 offset;
	uint64_t timestamp;
};

struct iio_device_pdata {
	FILE *f;
	unsigned int samples_count;
	/* ^^^ must be the same as in local.c */

	struct block blocks[NB_BLOCKS];
	void *addrs[NB_BLOCKS];
	int last_dequeued;
};


static const struct iio_backend_ops *local_ops;

static int local_mmap_open(const struct iio_device *dev,
		size_t samples_count, uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	struct block_alloc_req req;
	unsigned int i;
	int fd, ret;

	if (!samples_count) {
		ERROR("DDS mode is not supported on this backend\n");
		return -ENOSYS;
	}

	ret = local_ops->open(dev, samples_count, mask, words);
	if (ret < 0)
		return ret;

	/* It is not possible to allocate the memory buffers if the buffer
	 * mode is enabled, so we disable it for now. */
	local_ops->write_device_attr(dev, "buffer/enable", "0", false);

	req.type = 0;
	req.size = samples_count *
		iio_device_get_sample_size_mask(dev, mask, words);
	req.count = NB_BLOCKS;

	fd = fileno(pdata->f);
	ret = ioctl(fd, BLOCK_ALLOC_IOCTL, &req);
	if (ret < 0) {
		ret = -errno;
		ERROR("Failed to allocate memory buffers: %s\n",
				strerror(errno));
		goto err_close;
	}

	local_ops->write_device_attr(dev, "buffer/enable", "1", false);

	/* mmap all the blocks */
	for (i = 0; i < NB_BLOCKS; i++) {
		pdata->blocks[i].id = i;
		ret = ioctl(fd, BLOCK_QUERY_IOCTL, &pdata->blocks[i]);
		if (ret) {
			ret = -errno;
			ERROR("Failed to query block: %s\n", strerror(errno));
			goto err_munmap;
		}

		ret = ioctl(fd, BLOCK_ENQUEUE_IOCTL, &pdata->blocks[i]);
		if (ret) {
			ret = -errno;
			ERROR("Unable to enqueue block: %s\n", strerror(errno));
			goto err_munmap;
		}

		pdata->addrs[i] = mmap(0, pdata->blocks[i].size, PROT_READ,
				MAP_SHARED, fd, pdata->blocks[i].offset);
		if (pdata->addrs[i] == MAP_FAILED) {
			ret = -errno;
			ERROR("Failed to mmap block: %s\n", strerror(errno));
			goto err_munmap;
		}
	}

	return 0;

err_munmap:
	for (; i > 0; i--)
		munmap(pdata->addrs[i - 1], pdata->blocks[i - 1].size);
	ioctl(fd, BLOCK_FREE_IOCTL, 0);
err_close:
	local_ops->close(dev);
	return ret;
}

static int local_mmap_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	unsigned int i;

	if (!pdata->f)
		return -EBADF;

	for (i = 0; i < NB_BLOCKS; i++)
		munmap(pdata->addrs[i], pdata->blocks[i].size);
	ioctl(fileno(pdata->f), BLOCK_FREE_IOCTL, 0);

	return local_ops->close(dev);
}

static ssize_t local_mmap_get_buffer(const struct iio_device *dev,
		void **addr_ptr, uint32_t *mask, size_t words)
{
	struct block block;
	struct iio_device_pdata *pdata = dev->pdata;
	FILE *f = pdata->f;
	ssize_t ret;

	if (!f)
		return -EBADF;
	if (words != dev->words)
		return -EINVAL;

	memcpy(mask, dev->mask, words);

	if (pdata->last_dequeued >= 0) {
		ret = (ssize_t) ioctl(fileno(f), BLOCK_ENQUEUE_IOCTL,
				&pdata->blocks[pdata->last_dequeued]);
		if (ret) {
			ret = (ssize_t) -errno;
			ERROR("Unable to enqueue block: %s\n", strerror(errno));
			return ret;
		}
	}

	ret = (ssize_t) ioctl(fileno(f), BLOCK_DEQUEUE_IOCTL, &block);
	if (ret) {
		ret = (ssize_t) -errno;
		ERROR("Unable to dequeue block: %s\n", strerror(errno));
		return ret;
	}

	pdata->last_dequeued = block.id;
	*addr_ptr = pdata->addrs[block.id];
	return (ssize_t) block.bytes_used;
}

static void local_mmap_shutdown(struct iio_context *ctx)
{
	free((void *) ctx->ops);
	ctx->ops = local_ops;
	local_ops->shutdown(ctx);
}

struct iio_context * iio_create_local_mmap_context(void)
{
	unsigned int i;
	struct iio_backend_ops *ops;
	struct iio_context *ctx;

	ops = malloc(sizeof(*ops));
	if (!ops) {
		ERROR("Unable to allocate ops structure: %s\n",
				strerror(ENOMEM));
		return NULL;
	}

	ctx = iio_create_local_context();
	if (!ctx)
		goto err_free_ops;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device_pdata *pdata =
			realloc(ctx->devices[i]->pdata, sizeof(*pdata));
		if (!pdata) {
			ERROR("Unable to realloc pdata structure\n");
			goto err_free_ctx;
		}

		pdata->last_dequeued = -1;
		ctx->devices[i]->pdata = pdata;
	}

	local_ops = ctx->ops;
	memcpy(ops, local_ops, sizeof(*ops));
	ops->open = local_mmap_open;
	ops->close = local_mmap_close;
	ops->read = NULL;
	ops->write = NULL;
	ops->get_buffer = local_mmap_get_buffer;
	ops->shutdown = local_mmap_shutdown;

	ctx->ops = ops;
	return ctx;

err_free_ctx:
	iio_context_destroy(ctx);
err_free_ops:
	free(ops);
	return NULL;
}
