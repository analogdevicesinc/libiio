// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO block example
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Dan Nechita <dan.nechita@analog.com>
 **/

#include <errno.h>
#include <getopt.h>
#include <iio/iio-debug.h>
#include <iio/iio.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define error(...) \
	do { \
		fprintf(stderr, "%s, %d: ERROR: ", __func__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
	} while (0)

#define info(...) \
	do { \
		printf("%s, %d: INFO: ", __func__, __LINE__); \
		printf(__VA_ARGS__); \
	} while (0)

/* helper macros */
#define MHZ(x) ((long long)((x) * 1000000.0 + .5))
#define GHZ(x) ((long long)((x) * 1000000000.0 + .5))

/* size of each block in bytes */
#define BLOCK_SIZE (1024 * 1024)

/* how many blocks are kept in flight per direction */
#define NB_BLOCKS 4

/* number of channels (I and Q) used per streaming device */
#define NB_CHANNELS 2

/* Indices into the rx_chan / tx_chan arrays below */
enum {
	I_CHAN,
	Q_CHAN,
};

/* Offsets of the I and Q samples within one interleaved sample pair */
enum {
	I_OFF,
	Q_OFF,
};

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
	long long bw_hz;    /* Analog bandwidth in Hz */
	long long fs_hz;    /* Baseband sample rate in Hz */
	long long lo_hz;    /* Local oscillator frequency in Hz */
	const char *rfport; /* Port name */
};

/* IIO structs required for streaming */
static struct iio_context *ctx = NULL;
static struct iio_buffer_stream *rxbuf_stream = NULL;
static struct iio_buffer_stream *txbuf_stream = NULL;
static struct iio_block *rxblocks[NB_BLOCKS];
static struct iio_block *txblocks[NB_BLOCKS];
static struct iio_channels_mask *rxmask = NULL;
static struct iio_channels_mask *txmask = NULL;
/* I and Q channel of each streaming device, indexed with I_CHAN / Q_CHAN */
static struct iio_channel *rx_chan[NB_CHANNELS] = { NULL, NULL };
static struct iio_channel *tx_chan[NB_CHANNELS] = { NULL, NULL };
static bool rx_started = false;
static bool tx_started = false;

static volatile sig_atomic_t stop = false;

static void stop_stream(void)
{
	stop = true;
}

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI sig_handler(DWORD dwCtrlType)
{
	/* Runs in its own thread */
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		stop_stream();
		return TRUE;
	default:
		return FALSE;
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
	if (signum == SIGINT || signum == SIGTERM)
		stop_stream();
}

