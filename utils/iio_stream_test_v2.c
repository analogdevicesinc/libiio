// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_stream_test_v2 - Data capture utility for testing streaming with small buffers
 *
 * Functionally identical to iio_stream_test, but driven entirely through the
 * low-level libiio v1 block API instead of the iio_stream helper:
 *
 *   iio_buffer_open()                   -> open the buffer stream
 *   iio_buffer_stream_create_block()    -> allocate a ring of N blocks
 *   iio_block_enqueue()  x N            -> prime the whole ring before starting
 *   iio_buffer_stream_start()           -> arm the DMA
 *   loop: dequeue(ring[curr]) -> consume -> enqueue(ring[curr]) -> curr++ (mod N)
 *   iio_buffer_stream_stop()            -> disarm, then drain and free the ring
 *
 * The difference that matters versus the iio_stream helper: here all N blocks
 * are enqueued *before* the buffer is started, so the kernel has the full
 * pipeline depth available from the very first sample. iio_stream_get_next_block()
 * instead enqueues blocks 1..N-1, starts the buffer, and only reaches full depth
 * after a complete lap of the ring.
 *
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

#define MY_NAME "iio_stream_test_v2"

/* Default configuration */
#define DEFAULT_SAMPLE_RATE_HZ 8000000  /* 8 Msps */
#define DEFAULT_DURATION_SAMPLES (8 * 1024 * 1024)  /* 1 second at 8 Msps */
#define BUFFER_SIZE_128 128  /* 128 complex samples */
#define BUFFER_SIZE_256 256  /* 256 complex samples */
#define BYTES_PER_COMPLEX_SAMPLE 4  /* I and Q are int16_t each */
#define DEFAULT_PIPELINE_DEPTH 4  /* Number of blocks in the RX ring */
#define MAX_LATENCY_SAMPLES (16 * 1024 * 1024)  /* Safety cap, ~128 MiB of uint64_t */

/* Test modes */
enum test_mode {
	MODE_RX_128,
	MODE_RX_256,
	MODE_RXTX_128,
};

/* Per-block latency statistics, computed from a bounded sample buffer */
struct block_latency_stats {
	uint64_t *samples_us;   /* Array of per-block latency deltas, in microseconds */
	uint64_t count;         /* Number of latency samples actually recorded */
	uint64_t capacity;      /* Allocated capacity of samples_us */
};

/* A ring of blocks driven with manual enqueue/dequeue */
struct block_ring {
	struct iio_buffer_stream *buf_stream;
	struct iio_block **blocks;
	bool *enqueued;         /* Per-block: is it currently owned by the kernel? */
	size_t nb_blocks;
	size_t block_size;      /* In bytes */
	unsigned int curr;      /* Index of the next block to dequeue */
	bool started;
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
	"Output file for captured data (default: capture_<mode>_v2.bin).",
	"Sample rate in Hz (default: 8000000).",
	"Enable digital loopback mode (default: disabled).",
	"Number of blocks in the RX ring, i.e. pipeline depth (default: 4).",
	"Suppress progress output (recommended for timing-sensitive runs, e.g. under 'time').",
};

/* Global state */
static struct iio_context *ctx = NULL;
static bool uri_is_local = false;   /* True when running on the board over 'local:' */
static struct iio_device *rx_dev = NULL;
static struct iio_device *tx_dev = NULL;
static struct iio_device *phy_dev = NULL;
static volatile sig_atomic_t app_running = true;
static int exit_code = EXIT_SUCCESS;

/* Buffer streams reachable from the signal handler, so a second Ctrl-C can
 * unblock a thread that is stuck inside iio_block_dequeue(). */
static struct iio_buffer_stream *volatile g_rx_buf_stream = NULL;
static struct iio_buffer_stream *volatile g_tx_buf_stream = NULL;

