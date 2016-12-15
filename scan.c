/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
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
 */

#include "iio-config.h"
#include "iio-private.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

struct iio_scan_context {
#ifdef WITH_USB_BACKEND
	struct iio_scan_backend_context *usb_ctx;
#endif
	bool scan_local;
};

const char * iio_context_info_get_description(
		const struct iio_context_info *info)
{
	return info->description;
}

const char * iio_context_info_get_uri(
		const struct iio_context_info *info)
{
	return info->uri;
}

ssize_t iio_scan_context_get_info_list(struct iio_scan_context *ctx,
		struct iio_context_info ***info)
{
	struct iio_scan_result scan_result = { 0, NULL };

#ifdef WITH_LOCAL_BACKEND
	if (ctx->scan_local) {
		int ret = local_context_scan(&scan_result);
		if (ret < 0) {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return ret;
		}
	}
#endif

#ifdef WITH_USB_BACKEND
	if (ctx->usb_ctx) {
		int ret = usb_context_scan(ctx->usb_ctx, &scan_result);
		if (ret < 0) {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return ret;
		}
	}
#endif

	*info = scan_result.info;

	return (ssize_t) scan_result.size;
}

void iio_context_info_list_free(struct iio_context_info **list)
{
	struct iio_context_info **it;

	if (!list)
		return;

	for (it = list; *it; it++) {
		struct iio_context_info *info = *it;

		if (info->description)
			free(info->description);
		if (info->uri)
			free(info->uri);
		free(info);
	}

	free(list);
}

struct iio_context_info ** iio_scan_result_add(
		struct iio_scan_result *scan_result, size_t num)
{
	struct iio_context_info **info;
	size_t old_size, new_size;
	size_t i;

	old_size = scan_result->size;
	new_size = old_size + num;

	info = realloc(scan_result->info, (new_size + 1) * sizeof(*info));
	if (!info)
		return NULL;

	scan_result->info = info;
	scan_result->size = new_size;

	for (i = old_size; i < new_size; i++) {
		/* Make sure iio_context_info_list_free won't overflow */
		info[i + 1] = NULL;

		info[i] = zalloc(sizeof(**info));
		if (!info[i])
			return NULL;
	}

	return &info[old_size];
}

struct iio_scan_context * iio_create_scan_context(
		const char *backend, unsigned int flags)
{
	struct iio_scan_context *ctx;

	/* "flags" must be zero for now */
	if (flags != 0) {
		errno = EINVAL;
		return NULL;
	}

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		return NULL;
	}

	if (!backend || !strcmp(backend, "local"))
		ctx->scan_local = true;

#ifdef WITH_USB_BACKEND
	if (!backend || !strcmp(backend, "usb"))
		ctx->usb_ctx = usb_context_scan_init();
#endif

	return ctx;
}

void iio_scan_context_destroy(struct iio_scan_context *ctx)
{
#ifdef WITH_USB_BACKEND
	if (ctx->usb_ctx)
		usb_context_scan_free(ctx->usb_ctx);
#endif
	free(ctx);
}

#ifdef WITH_MATLAB_BINDINGS_API

struct iio_scan_block {
	struct iio_scan_context *ctx;
	struct iio_context_info **info;
	ssize_t ctx_cnt;
};

ssize_t iio_scan_block_scan(struct iio_scan_block *blk)
{
	iio_context_info_list_free(blk->info);
	blk->info = NULL;
	blk->ctx_cnt = iio_scan_context_get_info_list(blk->ctx, &blk->info);
	return blk->ctx_cnt;
}

struct iio_context_info *iio_scan_block_get_info(
		struct iio_scan_block *blk, unsigned int index)
{
	if (!blk->info || (ssize_t)index >= blk->ctx_cnt) {
		errno = EINVAL;
		return NULL;
	}
	return blk->info[index];
}

struct iio_scan_block *iio_create_scan_block(
		const char *backend, unsigned int flags)
{
	struct iio_scan_block *blk;

	blk = calloc(1, sizeof(*blk));
	if (!blk) {
		errno = ENOMEM;
		return NULL;
	}

	blk->ctx = iio_create_scan_context(backend, flags);
	if (!blk->ctx) {
		free(blk);
		return NULL;
	}

	return blk;
}

void iio_scan_block_destroy(struct iio_scan_block *blk)
{
	iio_context_info_list_free(blk->info);
	iio_scan_context_destroy(blk->ctx);
	free(blk);
}
#endif