static int register_signals(void)
{
	struct sigaction sa = { 0 };
	sigset_t mask;

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

/* Returns the ad9361 phy device */
static struct iio_device *get_ad9361_phy(void)
{
	return iio_context_find_device(ctx, "ad9361-phy");
}

/* Finds the AD9361 phy IIO configuration channel with id chid */
static struct iio_channel *get_phy_chan(enum iodev d, unsigned int chid)
{
	struct iio_device *phy = get_ad9361_phy();
	char name[20];

	if (!phy)
		return NULL;

	snprintf(name, sizeof(name), "voltage%u", chid);

	return iio_device_find_channel(phy, name, d == TX);
}

/* Finds the AD9361 local oscillator IIO configuration channel */
static struct iio_channel *get_lo_chan(enum iodev d)
{
	struct iio_device *phy = get_ad9361_phy();

	if (!phy)
		return NULL;

	/* the LO channels are always output, i.e. true */
	return iio_device_find_channel(phy, d == RX ? "altvoltage0" : "altvoltage1", true);
}

/* Writes a long long attribute, then reads it back. */
static int write_then_read_chn_lli(struct iio_channel *chn, const char *what, long long val)
{
	const struct iio_attr *attr;
	long long actual;
	int ret;

	attr = iio_channel_find_attr(chn, what);
	if (!attr) {
		error("Could not find attribute %s\n", what);
		return -ENOENT;
	}

	ret = iio_attr_write_longlong(attr, val);
	if (ret) {
		error("Could not write %lld to %s: %d\n", val, what, ret);
		return ret;
	}

	ret = iio_attr_read_longlong(attr, &actual);
	if (ret) {
		error("Could not read back %s: %d\n", what, ret);
		return ret;
	}

	info("\t%s: requested %lld, got %lld\n", what, val, actual);

	return 0;
}

/* Same as write_then_read_chn_lli(), for string attributes */
static int write_then_read_chn_str(struct iio_channel *chn, const char *what, const char *val)
{
	const struct iio_attr *attr;
	char actual[64];
	ssize_t sret;

	attr = iio_channel_find_attr(chn, what);
	if (!attr) {
		error("Could not find attribute %s\n", what);
		return -ENOENT;
	}

	sret = iio_attr_write_string(attr, val);
	if (sret < 0) {
		error("Could not write %s to %s: %zd\n", val, what, sret);
		return (int)sret;
	}

	sret = iio_attr_read_raw(attr, actual, sizeof(actual) - 1);
	if (sret < 0) {
		error("Could not read back %s: %zd\n", what, sret);
		return (int)sret;
	}

	/* Don't rely on the backend to NUL-terminate what it read */
	actual[sret] = '\0';

	info("\t%s: requested %s, got %s\n", what, val, actual);

	return 0;
}

/* Applies one direction of the streaming configuration through the ad9361-phy device */
static int cfg_ad9361_streaming_ch(const struct stream_cfg *cfg, enum iodev d, unsigned int chid)
{
	const char *dir = d == TX ? "TX" : "RX";
	struct iio_channel *chn;
	int ret;

	info("* Configuring AD9361 %s phy channel %u\n", dir, chid);
	chn = get_phy_chan(d, chid);
	if (!chn) {
		error("Could not find %s phy channel %u\n", dir, chid);
		return -ENODEV;
	}

	ret = write_then_read_chn_str(chn, "rf_port_select", cfg->rfport);
	if (ret)
		return ret;

	ret = write_then_read_chn_lli(chn, "rf_bandwidth", cfg->bw_hz);
	if (ret)
		return ret;

	ret = write_then_read_chn_lli(chn, "sampling_frequency", cfg->fs_hz);
	if (ret)
		return ret;

	info("* Configuring AD9361 %s lo channel\n", dir);
	chn = get_lo_chan(d);
	if (!chn) {
		error("Could not find %s lo channel\n", dir);
		return -ENODEV;
	}

	return write_then_read_chn_lli(chn, "frequency", cfg->lo_hz);
}

/* Finds the AD9361 streaming IIO devices */
static struct iio_device *get_ad9361_stream_dev(enum iodev d)
{
	switch (d) {
	case RX:
		return iio_context_find_device(ctx, "cf-ad9361-lpc");
	case TX:
		return iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
	default:
		return NULL;
	}
}

/* Finds the I and Q streaming channels of one AD9361 streaming device */
static int get_ad9361_stream_channels(
		const struct iio_device *dev, enum iodev d, struct iio_channel **chan)
{
	unsigned int i;
	char name[20];

	for (i = 0; i < NB_CHANNELS; i++) {
		/* voltage0 carries I, voltage1 carries Q */
		snprintf(name, sizeof(name), "voltage%u", i);

		chan[i] = iio_device_find_channel(dev, name, d == TX);
		if (!chan[i]) {
			error("Could not find %s channel %s\n", d == TX ? "TX" : "RX", name);
			return -ENODEV;
		}
	}

	return 0;
}

/* Allocates a channels mask for dev with the given channels enabled in it */
static struct iio_channels_mask *create_enabled_mask(
		const struct iio_device *dev, struct iio_channel **chan)
{
	struct iio_channels_mask *mask;
	unsigned int i;

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!mask)
		return NULL;

	for (i = 0; i < NB_CHANNELS; i++)
		iio_channel_enable(chan[i], mask);

	return mask;
}

/* Creates the blocks that will be handed back and forth with the hardware */
static int create_blocks(struct iio_buffer_stream *buf_stream, struct iio_block **blocks)
{
	unsigned int i;
	int err;

	for (i = 0; i < NB_BLOCKS; i++) {
		blocks[i] = iio_buffer_stream_create_block(buf_stream, BLOCK_SIZE);

		err = iio_err(blocks[i]);
		if (err) {
			blocks[i] = NULL;
			return err;
		}
	}

	return 0;
}

