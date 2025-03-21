#include "iio-private.h"
#include "iio/iio-backend.h"
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#define BLOCK_SIZE (1 << 20) // 1 MiB
#define N_BLOCKS 8

static struct iio_context *ctx;
static struct iio_device *tx;
static struct iio_device *rx;
static struct iio_buffer *rxbuf;
static struct iio_buffer *txbuf;
static struct iio_channels_mask *rxmask;
static struct iio_channels_mask *txmask;
static struct iio_channel *rx_chan[2];
static struct iio_channel *tx_chan[2];
static volatile bool running = true;

static void cleanup(void)
{
	if (rxbuf) {
		iio_buffer_disable(rxbuf);
		iio_buffer_destroy(rxbuf);
	}
	if (txbuf) {
		iio_buffer_disable(txbuf);
		iio_buffer_destroy(txbuf);
	}
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	if (txmask)
		iio_channels_mask_destroy(txmask);
	iio_context_destroy(ctx);
}

static void handle_sig(int sig)
{
	running = false;
}

static int configure_tx_lo(void)
{
	struct iio_device *phy;
	const struct iio_attr *attr;
	struct iio_channel *chan;
	long long val = 2400000000LL; // 2.4 GHz
	int ret;

	phy = iio_context_find_device(ctx, "adrv9002-phy");
	if (!phy) {
		fprintf(stderr, "Could not find adrv9002_phy\n");
		return -ENODEV;
	}

	chan = iio_device_find_channel(phy, "altvoltage2", true);
	if (!chan) {
		fprintf(stderr, "Could not find TX LO channel\n");
		return -ENODEV;
	}

	attr = iio_channel_find_attr(chan, "TX1_LO_frequency");
	if (attr)
		ret = iio_attr_write_longlong(attr, val);
	else
		ret = -ENOENT;
	return ret;
}

static struct iio_channels_mask *
stream_channels_get_mask(const struct iio_device *dev, struct iio_channel **chan, bool tx)
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
			fprintf(stderr, "Could not find %s channel tx=%d\n", str, tx);
			return iio_ptr(-ENODEV);
		}

		iio_channel_enable(chan[c], mask);
	}

	return mask;
}

static int test_stream(size_t block_sz, size_t rx_sample)
{
	const struct iio_block *rxblock;
	struct iio_stream *rxstream;
	ssize_t nrx = 0;
	int err;

	rxstream = iio_buffer_create_stream(rxbuf, 4, block_sz);
	if (iio_err(rxstream)) {
		rxstream = NULL;
		ctx_perror(ctx, iio_err(rxstream), "Could not create RX stream");
		exit(1);
	}

	while (running) {
		rxblock = iio_stream_get_next_block(rxstream);
		err = iio_err(rxblock);
		if (err) {
			ctx_perror(ctx, err, "Unable to receive block");
			exit(1);
		}

		nrx += block_sz / rx_sample;
		ctx_info(ctx, "\tRX %8.2f MSmp\n", nrx / 1e6);
	}
}

int main(void)
{
	size_t block_size = BLOCK_SIZE, sample_size;
	struct iio_block *blocks[N_BLOCKS] = { NULL };
	unsigned int rx_push, rx_pop = 0, tx_pop = 0;
	bool tx_en = false, start_tx = false;
	int ret = EXIT_FAILURE;
	int err;
	ssize_t nrx = 0, ntx = 0;

	signal(SIGINT, handle_sig);

	ctx = iio_create_context(NULL, NULL);
	err = iio_err(ctx);
	if (err) {
		fprintf(stderr, "Could not create IIO context\n");
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

	rxbuf = iio_device_create_buffer(rx, NULL, rxmask);
	if (iio_err(rxbuf)) {
		rxbuf = NULL;
		fprintf(stderr, "Could not create RX buffer\n");
		goto clean;
	}

	txbuf = iio_device_create_buffer(tx, NULL, txmask);
	if (iio_err(txbuf)) {
		txbuf = NULL;
		fprintf(stderr, "Could not create TX buffer\n");
		goto clean;
	}

	sample_size = iio_device_get_sample_size(rx, rxmask);
	block_size = sample_size * block_size;
	printf("Block size: %zu\n", block_size);

	for (unsigned int i = 0; i < N_BLOCKS; i++) {
		blocks[i] = iio_buffer_create_block(rxbuf, block_size);
		if (iio_err(blocks[i])) {
			fprintf(stderr, "Could not create RX block (%d)\n",
				iio_err(blocks[i]));
			goto clean;
		}

		/*ret  = iio_block_disable_cpu_access(blocks[i], true);
		if (ret) {
			printf("Could not disable CPU access (%d)\n", ret);
			goto clean;
		}*/

		ret = iio_block_share(txbuf, blocks[i]);
		if (ret) {
			fprintf(stderr, "Could not share RX block, ret=%d\n", ret);
			goto clean;
		}

	}

	for (rx_push = 0; rx_push < N_BLOCKS / 4; rx_push++) {
		ret = iio_block_enqueue(blocks[rx_push], 0, false);
		if (ret < 0) {
			fprintf(stderr, "%d: Could not enqueue RX block(ret=%d)\n", rx_push, ret);
			goto clean;
		}
	}

	ret = iio_buffer_enable(rxbuf);
	if (ret) {
		dev_perror(tx, ret, "Could not enable RX buffer\n");
		goto clean;
	}

	while (running) {
		ret = iio_block_enqueue(blocks[rx_push], 0, false);
		if (ret < 0) {
			fprintf(stderr, "%d: Could not enqueue RX block(ret=%d)\n", rx_push, ret);
			goto clean;
		}

		ret = iio_block_dequeue(blocks[rx_pop], false);
		if (ret < 0) {
			fprintf(stderr, "%d: Could not dequeue RX block(ret=%d)\n", rx_pop, ret);
			goto clean;
		}

		nrx += block_size / sample_size;
		ctx_info(ctx, "\tRX %8.2f MSmp\n", nrx / 1e6);

		/* put it on tx */
		ret = iio_block_enqueue_to_buf(txbuf, blocks[rx_pop], 0, false);
		if (ret < 0) {
			fprintf(stderr, "%d: Could not enqueue TX block(ret=%d)\n", rx_pop, ret);
			goto clean;
		}

		start_tx |= rx_pop == N_BLOCKS / 2 - 1;
		if (!start_tx)
			goto update_cntrs;

		if (!tx_en) {
			ret = iio_buffer_enable(txbuf);
			if (ret) {
				dev_perror(tx, ret, "Could not enable TX buffer");
				goto clean;
			}

			tx_en = true;
		}

		ret = iio_block_dequeue_from_buf(txbuf, blocks[tx_pop], false);
		if (ret < 0) {
			fprintf(stderr, "%d: Could not dequeue TX block(ret=%d)\n", tx_pop,
				ret);
			goto clean;
		}

		ntx += block_size / sample_size;
		ctx_info(ctx, "\tTX %8.2f MSmp\n", ntx / 1e6);

		tx_pop = (tx_pop + 1) % N_BLOCKS;
update_cntrs:
		rx_pop = (rx_pop + 1) % N_BLOCKS;
		rx_push = (rx_push + 1) % N_BLOCKS;
	}


	ret = EXIT_SUCCESS;
clean:
	for (int i = 0; i < 4; i++) {
		if (blocks[i])
			iio_block_destroy(blocks[i]);
	}
	cleanup();
	return ret;
}
