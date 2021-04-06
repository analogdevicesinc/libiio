// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2017 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"

#include <string.h>

unsigned int iio_get_backends_count(void)
{
	unsigned int count = 0;

#ifdef WITH_LOCAL_BACKEND
	count++;
#endif
#ifdef WITH_XML_BACKEND
	count++;
#endif
#ifdef WITH_NETWORK_BACKEND
	count++;
#endif
#ifdef WITH_USB_BACKEND
	count++;
#endif
#ifdef WITH_SERIAL_BACKEND
	count++;
#endif

	return count;
}

const char * iio_get_backend(unsigned int index)
{
#ifdef WITH_LOCAL_BACKEND
	if (index == 0)
		return "local";
	index--;
#endif
#ifdef WITH_XML_BACKEND
	if (index == 0)
		return "xml";
	index--;
#endif
#ifdef WITH_NETWORK_BACKEND
	if (index == 0)
		return "ip";
	index--;
#endif
#ifdef WITH_USB_BACKEND
	if (index == 0)
		return "usb";
	index--;
#endif
#ifdef WITH_SERIAL_BACKEND
	if (index == 0)
		return "serial";
#endif
	return NULL;
}

bool iio_has_backend(const char *backend)
{
	unsigned int i;

	for (i = 0; i < iio_get_backends_count(); i++)
		if (strcmp(backend, iio_get_backend(i)) == 0)
			return true;

	return false;
}
