// SPDX-License-Identifier: LGPL-2.1-or-later
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

struct iiod_ctx {
	struct parser_pdata parser_pdata;
	struct iiod_pdata *pdata;
	ssize_t (*read_cb)(struct iiod_pdata *, void *, size_t);
	ssize_t (*write_cb)(struct iiod_pdata *, const void *, size_t);
};

static ssize_t iiod_readfd(struct parser_pdata *pdata, void *buf, size_t len)
{
	struct iiod_ctx *ctx = container_of(pdata, struct iiod_ctx, parser_pdata);

	ctx->read_cb(ctx->pdata, buf, len);
}

static ssize_t iiod_writefd(struct parser_pdata *pdata, const void *buf, size_t len)
{
	struct iiod_ctx *ctx = container_of(pdata, struct iiod_ctx, parser_pdata);

	ctx->write_cb(ctx->pdata, buf, len);
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
	int ret;

	buflist_lock = iio_mutex_create();
	ret = iio_err(buflist_lock);
	if (ret)
		return ret;

	evlist_lock = iio_mutex_create();
	ret = iio_err(evlist_lock);
	if (ret)
		goto out_destroy_buflist_lock;

	binary_parse(&iiod_ctx.parser_pdata);

	iio_mutex_destroy(evlist_lock);
out_destroy_buflist_lock:
	iio_mutex_destroy(buflist_lock);
	return ret;
}
