/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
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

#define MY_NAME "iio_readdev"

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
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
	"Use the context with the provided URI.",
	"Use the specified trigger.",
	"Size of the capture buffer. Default is 256.",
	"Number of samples to capture, 0 = infinite. Default is 0.",
	"Buffer timeout in milliseconds. 0 = no timeout"
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-n <hostname>] [-t <trigger>] "
			"[-b <buffer-size>] [-s <samples>] "
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

#include <Windows.h>

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

static setup_sig_handler(void)
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
	int ret;

	/* Blocks until one of the termination signals is received */
	do {
		ret = sigwaitinfo(mask, NULL);
	} while (ret == -1 && errno == EINTR);

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

static struct iio_device * get_device(const struct iio_context *ctx,
		const char *id)
{

	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	struct iio_device *device;

	for (i = 0; i < nb_devices; i++) {
		const char *name;
		device = iio_context_get_device(ctx, i);
		name = iio_device_get_name(device);
		if (name && !strcmp(name, id))
			break;
		if (!strcmp(id, iio_device_get_id(device)))
			break;
	}

	if (i < nb_devices)
		return device;

	fprintf(stderr, "Device %s not found\n", id);
	return NULL;
}

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

int main(int argc, char **argv)
{
	unsigned int i, nb_channels;
	unsigned int buffer_size = SAMPLES_PER_READ;
	int c, option_index = 0, arg_index = 0, ip_index = 0, uri_index = 0;
	struct iio_device *dev;
	size_t sample_size;
	int timeout = -1;

	while ((c = getopt_long(argc, argv, "+hn:u:t:b:s:T:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			arg_index += 2;
			ip_index = arg_index;
			break;
		case 'u':
			arg_index += 2;
			uri_index = arg_index;
			break;
		case 't':
			arg_index += 2;
			trigger_name = argv[arg_index];
			break;
		case 'b':
			arg_index += 2;
			buffer_size = atoi(argv[arg_index]);
			break;
		case 's':
			arg_index += 2;
			num_samples = atoi(argv[arg_index]);
			break;
		case 'T':
			arg_index += 2;
			timeout = atoi(argv[arg_index]);
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (arg_index + 1 >= argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	setup_sig_handler();

	if (uri_index)
		ctx = iio_create_context_from_uri(argv[uri_index]);
	else if (ip_index)
		ctx = iio_create_network_context(argv[ip_index]);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		fprintf(stderr, "Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	if (timeout >= 0)
		iio_context_set_timeout(ctx, timeout);

	dev = get_device(ctx, argv[arg_index + 1]);
	if (!dev) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	if (trigger_name) {
		struct iio_device *trigger = get_device(ctx, trigger_name);
		if (!trigger) {
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

	if (argc == arg_index + 2) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++)
			iio_channel_enable(iio_device_get_channel(dev, i));
	} else {
		for (i = 0; i < nb_channels; i++) {
			unsigned int j;
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			for (j = arg_index + 2; j < (unsigned int) argc; j++) {
				const char *n = iio_channel_get_name(ch);
				if (!strcmp(argv[j], iio_channel_get_id(ch)) ||
						(n && !strcmp(n, argv[j])))
					iio_channel_enable(ch);
			}
		}
	}

	sample_size = iio_device_get_sample_size(dev);

	buffer = iio_device_create_buffer(dev, buffer_size, false);
	if (!buffer) {
		char buf[256];
		iio_strerror(errno, buf, sizeof(buf));
		fprintf(stderr, "Unable to allocate buffer: %s\n", buf);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	while (app_running) {
		int ret = iio_buffer_refill(buffer);
		if (ret < 0) {
			if (app_running) {
				char buf[256];
				iio_strerror(-ret, buf, sizeof(buf));
				fprintf(stderr, "Unable to refill buffer: %s\n", buf);
			}
			break;
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
			iio_buffer_foreach_sample(buffer, print_sample, NULL);
		}
	}

err_destroy_buffer:
	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	return exit_code;
}
