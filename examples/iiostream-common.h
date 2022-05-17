/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __IIOSTREAM_COMMON_H__
#define __IIOSTREAM_COMMON_H__

#include <stddef.h>

struct iio_channel;
struct iio_stream;

void stop_stream(void);

void stream(size_t rx_sample, size_t tx_sample, size_t block_size,
	    struct iio_stream *rxstream, struct iio_stream *txstream,
	    const struct iio_channel *rxchn, const struct iio_channel *txchn);

#endif /* __IIOSTREAM_COMMON_H__ */
