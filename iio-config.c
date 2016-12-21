
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2015 Analog Devices, Inc.
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
 *
 * */

#include "iio-config.h"
#include <stdbool.h>

bool iio_backend_has_local() 
{
#ifdef WITH_LOCAL_BACKEND
	return true;
#else
	return false;
#endif
}

bool iio_backend_has_xml() 
{
#ifdef WITH_XML_BACKEND
	return true;
#else
	return false;
#endif
}

bool iio_backend_has_network() 
{
#ifdef WITH_NETWORK_BACKEND
	return true;
#else
	return false;
#endif
}

bool iio_backend_has_usb() 
{
#ifdef WITH_USB_BACKEND
	return true;
#else
	return false;
#endif
}

bool iio_backend_has_serial() 
{
#ifdef WITH_SERIAL_BACKEND
	return true;
#else
	return false;
#endif
}