/* Signal handler */
static void quit_all(int sig)
{
	exit_code = sig;

	/* On the second signal, force any blocking dequeue to return. */
	if (!app_running) {
		if (g_rx_buf_stream)
			iio_buffer_stream_cancel(g_rx_buf_stream);
		if (g_tx_buf_stream)
			iio_buffer_stream_cancel(g_tx_buf_stream);
		return;
	}

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

/* Allocate a latency sample buffer sized to the expected block count */
static int latency_stats_alloc(struct block_latency_stats *stats,
                               uint64_t total_samples, size_t block_size)
{
	stats->count = 0;
	stats->capacity = (total_samples + block_size - 1) / block_size;
	if (stats->capacity > MAX_LATENCY_SAMPLES)
		stats->capacity = MAX_LATENCY_SAMPLES;

	stats->samples_us = malloc(stats->capacity * sizeof(*stats->samples_us));
	if (!stats->samples_us) {
		fprintf(stderr, "Error: Failed to allocate latency sample buffer (%" PRIu64 " entries)\n",
		        stats->capacity);
		return -ENOMEM;
	}

	return 0;
}

static void latency_stats_add(struct block_latency_stats *stats, uint64_t delta_us)
{
	if (stats->count < stats->capacity)
		stats->samples_us[stats->count++] = delta_us;
}

/* Compute and print min/max/mean/percentile latency stats from a bounded sample set.
 * Sorts the array in place; only ever called once, after capture has completed. */
static void print_latency_stats(const char *label, struct block_latency_stats *stats)
{
	uint64_t i, sum = 0;
	uint64_t p50_idx, p95_idx, p99_idx;

	if (stats->count == 0) {
		printf("%s: no samples recorded\n", label);
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

	printf("%s (%" PRIu64 " samples):\n", label, stats->count);
	printf("  Min:  %" PRIu64 " us\n", stats->samples_us[0]);
	printf("  Max:  %" PRIu64 " us\n", stats->samples_us[stats->count - 1]);
	printf("  Mean: %.2f us\n", (double)sum / (double)stats->count);
	printf("  p50:  %" PRIu64 " us\n", stats->samples_us[p50_idx]);
	printf("  p95:  %" PRIu64 " us\n", stats->samples_us[p95_idx]);
	printf("  p99:  %" PRIu64 " us\n", stats->samples_us[p99_idx]);
}

/* ------------------------------------------------------------------------- */
/* Block ring: manual enqueue/dequeue over a circular set of blocks          */
/* ------------------------------------------------------------------------- */

/* Allocate nb_blocks blocks of block_size bytes on an already-opened stream.
 * Does not enqueue anything; see ring_prime(). */
static int ring_alloc(struct block_ring *ring, struct iio_buffer_stream *buf_stream,
                      size_t nb_blocks, size_t block_size)
{
	size_t i;
	int ret;

	memset(ring, 0, sizeof(*ring));
	ring->buf_stream = buf_stream;
	ring->nb_blocks = nb_blocks;
	ring->block_size = block_size;

	ring->blocks = calloc(nb_blocks, sizeof(*ring->blocks));
	ring->enqueued = calloc(nb_blocks, sizeof(*ring->enqueued));
	if (!ring->blocks || !ring->enqueued)
		return -ENOMEM;

	for (i = 0; i < nb_blocks; i++) {
		/* NOTE: iio_buffer_stream_create_block() takes a size in BYTES,
		 * unlike iio_buffer_create_stream() which takes a sample count. */
		ring->blocks[i] = iio_buffer_stream_create_block(buf_stream, block_size);
		ret = iio_err(ring->blocks[i]);
		if (ret) {
			fprintf(stderr, "Error: Failed to create block %zu/%zu (%zu bytes): %d\n",
			        i + 1, nb_blocks, block_size, ret);
			ring->blocks[i] = NULL;
			return ret;
		}
	}

	return 0;
}

/* Enqueue every block, then start the stream. This is what gives the kernel
 * the full pipeline depth from the first sample onwards. */
static int ring_prime_and_start(struct block_ring *ring)
{
	size_t i;
	int ret;

	for (i = 0; i < ring->nb_blocks; i++) {
		/* bytes_used = 0 means "the whole block" (see iio_block_enqueue) */
		ret = iio_block_enqueue(ring->blocks[i], 0, false);
		if (ret < 0) {
			fprintf(stderr, "Error: Failed to enqueue block %zu: %d\n", i, ret);
			return ret;
		}
		ring->enqueued[i] = true;
	}

	ret = iio_buffer_stream_start(ring->buf_stream);
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to start buffer stream: %d\n", ret);
		return ret;
	}

	ring->started = true;
	ring->curr = 0;

	return 0;
}

/* Dequeue the block at the head of the ring. On success *block points at it
 * and it is owned by userspace until ring_recycle_head() is called. */
static int ring_dequeue_head(struct block_ring *ring, struct iio_block **block)
{
	int ret;

	ret = iio_block_dequeue(ring->blocks[ring->curr], false);
	if (ret < 0)
		return ret;

	ring->enqueued[ring->curr] = false;
	*block = ring->blocks[ring->curr];

	return 0;
}

/* Give the head block back to the kernel and advance the ring. */
static int ring_recycle_head(struct block_ring *ring)
{
	int ret;

	ret = iio_block_enqueue(ring->blocks[ring->curr], 0, false);
	if (ret < 0)
		return ret;

	ring->enqueued[ring->curr] = true;
	ring->curr = (ring->curr + 1) % ring->nb_blocks;

	return 0;
}

/* Stop the stream, drain every in-flight block, and release everything. */
static void ring_teardown(struct block_ring *ring)
{
	size_t i;

	if (!ring->blocks)
		goto free_arrays;

	if (ring->started) {
		iio_buffer_stream_stop(ring->buf_stream);

		/* Signal the cancellation eventfd so that the dequeues below
		 * cannot block, even if the DMA stalled and some blocks will
		 * never complete. */
		iio_buffer_stream_cancel(ring->buf_stream);
	}

	for (i = 0; i < ring->nb_blocks; i++) {
		if (!ring->blocks[i])
			continue;

		/* A block still owned by the kernel should be dequeued before
		 * it is destroyed. An error here just means it was already
		 * dequeued or the dequeue was cancelled, neither of which
		 * matters at this point. */
		if (ring->enqueued[i])
			iio_block_dequeue(ring->blocks[i], false);

		iio_block_destroy(ring->blocks[i]);
	}

free_arrays:
	free(ring->blocks);
	free(ring->enqueued);
	memset(ring, 0, sizeof(*ring));
}

/* Report which backend path the blocks actually landed on.
 *
 * This is only meaningful on the local backend, where the blocks in this
 * process are the ones handed to the kernel: a valid DMABUF fd means the
 * DMABUF path, -EINVAL means MMAP or the generic read()/write() fallback.
 *
 * Over a remote backend (ip:, usb:, ...) the blocks here are plain malloc()ed
 * buffers and there is never a DMABUF fd, regardless of what iiod uses on the
 * far side. Saying anything about the remote data path from here would be a
 * lie, so don't. */
static void print_block_backend(const struct block_ring *ring, const char *what, bool is_local)
{
	int fd;

	if (!ring->blocks || !ring->blocks[0])
		return;

	if (!is_local) {
		printf("%s block backend: unknown (remote backend - the block path "
		       "used by iiod on the board is not visible from here; re-run "
		       "over 'local:' on the board to detect it)\n", what);
		return;
	}

	fd = iio_block_get_dmabuf_fd(ring->blocks[0]);
	printf("%s block backend: %s\n", what,
	       fd >= 0 ? "DMABUF" : "MMAP or generic (no DMABUF fd)");
}

/* ------------------------------------------------------------------------- */
/* RX-only capture                                                           */
/* ------------------------------------------------------------------------- */

static int capture_rx_only(size_t block_size, uint64_t total_samples,
                           FILE *out_file, uint64_t sample_rate, bool loopback_enabled,
                           size_t pipeline_depth, bool quiet)
{
	struct iio_buffer_stream *rx_buf_stream = NULL;
	struct block_ring ring;
	struct iio_buffer *rxbuf = NULL;
	struct iio_channels_mask *rxmask = NULL;
	struct iio_channel *rx0_i, *rx0_q;
	struct block_latency_stats deq_stats = {0};
	struct block_latency_stats enq_stats = {0};
	uint64_t samples_captured = 0;
	uint64_t blocks_processed = 0;
	uint64_t start_time_us = 0, end_time_us = 0;
	ssize_t sample_size;
	int ret = 0;

	memset(&ring, 0, sizeof(ring));

	printf("\nCapturing RX data with block size %zu complex samples...\n", block_size);
	if (loopback_enabled)
		printf("Loopback mode enabled (digital loopback configured)\n");

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

	/* Get sample size, needed to convert the block size from samples to bytes */
	sample_size = iio_device_get_sample_size(rx_dev, rxmask);
	if (sample_size <= 0) {
		fprintf(stderr, "Error: Unable to get RX sample size\n");
		ret = -EINVAL;
		goto cleanup;
	}

	ret = latency_stats_alloc(&deq_stats, total_samples, block_size);
	if (ret < 0)
		goto cleanup;

	ret = latency_stats_alloc(&enq_stats, total_samples, block_size);
	if (ret < 0)
		goto cleanup;

	/* Open the buffer stream */
	rx_buf_stream = iio_buffer_open(rxbuf, rxmask);
	ret = iio_err(rx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to open RX buffer stream: %d\n", ret);
		rx_buf_stream = NULL;
		goto cleanup;
	}
	g_rx_buf_stream = rx_buf_stream;

	/* Allocate the ring of blocks */
	ret = ring_alloc(&ring, rx_buf_stream, pipeline_depth,
	                 block_size * (size_t)sample_size);
	if (ret < 0)
		goto cleanup;

	print_block_backend(&ring, "RX", uri_is_local);

#ifdef _WIN32
	/* Set binary mode for stdout (though we're writing to file) */
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	/* Prime the whole ring, then arm the DMA */
	ret = ring_prime_and_start(&ring);
	if (ret < 0)
		goto cleanup;

	start_time_us = get_time_us();

	/* Capture loop: dequeue -> consume -> re-enqueue -> advance */
	while (app_running && samples_captured < total_samples) {
		uint64_t t0, t1, t2;
		struct iio_block *rx_block;
		const void *start, *end;
		ptrdiff_t len;
		size_t bytes_to_write, remaining_bytes, written, samples_in_block;
		uint64_t remaining_samples;

		t0 = get_time_us();
		ret = ring_dequeue_head(&ring, &rx_block);
		t1 = get_time_us();
		if (ret < 0) {
			if (app_running)
				fprintf(stderr, "Error: Failed to dequeue RX block: %d\n", ret);
			break;
		}

		latency_stats_add(&deq_stats, t1 - t0);

		start = iio_block_start(rx_block);
		end = iio_block_end(rx_block);
		len = (uintptr_t)end - (uintptr_t)start;

		/* Write data to file */
		bytes_to_write = (size_t)len;
		remaining_samples = total_samples - samples_captured;
		remaining_bytes = remaining_samples * (size_t)sample_size;

		if (bytes_to_write > remaining_bytes)
			bytes_to_write = remaining_bytes;

		written = fwrite(start, 1, bytes_to_write, out_file);
		if (written != bytes_to_write) {
			fprintf(stderr, "Error: Failed to write data to file\n");
			ret = -EIO;
			break;
		}

		samples_in_block = bytes_to_write / (size_t)sample_size;
		samples_captured += samples_in_block;
		blocks_processed++;

		/* Hand the block back to the kernel and move on */
		t1 = get_time_us();
		ret = ring_recycle_head(&ring);
		t2 = get_time_us();
		if (ret < 0) {
			if (app_running)
				fprintf(stderr, "Error: Failed to re-enqueue RX block: %d\n", ret);
			break;
		}

		latency_stats_add(&enq_stats, t2 - t1);

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
	       block_size, block_size * (size_t)sample_size);
	printf("Sample size: %zd bytes\n", sample_size);
	printf("Ring depth: %zu blocks (all enqueued before start)\n", pipeline_depth);

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

	print_latency_stats("Block dequeue wait time", &deq_stats);
	print_latency_stats("Block enqueue cost", &enq_stats);

cleanup:
	ring_teardown(&ring);
	g_rx_buf_stream = NULL;
	if (rx_buf_stream)
		iio_buffer_close(rx_buf_stream);
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	free(deq_stats.samples_us);
	free(enq_stats.samples_us);

	return ret;
}

/* ------------------------------------------------------------------------- */
/* Simultaneous RX/TX capture                                                */
/* ------------------------------------------------------------------------- */

static int capture_rxtx_simultaneous(size_t block_size, uint64_t total_samples,
                                     FILE *rx_file, FILE *tx_file, uint64_t sample_rate,
                                     size_t pipeline_depth, bool quiet)
{
	struct iio_buffer_stream *rx_buf_stream = NULL;
	struct iio_buffer_stream *tx_buf_stream = NULL;
	struct block_ring ring;
	struct iio_block *tx_block = NULL;
	struct iio_buffer *rxbuf = NULL;
	struct iio_buffer *txbuf = NULL;
	struct iio_channels_mask *rxmask = NULL;
	struct iio_channels_mask *txmask = NULL;
	struct iio_channel *rx0_i, *rx0_q;
	struct iio_channel *tx0_i, *tx0_q;
	struct block_latency_stats deq_stats = {0};
	void *tx_start = NULL;
	size_t tx_len = 0;
	uint64_t samples_captured = 0;
	uint64_t blocks_processed = 0;
	uint64_t start_time_us = 0, end_time_us = 0;
	ssize_t rx_sample_size, tx_sample_size;
	bool tx_started = false, tx_enqueued = false;
	int ret = 0;

	memset(&ring, 0, sizeof(ring));

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

	/* Sample sizes, needed to convert the block size from samples to bytes */
	rx_sample_size = iio_device_get_sample_size(rx_dev, rxmask);
	tx_sample_size = iio_device_get_sample_size(tx_dev, txmask);
	if (rx_sample_size <= 0 || tx_sample_size <= 0) {
		fprintf(stderr, "Error: Unable to get sample sizes (rx %zd, tx %zd)\n",
		        rx_sample_size, tx_sample_size);
		ret = -EINVAL;
		goto cleanup;
	}

	ret = latency_stats_alloc(&deq_stats, total_samples, block_size);
	if (ret < 0)
		goto cleanup;

	/* Open buffer streams */
	rx_buf_stream = iio_buffer_open(rxbuf, rxmask);
	ret = iio_err(rx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to open RX buffer stream: %d\n", ret);
		rx_buf_stream = NULL;
		goto cleanup;
	}
	g_rx_buf_stream = rx_buf_stream;

	tx_buf_stream = iio_buffer_open(txbuf, txmask);
	ret = iio_err(tx_buf_stream);
	if (ret) {
		fprintf(stderr, "Error: Failed to open TX buffer stream: %d\n", ret);
		tx_buf_stream = NULL;
		goto cleanup;
	}
	g_tx_buf_stream = tx_buf_stream;

	/* Single cyclic TX block: the hardware repeats it forever, so there is
	 * nothing to enqueue per iteration on the TX side. */
	tx_block = iio_buffer_stream_create_block(tx_buf_stream,
	                                          block_size * (size_t)tx_sample_size);
	ret = iio_err(tx_block);
	if (ret) {
		fprintf(stderr, "Error: Failed to create TX block: %d\n", ret);
		tx_block = NULL;
		goto cleanup;
	}

	tx_start = iio_block_start(tx_block);
	tx_len = (size_t)((uintptr_t)iio_block_end(tx_block) - (uintptr_t)tx_start);

	/* Clear TX block (zeros). Write the reference pattern out now, while the
	 * block is still owned by userspace - once enqueued cyclically, its
	 * contents must not be touched. */
	memset(tx_start, 0, tx_len);
	if (tx_file)
		fwrite(tx_start, 1, tx_len, tx_file);

	/* RX ring, primed in full before the DMA is armed */
	ret = ring_alloc(&ring, rx_buf_stream, pipeline_depth,
	                 block_size * (size_t)rx_sample_size);
	if (ret < 0)
		goto cleanup;

	print_block_backend(&ring, "RX", uri_is_local);

	/* Start TX first so there is already something on the wire */
	ret = iio_block_enqueue(tx_block, tx_len, true);  /* cyclic = true */
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to enqueue TX block: %d\n", ret);
		goto cleanup;
	}
	tx_enqueued = true;

	ret = iio_buffer_stream_start(tx_buf_stream);
	if (ret < 0) {
		fprintf(stderr, "Error: Failed to start TX buffer stream: %d\n", ret);
		goto cleanup;
	}
	tx_started = true;

	ret = ring_prime_and_start(&ring);
	if (ret < 0)
		goto cleanup;

	start_time_us = get_time_us();

	/* Capture loop */
	while (app_running && samples_captured < total_samples) {
		uint64_t t0, t1;
		struct iio_block *rx_block;
		const void *rx_start;
		size_t bytes_to_write, remaining_bytes, written, samples_in_block;
		uint64_t remaining_samples;

		t0 = get_time_us();
		ret = ring_dequeue_head(&ring, &rx_block);
		t1 = get_time_us();
		if (ret < 0) {
			if (app_running)
				fprintf(stderr, "Error: Failed to dequeue RX block: %d\n", ret);
			break;
		}

		latency_stats_add(&deq_stats, t1 - t0);

		rx_start = iio_block_start(rx_block);
		bytes_to_write = (size_t)((uintptr_t)iio_block_end(rx_block)
		                          - (uintptr_t)rx_start);

		remaining_samples = total_samples - samples_captured;
		remaining_bytes = remaining_samples * (size_t)rx_sample_size;

		if (bytes_to_write > remaining_bytes)
			bytes_to_write = remaining_bytes;

		written = fwrite(rx_start, 1, bytes_to_write, rx_file);
		if (written != bytes_to_write) {
			fprintf(stderr, "Error: Failed to write RX data to file\n");
			ret = -EIO;
			break;
		}

		samples_in_block = bytes_to_write / (size_t)rx_sample_size;
		samples_captured += samples_in_block;
		blocks_processed++;

		ret = ring_recycle_head(&ring);
		if (ret < 0) {
			if (app_running)
				fprintf(stderr, "Error: Failed to re-enqueue RX block: %d\n", ret);
			break;
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
	       block_size, block_size * (size_t)rx_sample_size);
	printf("Ring depth: %zu blocks (all enqueued before start)\n", pipeline_depth);
	printf("TX: 1 cyclic block of %zu bytes\n", tx_len);

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

	print_latency_stats("Block dequeue wait time", &deq_stats);

cleanup:
	ring_teardown(&ring);

	if (tx_block) {
		if (tx_started) {
			iio_buffer_stream_stop(tx_buf_stream);
			iio_buffer_stream_cancel(tx_buf_stream);
		}
		if (tx_enqueued)
			iio_block_dequeue(tx_block, false);
		iio_block_destroy(tx_block);
	}

	g_rx_buf_stream = NULL;
	g_tx_buf_stream = NULL;

	if (rx_buf_stream)
		iio_buffer_close(rx_buf_stream);
	if (tx_buf_stream)
		iio_buffer_close(tx_buf_stream);
	if (rxmask)
		iio_channels_mask_destroy(rxmask);
	if (txmask)
		iio_channels_mask_destroy(txmask);
	free(deq_stats.samples_us);

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
			pipeline_depth = sanitize_clamp("ring depth", optarg, 1, UINT32_MAX);
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
		        "[-o <output_file>] [-r <sample_rate>] [-n <ring_depth>] [-q]\n", MY_NAME);
		return EXIT_FAILURE;
	}

	/* Determine block size and default output filename */
	switch (mode) {
	case MODE_RX_128:
		block_size = BUFFER_SIZE_128;
		snprintf(default_filename, sizeof(default_filename), "capture_rx128_v2.bin");
		break;
	case MODE_RX_256:
		block_size = BUFFER_SIZE_256;
		snprintf(default_filename, sizeof(default_filename), "capture_rx256_v2.bin");
		break;
	case MODE_RXTX_128:
		block_size = BUFFER_SIZE_128;
		snprintf(default_filename, sizeof(default_filename), "capture_rxtx128_v2_rx.bin");
		break;
	default:
		fprintf(stderr, "Unknown mode\n");
		return EXIT_FAILURE;
	}

	if (!output_file)
		output_file = default_filename;

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
		iio_snprintf(tx_filename, sizeof(tx_filename), "capture_rxtx128_v2_tx.bin");
		tx_ref_file = iio_fopen(tx_filename, "wb");
		if (tx_ref_file)
			printf("TX reference file: %s\n", tx_filename);
	}

	/* Create context. If ip_addr already looks like a full URI (contains a
	 * ':', e.g. "local:", "ip:1.2.3.4", "usb:1.5.5"), use it verbatim so
	 * non-network backends can be selected; otherwise treat it as a bare
	 * IP/hostname and build an "ip:" URI as before. */
	if (strchr(ip_addr, ':'))
		snprintf(uri, sizeof(uri), "%s", ip_addr);
	else
		snprintf(uri, sizeof(uri), "ip:%s", ip_addr);

	uri_is_local = !strncmp(uri, "local:", 6);
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
	if (ret < 0)
		goto cleanup_ctx;

	/* Configure digital loopback if requested */
	if (enable_loopback) {
		ret = set_digital_loopback(true);
		if (ret < 0)
			goto cleanup_ctx;
	}

	/* Configure sample rate */
	ret = configure_sample_rate(sample_rate);
	if (ret < 0)
		goto cleanup_ctx;

	/* Print test configuration */
	printf("\n========================================\n");
	printf("Test Configuration:\n");
	printf("  API: low-level blocks (manual enqueue/dequeue, circular ring)\n");
	printf("  Mode: %s\n", mode == MODE_RX_128 ? "RX 128" :
	                        mode == MODE_RX_256 ? "RX 256" : "RX/TX 128");
	printf("  Block size: %zu complex samples (%zu bytes)\n",
	       block_size, block_size * BYTES_PER_COMPLEX_SAMPLE);
	printf("  Sample rate: %" PRIu64 " Hz (%.1f Msps)\n",
	       sample_rate, (double)sample_rate / 1000000.0);
	printf("  Total samples: %" PRIu64 " (%.3f seconds)\n",
	       total_samples, (double)total_samples / sample_rate);
	printf("  URI: %s (%s)\n", uri, uri_is_local ? "local backend" : "remote backend");
	printf("  RX ring depth: %zu blocks\n", pipeline_depth);
	if (uri_is_local && pipeline_depth > 64) {
		printf("  Note: the local MMAP block path supports at most 64 blocks; "
		       "expect block creation to fail unless DMABUF is in use.\n");
	}
	if (!uri_is_local) {
		printf("  Note: over a remote backend every block is a network round-trip,\n"
		       "        so the measured rate reflects the link, not the board's DMA\n"
		       "        path. Run over 'local:' on the board for meaningful numbers.\n");
	}
	printf("========================================\n");

	/* Run capture */
	if (mode == MODE_RXTX_128) {
		ret = capture_rxtx_simultaneous(block_size, total_samples,
		                                out_file, tx_ref_file, sample_rate,
		                                pipeline_depth, quiet);
	} else {
		ret = capture_rx_only(block_size, total_samples, out_file,
		                      sample_rate, enable_loopback, pipeline_depth, quiet);
	}

	if (ret < 0) {
		fprintf(stderr, "\nCapture failed with error: %d\n", ret);
	} else {
		printf("\nCapture completed successfully\n");
		printf("Data saved to: %s\n", output_file);
		if (tx_ref_file)
			printf("TX reference saved to: capture_rxtx128_v2_tx.bin\n");
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

	(void)exit_code;

	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
