// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

struct iio_scan_context {
	bool scan_usb;
	bool scan_network;
	bool scan_local;
};

struct iio_scan {
	struct iio_scan_result scan_result;
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

	if (WITH_LOCAL_BACKEND && ctx->scan_local) {
		int ret = local_context_scan(&scan_result);
		if (ret < 0) {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return ret;
		}
	}

	if (WITH_USB_BACKEND && ctx->scan_usb) {
		int ret = usb_context_scan(&scan_result);
		if (ret < 0) {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return ret;
		}
	}

	if (HAVE_DNS_SD && ctx->scan_network) {
		int ret = dnssd_context_scan(&scan_result);
		if (ret < 0) {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return ret;
		}
	}

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

		free(info->description);
		free(info->uri);
		free(info);
	}

	free(list);
}

struct iio_context_info *
iio_scan_result_add(struct iio_scan_result *scan_result)
{
	struct iio_context_info **info;
	size_t size = scan_result->size;

	info = realloc(scan_result->info, (size + 2) * sizeof(*info));
	if (!info)
		return NULL;

	scan_result->info = info;
	scan_result->size = size + 1;

	/* Make sure iio_context_info_list_free won't overflow */
	info[size + 1] = NULL;

	info[size] = zalloc(sizeof(**info));
	if (!info[size])
		return NULL;

	return info[size];
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

	if (!backend || strstr(backend, "local"))
		ctx->scan_local = true;

	if (!backend || strstr(backend, "usb"))
		ctx->scan_usb = true;

	if (!backend || strstr(backend, "ip"))
		ctx->scan_network = true;

	return ctx;
}

void iio_scan_context_destroy(struct iio_scan_context *ctx)
{
	free(ctx);
}

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

static bool has_backend(const char *backends, const char *backend)
{
	return !backends || iio_list_has_elem(backends, backend);
}

struct iio_scan * iio_scan(const struct iio_context_params *params,
			   const char *backends)
{
	struct iio_scan *ctx;
	int ret;

	if (!params)
		params = get_default_params();

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		return NULL;
	}

	if (WITH_LOCAL_BACKEND && has_backend(backends, "local")) {
		ret = local_context_scan(&ctx->scan_result);
		if (ret < 0) {
			prm_perror(params, -ret,
				   "Unable to scan local context(s)");
		}
	}

	if (WITH_USB_BACKEND && has_backend(backends, "usb")) {
		ret = usb_context_scan(&ctx->scan_result);
		if (ret < 0) {
			prm_perror(params, -ret,
				   "Unable to scan USB context(s)");
		}
	}

	if (HAVE_DNS_SD && has_backend(backends, "ip")) {
		ret = dnssd_context_scan(&ctx->scan_result);
		if (ret < 0) {
			prm_perror(params, -ret,
				   "Unable to scan network context(s)");
		}
	}

	return ctx;
}

void iio_scan_destroy(struct iio_scan *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->scan_result.size; i++) {
		free(ctx->scan_result.info[i]->description);
		free(ctx->scan_result.info[i]->uri);
		free(ctx->scan_result.info[i]);
	}

	free(ctx);
}

size_t iio_scan_get_results_count(const struct iio_scan *ctx)
{
	return ctx->scan_result.size;
}

const char *
iio_scan_get_description(const struct iio_scan *ctx, size_t idx)
{
	if (idx >= ctx->scan_result.size)
		return NULL;

	return ctx->scan_result.info[idx]->description;
}

const char * iio_scan_get_uri(const struct iio_scan *ctx, size_t idx)
{
	if (idx >= ctx->scan_result.size)
		return NULL;

	return ctx->scan_result.info[idx]->uri;
}
