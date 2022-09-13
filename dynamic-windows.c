// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dynamic.h"

#include <errno.h>
#include <iio/iio.h>
#include <windows.h>

void * iio_dlopen(const char *path)
{
	return LoadLibrary(TEXT(path));
}

void iio_dlclose(void *lib)
{
	FreeLibrary((void *) lib);
}

void * iio_dlsym(void *lib, const char *symbol)
{
	void *ptr;

	ptr = GetProcAddress(lib, symbol);

	return ptr ? ptr : iio_ptr(-EINVAL);
}
