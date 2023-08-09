// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_readdev - Part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 * */

#include <errno.h>
#include <getopt.h>
#include <iio.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "iio_common.h"

#define MY_NAME "iio_readdev"

#define SAMPLES_PER_READ 256
#define DEFAULT_FREQ_HZ  100
#define REFILL_PER_BENCHMARK 10

static const struct option options[] = {
	  {"trigger", required_argument, 0, 't'},
	  {"buffer-size", required_argument, 0, 'b'},
	  {"samples", required_argument, 0, 's' },
	  {"auto", no_argument, 0, 'a'},
	  {"benchmark", no_argument, 0, 'B'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"[-t <trigger>] [-b <buffer-size>]"
		"[-s <samples>] <iio_device> [<channel> ...]",
	"Use the specified trigger.",
	"Size of the capture buffer. Default is 256.",
	"Number of samples to capture, 0 = infinite. Default is 0.",
	"Scan for available contexts and if only one is available use it.",
	"Benchmark throughput."
		"\n\t\t\tStatistics will be printed on the standard input.",
};

static struct iio_context *ctx;
static struct iio_buffer *buffer;
static const char *trigger_name = NULL;
static size_t num_samples;

static volatile sig_atomic_t app_running = true;
static int exit_code = EXIT_SUCCESS;

static void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
	if (buffer)
		iio_buffer_cancel(buffer);
}

#ifdef _WIN32

#include <windows.h>
#include <io.h>
#include <fcntl.h>

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

static ssize_t print_sample(const struct iio_channel *chn,
		void *buf, size_t len, void *d)
{
	fwrite(buf, 1, len, stdout);
	if (num_samples != 0) {
		num_samples--;
		if (num_samples == 0) {
			quit_all(EXIT_SUCCESS);
			return -1;
		}
	}
	return (ssize_t) len;
}

