// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_stream_test - Data capture utility for testing streaming with small buffers
 *
 * This utility captures streaming data using small buffer sizes for evaluation.
 * Supports three test modes:
 *   1. RX only with 128 complex samples buffer
 *   2. RX only with 256 complex samples buffer
 *   3. Simultaneous RX/TX with 128 complex samples buffer
 *
 * Captured data is saved to file for offline analysis.
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "iio_common.h"

#define MY_NAME "iio_stream_test"

/* Default configuration */
#define DEFAULT_SAMPLE_RATE_HZ 8000000  /* 8 Msps */
#define DEFAULT_DURATION_SAMPLES (8 * 1024 * 1024)  /* 1 second at 8 Msps */
#define BUFFER_SIZE_128 128  /* 128 complex samples */
#define BUFFER_SIZE_256 256  /* 256 complex samples */
#define BYTES_PER_COMPLEX_SAMPLE 4  /* I and Q are int16_t each */
#define DEFAULT_PIPELINE_DEPTH 4  /* Number of in-flight blocks in the RX stream */
#define MAX_LATENCY_SAMPLES (16 * 1024 * 1024)  /* Safety cap, ~128 MiB of uint64_t */

/* Test modes */
enum test_mode {
	MODE_RX_128,
	MODE_RX_256,
	MODE_RXTX_128,
};

/* Per-block RX latency statistics, computed from a bounded sample buffer */
struct block_latency_stats {
	uint64_t *samples_us;   /* Array of per-block latency deltas, in microseconds */
	uint64_t count;         /* Number of latency samples actually recorded */
	uint64_t capacity;      /* Allocated capacity of samples_us */
};

static const struct option options[] = {
	{ "ip", required_argument, 0, 'i' },
	{ "mode", required_argument, 0, 'm' },
	{ "samples", required_argument, 0, 's' },
	{ "output", required_argument, 0, 'o' },
	{ "rate", required_argument, 0, 'r' },
	{ "loopback", no_argument, 0, 'l' },
	{ "num-blocks", required_argument, 0, 'n' },
	{ "quiet", no_argument, 0, 'q' },
	{ 0, 0, 0, 0 },
};

static const char *options_descriptions[] = {
	"[-i <ip>] [-m <mode>] [-s <samples>] [-o <file>] [-r <rate>] [-l] [-n <blocks>] [-q]",
	"IP address/hostname of the remote board, or a full IIO URI "
	"(e.g. 'local:', 'usb:1.5.5') to use a different backend (required).",
	"Test mode: rx128, rx256, rxtx128 (default: rx128).",
	"Number of samples to capture (default: 8M).",
	"Output file for captured data (default: capture_<mode>.bin).",
	"Sample rate in Hz (default: 8000000).",
	"Enable digital loopback mode (default: disabled).",
	"RX stream pipeline depth / number of in-flight blocks, RX modes only (default: 4).",
	"Suppress progress output (recommended for timing-sensitive runs, e.g. under 'time').",
};

/* Global state */
static struct iio_context *ctx = NULL;
static struct iio_device *rx_dev = NULL;
static struct iio_device *tx_dev = NULL;
static struct iio_device *phy_dev = NULL;
static volatile sig_atomic_t app_running = true;
static int exit_code = EXIT_SUCCESS;

/* Signal handler */
static void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
}

#ifdef _WIN32
BOOL WINAPI sig_handler_fn(DWORD dwCtrlType)
{
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		quit_all(SIGTERM);
		return TRUE;
	default:
		return FALSE;
	}
}

static void setup_sig_handler(void)
{
	SetConsoleCtrlHandler(sig_handler_fn, TRUE);
}
#else
static void sig_handler(int sig)
{
	quit_all(sig);
}

static void setup_sig_handler(void)
{
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
}
#endif

