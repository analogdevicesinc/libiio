// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dynamic.h"

#include <dlfcn.h>
#include <errno.h>
#include <iio/iio.h>

void * iio_dlopen(const char *path)
{
	return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

void iio_dlclose(void *lib)
{
	dlclose(lib);
}

void * iio_dlsym(void *lib, const char *symbol)
{
	void *ptr;

	dlerror();
	ptr = dlsym(lib, symbol);

	return dlerror() ? iio_ptr(-EINVAL) : ptr;
}
