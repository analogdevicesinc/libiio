// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"
#include "local.h"

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

#define container_of(ptr, type, member)	\
	((type *)(void *)((uintptr_t)(ptr) - offsetof(type, member)))

#define BLOCK_ALLOC_IOCTL	_IOWR('i', 0xa0, struct block_alloc_req)
#define BLOCK_FREE_IOCTL	_IO('i', 0xa1)
#define BLOCK_QUERY_IOCTL	_IOWR('i', 0xa2, struct block)
#define BLOCK_ENQUEUE_IOCTL	_IOWR('i', 0xa3, struct block)
#define BLOCK_DEQUEUE_IOCTL	_IOWR('i', 0xa4, struct block)

#define BLOCK_FLAG_CYCLIC	BIT(1)

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

struct iio_buffer_impl_pdata {
	atomic_uint_fast64_t mmap_block_mask;
	atomic_uint_fast64_t mmap_enqueued_blocks_mask;
	bool mmap_check_done;
	bool cyclic_buffer_enqueued;
	unsigned int nb_blocks;
};

struct iio_block_impl_pdata {
	struct iio_block_pdata pdata;
	struct block block;
	unsigned int idx;
};

static struct iio_block_impl_pdata *
iio_block_get_impl(struct iio_block_pdata *pdata)
{
	return container_of(pdata, struct iio_block_impl_pdata, pdata);
}

static bool local_is_mmap_api_supported(int fd)
{
	/*
	 * For the BLOCK_ALLOC_IOCTL ioctl it is not possible to distinguish
	 * between an error during the allocation (e.g. incorrect size) or
	 * whether the high-speed interface is not supported. BLOCK_FREE_IOCTL does
	 * never fail if the device supports the high-speed interface, so we use it
	 * here. Calling it when no blocks are allocated the ioctl has no effect.
	 */

	return !ioctl_nointr(fd, BLOCK_FREE_IOCTL, NULL);
}

struct iio_block_pdata *
local_create_mmap_block(struct iio_buffer_pdata *pdata,
			size_t size, void **data)
{
	struct iio_buffer_impl_pdata *ppdata = pdata->pdata;
	struct iio_block_impl_pdata *priv;
	struct block_alloc_req req;
	int ret;

	if (!ppdata->mmap_check_done) {
		pdata->mmap_supported = local_is_mmap_api_supported(pdata->fd);
		ppdata->mmap_check_done = true;
	}

	if (!pdata->mmap_supported)
		return iio_ptr(-ENOSYS);

	priv = zalloc(sizeof(*priv));
	if (!priv)
		return iio_ptr(-ENOMEM);

	if (ppdata->mmap_block_mask == (uint64_t) -1) {
		/* 64 blocks is the maximum */
		dev_err(pdata->dev, "64 blocks is the maximum with the MMAP API.\n");
		ret = -EINVAL;
		goto out_free_priv;
	}

	if (__builtin_popcountl(ppdata->mmap_block_mask) == ppdata->nb_blocks) {
		/* All our allocated blocks are used; we need to create one more. */
		priv->idx = ppdata->nb_blocks;
		req.id = 0;
		req.type = 0;
		req.size = size;
		req.count = priv->idx + 1;

		ret = ioctl_nointr(pdata->fd, BLOCK_ALLOC_IOCTL, &req);
		if (ret < 0)
			goto out_free_priv;

		if (req.count < priv->idx + 1) {
			ret = -ENOMEM;
			goto out_free_priv;
		}

		ppdata->nb_blocks++;
	} else {
		/* One of our previously allocated blocks has been freed;
		 * re-use it now. */

		/* XXX: This only works if the blocks are the same size... */
		priv->idx = __builtin_ffsl(~ppdata->mmap_block_mask) - 1;
	}

	priv->block.id = priv->idx;

	ret = ioctl_nointr(pdata->fd, BLOCK_QUERY_IOCTL, &priv->block);
	if (ret < 0)
		goto out_free_priv;

	priv->pdata.data = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
				pdata->fd, priv->block.offset);
	if (priv->pdata.data == MAP_FAILED) {
		ret = -errno;
		goto out_free_priv;
	}

	priv->pdata.size = size;
	priv->pdata.buf = pdata;

	*data = priv->pdata.data;
	ppdata->mmap_block_mask |= BIT(priv->idx);

	return &priv->pdata;

out_free_priv:
	free(priv);
	return iio_ptr(ret);
}

void local_free_mmap_block(struct iio_block_pdata *pdata)
{
	struct iio_block_impl_pdata *priv = iio_block_get_impl(pdata);
	struct iio_buffer_pdata *buf = pdata->buf;

	munmap(pdata->data, pdata->size);

	if (!atomic_fetch_xor(&buf->pdata->mmap_block_mask, BIT(priv->idx))) {
		ioctl_nointr(pdata->buf->fd, BLOCK_FREE_IOCTL, 0);
		pdata->buf->pdata->nb_blocks = 0;
	}

	free(priv);
}

int local_enqueue_mmap_block(struct iio_block_pdata *pdata,
			     size_t bytes_used, bool cyclic)
{
	struct iio_block_impl_pdata *priv = iio_block_get_impl(pdata);
	struct iio_buffer_pdata *buf = pdata->buf;
	int ret, fd = buf->fd;
	uint_fast64_t mask;

	if (cyclic) {
		if (buf->pdata->cyclic_buffer_enqueued)
			return -EBUSY;

		priv->block.flags |= BLOCK_FLAG_CYCLIC;
	}

	if (bytes_used != priv->block.size && !iio_device_is_tx(buf->dev)) {
		/* MMAP interface only supports bytes_used on TX */
		return -EINVAL;
	}

	mask = atomic_fetch_or(&buf->pdata->mmap_enqueued_blocks_mask, BIT(priv->idx));
	if (mask & BIT(priv->idx)) {
		/* Already enqueued */
		return -EPERM;
	}

	priv->block.bytes_used = (uint32_t) bytes_used;

	ret = ioctl_nointr(fd, BLOCK_ENQUEUE_IOCTL, &priv->block);
	if (ret < 0) {
		atomic_fetch_xor(&buf->pdata->mmap_enqueued_blocks_mask, BIT(priv->idx));
		return ret;
	}

	if (cyclic)
		buf->pdata->cyclic_buffer_enqueued = true;

	return 0;
}

int local_dequeue_mmap_block(struct iio_block_pdata *pdata, bool nonblock)
{
	struct iio_block_impl_pdata *priv = iio_block_get_impl(pdata);
	struct iio_buffer_pdata *buf = pdata->buf;
	struct timespec start, *time_ptr = NULL;
	struct block block;
	int ret, fd = buf->fd;

	if (!(atomic_load(&buf->pdata->mmap_enqueued_blocks_mask) & BIT(priv->idx))) {
		/* Already dequeued */
		return -EPERM;
	}

	if (!nonblock) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		time_ptr = &start;
	}

	for (;;) {
		ret = buffer_check_ready(buf, fd, POLLIN | POLLOUT, time_ptr);
		if (ret < 0)
			return ret;

		ret = ioctl_nointr(fd, BLOCK_DEQUEUE_IOCTL, &block);
		if (ret < 0)
			return ret;

		atomic_fetch_xor(&buf->pdata->mmap_enqueued_blocks_mask, BIT(block.id));

		if (block.id == priv->idx)
			break;
	}

	return 0;
}

struct iio_buffer_impl_pdata * local_alloc_mmap_buffer_impl(void)
{
	struct iio_buffer_impl_pdata *pdata;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	return pdata;
}
