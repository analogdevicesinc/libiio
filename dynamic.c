// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-backend.h"
#include "iio-config.h"
#include "iio-debug.h"
#include "iio-private.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

static const struct iio_backend *
get_iio_backend(const struct iio_context_params *params,
		const char *name, struct iio_module **libp)
{
	const struct iio_backend *backend;
	char buf[PATH_MAX];
	struct iio_module *lib;

	iio_snprintf(buf, sizeof(buf),
		     IIO_MODULES_DIR "libiio-%s" IIO_LIBRARY_SUFFIX, name);

	prm_dbg(params, "Looking for plugin: \'%s\'\n", buf);

	lib = iio_open_module(buf);
	if (!lib) {
		prm_dbg(params, "Unable to open plug-in\n");
		return ERR_PTR(-ENOSYS);
	}

	iio_snprintf(buf, sizeof(buf), "iio_%s_backend", name);

	backend = iio_module_get_backend(lib, buf);
	if (!backend) {
		prm_err(params, "No \'%s\' symbol\n", buf);
		iio_release_module(lib);
		return ERR_PTR(-EINVAL);
	}

	*libp = lib;

	return backend;
}

struct iio_context *
iio_create_dynamic_context(const struct iio_context_params *params,
			   const char *uri)
{
	struct iio_context_params params2 = *params;
	const struct iio_backend *backend;
	struct iio_context *ctx;
	const char *ptr;
	char buf[256];
	struct iio_module *lib;

	ptr = strchr(uri, ':');

	if (!ptr) {
		prm_err(params, "Invalid URI: %s\n", uri);
		errno = EINVAL;
		return NULL;
	}

	iio_snprintf(buf, sizeof(buf), "%.*s", (int) (ptr - uri), uri);

	backend = get_iio_backend(params, buf, &lib);
	if (IS_ERR(backend)) {
		errno = -PTR_ERR(backend);
		return NULL;
	}

	if (!backend->ops || !backend->ops->create) {
		prm_err(params, "Backend has no create function\n");
		errno = EINVAL;
		goto out_release_module;
	}

	prm_dbg(params, "Found backend: %s\n", backend->name);

	if (!params2.timeout_ms)
		params2.timeout_ms = backend->default_timeout_ms;

	uri += strlen(backend->uri_prefix);
	ctx = (*backend->ops->create)(&params2, uri);
	if (!ctx)
		goto out_release_module;

	ctx->lib = lib;
	return ctx;

out_release_module:
	iio_release_module(lib);
	return NULL;
}

int iio_dynamic_scan(const struct iio_context_params *params,
		     struct iio_scan *ctx, const char *backends)
{
	struct iio_context_params params2 = *params;
	const struct iio_backend *backend;
	const char *path, *endchar, *startchar;
	struct iio_module *lib;
	struct iio_directory *dir;
	char buf[256];
	int ret = 0;

	dir = iio_open_dir(IIO_MODULES_DIR);
	if (IS_ERR(dir)) {
		ret = PTR_ERR(dir);

		if (ret != -ENOENT)
			prm_perror(params, -ret, "Unable to open modules directory");
		return ret;
	}

	prm_dbg(params, "Opened directory " IIO_MODULES_DIR "\n");

	while (true) {
		path = iio_dir_get_next_file_name(dir);
		if (!path)
			break;

		endchar = strrchr(path, '.');
		if (!endchar || strcmp(endchar, IIO_LIBRARY_SUFFIX) ||
		    strncmp(path, "libiio-", sizeof("libiio-") - 1))
			continue;

		startchar = path + sizeof("libiio-") - 1;

		iio_snprintf(buf, sizeof(buf), "%.*s",
			     (int) (endchar - startchar), startchar);

		if (backends && !iio_list_has_elem(backends, buf))
			continue;

		backend = get_iio_backend(params, buf, &lib);
		if (IS_ERR(backend)) {
			iio_close_dir(dir);
			return PTR_ERR(backend);
		}

		prm_dbg(params, "Found backend: %s\n", backend->name);

		if (params->timeout_ms)
			params2.timeout_ms = params->timeout_ms;
		else
			params2.timeout_ms = backend->default_timeout_ms;

		if (backend->ops && backend->ops->scan) {
			ret = backend->ops->scan(&params2, ctx);
			if (ret < 0) {
				prm_perror(params, -ret,
					   "Unable to scan %s context(s)", buf);
			}
		}

		iio_release_module(lib);
	}

	iio_close_dir(dir);

	return 0;
}
