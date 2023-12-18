// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9371 IIO streaming example
 *
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 * Copyright (C) 2017 Analog Devices Inc.
 **/

#include "iiostream-common.h"

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

/* helper macros */
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

#define IIO_ENSURE(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}

#define BLOCK_SIZE (1024 * 1024)

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
	long long lo_hz; // Local oscillator frequency in Hz
};

/* static scratch mem for strings */
static char tmpstr[64];

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx0_q = NULL;
static struct iio_channel *tx0_i = NULL;
static struct iio_channel *tx0_q = NULL;
static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *txbuf = NULL;
static struct iio_stream  *rxstream = NULL;
static struct iio_stream  *txstream = NULL;
static struct iio_channels_mask *rxmask = NULL;
static struct iio_channels_mask *txmask = NULL;

/* cleanup and exit */
static void shutdown(void)
{
	printf("* Destroying streams\n");
	if (rxstream) {iio_stream_destroy(rxstream); }
	if (txstream) { iio_stream_destroy(txstream); }

	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }
	if (txbuf) { iio_buffer_destroy(txbuf); }

	printf("* Destroying channel masks\n");
	if (rxmask) { iio_channels_mask_destroy(rxmask); }
	if (txmask) { iio_channels_mask_destroy(txmask); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish... Got signal %d\n", sig);
	stop_stream();
}

/* check return value of attr_write function */
static void errchk(int v, const char* what) {
	 if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\nvalue may not be supported.\n", v, what); shutdown(); }
}

/* write attribute: long long int */
static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, what);

	errchk(attr ? iio_attr_write_longlong(attr, val) : -ENOENT, what);
}

/* write attribute: long long int */
static long long rd_ch_lli(struct iio_channel *chn, const char* what)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, what);
	long long val;

	errchk(attr ? iio_attr_read_longlong(attr, &val) : -ENOENT, what);

	printf("\t %s: %lld\n", what, val);
	return val;
}

#if 0
/* write attribute: string */
static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str)
{
	errchk(iio_channel_attr_write(chn, what, str), what);
}
#endif

/* helper function generating channel names */
static char* get_ch_name_mod(const char* type, int id, char modify)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d_%c", type, id, modify);
	return tmpstr;
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns ad9371 phy device */
static struct iio_device* get_ad9371_phy(void)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "ad9371-phy");
	IIO_ENSURE(dev && "No ad9371-phy found");
	return dev;
}

/* finds AD9371 streaming IIO devices */
static bool get_ad9371_stream_dev(enum iodev d, struct iio_device **dev)
{
	switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "axi-ad9371-tx-hpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "axi-ad9371-rx-hpc");  return *dev != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9371 streaming IIO channels */
static bool get_ad9371_stream_ch(enum iodev d, struct iio_device *dev, int chid, char modify, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, modify ? get_ch_name_mod("voltage", chid, modify) : get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, modify ? get_ch_name_mod("voltage", chid, modify) : get_ch_name("voltage", chid), d == TX);
	return *chn != NULL;
}

