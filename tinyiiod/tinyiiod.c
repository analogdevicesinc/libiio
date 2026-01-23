// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "../iiod/ops.h"
#include "tinyiiod.h"

#include <iio/iio-lock.h>

#define container_of(ptr, type, member)	\
	((type *)(void *)((uintptr_t)(ptr) - offsetof(type, member)))

struct iiod_pdata;

struct iio_context_params iiod_params = {
	.log_level = LEVEL_INFO,
};

/* Global state for managing tinyiiod initialization */
static struct iio_mutex *iiod_init_lock = NULL;
static int iiod_ref_count = 0;
static bool iiod_locks_created = false;

/* These are the global locks used by all iiod operations, declared in ops.h */
extern struct iio_mutex *buflist_lock, *evlist_lock;

struct iiod_ctx {
	struct parser_pdata parser_pdata;
	struct iiod_pdata *pdata;
	ssize_t (*read_cb)(struct iiod_pdata *, void *, size_t);
	ssize_t (*write_cb)(struct iiod_pdata *, const void *, size_t);
};

static ssize_t iiod_readfd(struct parser_pdata *pdata, void *buf, size_t len)
{
	struct iiod_ctx *ctx = container_of(pdata, struct iiod_ctx, parser_pdata);

	return ctx->read_cb(ctx->pdata, buf, len);
}

static ssize_t iiod_writefd(struct parser_pdata *pdata, const void *buf, size_t len)
{
	struct iiod_ctx *ctx = container_of(pdata, struct iiod_ctx, parser_pdata);

	return ctx->write_cb(ctx->pdata, buf, len);
}

int iiod_init(void)
{
	int ret = 0;

	/* Create the initialization lock if it doesn't exist.
	 * There's a small race condition here, but it's acceptable since
	 * at worst we might create an extra mutex that gets destroyed. */
	if (!iiod_init_lock) {
		struct iio_mutex *new_lock = iio_mutex_create();
		if (iio_err(new_lock))
			return iio_err(new_lock);

		/* Atomically set the init lock if it's still NULL */
		if (!iiod_init_lock) {
			iiod_init_lock = new_lock;
		} else {
			/* Someone else created it first, clean up our copy */
			iio_mutex_destroy(new_lock);
		}
	}

	iio_mutex_lock(iiod_init_lock);

	/* Increment reference count */
	iiod_ref_count++;

	/* Only create locks on first initialization */
	if (!iiod_locks_created) {
		/* Create buflist_lock */
		if (!buflist_lock) {
			buflist_lock = iio_mutex_create();
			ret = iio_err(buflist_lock);
			if (ret)
				goto err_dec_ref;
		}

		/* Create evlist_lock */
		if (!evlist_lock) {
			evlist_lock = iio_mutex_create();
			ret = iio_err(evlist_lock);
			if (ret)
				goto err_destroy_buflist;
		}

		iiod_locks_created = true;
	}

	iio_mutex_unlock(iiod_init_lock);
	return 0;

err_destroy_buflist:
	if (buflist_lock) {
		iio_mutex_destroy(buflist_lock);
		buflist_lock = NULL;
	}
err_dec_ref:
	iiod_ref_count--;
	iio_mutex_unlock(iiod_init_lock);
	return ret;
}

void iiod_cleanup(void)
{
	/* If init lock was never created, nothing to do */
	if (!iiod_init_lock)
		return;

	iio_mutex_lock(iiod_init_lock);

	/* Decrement reference count */
	if (iiod_ref_count > 0)
		iiod_ref_count--;

	/* Only destroy locks when reference count reaches zero */
	if (iiod_ref_count == 0 && iiod_locks_created) {
		if (evlist_lock) {
			iio_mutex_destroy(evlist_lock);
			evlist_lock = NULL;
		}
		if (buflist_lock) {
			iio_mutex_destroy(buflist_lock);
			buflist_lock = NULL;
		}
		iiod_locks_created = false;
	}

	iio_mutex_unlock(iiod_init_lock);

	/* Note: We intentionally don't destroy iiod_init_lock itself
	 * to avoid race conditions with other threads that might be
	 * calling iiod_init() concurrently. This is a small memory leak
	 * but is acceptable for most use cases. */
}

int iiod_interpreter(struct iio_context *ctx,
		     struct iiod_pdata *pdata,
		     ssize_t (*read_cb)(struct iiod_pdata *, void *, size_t),
		     ssize_t (*write_cb)(struct iiod_pdata *, const void *, size_t),
		     const void *xml_zstd, size_t xml_zstd_len)
{
	struct iiod_ctx iiod_ctx = {
		.parser_pdata = {
			.ctx = ctx,
			.xml_zstd = xml_zstd,
			.xml_zstd_len = xml_zstd_len,
			.readfd = iiod_readfd,
			.writefd = iiod_writefd,
		},
		.read_cb = read_cb,
		.write_cb = write_cb,
		.pdata = pdata,
	};
	int ret = 0;

	/* Verify that iiod_init() was called */
	if (!iiod_locks_created || !buflist_lock || !evlist_lock)
		return -EINVAL;

	binary_parse(&iiod_ctx.parser_pdata);

	return ret;
}