/* Find and configure devices */
static int find_devices(void)
{
	/* Find RX device (cf-ad9361-lpc) */
	rx_dev = iio_context_find_device(ctx, "cf-ad9361-lpc");
	if (!rx_dev) {
		fprintf(stderr, "Error: RX device 'cf-ad9361-lpc' not found\n");
		return -ENODEV;
	}

	/* Find TX device (cf-ad9361-dds-core-lpc) */
	tx_dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
	if (!tx_dev) {
		fprintf(stderr, "Error: TX device 'cf-ad9361-dds-core-lpc' not found\n");
		return -ENODEV;
	}

	/* Find PHY device (ad9361-phy) */
	phy_dev = iio_context_find_device(ctx, "ad9361-phy");
	if (!phy_dev) {
		fprintf(stderr, "Error: PHY device 'ad9361-phy' not found\n");
		return -ENODEV;
	}

	return 0;
}

/* Enable or disable digital loopback */
static int set_digital_loopback(bool enable)
{
	const struct iio_attr *attr;
	ssize_t ret;

	/* Find the loopback debug attribute */
	attr = iio_device_find_debug_attr(phy_dev, "loopback");
	if (!attr) {
		fprintf(stderr, "Error: Digital loopback attribute not found\n");
		return -ENOENT;
	}

	/* Set loopback: "0" = disabled, "1" = enabled */
	ret = iio_attr_write_string(attr, enable ? "1" : "0");
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to set digital loopback: %zd\n", ret);
		return (int)ret;
	}

	printf("Digital loopback %s\n", enable ? "enabled" : "disabled");
	return 0;
}

/* Helper: Set sample rate for a direction (RX or TX) */
static int set_sample_rate_for_direction(bool is_tx, uint64_t sample_rate)
{
	const struct iio_attr *attr;
	struct iio_channel *chn;
	int ret;

	chn = iio_device_find_channel(phy_dev, "voltage0", is_tx);
	if (!chn) {
		fprintf(stderr, "Error: %s voltage0 channel not found\n", is_tx ? "TX" : "RX");
		return -ENOENT;
	}

	attr = iio_channel_find_attr(chn, "sampling_frequency");
	if (!attr) {
		fprintf(stderr, "Error: %s sampling_frequency attribute not found\n", is_tx ? "TX" : "RX");
		return -ENOENT;
	}

	ret = iio_attr_write_longlong(attr, sample_rate);
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to set %s sample rate: %d\n", is_tx ? "TX" : "RX", ret);
		return ret;
	}

	return 0;
}

/* Configure sample rate for both RX and TX */
static int configure_sample_rate(uint64_t sample_rate)
{
	int ret;

	ret = set_sample_rate_for_direction(false, sample_rate);  /* RX */
	if (ret < 0)
		return ret;

	ret = set_sample_rate_for_direction(true, sample_rate);   /* TX */
	if (ret < 0)
		return ret;

	printf("Configured sample rate: %" PRIu64 " Hz\n", sample_rate);
	return 0;
}

/* Comparator for qsort() over an array of uint64_t microsecond latencies */
static int compare_u64(const void *a, const void *b)
{
	uint64_t va = *(const uint64_t *)a;
	uint64_t vb = *(const uint64_t *)b;

	if (va < vb)
		return -1;
	if (va > vb)
		return 1;
	return 0;
}

/* Compute and print min/max/mean/percentile latency stats from a bounded sample set.
 * Sorts the array in place; only ever called once, after capture has completed. */
