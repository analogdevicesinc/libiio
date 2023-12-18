// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Nuno Sá <nuno.sa@analog.com>
 */

#include "iiostream-common.h"

#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#define ARGS(fmt, ...)	__VA_ARGS__
#define FMT(fmt, ...)	fmt
#define error(...) \
	printf("%s, %d: ERROR: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

#define info(...) \
	printf("%s, %d: INFO: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

/* helper macros */
#define GHZ(x) ((long long)(x * 1000000000.0 + .5))

#define BLOCK_SIZE (1024 * 1024)

static struct iio_context *ctx = NULL;
static struct iio_buffer *rxbuf = NULL;
static struct iio_buffer *txbuf = NULL;
static struct iio_stream *rxstream = NULL;
static struct iio_stream *txstream = NULL;
static struct iio_channels_mask *rxmask = NULL;
static struct iio_channels_mask *txmask = NULL;
static struct iio_channel *rx_chan[2] = { NULL, NULL };
static struct iio_channel *tx_chan[2] = { NULL, NULL };

enum {
	I_CHAN,
	Q_CHAN
};

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>

BOOL WINAPI sig_handler(DWORD dwCtrlType)
{
	/* Runs in its own thread */
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		stop_stream();
		return true;
	default:
		return false;
	}
}

static int register_signals(void)
{
	if (!SetConsoleCtrlHandler(sig_handler, TRUE))
		return -1;

	return 0;
}
#else
static void sig_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		info("Exit....\n");
		stop_stream();
	}
}

static int register_signals(void)
{
	struct sigaction sa = {0};
	sigset_t mask = {0};

	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sigemptyset(&mask);

	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		error("sigaction: %s\n", strerror(errno));
		return -1;
	}

	if (sigaction(SIGINT, &sa, NULL) < 0) {
		error("sigaction: %s\n", strerror(errno));
		return -1;
	}

	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	/* make sure these signals are unblocked */
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
		error("sigprocmask: %s", strerror(errno));
		return -1;
	}

	return 0;
}
#endif

static int configure_tx_lo(void)
{
	struct iio_device *phy;
	struct iio_channel *chan;
	const struct iio_attr *attr;
	int ret;
	long long val;

	phy = iio_context_find_device(ctx, "adrv9002-phy");
	if (!phy) {
		error("Could not find adrv9002_phy\n");
		return -ENODEV;
	}

	chan = iio_device_find_channel(phy, "voltage0", true);
	if (!chan) {
		error("Could not find TX voltage0 channel\n");
		return -ENODEV;
	}

	/* printout some useful info */
	attr = iio_channel_find_attr(chan, "rf_bandwidth");
	if (attr)
		ret = iio_attr_read_longlong(attr, &val);
	else
		ret = -ENOENT;
	if (ret)
		return ret;

	info("adrv9002 bandwidth: %lld\n", val);

	attr = iio_channel_find_attr(chan, "sampling_frequency");
	if (attr)
		ret = iio_attr_read_longlong(attr, &val);
	else
		ret = -ENOENT;
	if (ret)
		return ret;

	info("adrv9002 sampling_frequency: %lld\n", val);

	/* set the LO to 2.5GHz */
	val = GHZ(2.5);
	chan = iio_device_find_channel(phy, "altvoltage2", true);
	if (!chan) {
		error("Could not find TX LO channel\n");
		return -ENODEV;
	}

	attr = iio_channel_find_attr(chan, "TX1_LO_frequency");
	if (attr)
		ret = iio_attr_write_longlong(attr, val);
	else
		ret = -ENOENT;
	return ret;
}

static void cleanup(void)
{
	int c;

	if (rxstream)
		iio_stream_destroy(rxstream);
	if (txstream)
		iio_stream_destroy(txstream);

	if (rxbuf)
		iio_buffer_destroy(rxbuf);
	if (txbuf)
		iio_buffer_destroy(txbuf);

	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	if (txmask)
		iio_channels_mask_destroy(txmask);

	iio_context_destroy(ctx);
}

static struct iio_channels_mask *
stream_channels_get_mask(const struct iio_device *dev, struct iio_channel **chan,
				      bool tx)
{
	const char * const channels[] = {
		"voltage0_i", "voltage0_q", "voltage0", "voltage1"
	};
	unsigned int c, nb_channels = iio_device_get_channels_count(dev);
	struct iio_channels_mask *mask;
	const char *str;

	mask = iio_create_channels_mask(nb_channels);
	if (!mask)
		return iio_ptr(-ENOMEM);

	for (c = 0; c < 2; c++) {
		str = channels[tx * 2 + c];

		chan[c] = iio_device_find_channel(dev, str, tx);
		if (!chan[c]) {
			error("Could not find %s channel tx=%d\n", str, tx);
			return iio_ptr(-ENODEV);
		}

		iio_channel_enable(chan[c], mask);
	}

	return mask;
}

int main(void)
{
	struct iio_device *tx;
	struct iio_device *rx;
	size_t tx_sample_sz, rx_sample_sz;
	int ret = EXIT_FAILURE;

	if (register_signals() < 0)
		return EXIT_FAILURE;

	ctx = iio_create_context(NULL, NULL);
	if (!ctx) {
		error("Could not create IIO context\n");
		return EXIT_FAILURE;
	}

	ret = configure_tx_lo();
	if (ret)
		goto clean;

	tx = iio_context_find_device(ctx, "axi-adrv9002-tx-lpc");
	if (!tx)
		goto clean;

	rx = iio_context_find_device(ctx, "axi-adrv9002-rx-lpc");
	if (!rx)
		goto clean;

	rxmask = stream_channels_get_mask(rx, rx_chan, false);
	if (iio_err(rxmask)) {
		rxmask = NULL;
		goto clean;
	}

	txmask = stream_channels_get_mask(tx, tx_chan, true);
	if (iio_err(txmask)) {
		txmask = NULL;
		goto clean;
	}

	info("* Creating non-cyclic IIO buffers with 1 MiS\n");
	rxbuf = iio_device_create_buffer(rx, 0, rxmask);
	if (iio_err(rxbuf)) {
		rxbuf = NULL;
		ctx_perror(ctx, iio_err(rxbuf), "Could not create RX buffer");
		goto clean;
	}

	txbuf = iio_device_create_buffer(tx, 0, txmask);
	if (iio_err(txbuf)) {
		txbuf = NULL;
		ctx_perror(ctx, iio_err(txbuf), "Could not create TX buffer");
		goto clean;
	}

	rxstream = iio_buffer_create_stream(rxbuf, 4, BLOCK_SIZE);
	if (iio_err(rxstream)) {
		rxstream = NULL;
		ctx_perror(ctx, iio_err(rxstream), "Could not create RX stream");
		goto clean;
	}

	txstream = iio_buffer_create_stream(txbuf, 4, BLOCK_SIZE);
	if (iio_err(txstream)) {
		txstream = NULL;
		ctx_perror(ctx, iio_err(txstream), "Could not create TX stream");
		goto clean;
	}

	rx_sample_sz = iio_device_get_sample_size(rx, rxmask);
	tx_sample_sz = iio_device_get_sample_size(tx, txmask);

	info("* Starting IO streaming (press CTRL+C to cancel)\n");
	stream(rx_sample_sz, tx_sample_sz, BLOCK_SIZE, rxstream, txstream,
	       rx_chan[I_CHAN], tx_chan[I_CHAN]);

	ret = EXIT_SUCCESS;
clean:
	cleanup();
	return ret;
}
