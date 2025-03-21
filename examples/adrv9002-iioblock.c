#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>

#define BLOCK_SIZE (1 << 20) // 1 MiB

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

static void handle_sig(int sig)
{
	running = false;
}

static int configure_tx_lo(void)
{
	const struct iio_attr *attr;
	struct iio_channel *chan;
	long long val = 2500000000LL; // 2.5 GHz
	int ret;

	chan = iio_device_find_channel(tx, "altvoltage2", true);
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

int main(void)
{
	size_t block_size = BLOCK_SIZE;
	struct iio_block *blocks[4];
	unsigned int i;
	int ret = EXIT_FAILURE;
	int err;

	signal(SIGINT, handle_sig);

	ctx = iio_create_context(NULL, NULL);
	err = iio_err(ctx);
	if (err) {
		fprintf(stderr, "Could not create IIO context\n");
		return EXIT_FAILURE;
	}

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

	rxbuf = iio_device_create_buffer(rx, block_size, rxmask);
	if (iio_err(rxbuf)) {
		rxbuf = NULL;
		fprintf(stderr, "Could not create RX buffer\n");
		goto clean;
	}

	txbuf = iio_device_create_buffer(tx, block_size, txmask);
	if (iio_err(txbuf)) {
		txbuf = NULL;
		fprintf(stderr, "Could not create TX buffer\n");
		goto clean;
	}

	for (i = 0; i < 4; i++) {
		blocks[i] = iio_buffer_create_block(rxbuf, block_size);
		if (iio_err(blocks[i])) {
			fprintf(stderr, "Could not create RX block (%d)\n",
				iio_err(blocks[i]));
			goto clean;
		}

		ret  = iio_block_disable_cpu_access(blocks[i], true);
		if (ret)
			goto clean;

		ret = iio_block_share(txbuf, blocks[i]);
		if (ret)
			goto clean;

		ret = iio_block_enqueue(blocks[i], 0, false);
		if (ret)
			goto clean;
	}

	i = 0;
	while (0) {
		ret = iio_block_dequeue(blocks[i], false);
		if (ret < 0)
			goto clean;

		/* put it on tx */
		ret = iio_block_enqueue_to_buf(txbuf, blocks[i], 0, false);
		if (ret < 0) {
			fprintf(stderr, "Could not enqueue TX block\n");
			goto clean;
		}

		ret = iio_block_dequeue_from_buf(txbuf, blocks[i], false);
		if (ret < 0) {
			fprintf(stderr, "Could not enqueue TX block\n");
			goto clean;
		}

		ret = iio_block_enqueue(blocks[i], 0, false);
		if (ret < 0)
			goto clean;

		i = (i + 1) % 4;
	}


	ret = EXIT_SUCCESS;
clean:
	for (int i = 0; i < 4; i++) {
		if (blocks[i]) {
			iio_block_unshare(txbuf, blocks[i]);
			iio_block_destroy(blocks[i]);
		}
	}
	cleanup();
	return ret;
}
