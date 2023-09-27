// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2017 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dynamic.h"
#include "iio-config.h"
#include "iio-private.h"

#include <string.h>

unsigned int iio_get_builtin_backends_count(void)
{
	unsigned int i, count = 0;

	for (i = 0; i < iio_backends_size; i++)
		count += !!iio_backends[i];

	return count;
}

const char * iio_get_builtin_backend(unsigned int index)
{
	unsigned int i;

	for (i = 0; i < iio_backends_size; i++) {
		if (index == 0 && iio_backends[i])
			return iio_backends[i]->name;

		index -= !!iio_backends[i];
	}

	return NULL;
}

bool
iio_has_backend(const struct iio_context_params *params, const char *backend)
{
	unsigned int i;

	for (i = 0; i < iio_get_builtin_backends_count(); i++)
		if (strcmp(backend, iio_get_builtin_backend(i)) == 0)
			return true;

	if (WITH_MODULES)
		return iio_has_backend_dynamic(params, backend);

	return false;
}
