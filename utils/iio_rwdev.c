// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_rwdev - Part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 * */

#include <errno.h>
#include <getopt.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "iio_common.h"

#define MY_NAME "iio_rwdev"

#define SAMPLES_PER_READ 256
#define DEFAULT_FREQ_HZ  100
#define REFILL_PER_BENCHMARK 10

static const struct option options[] = {
	  {"trigger", required_argument, 0, 't'},
	  {"buffer-size", required_argument, 0, 'b'},
	  {"samples", required_argument, 0, 's' },
	  {"auto", no_argument, 0, 'a'},
	  {"write", no_argument, 0, 'w'},
	  {"cyclic", no_argument, 0, 'c'},
	  {"benchmark", no_argument, 0, 'B'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"[-t <trigger>] [-b <buffer-size>]"
		"[-s <samples>] <iio_device> [<channel> ...]",
	"Use the specified trigger.",
	"Size of the transfer buffer. Default is 256.",
	"Number of samples to transfer, 0 = infinite. Default is 0.",
	"Scan for available contexts and if only one is available use it.",
	"Transmit to IIO device (TX) instead of receiving (RX).",
	"Use cyclic buffer mode.",
	"Benchmark throughput."
		"\n\t\t\tStatistics will be printed on the standard input.",
};

static struct iio_context *ctx;
static struct iio_buffer *buffer;
static const char *trigger_name = NULL;
static size_t num_samples;

static volatile sig_atomic_t app_running = true;
static int exit_code = EXIT_FAILURE;

static void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
	if (buffer)
		iio_buffer_cancel(buffer);
}

#ifdef _WIN32

BOOL WINAPI sig_handler_fn(DWORD dwCtrlType)
{
	/* Runs in its own thread */

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

#elif NO_THREADS

static void sig_handler(int sig)
{
	/*
	 * If the main function is stuck waiting for data it will not abort. If the
	 * user presses Ctrl+C a second time we abort without cleaning up.
	 */
	if (!app_running)
		exit(sig);
	app_running = false;
}

static void set_handler(int sig)
{
	struct sigaction action;

	sigaction(sig, NULL, &action);
	action.sa_handler = sig_handler;
	sigaction(sig, &action, NULL);
}

static void setup_sig_handler(void)
{
	set_handler(SIGHUP);
	set_handler(SIGPIPE);
	set_handler(SIGINT);
	set_handler(SIGSEGV);
	set_handler(SIGTERM);
}

#else

#include <pthread.h>

static void * sig_handler_thd(void *data)
{
	sigset_t *mask = data;
	int ret, sig;

	/* Blocks until one of the termination signals is received */
	do {
		ret = sigwait(mask, &sig);
	} while (ret == EINTR);

	quit_all(ret);

	return NULL;
}

static void setup_sig_handler(void)
{
	sigset_t mask, oldmask;
	pthread_t thd;
	int ret;

	/*
	 * Async signals are difficult to handle and the IIO API is not signal
	 * safe. Use a separate thread and handle the signals synchronous so we
	 * can call iio_buffer_cancel().
	 */

	sigemptyset(&mask);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGPIPE);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGSEGV);
	sigaddset(&mask, SIGTERM);

	pthread_sigmask(SIG_BLOCK, &mask, &oldmask);

	ret = pthread_create(&thd, NULL, sig_handler_thd, &mask);
	if (ret) {
		fprintf(stderr, "Failed to create signal handler thread: %d\n", ret);
		pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
	}
}

#endif

static ssize_t transfer_sample(const struct iio_channel *chn,
		void *buf, size_t len, void *d)
{
	bool *is_write = d;
	size_t nb;

	if (*is_write)
		nb = fread(buf, 1, len, stdin);
	else
		nb = fwrite(buf, 1, len, stdout);

	if (num_samples != 0) {
		num_samples--;
		if (num_samples == 0) {
			quit_all(EXIT_SUCCESS);
			return -1;
		}
	}
	return (ssize_t) nb;
}

#define MY_OPTS "t:b:s:T:wcB"

