/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline struct iio_context *create_test_context(const char *env_var_name,
		const char *default_uri, const struct iio_context_params *params)
{
	const char *requested_uri = getenv(env_var_name);
	const char *uri = requested_uri;
	if (!uri) {
		uri = default_uri;
	}

	struct iio_context *ctx = iio_create_context(params, uri);
	int err = iio_err(ctx);
	if (err) {
		if (requested_uri) {
			char error[256];

			iio_strerror(err, error, sizeof(error));
			fprintf(stderr, "%s='%s': unable to create requested test context: %s\n",
					env_var_name, requested_uri, error);
			exit(EXIT_FAILURE);
		}
		return NULL;
	}

	return ctx;
}

#endif /* TEST_HELPERS_H */
