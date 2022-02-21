// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dynamic.h"

#include <dlfcn.h>

void * iio_dlopen(const char *path)
{
	return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

void iio_dlclose(void *lib)
{
	dlclose(lib);
}

const void * iio_dlsym(void *lib, const char *symbol)
{
	return dlsym(lib, symbol);
}