int main(int argc, char **argv)
{
	char **argw;
	unsigned int i, j, nb_channels;
	unsigned int nb_active_channels = 0;
	unsigned int buffer_size = SAMPLES_PER_READ;
	uint64_t refill_per_benchmark = REFILL_PER_BENCHMARK;
	struct iio_device *dev, *trigger;
	struct iio_channel *ch;
	ssize_t sample_size, hw_sample_size;
	bool hit, mib, is_write = false, cyclic_buffer = false,
	     benchmark = false, do_write = false;
	struct iio_stream *stream;
	const struct iio_block *block;
	struct iio_channels_mask *mask;
	const struct iio_channels_mask *hw_mask;
	const struct iio_attr *uri, *attr;
	struct option *opts;
	uint64_t before = 0, after, rate, total;
	size_t rw_len, len, nb;
	void *start;
	int c, ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	setup_sig_handler();

	ctx = handle_common_opts(MY_NAME, argc, argw, MY_OPTS,
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		goto err_free_ctx;
	}

	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS,	/* Flawfinder: ignore */
					opts, NULL)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'V':
		case 'u':
		case 'T':
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argv[optind] != NULL
					&& argv[optind][0] != '-')
				optind++;
			break;
		case 't':
			if (!optarg) {
				fprintf(stderr, "Trigger requires an argument\n");
				goto err_free_ctx;
			}
			trigger_name = optarg;
			break;
		case 'b':
			if (!optarg) {
				fprintf(stderr, "Buffersize requires an argument\n");
				goto err_free_ctx;
			}
			buffer_size = sanitize_clamp("buffer size", optarg, 1, SIZE_MAX);
			break;
		case 'B':
			benchmark = true;
			break;
		case 's':
			if (!optarg) {
				fprintf(stderr, "Number of Samples requires an argument\n");
				goto err_free_ctx;
			}
			num_samples = sanitize_clamp("number of samples", optarg, 0, SIZE_MAX);
			break;
		case 'c':
			cyclic_buffer = true;
			break;
		case 'w':
			is_write = true;
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			goto err_free_ctx;
		}
	}
	free(opts);

	if (!ctx)
		goto err_free_argw;

	if (argc < optind) {
		fprintf(stderr, "Too few arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		goto err_free_ctx;
	}

	if (!is_write && cyclic_buffer) {
		fprintf(stderr, "Cyclic buffer can only be used on output buffers.\n");
		goto err_free_ctx;
	}

	if (benchmark && cyclic_buffer) {
		fprintf(stderr, "Cannot benchmark in cyclic mode.\n");
		goto err_free_ctx;
	}

	if (!ctx)
		return ret;

	if (!argw[optind]) {
		uri = iio_context_find_attr(ctx, "uri");

		for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
			dev = iio_context_get_device(ctx, i);
			nb_channels = iio_device_get_channels_count(dev);
			if (!nb_channels)
				continue;

			hit = false;
			for (j = 0; j < nb_channels; j++) {
				ch = iio_device_get_channel(dev, j);

				if (!iio_channel_is_scan_element(ch) ||
						is_write ^ iio_channel_is_output(ch))
					continue;

				hit = true;

				printf("Example : " MY_NAME " -u %s -b 256 -s 1024 %s %s\n",
						iio_attr_get_static_value(uri),
						dev_name(dev), iio_channel_get_id(ch));
			}
			if (hit)
				printf("Example : " MY_NAME " -u %s -b 256 -s 1024 %s\n",
						iio_attr_get_static_value(uri),
						dev_name(dev));
		}
		usage(MY_NAME, options, options_descriptions);
		goto err_free_ctx;
	}

	dev = iio_context_find_device(ctx, argw[optind]);
	if (!dev) {
		fprintf(stderr, "Device %s not found\n", argw[optind]);
		goto err_free_ctx;
	}

	if (trigger_name) {
		trigger = iio_context_find_device(ctx, trigger_name);
		if (!trigger) {
			fprintf(stderr, "Trigger %s not found\n", trigger_name);
			goto err_free_ctx;
		}

		if (!iio_device_is_trigger(trigger)) {
			fprintf(stderr, "Specified device is not a trigger\n");
			goto err_free_ctx;
		}

		/*
		 * Fixed rate for now. Try new ABI first,
		 * fail gracefully to remain compatible.
		 */
		attr = iio_device_find_attr(trigger, "sampling_frequency");

		if (!attr || iio_attr_write_longlong(attr, DEFAULT_FREQ_HZ) < 0) {
			attr = iio_device_find_attr(trigger, "frequency");
			if (!attr)
				ret = -ENOENT;
			else
				ret = iio_attr_write_longlong(attr, DEFAULT_FREQ_HZ);
			if (ret < 0)
				dev_perror(trigger, ret, "Sample rate not set");
		}

		ret = iio_device_set_trigger(dev, trigger);
		if (ret < 0)
			dev_perror(dev, ret, "Unable to set trigger");
	}

	nb_channels = iio_device_get_channels_count(dev);
	mask = iio_create_channels_mask(nb_channels);
	if (!mask) {
		fprintf(stderr, "Unable to create channels mask\n");
		goto err_free_ctx;
	}

	if (argc == optind + 1) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++) {
			ch = iio_device_get_channel(dev, i);

			if (is_write == iio_channel_is_output(ch)) {
				iio_channel_enable(ch, mask);
				nb_active_channels++;
			}
		}
	} else {
		for (j = optind + 1; j < (unsigned int) argc; j++) {
			ret = iio_device_enable_channel(dev, argw[j], is_write, mask);
			if (ret < 0) {
				dev_perror(dev, ret, "Bad channel name \"%s\"", argw[j]);
				goto err_free_mask;
			}
			nb_active_channels++;
		}
	}

	if (!nb_active_channels) {
		fprintf(stderr, "No %sput channels found\n", is_write ? "out" : "in");
		goto err_free_mask;
	}

	sample_size = iio_device_get_sample_size(dev, mask);
	/* Zero isn't normally an error code, but in this case it is an error */
	if (sample_size == 0) {
		fprintf(stderr, "Unable to get sample size, returned 0\n");
		goto err_free_mask;
	} else if (sample_size < 0) {
		dev_perror(dev, (int) sample_size, "Unable to get sample size");
		goto err_free_mask;
	}

	buffer = iio_device_create_buffer(dev, 0, mask);
	ret = iio_err(buffer);
	if (ret) {
		dev_perror(dev, ret, "Unable to allocate buffer");
		goto err_free_mask;
	}

	hw_mask = iio_buffer_get_channels_mask(buffer);
	hw_sample_size = iio_device_get_sample_size(dev, hw_mask);

	stream = iio_buffer_create_stream(buffer, 4, buffer_size);
	ret = iio_err(stream);
	if (ret) {
		dev_perror(dev, ret, "Unable to create stream");
		goto err_destroy_buffer;
	}