static void print_latency_stats(struct block_latency_stats *stats)
{
	uint64_t i, sum = 0;
	uint64_t p50_idx, p95_idx, p99_idx;

	if (stats->count == 0) {
		printf("Block latency: no samples recorded\n");
		return;
	}

	qsort(stats->samples_us, stats->count, sizeof(*stats->samples_us), compare_u64);

	for (i = 0; i < stats->count; i++)
		sum += stats->samples_us[i];

	/* Nearest-rank percentile method, clamped to the last valid index */
	p50_idx = (stats->count * 50) / 100;
	p95_idx = (stats->count * 95) / 100;
	p99_idx = (stats->count * 99) / 100;
	if (p50_idx >= stats->count)
		p50_idx = stats->count - 1;
	if (p95_idx >= stats->count)
		p95_idx = stats->count - 1;
	if (p99_idx >= stats->count)
		p99_idx = stats->count - 1;

	printf("Block latency (get-next-block wait time, %" PRIu64 " samples):\n", stats->count);
	printf("  Min:  %" PRIu64 " us\n", stats->samples_us[0]);
	printf("  Max:  %" PRIu64 " us\n", stats->samples_us[stats->count - 1]);
	printf("  Mean: %.2f us\n", (double)sum / (double)stats->count);
	printf("  p50:  %" PRIu64 " us\n", stats->samples_us[p50_idx]);
	printf("  p95:  %" PRIu64 " us\n", stats->samples_us[p95_idx]);
	printf("  p99:  %" PRIu64 " us\n", stats->samples_us[p99_idx]);
}

