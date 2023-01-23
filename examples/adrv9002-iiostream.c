// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Nuno Sá <nuno.sa@analog.com>
 */
#include <iio.h>
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

static bool stop = false;
static struct iio_context *ctx = NULL;
static struct iio_buffer *rxbuf = NULL;
static struct iio_buffer *txbuf = NULL;
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
		stop = true;
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
		stop = true;
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
	ret = iio_channel_attr_read_longlong(chan, "rf_bandwidth", &val);
	if (ret)
		return ret;

	info("adrv9002 bandwidth: %lld\n", val);

	ret = iio_channel_attr_read_longlong(chan, "sampling_frequency", &val);
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

	return iio_channel_attr_write_longlong(chan, "TX1_LO_frequency", val);
}

static void cleanup(void)
{
	int c;

	if (rxbuf)
		iio_buffer_destroy(rxbuf);

	if (txbuf)
		iio_buffer_destroy(txbuf);

	for (c = 0; c < 2; c++) {
		if (rx_chan[c])
			iio_channel_disable(rx_chan[c]);

		if (tx_chan[c])
			iio_channel_disable(tx_chan[c]);
	}

	iio_context_destroy(ctx);
}

static int stream_channels_get_enable(const struct iio_device *dev, struct iio_channel **chan,
				      bool tx)
{
	int c;
	const char * const channels[] = {
		"voltage0_i", "voltage0_q", "voltage0", "voltage1"
	};

	for (c = 0; c < 2; c++) {
		const char *str = channels[tx * 2 + c];

		chan[c] = iio_device_find_channel(dev, str, tx);
		if (!chan[c]) {
			error("Could not find %s channel tx=%d\n", str, tx);
			return -ENODEV;
		}

		iio_channel_enable(chan[c]);
	}

	return 0;
}

static void stream(ssize_t rx_sample, ssize_t tx_sample)
{
	const struct iio_channel *rx_i_chan = rx_chan[I_CHAN];
	const struct iio_channel *tx_i_chan = tx_chan[I_CHAN];
	ssize_t nrx = 0;
	ssize_t ntx = 0;

	while (!stop) {
		ssize_t nbytes_rx, nbytes_tx;
		int16_t *p_dat, *p_end;
		ptrdiff_t p_inc;


		nbytes_tx = iio_buffer_push(txbuf);
		if (nbytes_tx < 0) {
			error("Error pushing buf %zd\n", nbytes_tx);
			return;
		}

		nbytes_rx = iio_buffer_refill(rxbuf);
		if (nbytes_rx < 0) {
			error("Error refilling buf %zd\n", nbytes_rx);
			return;
		}

		/* READ: Get pointers to RX buf and read IQ from RX buf port 0 */
		p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);
		for (p_dat = iio_buffer_first(rxbuf, rx_i_chan); p_dat < p_end;
		     p_dat += p_inc / sizeof(*p_dat)) {
			/* Example: swap I and Q */
			int16_t i = p_dat[0];
			int16_t q = p_dat[1];

			p_dat[0] = q;
			p_dat[1] = i;
		}

		/* WRITE: Get pointers to TX buf and write IQ to TX buf port 0 */
		p_inc = iio_buffer_step(txbuf);
		p_end = iio_buffer_end(txbuf);
		for (p_dat = iio_buffer_first(txbuf, tx_i_chan); p_dat < p_end;
		     p_dat += p_inc / sizeof(*p_dat)) {
			p_dat[0] = 0; /* Real (I) */
			p_dat[1] = 0; /* Imag (Q) */
		}

		nrx += nbytes_rx / rx_sample;
		ntx += nbytes_tx / tx_sample;
		info("\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx / 1e6, ntx / 1e6);
	}
}

int main(void)
{
	struct iio_device *tx;
	struct iio_device *rx;
	ssize_t tx_sample_sz, rx_sample_sz;
	int ret;

	if (register_signals() < 0)
		return EXIT_FAILURE;

	ctx = iio_create_default_context();
	if (!ctx) {
		error("Could not create IIO context\n");
		return EXIT_FAILURE;
	}

	ret = configure_tx_lo();
	if (ret)
		goto clean;

	tx = iio_context_find_device(ctx, "axi-adrv9002-tx-lpc");
	if (!tx) {
		ret = EXIT_FAILURE;
		goto clean;
	}

	rx = iio_context_find_device(ctx, "axi-adrv9002-rx-lpc");
	if (!rx) {
		ret = EXIT_FAILURE;
		goto clean;
	}

	ret = stream_channels_get_enable(rx, rx_chan, false);
	if (ret)
		goto clean;

	ret = stream_channels_get_enable(tx, tx_chan, true);
	if (ret)
		goto clean;

	info("* Creating non-cyclic IIO buffers with 1 MiS\n");
	rxbuf = iio_device_create_buffer(rx, 1024 * 1024, false);
	if (!rxbuf) {
		error("Could not create RX buffer: %s\n", strerror(errno));
		ret = EXIT_FAILURE;
		goto clean;
	}

	txbuf = iio_device_create_buffer(tx, 1024 * 1024, false);
	if (!txbuf) {
		error("Could not create TX buffer: %s\n", strerror(errno));
		ret = EXIT_FAILURE;
		goto clean;
	}

	tx_sample_sz = iio_device_get_sample_size(tx);
	rx_sample_sz = iio_device_get_sample_size(rx);

	stream(rx_sample_sz, tx_sample_sz);

clean:
	cleanup();
	return ret;
}
