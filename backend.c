/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2017 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "iio-config.h"
#include "iio-private.h"

#include <string.h>

unsigned int iio_get_backends_count(void)
{
	unsigned int count = 0;

	count += WITH_LOCAL_BACKEND;
	count += WITH_XML_BACKEND;
	count += WITH_NETWORK_BACKEND;
	count += WITH_USB_BACKEND;
	count += WITH_SERIAL_BACKEND;

	return count;
}

const char * iio_get_backend(unsigned int index)
{
	if (WITH_LOCAL_BACKEND) {
		if (index == 0)
			return "local";
		index--;
	}

	if (WITH_XML_BACKEND) {
		if (index == 0)
			return "xml";
		index--;
	}

	if (WITH_NETWORK_BACKEND) {
		if (index == 0)
			return "ip";
		index--;
	}

	if (WITH_USB_BACKEND) {
		if (index == 0)
			return "usb";
		index--;
	}

	if (WITH_SERIAL_BACKEND) {
		if (index == 0)
			return "serial";
		index --;
	}

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