/*
 * Walks one RX block sample by sample. iio_block_first() gives the address of the first
 * sample of the given channel and iio_block_end() the address just past the last one; the
 * step between two samples is the size of one sample of the whole enabled set, because the
 * channels are interleaved. I and Q of the same sample are therefore adjacent.
 */
static void swap_iq(const struct iio_block *block, const struct iio_channel *chn, ptrdiff_t p_inc)
{
	const ptrdiff_t step = p_inc / (ptrdiff_t)sizeof(int16_t);
	int16_t *p_dat, *p_end;

	p_end = iio_block_end(block);

	for (p_dat = iio_block_first(block, chn); p_dat < p_end; p_dat += step) {
		/* Example: swap I and Q */
		int16_t i = p_dat[I_OFF];
		int16_t q = p_dat[Q_OFF];

		p_dat[I_OFF] = q;
		p_dat[Q_OFF] = i;
	}
}

static ssize_t zero_sample(
		__notused const struct iio_channel *chn, void *dst, size_t bytes, __notused void *d)
{
	memset(dst, 0, bytes);

	return (ssize_t)bytes;
}

/*
 * Fills one TX block with zeros. This is the other way of walking a block: instead of
 * computing addresses, iio_block_foreach_sample() visits every sample
 * of every channel in the mask and calls zero_sample() on it.
 */
static int fill_tx_block(const struct iio_block *block, const struct iio_channels_mask *mask)
{
	ssize_t sret;

	sret = iio_block_foreach_sample(block, mask, zero_sample, NULL);
	if (sret < 0)
		return (int)sret;

	return 0;
}

static void destroy_blocks(struct iio_block **blocks)
{
	unsigned int i;

	/*
	 * Dequeue before destroying: a block that is still in flight belongs to the
	 * hardware. If it was never enqueued the dequeue just fails, which is harmless.
	 */
	for (i = 0; i < NB_BLOCKS; i++) {
		if (blocks[i]) {
			iio_block_dequeue(blocks[i], false);
			iio_block_destroy(blocks[i]);
		}
	}
}

static void cleanup(void)
{
	/*
	 * A stream that never started will never hand its blocks back, so the blocking
	 * dequeues in destroy_blocks() below would wait for nothing. Cancel those, but
	 * leave a running stream alone: its blocks are still on their way back, and
	 * cancelling would tear down the transport that delivers them.
	 */
	if (rxbuf_stream && !rx_started)
		iio_buffer_stream_cancel(rxbuf_stream);
	if (txbuf_stream && !tx_started)
		iio_buffer_stream_cancel(txbuf_stream);

	info("* Destroying blocks\n");
	destroy_blocks(rxblocks);
	destroy_blocks(txblocks);

	info("* Stopping and closing the buffer streams\n");
	if (rx_started)
		iio_buffer_stream_stop(rxbuf_stream);
	if (tx_started)
		iio_buffer_stream_stop(txbuf_stream);

	if (rxbuf_stream)
		iio_buffer_close(rxbuf_stream);
	if (txbuf_stream)
		iio_buffer_close(txbuf_stream);

	info("* Destroying channel masks\n");
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	if (txmask)
		iio_channels_mask_destroy(txmask);

	info("* Destroying context\n");
	iio_context_destroy(ctx);
}

static void usage(const char *argv0)
{
	printf("Usage: %s [-h] [<uri>]\n", argv0);
	printf("  -h\tprint this help\n");
	printf("\nStreams continuously in both directions with the low level block API,\n"
	       "keeping %d blocks of %d bytes in flight per direction. Received samples get\n"
	       "their I and Q swapped, transmitted samples are set to zero. Press CTRL+C to\n"
	       "stop.\n",
			NB_BLOCKS, BLOCK_SIZE);
	printf("\nWithout a uri the default context is used, which assumes the program runs on\n"
	       "the platform the AD9361 is connected to. Otherwise pass a uri as printed by\n"
	       "`iio_info -s`, for example usb:3.32.5 or ip:192.168.2.1\n");
}

/*
 * Hands back the uri to connect to, NULL meaning the default context. Returns 0 to carry
 * on, 1 once the help has been printed, or a negative error code.
 */
