// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iiostream-common.h"

#include <iio/iio.h>
#include <iio/iio-debug.h>

static bool stop = false;

void stop_stream(void)
{
	stop = true;
}

void stream(size_t rx_sample, size_t tx_sample, size_t block_size,
	    struct iio_stream *rxstream, struct iio_stream *txstream,
	    const struct iio_channel *rxchn, const struct iio_channel *txchn)
{
	const struct iio_device *dev;
	const struct iio_context *ctx;
	const struct iio_block *txblock, *rxblock;
	ssize_t nrx = 0;
	ssize_t ntx = 0;
	int err;

	dev = iio_channel_get_device(rxchn);
	ctx = iio_device_get_context(dev);

	while (!stop) {
		int16_t *p_dat, *p_end;
		ptrdiff_t p_inc;

		rxblock = iio_stream_get_next_block(rxstream);
		err = iio_err(rxblock);
		if (err) {
			ctx_perror(ctx, err, "Unable to receive block");
			return;
		}

		txblock = iio_stream_get_next_block(txstream);
		err = iio_err(txblock);
		if (err) {
			ctx_perror(ctx, err, "Unable to send block");
			return;
		}

		/* READ: Get pointers to RX buf and read IQ from RX buf port 0 */
		p_inc = rx_sample;
		p_end = iio_block_end(rxblock);
		for (p_dat = iio_block_first(rxblock, rxchn); p_dat < p_end;
		     p_dat += p_inc / sizeof(*p_dat)) {
			/* Example: swap I and Q */
			int16_t i = p_dat[0];
			int16_t q = p_dat[1];

			p_dat[0] = q;
			p_dat[1] = i;
		}

		/* WRITE: Get pointers to TX buf and write IQ to TX buf port 0 */
		p_inc = tx_sample;
		p_end = iio_block_end(txblock);
		for (p_dat = iio_block_first(txblock, txchn); p_dat < p_end;
		     p_dat += p_inc / sizeof(*p_dat)) {
			p_dat[0] = 0; /* Real (I) */
			p_dat[1] = 0; /* Imag (Q) */
		}

		nrx += block_size / rx_sample;
		ntx += block_size / tx_sample;
		ctx_info(ctx, "\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx / 1e6, ntx / 1e6);
	}
}
