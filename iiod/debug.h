/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __IIOD_DEBUG_H__
#define __IIOD_DEBUG_H__

#include <iio/iio-debug.h>

extern struct iio_context_params iiod_params;

#define IIO_DEBUG(...) \
	prm_dbg(&iiod_params, __VA_ARGS__)

#define IIO_INFO(...) \
	prm_info(&iiod_params, __VA_ARGS__)

#define IIO_WARNING(...) \
	prm_warn(&iiod_params, __VA_ARGS__)

#define IIO_ERROR(...) \
	prm_err(&iiod_params, __VA_ARGS__)

#define IIO_PERROR(err, ...) \
	prm_perror(&iiod_params, err, __VA_ARGS__)

#endif /* __IIOD_DEBUG_H__ */
