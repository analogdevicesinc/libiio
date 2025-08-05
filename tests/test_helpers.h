/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <iio/iio.h>
#include <string.h>

static inline struct iio_context *create_test_context(const char *env_var_name,
	const char *default_uri, const struct iio_context_params *params)
{
    const char *uri = getenv(env_var_name);
    if (!uri) {
        uri = default_uri;
    }

    struct iio_context *ctx = iio_create_context(params, uri);
    if (iio_err(ctx)) {
        return NULL;
    }

    return ctx;
}

#endif /* TEST_HELPERS_H */
