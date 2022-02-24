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
	struct iio_context_info *info;
	char *backends;
	size_t count;
};

struct iio_scan * iio_scan(const struct iio_context_params *params,
			   const char *backends)
{
	const struct iio_context_params *default_params = get_default_params();
	struct iio_context_params params2 = { 0 };
	struct iio_scan *ctx;
	const char *args, *uri;
	unsigned int i;
	char *token, *rest = NULL;
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

	if (!backends)
		backends = LIBIIO_SCAN_BACKENDS;

	ctx->backends = iio_strdup(backends);
	if (!ctx->backends) {
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}

	for (token = iio_strtok_r(ctx->backends, ",", &rest);
	     token; token = iio_strtok_r(NULL, ",", &rest)) {
		for (i = 0; i < iio_backends_size; i++) {
			if (!iio_backends[i] || !iio_backends[i]->ops->scan)
				continue;

			uri = iio_backends[i]->uri_prefix;
			len = strlen(uri) - 1; /* -1: to remove the colon of the URI */

			if (strncmp(token, uri, len))
				continue;

			if (token[len] == '\0')
				args = NULL;
			else if (token[len] == '=')
				args = token + len + 1;
			else
				continue;

			if (params->timeout_ms)
				params2.timeout_ms = params->timeout_ms;
			else
				params2.timeout_ms = iio_backends[i]->default_timeout_ms;

			ret = iio_backends[i]->ops->scan(&params2, ctx, args);
			if (ret < 0) {
				prm_perror(&params2, -ret,
					   "Unable to scan %s context", token);
			}
		}
	}

	if (WITH_MODULES) {
		params2.timeout_ms = params->timeout_ms;

		ret = iio_dynamic_scan(&params2, ctx, backends);
		if (ret < 0) {
			prm_perror(&params2, -ret,
				   "Error while scanning dynamic context(s)");
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
	free(ctx->backends);
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