#define MY_OPTS "t:b:s:T:B"
int main(int argc, char **argv)
{
	char **argw;
	unsigned int i, j, nb_channels;
	unsigned int nb_active_channels = 0;
	unsigned int buffer_size = SAMPLES_PER_READ;
	uint64_t refill_per_benchmark = REFILL_PER_BENCHMARK;
	int c;
	struct iio_device *dev;
	ssize_t sample_size;
	ssize_t ret;
	struct option *opts;
	bool mib, benchmark = false;
	uint64_t before = 0, after, rate, total;
	int err_code = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	setup_sig_handler();

	ctx = handle_common_opts(MY_NAME, argc, argw, MY_OPTS,
				 options, options_descriptions, &err_code);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}

	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS,	/* Flawfinder: ignore */
					opts, NULL)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'V':
		case 'n':
		case 'x':
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
				return EXIT_FAILURE;
			}
			trigger_name = optarg;
			break;
		case 'b':
			if (!optarg) {
				fprintf(stderr, "Buffersize requires an argument\n");
				return EXIT_FAILURE;
			}
			buffer_size = sanitize_clamp("buffer size", optarg, 1, SIZE_MAX);
			break;
		case 'B':
			benchmark = true;
			break;
		case 's':
			if (!optarg) {
				fprintf(stderr, "Number of Samples requires an argument\n");
				return EXIT_FAILURE;
			}
			num_samples = sanitize_clamp("number of samples", optarg, 0, SIZE_MAX);
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}
	free(opts);

	if (argc < optind) {
		fprintf(stderr, "Too few arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	if (!ctx)
		return err_code;

	if (!argw[optind]) {
		unsigned int nb_devices = iio_context_get_devices_count(ctx);

		for (i = 0; i < nb_devices; i++) {
			const char *dev_id = NULL, *label = NULL, *name = NULL;
			bool hit;

			dev = iio_context_get_device(ctx, i);
			nb_channels = iio_device_get_channels_count(dev);

			if (!nb_channels)
				continue;

			hit = false;
			for (j = 0; j < nb_channels; j++) {
				struct iio_channel *ch = iio_device_get_channel(dev, j);

				if (!iio_channel_is_scan_element(ch) ||
						iio_channel_is_output(ch))
					continue;

				hit = true;

				dev_id = iio_device_get_id(dev);
				label = iio_device_get_label(dev);
				name = iio_device_get_name(dev);

				printf("Example : " MY_NAME " -u %s -b 256 -s 1024 %s %s\n",
						iio_context_get_attr_value(ctx, "uri"),
						label ? label : name ? name : dev_id,
						iio_channel_get_id(ch));
			}
			if (hit)
				printf("Example : " MY_NAME " -u %s -b 256 -s 1024 %s\n",
						iio_context_get_attr_value(ctx, "uri"),
						label ? label : name ? name : dev_id);
		}
		iio_context_destroy(ctx);
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	dev = iio_context_find_device(ctx, argw[optind]);
	if (!dev) {
		fprintf(stderr, "Device %s not found\n", argw[optind]);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	if (trigger_name) {
		struct iio_device *trigger = iio_context_find_device(
				ctx, trigger_name);
		if (!trigger) {
			fprintf(stderr, "Trigger %s not found\n", trigger_name);
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		if (!iio_device_is_trigger(trigger)) {
			fprintf(stderr, "Specified device is not a trigger\n");
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		/*
		 * Fixed rate for now. Try new ABI first,
		 * fail gracefully to remain compatible.
		 */
		if (iio_device_attr_write_longlong(trigger,
				"sampling_frequency", DEFAULT_FREQ_HZ) < 0) {
			ret = iio_device_attr_write_longlong(trigger,
				"frequency", DEFAULT_FREQ_HZ);
			if (ret < 0) {
				char buf[256];
				iio_strerror(-(int)ret, buf, sizeof(buf));
				fprintf(stderr, "sample rate not set : %s\n", buf);
			}
		}

		ret = iio_device_set_trigger(dev, trigger);
		if (ret < 0) {
			char buf[256];
			iio_strerror(-(int)ret, buf, sizeof(buf));
			fprintf(stderr, "set triffer failed : %s\n", buf);
		}
	}

	nb_channels = iio_device_get_channels_count(dev);

	if (argc == optind + 1) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++) {
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			if (!iio_channel_is_output(ch)) {
				iio_channel_enable(ch);
				nb_active_channels++;
			}
		}
	} else {
		for (j = optind + 1; j < (unsigned int) argc; j++) {
			ret = iio_device_enable_channel(dev, argw[j], false);
			if (ret < 0) {
				char buf[256];
				iio_strerror(-(int) ret, buf, sizeof(buf));
				fprintf(stderr, "Bad channel name \"%s\" : %s\n", argw[j], buf);
				iio_context_destroy(ctx);
				return EXIT_FAILURE;
			}
			nb_active_channels++;
		}
	}

	if (!nb_active_channels) {
		fprintf(stderr, "No input channels found.\n");
		return EXIT_FAILURE;
	}

	sample_size = iio_device_get_sample_size(dev);
	/* Zero isn't normally an error code, but in this case it is an error */
	if (sample_size == 0) {
		fprintf(stderr, "Unable to get sample size, returned 0\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	} else if (sample_size < 0) {
		char buf[256];
		iio_strerror(errno, buf, sizeof(buf));
		fprintf(stderr, "Unable to get sample size : %s\n", buf);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	buffer = iio_device_create_buffer(dev, buffer_size, false);
	if (!buffer) {
		char buf[256];
		iio_strerror(errno, buf, sizeof(buf));
		fprintf(stderr, "Unable to allocate buffer: %s\n", buf);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

#ifdef _WIN32
	/*
	 * Deactivate the translation for the stdout. Otherwise, bytes that have
	 * the same value as line feed character (LF) will be translated to CR-LF.
	 */
	_setmode(_fileno(stdout), _O_BINARY);
#endif


	for (i = 0, total = 0; app_running; ) {
		if (benchmark)
			before = get_time_us();

		ret = iio_buffer_refill(buffer);
		if (ret < 0) {
			if (app_running) {
				char buf[256];
				iio_strerror(-(int)ret, buf, sizeof(buf));
				fprintf(stderr, "Unable to refill buffer: %s\n", buf);
			}
			break;
		}

		if (benchmark) {
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

			continue;
		}

		/* If there are only the samples we requested, we don't need to
		 * demux */
		if (iio_buffer_step(buffer) == sample_size) {
			void *start = iio_buffer_start(buffer);
			size_t read_len, len = (intptr_t) iio_buffer_end(buffer)
				- (intptr_t) start;

			if (num_samples && len > num_samples * sample_size)
				len = num_samples * sample_size;

			for (read_len = len; len; ) {
				size_t nb = fwrite(start, 1, len, stdout);
				if (!nb)
					goto err_destroy_buffer;

				len -= nb;
				start = (void *)((intptr_t) start + nb);
			}

			if (num_samples) {
				num_samples -= read_len / sample_size;
				if (!num_samples)
					quit_all(EXIT_SUCCESS);
			}
		} else {
			ret = iio_buffer_foreach_sample(buffer, print_sample, NULL);
			if (ret < 0) {
				char buf[256];
				iio_strerror(-(int)ret, buf, sizeof(buf));
				fprintf(stderr, "buffer processing failed : %s\n", buf);
			}
		}
	}

err_destroy_buffer:
	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	free_argw(argc, argw);
	return exit_code;
}
