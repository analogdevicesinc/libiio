// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Libiio 0.x to 1.x compat library
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-backend.h"
#include "iio-compat.h"

static struct iio_context *
create_context_with_arg(const char *prefix, const char *arg)
{
	char buf[256];

	iio_snprintf(buf, sizeof(buf), "%s%s", prefix, arg);

	return iio_create_context_from_uri(buf);
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
	return create_context_with_arg("xml:", xml_file);
}

struct iio_context * iio_create_network_context(const char *hostname)
{
	return create_context_with_arg("ip:", hostname);
}

struct iio_context * iio_create_local_context(void)
{
	return iio_create_context_from_uri("local:");
}