/* finds AD9371 phy IIO configuration channel with id chid */
static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_ad9371_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9371_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9371 local oscillator IIO configuration channels */
static bool get_lo_chan(enum iodev d, struct iio_channel **chn)
{
	switch (d) {
	 // LO chan is always output, i.e. true
	case RX: *chn = iio_device_find_channel(get_ad9371_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9371_phy(), get_ch_name("altvoltage", 1), true); return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* applies streaming configuration through IIO */
bool cfg_ad9371_streaming_ch(struct stream_cfg *cfg, enum iodev type, int chid)
{
	struct iio_channel *chn = NULL;

	// Configure phy and lo channels
	printf("* Acquiring AD9371 phy %s channel %d\n", type == TX ? "TX" : "RX", chid);
	if (!get_phy_chan(type, chid, &chn)) {	return false; }

	rd_ch_lli(chn, "rf_bandwidth");
	rd_ch_lli(chn, "sampling_frequency");

	// Configure LO channel
	printf("* Acquiring AD9371 %s lo channel\n", type == TX ? "TX" : "RX");
	if (!get_lo_chan(type, &chn)) { return false; }
	wr_ch_lli(chn, type == TX ? "TX_LO_frequency" : "RX_LO_frequency" , cfg->lo_hz);
	return true;
}

/* simple configuration and streaming */
int main (__notused int argc, __notused char **argv)
{
	// Streaming devices
	struct iio_device *tx;
	struct iio_device *rx;

	// RX and TX sample counters
	size_t nrx = 0;
	size_t ntx = 0;

	// RX and TX sample size
	size_t rx_sample_sz, tx_sample_sz;

	// Stream configurations
	struct stream_cfg rxcfg;
	struct stream_cfg txcfg;

	int err;

	// Listen to ctrl+c and IIO_ENSURE
	signal(SIGINT, handle_sig);

	// RX stream config
	rxcfg.lo_hz = GHZ(2.5); // 2.5 GHz rf frequency

	// TX stream config
	txcfg.lo_hz = GHZ(2.5); // 2.5 GHz rf frequency

	printf("* Acquiring IIO context\n");
	IIO_ENSURE((ctx = iio_create_context(NULL, NULL)) && "No context");
	IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

	printf("* Acquiring AD9371 streaming devices\n");
	IIO_ENSURE(get_ad9371_stream_dev(TX, &tx) && "No tx dev found");
	IIO_ENSURE(get_ad9371_stream_dev(RX, &rx) && "No rx dev found");

	printf("* Configuring AD9371 for streaming\n");
	IIO_ENSURE(cfg_ad9371_streaming_ch(&rxcfg, RX, 0) && "RX port 0 not found");
	IIO_ENSURE(cfg_ad9371_streaming_ch(&txcfg, TX, 0) && "TX port 0 not found");

	printf("* Initializing AD9371 IIO streaming channels\n");
	IIO_ENSURE(get_ad9371_stream_ch(RX, rx, 0, 'i', &rx0_i) && "RX chan i not found");
	IIO_ENSURE(get_ad9371_stream_ch(RX, rx, 0, 'q', &rx0_q) && "RX chan q not found");
	IIO_ENSURE(get_ad9371_stream_ch(TX, tx, 0, 0, &tx0_i) && "TX chan i not found");
	IIO_ENSURE(get_ad9371_stream_ch(TX, tx, 1, 0, &tx0_q) && "TX chan q not found");

	rxmask = iio_create_channels_mask(iio_device_get_channels_count(rx));
	if (!rxmask) {
		fprintf(stderr, "Unable to alloc channels mask\n");
		shutdown();
	}

	txmask = iio_create_channels_mask(iio_device_get_channels_count(tx));
	if (!txmask) {
		fprintf(stderr, "Unable to alloc channels mask\n");
		shutdown();
	}

	printf("* Enabling IIO streaming channels\n");
	iio_channel_enable(rx0_i, rxmask);
	iio_channel_enable(rx0_q, rxmask);
	iio_channel_enable(tx0_i, txmask);
	iio_channel_enable(tx0_q, txmask);

	printf("* Creating non-cyclic IIO buffers with 1 MiS\n");
	rxbuf = iio_device_create_buffer(rx, 0, rxmask);
	err = iio_err(rxbuf);
	if (err) {
		rxbuf = NULL;
		dev_perror(rx, err, "Could not create RX buffer");
		shutdown();
	}
	txbuf = iio_device_create_buffer(tx, 0, txmask);
	err = iio_err(txbuf);
	if (err) {
		txbuf = NULL;
		dev_perror(tx, err, "Could not create TX buffer");
		shutdown();
	}

	rxstream = iio_buffer_create_stream(rxbuf, 4, BLOCK_SIZE);
	err = iio_err(rxstream);
	if (err) {
		rxstream = NULL;
		dev_perror(rx, iio_err(rxstream), "Could not create RX stream");
		shutdown();
	}

	txstream = iio_buffer_create_stream(txbuf, 4, BLOCK_SIZE);
	err = iio_err(txstream);
	if (err) {
		txstream = NULL;
		dev_perror(tx, iio_err(txstream), "Could not create TX stream");
		shutdown();
	}

	rx_sample_sz = iio_device_get_sample_size(rx, rxmask);
	tx_sample_sz = iio_device_get_sample_size(tx, txmask);

	printf("* Starting IO streaming (press CTRL+C to cancel)\n");
	stream(rx_sample_sz, tx_sample_sz, BLOCK_SIZE,
	       rxstream, txstream, rx0_i, tx0_i);
	shutdown();

	return 0;
}
