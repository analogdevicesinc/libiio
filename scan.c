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

struct iio_context_info {
	char *description;
	char *uri;
};

struct iio_scan {
	struct iio_context_info *info;
	size_t count;
};

static bool has_backend(const char *backends, const char *backend)
{
	return !backends || iio_list_has_elem(backends, backend);
}

struct iio_scan * iio_scan(const struct iio_context_params *params,
			   const char *backends)
{
	const struct iio_context_params *default_params = get_default_params();
	struct iio_context_params params2 = { 0 };
	struct iio_scan *ctx;
	unsigned int i;
	char buf[256];
	size_t len;
	int ret;

	if (!params)
		params = default_params;
	params2 = *params;
	if (!params2.log_level)
		params2.log_level = default_params->log_level;
	if (!params2.stderr_level)
		params2.stderr_level = default_params->stderr_level;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		return NULL;
	}

	for (i = 0; i < iio_backends_size; i++) {
		if (!iio_backends[i] || !iio_backends[i]->ops->scan)
			continue;

		len = strlen(iio_backends[i]->uri_prefix);

		snprintf(buf, sizeof(buf), "%.*s",
			 (int)(len - 1), iio_backends[i]->uri_prefix);

		if (has_backend(backends, buf)) {
			if (params->timeout_ms)
				params2.timeout_ms = params->timeout_ms;
			else
				params2.timeout_ms = iio_backends[i]->default_timeout_ms;

			ret = iio_backends[i]->ops->scan(&params2, ctx);
			if (ret < 0) {
				prm_perror(&params2, -ret,
					   "Unable to scan %s context(s)", buf);
			}
		}
	}

	return ctx;
}

void iio_scan_destroy(struct iio_scan *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->count; i++) {
		free(ctx->info[i].description);
		free(ctx->info[i].uri);
	}

	free(ctx->info);
	free(ctx);
}

size_t iio_scan_get_results_count(const struct iio_scan *ctx)
{
	return ctx->count;
}

const char *
iio_scan_get_description(const struct iio_scan *ctx, size_t idx)
{
	if (idx >= ctx->count)
		return NULL;

	return ctx->info[idx].description;
}

const char * iio_scan_get_uri(const struct iio_scan *ctx, size_t idx)
{
	if (idx >= ctx->count)
		return NULL;

	return ctx->info[idx].uri;
}

int iio_scan_add_result(struct iio_scan *ctx, const char *desc, const char *uri)
{
	struct iio_context_info *info;
	size_t size = ctx->count;

	info = realloc(ctx->info, (size + 1) * sizeof(*info));
	if (!info)
		return -ENOMEM;

	ctx->info = info;
	ctx->count = size + 1;

	info = &info[size];
	info->description = iio_strdup(desc);
	info->uri = iio_strdup(uri);

	if (!info->description || !info->uri)
		return -ENOMEM;

	return 0;
}
