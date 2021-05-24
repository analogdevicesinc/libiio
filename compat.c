// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Libiio 0.x to 1.x compat library
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-compat.h"

/*
 * Dummy function with a call to a libiio 1.x function to force the linker to
 * link with libiio.so.1
 */
int __foo(void)
{
	iio_library_get_version(NULL, NULL, NULL);
}
