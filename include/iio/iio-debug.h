/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __IIO_DEBUG_H__
#define __IIO_DEBUG_H__

#include <iio/iio.h>

#define __api __iio_api

#if defined(__MINGW32__)
#   define __iio_printf __attribute__((__format__(gnu_printf, 3, 4)))
#elif defined(__GNUC__)
#   define __iio_printf __attribute__((__format__(printf, 3, 4)))
#else
#   define __iio_printf
#endif

#define __FIRST(a, ...)		a
#define ___OTHERS(a, ...)	a, __VA_ARGS__
#define __OTHERS(a, b, ...)	___OTHERS(a, __VA_ARGS__)


/** @brief Print a message with the given priority
 * @param params A pointer to a iio_context_params structure that contains
 *   context creation information; can be NULL
 * @param msg_level The priority of the message
 * @fmt A printf-like format string, followed by its arguments if any. */
__api __iio_printf void
iio_prm_printf(const struct iio_context_params *params,
	       enum iio_log_level msg_level,
	       const char *fmt, ...);

#define __ctx_params_or_null(ctx)	((ctx) ? iio_context_get_params(ctx) : NULL)
#define __dev_ctx_or_null(dev)	((dev) ? iio_device_get_context(dev) : NULL)
#define __chn_dev_or_null(chn)	((chn) ? iio_channel_get_device(chn) : NULL)

#define prm_err(prm, ...)	iio_prm_printf((prm), LEVEL_ERROR, "ERROR: " __VA_ARGS__)
#define prm_warn(prm, ...)	iio_prm_printf((prm), LEVEL_WARNING, "WARNING: " __VA_ARGS__)
#define prm_info(prm, ...)	iio_prm_printf((prm), LEVEL_INFO, __VA_ARGS__)
#define prm_dbg(prm, ...)	iio_prm_printf((prm), LEVEL_DEBUG, "DEBUG: " __VA_ARGS__)

#define ctx_err(ctx, ...)	prm_err(__ctx_params_or_null(ctx), __VA_ARGS__)
#define ctx_warn(ctx, ...)	prm_warn(__ctx_params_or_null(ctx), __VA_ARGS__)
#define ctx_info(ctx, ...)	prm_info(__ctx_params_or_null(ctx), __VA_ARGS__)
#define ctx_dbg(ctx, ...)	prm_dbg(__ctx_params_or_null(ctx), __VA_ARGS__)

#define dev_err(dev, ...)	ctx_err(__dev_ctx_or_null(dev), __VA_ARGS__)
#define dev_warn(dev, ...)	ctx_warn(__dev_ctx_or_null(dev), __VA_ARGS__)
#define dev_info(dev, ...)	ctx_info(__dev_ctx_or_null(dev), __VA_ARGS__)
#define dev_dbg(dev, ...)	ctx_dbg(__dev_ctx_or_null(dev), __VA_ARGS__)

#define chn_err(chn, ...)	dev_err(__chn_dev_or_null(chn), __VA_ARGS__)
#define chn_warn(chn, ...)	dev_warn(__chn_dev_or_null(chn), __VA_ARGS__)
#define chn_info(chn, ...)	dev_info(__chn_dev_or_null(chn), __VA_ARGS__)
#define chn_dbg(chn, ...)	dev_dbg(__chn_dev_or_null(chn), __VA_ARGS__)

#define prm_perror(params, err, ...) do {				\
	char _buf[1024];						\
	int _err = -(err);						\
	iio_strerror(_err, _buf, sizeof(_buf));				\
	prm_err(params, __FIRST(__VA_ARGS__, 0)				\
		__OTHERS(": %s\n",__VA_ARGS__, _buf));			\
} while (0)
#define ctx_perror(ctx, err, ...)	prm_perror(__ctx_params_or_null(ctx), err, __VA_ARGS__)
#define dev_perror(dev, err, ...)	ctx_perror(__dev_ctx_or_null(dev), err, __VA_ARGS__)
#define chn_perror(dev, err, ...)	dev_perror(__chn_dev_or_null(chn), err, __VA_ARGS__)

#undef __api

#endif /* __IIO_DEBUG_H__ */
