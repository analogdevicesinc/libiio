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
	char *backendopts;
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

#define SCAN_DELIMIT ":"

ssize_t iio_scan_context_get_info_list(struct iio_scan_context *ctx,
		struct iio_context_info ***info)
{
	struct iio_scan_result scan_result = { 0, NULL };
	char *token, *rest=NULL;
	ssize_t ret;

	for (token = iio_strtok_r(ctx->backendopts, SCAN_DELIMIT, &rest);
			token; token = iio_strtok_r(NULL, SCAN_DELIMIT, &rest)) {

		/* Since tokens are all null terminated, it's safe to use strcmp on them */
		if (WITH_LOCAL_BACKEND && !strcmp(token, "local")) {
			ret = local_context_scan(&scan_result);
			if (ret < 0) {
				if (scan_result.info)
					iio_context_info_list_free(scan_result.info);
				return ret;
			}
		} else if (WITH_USB_BACKEND && (!strcmp(token, "usb") ||
					!strncmp(token, "usb=", sizeof("usb=") - 1))) {
			ret = usb_context_scan(&scan_result, token);
			if (ret < 0) {
				if (scan_result.info)
					iio_context_info_list_free(scan_result.info);
				return ret;
			}
		} else if (HAVE_DNS_SD && !strcmp(token, "ip")) {
			ret = dnssd_context_scan(&scan_result);
			if (ret < 0) {
				if (scan_result.info)
					iio_context_info_list_free(scan_result.info);
				return ret;
			}
		} else {
			if (scan_result.info)
				iio_context_info_list_free(scan_result.info);
			return -ENODEV;
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
	char *ptr, *ptr2, *ptr3;

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

	ctx->backendopts = iio_strndup(backend ? backend : "local:usb:ip:", PATH_MAX);
	if (!ctx->backendopts) {
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}

	/* while we hope people use usb=vid,pid ; iit's common to use
	 * usb=vid:pid ; so fix it up
	 */
	ptr = ctx->backendopts;
	while ((ptr = strstr(ptr, "usb="))) {
		uint32_t val;

		val = strtoul(&ptr[sizeof("usb=") - 1], &ptr2, 16);
		if (val < 0xFFFF && val > 0 && ptr2) {
			if (ptr2[0] == ':' ) {
				if (ptr2[1] == '*' || ptr2[1] == '\0') {
					ptr2[0] = ',';
				} else {
					val = strtoul(&ptr2[1], &ptr3, 16);
					if (val < 0xFFFF && val > 0) {
						ptr2[0] = ',';
						ptr = ptr3 - sizeof("usb=");
					}
				}
			}

		}
		ptr = ptr + sizeof("usb=");
	}

	return ctx;
}

void iio_scan_context_destroy(struct iio_scan_context *ctx)
{
	if (!ctx)
		return;

	if (ctx->backendopts)
		free(ctx->backendopts);

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
