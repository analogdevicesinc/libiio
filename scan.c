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

struct iio_scan {
	struct iio_scan_result scan_result;
};

struct iio_context_info *
iio_scan_result_add(struct iio_scan_result *scan_result)
{
	struct iio_context_info **info;
	size_t size = scan_result->size;

	info = realloc(scan_result->info, (size + 1) * sizeof(*info));
	if (!info)
		return NULL;

	scan_result->info = info;
	scan_result->size = size + 1;

	info[size] = zalloc(sizeof(**info));
	if (!info[size])
		return NULL;

	return info[size];
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
