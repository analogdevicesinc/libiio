/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2018 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         that Michael Hennerich <michael.hennerich@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#include <errno.h>
#include <getopt.h>
#include <iio.h>
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

#define MY_NAME "iio_writedev"

#define SAMPLES_PER_READ 256
#define DEFAULT_FREQ_HZ  100

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"network", required_argument, 0, 'n'},
	  {"uri", required_argument, 0, 'u'},
	  {"trigger", required_argument, 0, 't'},
	  {"buffer-size", required_argument, 0, 'b'},
	  {"samples", required_argument, 0, 's' },
	  {"timeout", required_argument, 0, 'T'},
	  {"auto", no_argument, 0, 'a'},
	  {"cyclic", no_argument, 0, 'c'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
	"Use the context with the provided URI.",
	"Use the specified trigger.",
	"Size of the capture buffer. Default is 256.",
	"Number of samples to write, 0 = infinite. Default is 0.",
	"Buffer timeout in milliseconds. 0 = no timeout",
	"Scan for available contexts and if only one is available use it.",
	"Use cyclic buffer mode.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-n <hostname>] [-t <trigger>] "
			"[-T <timeout-ms>] [-b <buffer-size>] [-s <samples>] "
			"<iio_device> [<channel> ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

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
	 * safe. Use a seperate thread and handle the signals synchronous so we
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

static ssize_t read_sample(const struct iio_channel *chn,
		void *buf, size_t len, void *d)
{
	size_t nb = fread(buf, 1, len, stdin);
	if (num_samples != 0) {
		num_samples--;
		if (num_samples == 0) {
			quit_all(EXIT_SUCCESS);
			return -1;
		}
	}
	return (ssize_t) nb;
}

static struct iio_context *scan(void)
{
	struct iio_scan_context *scan_ctx;
	struct iio_context_info **info;
	struct iio_context *ctx = NULL;
	unsigned int i;
	ssize_t ret;

	scan_ctx = iio_create_scan_context(NULL, 0);
	if (!scan_ctx) {
		fprintf(stderr, "Unable to create scan context\n");
		return NULL;
	}

	ret = iio_scan_context_get_info_list(scan_ctx, &info);
	if (ret < 0) {
		char err_str[1024];
		iio_strerror(-ret, err_str, sizeof(err_str));
		fprintf(stderr, "Scanning for IIO contexts failed: %s\n", err_str);
		goto err_free_ctx;
	}

	if (ret == 0) {
		printf("No IIO context found.\n");
		goto err_free_info_list;
	}

	if (ret == 1) {
		ctx = iio_create_context_from_uri(iio_context_info_get_uri(info[0]));
	} else {
		fprintf(stderr, "Multiple contexts found. Please select one using --uri:\n");

		for (i = 0; i < (size_t) ret; i++) {
			fprintf(stderr, "\t%d: %s [%s]\n", i,
				iio_context_info_get_description(info[i]),
				iio_context_info_get_uri(info[i]));
		}
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctx:
	iio_scan_context_destroy(scan_ctx);

	return ctx;
}

int main(int argc, char **argv)
{
	unsigned int i, nb_channels;
	unsigned int buffer_size = SAMPLES_PER_READ;
	const char *arg_uri = NULL;
	const char *arg_ip = NULL;
	int c, option_index = 0;
	struct iio_device *dev;
	size_t sample_size;
	int timeout = -1;
	bool scan_for_context = false;
	bool cyclic_buffer = false;

	while ((c = getopt_long(argc, argv, "+hn:u:t:b:s:T:ac",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			arg_ip = optarg;
			break;
		case 'u':
			arg_uri = optarg;
			break;
		case 'a':
			scan_for_context = true;
			break;
		case 't':
			trigger_name = optarg;
			break;
		case 'b':
			buffer_size = atoi(optarg);
			break;
		case 's':
			num_samples = atoi(optarg);
			break;
		case 'T':
			timeout = atoi(optarg);
			break;
		case 'c':
			cyclic_buffer = true;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (argc == optind) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	setup_sig_handler();

	if (scan_for_context)
		ctx = scan();
	else if (arg_uri)
		ctx = iio_create_context_from_uri(arg_uri);
	else if (arg_ip)
		ctx = iio_create_network_context(arg_ip);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		fprintf(stderr, "Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	if (timeout >= 0)
		iio_context_set_timeout(ctx, timeout);

	dev = iio_context_find_device(ctx, argv[optind]);
	if (!dev) {
		fprintf(stderr, "Device %s not found\n", argv[optind]);
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
				"sampling_frequency", DEFAULT_FREQ_HZ) < 0)
			iio_device_attr_write_longlong(trigger,
				"frequency", DEFAULT_FREQ_HZ);

		iio_device_set_trigger(dev, trigger);
	}

	nb_channels = iio_device_get_channels_count(dev);

	if (argc == optind + 1) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++)
			iio_channel_enable(iio_device_get_channel(dev, i));
	} else {
		for (i = 0; i < nb_channels; i++) {
			unsigned int j;
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			for (j = optind + 1; j < (unsigned int) argc; j++) {
				const char *n = iio_channel_get_name(ch);
				if (!strcmp(argv[j], iio_channel_get_id(ch)) ||
						(n && !strcmp(n, argv[j])))
					iio_channel_enable(ch);
			}
		}
	}

	sample_size = iio_device_get_sample_size(dev);

	buffer = iio_device_create_buffer(dev, buffer_size, cyclic_buffer);
	if (!buffer) {
		char buf[256];
		iio_strerror(errno, buf, sizeof(buf));
		fprintf(stderr, "Unable to allocate buffer: %s\n", buf);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

#ifdef _WIN32
	_setmode(_fileno( stdin ), _O_BINARY);
#endif

	while (app_running) {
		/* If there are only the samples we requested, we don't need to
		 * demux */
		if (iio_buffer_step(buffer) == sample_size) {
			void *start = iio_buffer_start(buffer);
			size_t write_len, len = (intptr_t) iio_buffer_end(buffer)
				- (intptr_t) start;

			if (num_samples && len > num_samples * sample_size)
				len = num_samples * sample_size;

			for (write_len = len; len; ) {
				size_t nb = fread(start, 1, len, stdin);
				if (!nb)
					goto err_destroy_buffer;

				len -= nb;
				start = (void *)((intptr_t) start + nb);
			}

			if (num_samples) {
				num_samples -= write_len / sample_size;
				if (!num_samples)
					quit_all(EXIT_SUCCESS);
			}
		} else {
			iio_buffer_foreach_sample(buffer, read_sample, NULL);
		}

		int ret = iio_buffer_push(buffer);
		if (ret < 0) {
			if (app_running) {
				char buf[256];
				iio_strerror(-ret, buf, sizeof(buf));
				fprintf(stderr, "Unable to push buffer: %s\n", buf);
			}
			break;
		}

		while(cyclic_buffer && app_running) {
#ifdef _WIN32
			Sleep(1000);
#else
			sleep(1);
#endif
		}
	}


err_destroy_buffer:
	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	return exit_code;
}
