/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Libiio 0.x to 1.x compat library
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */
/** @file iio-compat.h
 * @brief Libiio 0.x to 1.x compatibility library */

#ifndef __LIBIIO_COMPAT_H__
#define __LIBIIO_COMPAT_H__

#include <iio.h>

#ifndef DOXYGEN
#ifdef LIBIIO_COMPAT_EXPORTS
#define __api __iio_api_export
#else
#define __api __iio_api_import
#endif
#endif


/* ---------------------------------------------------------------------------*/
/* ------------------------- Libiio 0.x to 1.x compat API --------------------*/
/** @defgroup TopLevel Top-level functions
 * @{ */


/** @} *//* ------------------------------------------------------------------*/

#undef __api

#endif /* __LIBIIO_COMPAT_H__ */