/* RX-only capture using stream API */
static int capture_rx_only(size_t block_size, uint64_t total_samples,
                           FILE *out_file, uint64_t sample_rate, bool loopback_enabled,
                           size_t pipeline_depth, bool quiet)
{
	struct iio_stream *rx_stream = NULL;
	struct iio_buffer *rxbuf = NULL;
	struct iio_channels_mask *rxmask = NULL;
	struct iio_channel *rx0_i, *rx0_q;
	const struct iio_block *rx_block = NULL;
	struct block_latency_stats lat_stats = {0};
	uint64_t samples_captured = 0;
	uint64_t blocks_processed = 0;
	uint64_t start_time_us, end_time_us;
	ssize_t sample_size;
	int ret = 0;

	printf("\nCapturing RX data with block size %zu complex samples...\n", block_size);
	if (loopback_enabled) {
		printf("Loopback mode enabled (digital loopback configured)\n");
	}

	/* Find RX channels */
	rx0_i = iio_device_find_channel(rx_dev, "voltage0", false);
	rx0_q = iio_device_find_channel(rx_dev, "voltage1", false);

	if (!rx0_i || !rx0_q) {
		fprintf(stderr, "Error: RX channels not found\n");
		return -ENODEV;
	}

	/* Get RX buffer */
	rxbuf = iio_device_get_buffer(rx_dev, 0);
	if (!rxbuf) {
		fprintf(stderr, "Error: Could not get RX buffer\n");
		return -ENODEV;
	}

	/* Create RX channel mask and enable channels */
	rxmask = iio_create_channels_mask(iio_device_get_channels_count(rx_dev));
	if (!rxmask) {
		fprintf(stderr, "Error: Failed to create RX channel mask\n");
		return -ENOMEM;
	}

	iio_channel_enable(rx0_i, rxmask);
	iio_channel_enable(rx0_q, rxmask);

	/* Get sample size */
	sample_size = iio_device_get_sample_size(rx_dev, rxmask);
	if (sample_size <= 0) {
		fprintf(stderr, "Error: Unable to get RX sample size\n");
		ret = -EINVAL;
		goto cleanup;
	}

	/* Allocate per-block latency sample buffer, sized to the expected block count */
	lat_stats.capacity = (total_samples + block_size - 1) / block_size;
	if (lat_stats.capacity > MAX_LATENCY_SAMPLES)
		lat_stats.capacity = MAX_LATENCY_SAMPLES;

	lat_stats.samples_us = malloc(lat_stats.capacity * sizeof(*lat_stats.samples_us));
	if (!lat_stats.samples_us) {
		fprintf(stderr, "Error: Failed to allocate latency sample buffer (%" PRIu64 " entries)\n",
		        lat_stats.capacity);
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Create RX stream */
	rx_stream = iio_buffer_create_stream(rxbuf, pipeline_depth, block_size, rxmask);
	ret = iio_err(rx_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to create RX stream: %d\n", ret);
		goto cleanup;
	}

#ifdef _WIN32
	/* Set binary mode for stdout (though we're writing to file) */
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	start_time_us = get_time_us();

	/* Capture loop */
	while (app_running && samples_captured < total_samples) {
		uint64_t block_wait_start_us, block_wait_end_us;

		/* Get next RX block */
		block_wait_start_us = get_time_us();
		rx_block = iio_stream_get_next_block(rx_stream);
		block_wait_end_us = get_time_us();
		ret = iio_err(rx_block);
		if (ret) {
			if (app_running)
				fprintf(stderr, "Error: Failed to get next RX block: %d\n", ret);
			break;
		}

		if (lat_stats.count < lat_stats.capacity)
			lat_stats.samples_us[lat_stats.count++] = block_wait_end_us - block_wait_start_us;

		const void *start = iio_block_start(rx_block);
		const void *end = iio_block_end(rx_block);
		ptrdiff_t len = (uintptr_t)end - (uintptr_t)start;

		/* Write data to file */
		size_t bytes_to_write = len;
		uint64_t remaining_samples = total_samples - samples_captured;
		size_t remaining_bytes = remaining_samples * sample_size;

		if (bytes_to_write > remaining_bytes) {
			bytes_to_write = remaining_bytes;
		}

		size_t written = fwrite(start, 1, bytes_to_write, out_file);
		if (written != bytes_to_write) {
			fprintf(stderr, "Error: Failed to write data to file\n");
			ret = -EIO;
			break;
		}

		size_t samples_in_block = bytes_to_write / sample_size;
		samples_captured += samples_in_block;
		blocks_processed++;

		/* Progress indicator */
		if (!quiet && blocks_processed % 100 == 0) {
			double progress = (double)samples_captured / total_samples * 100.0;
			printf("\rProgress: %.1f%% (%" PRIu64 " / %" PRIu64 " samples, %"
			       PRIu64 " blocks)",
			       progress, samples_captured, total_samples, blocks_processed);
			fflush(stdout);
		}
	}

	end_time_us = get_time_us();
	if (!quiet)
		printf("\n");

	/* Print statistics */
	printf("\n--- Capture Statistics ---\n");
	printf("Samples captured: %" PRIu64 "\n", samples_captured);
	printf("Blocks processed: %" PRIu64 "\n", blocks_processed);
	printf("Block size: %zu complex samples\n", block_size);
	printf("Sample size: %zd bytes\n", sample_size);
	printf("Pipeline depth: %zu blocks\n", pipeline_depth);

	if (end_time_us > start_time_us) {
		uint64_t duration_us = end_time_us - start_time_us;
		double duration_s = duration_us / 1000000.0;
		double actual_rate = (double)samples_captured / duration_s;
		double expected_rate = (double)sample_rate;
		double rate_error = (actual_rate - expected_rate) / expected_rate * 100.0;

		printf("Duration: %.3f seconds\n", duration_s);
		printf("Actual sample rate: %.3f Msps\n", actual_rate / 1000000.0);
		printf("Expected sample rate: %.3f Msps\n", expected_rate / 1000000.0);
		printf("Rate error: %.2f%%\n", rate_error);
	}

	print_latency_stats(&lat_stats);

cleanup:
	if (rx_stream && !iio_err(rx_stream))
		iio_stream_destroy(rx_stream);
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	free(lat_stats.samples_us);

	return ret;
}

/* Simultaneous RX/TX capture using block API */
static int capture_rxtx_simultaneous(size_t block_size, uint64_t total_samples,
                                     FILE *rx_file, FILE *tx_file, uint64_t sample_rate,
                                     bool quiet)
{
	struct iio_buffer_stream *rx_buf_stream = NULL;
	struct iio_buffer_stream *tx_buf_stream = NULL;
	struct iio_buffer *rxbuf = NULL;
	struct iio_buffer *txbuf = NULL;
	struct iio_channels_mask *rxmask = NULL;
	struct iio_channels_mask *txmask = NULL;
	struct iio_channel *rx0_i, *rx0_q;
	struct iio_channel *tx0_i, *tx0_q;
	struct iio_block *rx_block = NULL;
	struct iio_block *tx_block = NULL;
	uint64_t samples_captured = 0;
	uint64_t blocks_processed = 0;
	uint64_t start_time_us, end_time_us;
	int ret = 0;

	printf("\nCapturing RX/TX data with block size %zu complex samples...\n", block_size);

	/* Find RX channels */
	rx0_i = iio_device_find_channel(rx_dev, "voltage0", false);
	rx0_q = iio_device_find_channel(rx_dev, "voltage1", false);

	if (!rx0_i || !rx0_q) {
		fprintf(stderr, "Error: RX channels not found\n");
		return -ENODEV;
	}

	/* Find TX channels */
	tx0_i = iio_device_find_channel(tx_dev, "voltage0", true);
	tx0_q = iio_device_find_channel(tx_dev, "voltage1", true);

	if (!tx0_i || !tx0_q) {
		fprintf(stderr, "Error: TX channels not found\n");
		return -ENODEV;
	}

	/* Get buffers */
	rxbuf = iio_device_get_buffer(rx_dev, 0);
	txbuf = iio_device_get_buffer(tx_dev, 0);

	if (!rxbuf || !txbuf) {
		fprintf(stderr, "Error: Could not get buffers\n");
		return -ENODEV;
	}

	/* Create channel masks */
	rxmask = iio_create_channels_mask(iio_device_get_channels_count(rx_dev));
	txmask = iio_create_channels_mask(iio_device_get_channels_count(tx_dev));

	if (!rxmask || !txmask) {
		fprintf(stderr, "Error: Failed to create channel masks\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Enable channels */
	iio_channel_enable(rx0_i, rxmask);
	iio_channel_enable(rx0_q, rxmask);
	iio_channel_enable(tx0_i, txmask);
	iio_channel_enable(tx0_q, txmask);

	/* Open buffer streams */
	rx_buf_stream = iio_buffer_open(rxbuf, rxmask);
	ret = iio_err(rx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to open RX buffer stream: %d\n", ret);
		goto cleanup;
	}

	tx_buf_stream = iio_buffer_open(txbuf, txmask);
	ret = iio_err(tx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to open TX buffer stream: %d\n", ret);
		goto cleanup;
	}

	/* Create blocks */
	rx_block = iio_buffer_stream_create_block(rx_buf_stream, block_size);
	ret = iio_err(rx_block);
	if (ret) {
		fprintf(stderr, "Error: Failed to create RX block: %d\n", ret);
		goto cleanup;
	}

	tx_block = iio_buffer_stream_create_block(tx_buf_stream, block_size);
	ret = iio_err(tx_block);
	if (ret) {
		fprintf(stderr, "Error: Failed to create TX block: %d\n", ret);
		goto cleanup;
	}

	/* Get RX block size and enqueue it to receive data */
	const void *rx_block_start = iio_block_start(rx_block);
	const void *rx_block_end = iio_block_end(rx_block);
	ptrdiff_t rx_block_len = (uintptr_t)rx_block_end - (uintptr_t)rx_block_start;

	ret = iio_block_enqueue(rx_block, rx_block_len, false);
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to enqueue RX block: %d\n", ret);
		goto cleanup;
	}

	/* Clear TX block (zeros) */
	void *tx_start = iio_block_start(tx_block);
	void *tx_end = iio_block_end(tx_block);
	ptrdiff_t tx_len = (uintptr_t)tx_end - (uintptr_t)tx_start;

	memset(tx_start, 0, tx_len);

	/* Enqueue TX block to start cyclic transmission */
	ret = iio_block_enqueue(tx_block, tx_len, true);  /* cyclic = true */
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to enqueue TX block: %d\n", ret);
		goto cleanup;
	}

	/* Start streaming */
	ret = iio_buffer_stream_start(tx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to start TX buffer stream: %d\n", ret);
		goto cleanup;
	}

	ret = iio_buffer_stream_start(rx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to start RX buffer stream: %d\n", ret);
		goto cleanup;
	}

	start_time_us = get_time_us();

	/* Capture loop */
	while (app_running && samples_captured < total_samples) {
		/* Dequeue RX block */
		ret = iio_block_dequeue(rx_block, false);
		if (ret < 0) {
			fprintf(stderr, "Error: Failed to dequeue RX block: %d\n", ret);
			goto cleanup;
		}

		const void *rx_start = iio_block_start(rx_block);
		const void *rx_end = iio_block_end(rx_block);
		ptrdiff_t rx_len = (uintptr_t)rx_end - (uintptr_t)rx_start;

		/* Write RX data to file */
		size_t bytes_to_write = rx_len;
		uint64_t remaining_samples = total_samples - samples_captured;
		size_t remaining_bytes = remaining_samples * BYTES_PER_COMPLEX_SAMPLE;

		if (bytes_to_write > remaining_bytes) {
			bytes_to_write = remaining_bytes;
		}

		size_t written = fwrite(rx_start, 1, bytes_to_write, rx_file);
		if (written != bytes_to_write) {
			fprintf(stderr, "Error: Failed to write RX data to file\n");
			ret = -EIO;
			goto cleanup;
		}

		/* Also save the TX pattern for reference if tx_file is provided */
		if (tx_file) {
			fwrite(tx_start, 1, tx_len, tx_file);
		}

		size_t samples_in_block = bytes_to_write / BYTES_PER_COMPLEX_SAMPLE;
		samples_captured += samples_in_block;
		blocks_processed++;

		/* Enqueue RX block for reuse */
		ret = iio_block_enqueue(rx_block, rx_len, false);
		if (ret < 0) {
			fprintf(stderr, "Error: Failed to enqueue RX block: %d\n", ret);
			goto cleanup;
		}

		/* Progress indicator */
		if (!quiet && blocks_processed % 100 == 0) {
			double progress = (double)samples_captured / total_samples * 100.0;
			printf("\rProgress: %.1f%% (%" PRIu64 " / %" PRIu64 " samples, %"
			       PRIu64 " blocks)",
			       progress, samples_captured, total_samples, blocks_processed);
			fflush(stdout);
		}
	}

	end_time_us = get_time_us();
	if (!quiet)
		printf("\n");

	/* Print statistics */
	printf("\n--- Capture Statistics ---\n");
	printf("Samples captured: %" PRIu64 "\n", samples_captured);
	printf("Blocks processed: %" PRIu64 "\n", blocks_processed);
	printf("Block size: %zu complex samples (%zu bytes)\n",
	       block_size, block_size * BYTES_PER_COMPLEX_SAMPLE);

	if (end_time_us > start_time_us) {
		uint64_t duration_us = end_time_us - start_time_us;
		double duration_s = duration_us / 1000000.0;
		double actual_rate = (double)samples_captured / duration_s;
		double expected_rate = (double)sample_rate;
		double rate_error = (actual_rate - expected_rate) / expected_rate * 100.0;

		printf("Duration: %.3f seconds\n", duration_s);
		printf("Actual sample rate: %.3f Msps\n", actual_rate / 1000000.0);
		printf("Expected sample rate: %.3f Msps\n", expected_rate / 1000000.0);
		printf("Rate error: %.2f%%\n", rate_error);
	}

cleanup:
	if (rx_block && !iio_err(rx_block))
		iio_block_destroy(rx_block);
	if (tx_block && !iio_err(tx_block))
		iio_block_destroy(tx_block);
	if (rx_buf_stream && !iio_err(rx_buf_stream))
		iio_buffer_close(rx_buf_stream);
	if (tx_buf_stream && !iio_err(tx_buf_stream))
		iio_buffer_close(tx_buf_stream);
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	if (txmask)
		iio_channels_mask_destroy(txmask);

	return ret;
}

/* Main */
int main(int argc, char **argv)
{
	char uri[256];
	char default_filename[256];
	const char *ip_addr = NULL;
	const char *output_file = NULL;
	enum test_mode mode = MODE_RX_128;
	uint64_t total_samples = DEFAULT_DURATION_SAMPLES;
	uint64_t sample_rate = DEFAULT_SAMPLE_RATE_HZ;
	bool enable_loopback = false;
	bool quiet = false;
	struct iio_context_params params = {0};
	struct option *opts;
	FILE *out_file = NULL;
	FILE *tx_ref_file = NULL;
	size_t block_size;
	size_t pipeline_depth = DEFAULT_PIPELINE_DEPTH;
	int c, ret = EXIT_FAILURE;

	/* Parse arguments */
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}

	while ((c = getopt_long(argc, argv, "i:m:s:o:r:ln:q" COMMON_OPTIONS, opts, NULL)) != -1) {
		switch (c) {
		case 'i':
			ip_addr = optarg;
			break;
		case 'm':
			if (strcmp(optarg, "rx128") == 0)
				mode = MODE_RX_128;
			else if (strcmp(optarg, "rx256") == 0)
				mode = MODE_RX_256;
			else if (strcmp(optarg, "rxtx128") == 0)
				mode = MODE_RXTX_128;
			else {
				fprintf(stderr, "Invalid test mode: %s\n", optarg);
				fprintf(stderr, "Valid modes: rx128, rx256, rxtx128\n");
				free(opts);
				return EXIT_FAILURE;
			}
			break;
		case 's':
			total_samples = strtoull(optarg, NULL, 10);
			if (total_samples == 0) {
				fprintf(stderr, "Invalid sample count: %s\n", optarg);
				free(opts);
				return EXIT_FAILURE;
			}
			break;
		case 'o':
			output_file = optarg;
			break;
		case 'r':
			sample_rate = strtoull(optarg, NULL, 10);
			if (sample_rate == 0) {
				fprintf(stderr, "Invalid sample rate: %s\n", optarg);
				free(opts);
				return EXIT_FAILURE;
			}
			break;
		case 'l':
			enable_loopback = true;
			break;
		case 'n':
			pipeline_depth = sanitize_clamp("pipeline depth", optarg, 1, 4096);
			break;
		case 'q':
			quiet = true;
			break;
		case 'h':
			usage(MY_NAME, options, options_descriptions);
			free(opts);
			return EXIT_SUCCESS;
		case 'V':
			version(MY_NAME);
			free(opts);
			return EXIT_SUCCESS;
		case '?':
			free(opts);
			return EXIT_FAILURE;
		}
	}

	free(opts);

	if (!ip_addr) {
		fprintf(stderr, "Error: IP address or URI is required\n");
		fprintf(stderr, "Usage: %s -i <ip_address|uri> [-m <mode>] [-s <samples>] "
		        "[-o <output_file>] [-r <sample_rate>] [-n <pipeline_depth>] [-q]\n", MY_NAME);
		return EXIT_FAILURE;
	}

	/* Determine block size and default output filename */
	switch (mode) {
	case MODE_RX_128:
		block_size = BUFFER_SIZE_128;
		snprintf(default_filename, sizeof(default_filename), "capture_rx128.bin");
		break;
	case MODE_RX_256:
		block_size = BUFFER_SIZE_256;
		snprintf(default_filename, sizeof(default_filename), "capture_rx256.bin");
		break;
	case MODE_RXTX_128:
		block_size = BUFFER_SIZE_128;
		snprintf(default_filename, sizeof(default_filename), "capture_rxtx128_rx.bin");
		break;
	default:
		fprintf(stderr, "Unknown mode\n");
		return EXIT_FAILURE;
	}

	if (!output_file) {
		output_file = default_filename;
	}

	/* Setup signal handler */
	setup_sig_handler();

	/* Open output file */
	out_file = iio_fopen(output_file, "wb");
	if (!out_file) {
		char err_buf[1024];
		iio_strerror(errno, err_buf, sizeof(err_buf));
		fprintf(stderr, "Error: Failed to open output file '%s': %s\n",
		        output_file, err_buf);
		return EXIT_FAILURE;
	}

	printf("Output file: %s\n", output_file);

	/* For RXTX mode, also open a file for TX reference pattern */
	if (mode == MODE_RXTX_128) {
		char tx_filename[256];
		iio_snprintf(tx_filename, sizeof(tx_filename), "capture_rxtx128_tx.bin");
		tx_ref_file = iio_fopen(tx_filename, "wb");
		if (tx_ref_file) {
			printf("TX reference file: %s\n", tx_filename);
		}
	}

	/* Create context. If ip_addr already looks like a full URI (contains a
	 * ':', e.g. "local:", "ip:1.2.3.4", "usb:1.5.5"), use it verbatim so
	 * non-network backends can be selected; otherwise treat it as a bare
	 * IP/hostname and build an "ip:" URI as before. */
	if (strchr(ip_addr, ':'))
		snprintf(uri, sizeof(uri), "%s", ip_addr);
	else
		snprintf(uri, sizeof(uri), "ip:%s", ip_addr);
	printf("Connecting to %s...\n", uri);

	ctx = iio_create_context(&params, uri);
	ret = iio_err(ctx);
	if (ret) {
		fprintf(stderr, "Error: Failed to create context: %d\n", ret);
		goto cleanup_files;
	}

	printf("Connected to %s\n", iio_context_get_description(ctx));

	/* Find devices */
	ret = find_devices();
	if (ret < 0) {
		goto cleanup_ctx;
	}

	/* Configure digital loopback if requested */
	if (enable_loopback) {
		ret = set_digital_loopback(true);
		if (ret < 0) {
			goto cleanup_ctx;
		}
	}

	/* Configure sample rate */
	ret = configure_sample_rate(sample_rate);
	if (ret < 0) {
		goto cleanup_ctx;
	}

	/* Print test configuration */
	printf("\n========================================\n");
	printf("Test Configuration:\n");
	printf("  Mode: %s\n", mode == MODE_RX_128 ? "RX 128" :
	                        mode == MODE_RX_256 ? "RX 256" : "RX/TX 128");
	printf("  Block size: %zu complex samples (%zu bytes)\n",
	       block_size, block_size * BYTES_PER_COMPLEX_SAMPLE);
	printf("  Sample rate: %" PRIu64 " Hz (%.1f Msps)\n",
	       sample_rate, (double)sample_rate / 1000000.0);
	printf("  Total samples: %" PRIu64 " (%.3f seconds)\n",
	       total_samples, (double)total_samples / sample_rate);
	if (mode == MODE_RXTX_128) {
		if (pipeline_depth != DEFAULT_PIPELINE_DEPTH)
			printf("  Note: -n/--num-blocks has no effect in rxtx128 mode "
			       "(single RX/TX block, no pipelining).\n");
	} else {
		printf("  RX pipeline depth: %zu blocks\n", pipeline_depth);
	}
	printf("========================================\n");

	/* Run capture */
	if (mode == MODE_RXTX_128) {
		ret = capture_rxtx_simultaneous(block_size, total_samples,
		                                out_file, tx_ref_file, sample_rate, quiet);
	} else {
		ret = capture_rx_only(block_size, total_samples, out_file,
		                      sample_rate, enable_loopback, pipeline_depth, quiet);
	}

	if (ret < 0) {
		fprintf(stderr, "\nCapture failed with error: %d\n", ret);
	} else {
		printf("\nCapture completed successfully\n");
		printf("Data saved to: %s\n", output_file);
		if (tx_ref_file) {
			printf("TX reference saved to: capture_rxtx128_tx.bin\n");
		}
		printf("\nYou can analyze the captured data using external tools.\n");
	}

cleanup_ctx:
	if (ctx)
		iio_context_destroy(ctx);

cleanup_files:
	if (out_file)
		fclose(out_file);
	if (tx_ref_file)
		fclose(tx_ref_file);

	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
