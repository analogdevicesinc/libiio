// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"
#include "sort.h"

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

ssize_t iio_scan_context_get_info_list(struct iio_scan_context *ctx,
		struct iio_context_info ***info)
{
	struct iio_scan_result scan_result = { 0, NULL };
	struct iio_context_info *out;
	char *token, *rest=NULL;
	size_t i, j = 0;
	ssize_t ret;

	for (token = iio_strtok_r(ctx->backendopts, ",", &rest);
			token; token = iio_strtok_r(NULL, ",", &rest)) {

		/* Since tokens are all null terminated, it's safe to use strcmp on them */
		if (WITH_LOCAL_BACKEND && !strcmp(token, "local")) {
			ret = local_context_scan(&scan_result);
		} else if (WITH_USB_BACKEND && (!strcmp(token, "usb") ||
						!strncmp(token, "usb=", sizeof("usb=") - 1))) {
			token = token[3] == '=' ? token + 4 : NULL;
			ret = usb_context_scan(&scan_result, token);
		} else if (HAVE_DNS_SD && !strcmp(token, "ip")) {
			ret = dnssd_context_scan(&scan_result);
		} else {
			ret = -ENODEV;
		}
		if (ret < 0)
			goto err_free_scan_result_info;
	}

	*info = scan_result.info;

	if (scan_result.size > 1) {
		qsort(scan_result.info, scan_result.size,
		      sizeof(struct iio_context_info *),
		      iio_context_info_compare);

		/* there might be duplicates */
		for (i = 1; i < scan_result.size; i++) {
			/* ipv6 and ipv4 can have the same uri, but have different descriptions,
			 * so check both if necessary
			 */
			if ((!strcmp(scan_result.info[i - 1]->uri,
				     scan_result.info[i]->uri)) &&
			    (!strcmp(scan_result.info[i - 1]->description,
				     scan_result.info[i]->description))) {
				out = scan_result.info[i - 1];
				j++;
				free(out->description);
				free(out->uri);
				out->description = NULL;
				out->uri = NULL;
			}
		}
		if (j) {
			/* Force all the nulls to the end */
			qsort(scan_result.info, scan_result.size,
					sizeof(struct iio_context_info *),
					iio_context_info_compare);
			return (ssize_t) scan_result.size - j;
		}
	}


	return (ssize_t) scan_result.size;

err_free_scan_result_info:
	if (scan_result.info)
		iio_context_info_list_free(scan_result.info);
	return ret;
}

void iio_context_info_list_free(struct iio_context_info **list)
{
	unsigned int i;

	for (i = 0; list && list[i]; i++) {
		free(list[i]->description);
		free(list[i]->uri);
		free(list[i]);
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
	char *ptr, *ptr2;
	unsigned int i, len;

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

	ctx->backendopts = iio_strndup(backend ? backend : LIBIIO_SCAN_BACKENDS, PATH_MAX);
	if (!ctx->backendopts) {
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}

	if (backend) {
		/* Replace the colon separator with a comma. */
		len = (unsigned int)strlen(ctx->backendopts);
		for (i = 0; i < len; i++)
			if (ctx->backendopts[i] == ':')
				ctx->backendopts[i] = ',';

		/* The only place where a colon is accepted is in the usb arguments:
		 * usb=vid:pid */
		for (ptr = strstr(ctx->backendopts, "usb="); ptr;
		     ptr = strstr(ptr, "usb=")) {
			ptr += sizeof("usb=");
			strtoul(ptr, &ptr2, 16);

			/* The USB backend will take care of errors */
			if (ptr2 != ptr && *ptr2 == ',')
				*ptr2 = ':';
		}
	}

	return ctx;
}

void iio_scan_context_destroy(struct iio_scan_context *ctx)
{
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