#ifdef _WIN32
	/*
	 * Deactivate the translation for the stdout. Otherwise, bytes that have
	 * the same value as line feed character (LF) will be translated to CR-LF.
	 */
	_setmode(_fileno(is_write ? stdin : stdout), _O_BINARY);
#endif

	for (i = 0, total = 0; app_running; ) {
		if (benchmark)
			before = get_time_us();

		block = iio_stream_get_next_block(stream);
		ret = iio_err(block);
		if (ret && app_running) {
			dev_perror(dev, ret, "Unable to get next block");
			break;
		}

		if (benchmark && is_write == do_write) {
			after = get_time_us();
			total += after - before;

			if (++i == refill_per_benchmark) {
				rate = buffer_size * sample_size *
					refill_per_benchmark * 1000000ull / total;
				mib = rate > 1048576;

				fprintf(stderr, "\33[2K\rThroughput: %" PRIu64 " %ciB/s",
					rate / (1024 * (mib ? 1024 : 1)),
					mib ? 'M' : 'K');

				/* Print every 100ms more or less */
				refill_per_benchmark = refill_per_benchmark * 100000 / total;
				if (refill_per_benchmark < REFILL_PER_BENCHMARK)
					refill_per_benchmark = REFILL_PER_BENCHMARK;

				i = 0;
				total = 0;
			}
		}

		if (do_write && cyclic_buffer) {
			while(app_running) {
#ifdef _WIN32
				Sleep(1000);
#else
				sleep(1);
#endif
			}

			break;
		}

		do_write = is_write;

		if (benchmark)
			continue;

		/* If there are only the samples we requested, we don't need to
		 * demux */
		if (hw_sample_size == sample_size) {
			start = iio_block_start(block);
			len = (intptr_t) iio_block_end(block) - (intptr_t) start;

			if (num_samples && len > num_samples * sample_size)
				len = num_samples * sample_size;

			for (rw_len = len; len; ) {
				if (is_write)
					nb = fread(start, 1, len, stdin);
				else
					nb = fwrite(start, 1, len, stdout);
				if (!nb)
					goto err_destroy_stream;

				len -= nb;
				start = (void *)((intptr_t) start + nb);
			}

			if (num_samples) {
				num_samples -= rw_len / sample_size;
				if (!num_samples)
					quit_all(EXIT_SUCCESS);
			}
		} else {
			ret = (int) iio_block_foreach_sample(block, mask,
							     transfer_sample,
							     &is_write);
			if (ret < 0)
				dev_perror(dev, ret, "Buffer processing failed");
		}
	}

err_destroy_stream:
	iio_stream_destroy(stream);
err_destroy_buffer:
	iio_buffer_destroy(buffer);
err_free_mask:
	iio_channels_mask_destroy(mask);
err_free_ctx:
	if (ctx)
		iio_context_destroy(ctx);
err_free_argw:
	free_argw(argc, argw);
	return exit_code;
}
