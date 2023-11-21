// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"
#include <iio-config.h>

static void libiio_init(void)
{
}

static void libiio_exit(void)
{
	if (WITH_XML_BACKEND)
		libiio_cleanup_xml_backend();
}

#if defined(_MSC_BUILD)
#pragma section(".CRT$XCU", read)
#define __CONSTRUCTOR(f, p) \
  static void f(void); \
  __declspec(allocate(".CRT$XCU")) void (*f##_)(void) = f; \
  __pragma(comment(linker,"/include:" p #f "_")) \
  static void f(void)
#ifdef _WIN64
#define _CONSTRUCTOR(f) __CONSTRUCTOR(f, "")
#else
#define _CONSTRUCTOR(f) __CONSTRUCTOR(f, "_")
#endif
#elif defined(__GNUC__)
#define _CONSTRUCTOR(f) static void __attribute__((constructor)) f(void)
#else
#define _CONSTRUCTOR(f) static void f(void)
#endif

_CONSTRUCTOR(initialize)
{
	libiio_init();

	/*
	 * When the library loads, register our destructor.
	 * Do it here and not in the context creation function,
	 * as it could otherwise end up registering the destructor
	 * many times.
	 */
	atexit(libiio_exit);
}
#undef _CONSTRUCTOR
