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
	size_t count;
};

struct iio_scan * iio_scan(const struct iio_context_params *params,
			   const char *backends)
{
	const struct iio_context_params *default_params = get_default_params();
	struct iio_context_params params2 = { 0 };
	char *token, *rest, *rest2, *backend_name;
	const struct iio_backend *backend = NULL;
	struct iio_module *module;
	const char *args, *uri;
	struct iio_scan *ctx;
	unsigned int i;
	char buf[1024];
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
	if (!ctx)
		return iio_ptr(-ENOMEM);

	if (!backends)
		backends = LIBIIO_SCAN_BACKENDS;

	/* Copy the string into an intermediate buffer for strtok() usage */
	iio_snprintf(buf, sizeof(buf), "%s", backends);

	for (token = iio_strtok_r(buf, ",", &rest);
	     token; token = iio_strtok_r(NULL, ",", &rest)) {
		args = NULL;

		for (i = 0; i < iio_backends_size; i++) {
			backend = iio_backends[i];

			if (!backend || !backend->ops->scan)
				continue;

			uri = backend->uri_prefix;
			len = strlen(uri) - 1; /* -1: to remove the colon of the URI */

			if (strncmp(token, uri, len))
				continue;

			if (token[len] == '\0')
				args = NULL;
			else if (token[len] == '=')
				args = token + len + 1;
			else
				continue;

			break;
		}

		if (i == iio_backends_size)
			backend = NULL;

		if (WITH_MODULES && !backend) {
			backend_name = iio_strtok_r(token, "=", &rest2);

			module = iio_open_module(&params2, backend_name);
			if (iio_err(module)) {
				module = NULL;
			} else {
				backend = iio_module_get_backend(module);
				if (iio_err(backend)) {
					iio_release_module(module);
					backend = NULL;
					module = NULL;
				}
			}

			args = iio_strtok_r(NULL, "=", &rest2);
		} else {
			module = NULL;
		}

		if (!backend) {
			prm_warn(params, "No backend found for scan string \'%s\'\n",
				 token);
			continue;
		}

		if (!backend->ops || !backend->ops->scan) {
			prm_warn(params, "Backend %s does not support scanning.\n",
				 token);
			continue;
		}

		if (params->timeout_ms)
			params2.timeout_ms = params->timeout_ms;
		else
			params2.timeout_ms = backend->default_timeout_ms;

		ret = backend->ops->scan(&params2, ctx, args);
		if (ret < 0) {
			prm_perror(&params2, ret,
				   "Unable to scan %s context", token);
		}

		if (WITH_MODULES && module)
			iio_release_module(module);
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
