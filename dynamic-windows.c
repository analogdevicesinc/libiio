// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <windows.h>

struct iio_module * iio_open_module(const char *path)
{
	return (struct iio_module *) LoadLibrary(TEXT(path));
}

void iio_release_module(struct iio_module *module)
{
	FreeLibrary((void *) module);
}

const struct iio_backend *
iio_module_get_backend(struct iio_module *module, const char *symbol)
{
	return (const void *) GetProcAddress((void *) module, symbol);
}