static int parse_options(int argc, char **argv, const char **uri)
{
	int c;

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			return 1;
		default:
			usage(argv[0]);
			return -EINVAL;
		}
	}

	if (argc - optind > 1) {
		error("At most one uri may be given\n");
		usage(argv[0]);
		return -EINVAL;
	}

	*uri = optind < argc ? argv[optind] : NULL;

	return 0;
}

int main(int argc, char **argv)
{
	/* streaming devices */
	struct iio_device *rx;
	struct iio_device *tx;

	/* the buffers the streams are opened on; owned by the devices, not freed here */
	struct iio_buffer *rxbuf;
	struct iio_buffer *txbuf;

	/* RX and TX stream configurations */
	struct stream_cfg rxcfg;
	struct stream_cfg txcfg;

	ssize_t rx_sample;
	ssize_t tx_sample;
	size_t nrx = 0;
	size_t ntx = 0;
	const char *uri = NULL;
	unsigned int i;
	int ret = EXIT_FAILURE;
	int err;

	err = parse_options(argc, argv, &uri);
	if (err)
		return err > 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	if (register_signals() < 0)
		return EXIT_FAILURE;

	/* RX stream config */
	rxcfg.bw_hz = MHZ(6.4);	     /* 6.4 MHz rf bandwidth (0.8 * fs) */
	rxcfg.fs_hz = MHZ(8);	     /* 8 MS/s rx sample rate */
	rxcfg.lo_hz = GHZ(2.5);	     /* 2.5 GHz rf frequency */
	rxcfg.rfport = "A_BALANCED"; /* port A (select for rf freq.) */

	/* TX stream config */
	txcfg.bw_hz = MHZ(6.4); /* 6.4 MHz rf bandwidth (0.8 * fs) */
	txcfg.fs_hz = MHZ(8);	/* 8 MS/s tx sample rate */
	txcfg.lo_hz = GHZ(2.5); /* 2.5 GHz rf frequency */
	txcfg.rfport = "A";	/* port A (select for rf freq.) */

	info("* Acquiring IIO context\n");
	ctx = iio_create_context(NULL, uri);

	err = iio_err(ctx);
	if (err) {
		ctx = NULL;
		error("Could not create IIO context\n");
		return EXIT_FAILURE;
	}

	if (!get_ad9361_phy()) {
		error("No ad9361-phy device found\n");
		goto clean;
	}

	err = cfg_ad9361_streaming_ch(&rxcfg, RX, 0);
	if (err)
		goto clean;

	err = cfg_ad9361_streaming_ch(&txcfg, TX, 0);
	if (err)
		goto clean;

	info("* Acquiring AD9361 streaming devices\n");
	rx = get_ad9361_stream_dev(RX);
	if (!rx) {
		error("No RX device (cf-ad9361-lpc) found\n");
		goto clean;
	}

	tx = get_ad9361_stream_dev(TX);
	if (!tx) {
		error("No TX device (cf-ad9361-dds-core-lpc) found\n");
		goto clean;
	}

	info("* Acquiring AD9361 streaming channels\n");
	err = get_ad9361_stream_channels(rx, RX, rx_chan);
	if (err)
		goto clean;

	err = get_ad9361_stream_channels(tx, TX, tx_chan);
	if (err)
		goto clean;

	info("* Enabling AD9361 streaming channels\n");
	rxmask = create_enabled_mask(rx, rx_chan);
	if (!rxmask) {
		error("Could not create the RX channels mask\n");
		goto clean;
	}

	txmask = create_enabled_mask(tx, tx_chan);
	if (!txmask) {
		error("Could not create the TX channels mask\n");
		goto clean;
	}

	rx_sample = iio_device_get_sample_size(rx, rxmask);
	tx_sample = iio_device_get_sample_size(tx, txmask);
	if (rx_sample <= 0 || tx_sample <= 0) {
		error("Could not get the sample size\n");
		goto clean;
	}

	info("* Acquiring AD9361 buffers\n");
	rxbuf = iio_device_get_buffer(rx, 0);
	if (!rxbuf) {
		dev_perror(rx, -ENODEV, "Could not get the RX buffer");
		goto clean;
	}

	txbuf = iio_device_get_buffer(tx, 0);
	if (!txbuf) {
		dev_perror(tx, -ENODEV, "Could not get the TX buffer");
		goto clean;
	}

	/* A buffer stream is what blocks are created from. */
	info("* Opening the buffer streams\n");
	rxbuf_stream = iio_buffer_open(rxbuf, rxmask);
	err = iio_err(rxbuf_stream);
	if (err) {
		rxbuf_stream = NULL;
		dev_perror(rx, err, "Could not open the RX buffer stream");
		goto clean;
	}

	txbuf_stream = iio_buffer_open(txbuf, txmask);
	err = iio_err(txbuf_stream);
	if (err) {
		txbuf_stream = NULL;
		dev_perror(tx, err, "Could not open the TX buffer stream");
		goto clean;
	}

	info("* Creating %d block(s) of %d bytes per direction\n", NB_BLOCKS, BLOCK_SIZE);
	err = create_blocks(rxbuf_stream, rxblocks);
	if (err) {
		dev_perror(rx, err, "Could not create the RX blocks");
		goto clean;
	}

	err = create_blocks(txbuf_stream, txblocks);
	if (err) {
		dev_perror(tx, err, "Could not create the TX blocks");
		goto clean;
	}

	/*
	 * Hand every block over before starting, so that neither direction is left waiting
	 * on this program the moment the streams come up: the RX DMA already has somewhere
	 * to write and the TX DMA already has something to send. Passing bytes_used = 0
	 * means the whole block.
	 */
	info("* Enqueueing the initial blocks\n");
	for (i = 0; i < NB_BLOCKS; i++) {
		err = iio_block_enqueue(rxblocks[i], 0, false);
		if (err < 0) {
			dev_perror(rx, err, "Could not enqueue an RX block");
			goto clean;
		}

		err = fill_tx_block(txblocks[i], txmask);
		if (err) {
			dev_perror(tx, err, "Could not fill a TX block");
			goto clean;
		}

		err = iio_block_enqueue(txblocks[i], 0, false);
		if (err < 0) {
			dev_perror(tx, err, "Could not enqueue a TX block");
			goto clean;
		}
	}

	err = iio_buffer_stream_start(rxbuf_stream);
	if (err) {
		dev_perror(rx, err, "Could not start the RX stream");
		goto clean;
	}

	rx_started = true;

	err = iio_buffer_stream_start(txbuf_stream);
	if (err) {
		dev_perror(tx, err, "Could not start the TX stream");
		goto clean;
	}

	tx_started = true;

	info("* Starting IO streaming using blocks (press CTRL+C to cancel)\n");

	for (i = 0; !stop; i = (i + 1) % NB_BLOCKS) {
		/* Wait for the hardware to hand back the oldest block of each direction */
		err = iio_block_dequeue(rxblocks[i], false);
		if (err < 0) {
			dev_perror(rx, err, "Could not dequeue an RX block");
			break;
		}

		err = iio_block_dequeue(txblocks[i], false);
		if (err < 0) {
			dev_perror(tx, err, "Could not dequeue a TX block");
			break;
		}

		/* READ: swap I and Q of every received sample, in place */
		swap_iq(rxblocks[i], rx_chan[I_CHAN], rx_sample);

		/* WRITE: transmit zeros */
		err = fill_tx_block(txblocks[i], txmask);
		if (err) {
			dev_perror(tx, err, "Could not fill a TX block");
			break;
		}

		/* Hand both RX and TX blocks back to hardware */
		err = iio_block_enqueue(rxblocks[i], 0, false);
		if (err < 0) {
			dev_perror(rx, err, "Could not re-enqueue an RX block");
			break;
		}

		err = iio_block_enqueue(txblocks[i], 0, false);
		if (err < 0) {
			dev_perror(tx, err, "Could not re-enqueue a TX block");
			break;
		}

		nrx += BLOCK_SIZE / (size_t)rx_sample;
		ntx += BLOCK_SIZE / (size_t)tx_sample;
		info("\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx / 1e6, ntx / 1e6);
	}

	if (err >= 0)
		ret = EXIT_SUCCESS;
clean:
	cleanup();

	info("Exit....\n");

	return ret;
}
