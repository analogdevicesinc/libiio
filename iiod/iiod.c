// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "../iio-config.h"
#include "debug.h"
#include "ops.h"
#include "thread-pool.h"

#include <iio/iio-lock.h>

#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#if WITH_ZSTD
#include <zstd.h>
#endif

#define MY_NAME "iiod"

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

static int start_iiod(const char *uri, const char *ffs_mountpoint,
		      const char *uart_params,
		      uint16_t port, unsigned int nb_pipes, int ep0_fd);

bool server_demux;

struct thread_pool *main_thread_pool;

struct iio_context_params iiod_params = {
	.log_level = LEVEL_INFO,
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"version", no_argument, 0, 'V'},
	  {"debug", no_argument, 0, 'd'},
	  {"demux", no_argument, 0, 'D'},
	  {"ffs", required_argument, 0, 'F'},
	  {"nb-pipes", required_argument, 0, 'n'},
	  {"serial", required_argument, 0, 's'},
	  {"port", required_argument, 0, 'p'},
	  {"uri", required_argument, 0, 'u'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Display the version of this program.",
	"Output debug log to the standard output.",
	"Demux channels directly on the server.",
	"Use the given FunctionFS mountpoint to serve over USB",
	"Specify the number of USB pipes (ep couples) to use",
	"Run " MY_NAME " on the specified UART.",
	"Port to listen on (default = " STRINGIFY(IIOD_PORT) ").",
	("Use the context at the provided URI."
		"\n\t\t\teg: 'ip:192.168.2.1', 'ip:pluto.local', or 'ip:'"
		"\n\t\t\t    'usb:1.2.3', or 'usb:'"
		"\n\t\t\t    'serial:/dev/ttyUSB0,115200,8n1'"
		"\n\t\t\t    'local:' (default)"),
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [OPTIONS ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static void set_handler(int signal, void (*handler)(int))
{
	struct sigaction sig;
	sigaction(signal, NULL, &sig);
	sig.sa_handler = handler;
	sigaction(signal, &sig, NULL);
}

static void sig_handler(int sig)
{
	thread_pool_stop(main_thread_pool);
}

static bool restart_usr1;

static void sig_handler_usr1(int sig)
{
	restart_usr1 = true;
	thread_pool_stop(main_thread_pool);
}

static void *get_xml_zstd_data(const struct iio_context *ctx, size_t *out_len)
{
	char *xml = iio_context_get_xml(ctx);
	size_t len, xml_len = strlen(xml);
	void *buf;
#if WITH_ZSTD
	size_t ret;

	len = ZSTD_compressBound(xml_len);
	buf = malloc(len);
	if (!buf) {
		free(xml);
		return NULL;
	}

	ret = ZSTD_compress(buf, len, xml, xml_len, 3);
	free(xml);

	if (ZSTD_isError(ret)) {
		IIO_WARNING("Unable to compress XML string: %s\n",
			    ZSTD_getErrorName(xml_len));
		free(buf);
		return NULL;
	}

	*out_len = ret;
#else
	buf = xml;
	*out_len = xml_len;
#endif

	return buf;
}

static void free_device_pdata(struct iio_context *ctx)
{
	unsigned int i;
	struct iio_device *dev;
	struct iio_device_pdata *pdata;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		dev = iio_context_get_device(ctx, i);
		pdata = iio_device_get_data(dev);
		free(pdata);
	}
}

static int init_device_pdata(struct iio_context *ctx)
{
	unsigned int i;
	struct iio_device *dev;
	struct iio_device_pdata *pdata;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		dev = iio_context_get_device(ctx, i);

		pdata = zalloc(sizeof(*pdata));
		if (!pdata)
			goto err_free_pdata;

		pdata->nb_blocks = 4;

		iio_device_set_data(dev, pdata);
	}

	return 0;

err_free_pdata:
	free_device_pdata(ctx);
	return -ENOMEM;
}

int main(int argc, char **argv)
{
	long nb_pipes = 3, val;
	char *end;
	const char *uri = "local:";
	int c, option_index = 0;
	char *ffs_mountpoint = NULL;
	char *uart_params = NULL;
	char err_str[1024];
	uint16_t port = IIOD_PORT;
	int ret, ep0_fd = 0;

	while ((c = getopt_long(argc, argv, "+hVdDF:n:s:p:u:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			iiod_params.log_level = LEVEL_DEBUG;
			break;
		case 'D':
			server_demux = true;
			break;
		case 'F':
			if (!WITH_IIOD_USBD) {
				IIO_ERROR("IIOD was not compiled with USB support.\n");
				return EXIT_FAILURE;
			}

			ffs_mountpoint = optarg;
			break;
		case 'n':
			if (!WITH_IIOD_USBD) {
				IIO_ERROR("IIOD was not compiled with USB support.\n");
				return EXIT_FAILURE;
			}

			errno = 0;
			nb_pipes = strtol(optarg, &end, 10);
			if (optarg == end || nb_pipes < 1 || errno == ERANGE) {
				IIO_ERROR("--nb-pipes: Invalid parameter\n");
				return EXIT_FAILURE;
			}
			break;
		case 's':
			if (!WITH_IIOD_SERIAL) {
				IIO_ERROR("IIOD was not compiled with serial support.\n");
				return EXIT_FAILURE;

			}

			uart_params = optarg;
			break;
		case 'p':
			val = strtoul(optarg, &end, 10);
			if (optarg == end || (end && *end != '\0') || val > 0xFFFF || val < 0) {
				IIO_ERROR("IIOD invalid port number\n");
				return EXIT_FAILURE;
			}
			port = (uint16_t)val;
			break;
		case 'u':
			uri = optarg;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'V':
			printf("%u.%u\n", LIBIIO_VERSION_MAJOR,
					LIBIIO_VERSION_MINOR);
			return EXIT_SUCCESS;
		case '?':
			return EXIT_FAILURE;
		}
	}

	main_thread_pool = thread_pool_new();
	if (!main_thread_pool) {
		IIO_PERROR(errno, "Unable to create thread pool");
		return EXIT_FAILURE;
	}

	if (WITH_IIOD_USBD && ffs_mountpoint) {
		ret = init_usb_daemon(ffs_mountpoint, nb_pipes);
		if (ret < 0) {
			IIO_PERROR(ret, "Unable to init USB");

			thread_pool_destroy(main_thread_pool);
			return EXIT_FAILURE;
		}

		ep0_fd = ret;
	}

	set_handler(SIGHUP, sig_handler);
	set_handler(SIGPIPE, sig_handler);
	set_handler(SIGINT, sig_handler);
	set_handler(SIGTERM, sig_handler);
	set_handler(SIGUSR1, sig_handler_usr1);

	do {
		thread_pool_restart(main_thread_pool);
		restart_usr1 = false;

		ret = start_iiod(uri, ffs_mountpoint, uart_params,
				 port, nb_pipes, ep0_fd);
	} while (!ret && restart_usr1);

	thread_pool_destroy(main_thread_pool);

	if (WITH_IIOD_USBD && ffs_mountpoint)
		close(ep0_fd);

	return ret;
}

static int start_iiod(const char *uri, const char *ffs_mountpoint,
		      const char *uart_params,
		      uint16_t port, unsigned int nb_pipes, int ep0_fd)
{
	struct iio_context *ctx;
	void *xml_zstd;
	size_t xml_zstd_len = 0;
	int ret;

	IIO_INFO("Starting IIO Daemon version %u.%u.%s\n",
		 LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR,
		 LIBIIO_VERSION_GIT);

	if (!WITH_IIOD_NETWORK
	    && (!WITH_IIOD_USBD || !ffs_mountpoint)
	    && (!WITH_IIOD_SERIAL || !uart_params)) {
		IIO_ERROR("Not enough parameters given.\n");
		return EXIT_FAILURE;
	}

	ctx = iio_create_context(&iiod_params, uri);
	if (iio_err(ctx)) {
		IIO_PERROR(iio_err(ctx), "Unable to create local context");
		return EXIT_FAILURE;
	}

	ret = init_device_pdata(ctx);
	if (ret) {
		ret = EXIT_FAILURE;
		goto out_destroy_context;
	}

	xml_zstd = get_xml_zstd_data(ctx, &xml_zstd_len);

	buflist_lock = iio_mutex_create();
	if (iio_err(buflist_lock)) {
		ret = EXIT_FAILURE;
		goto out_free_xml_data;
	}

	evlist_lock = iio_mutex_create();
	if (iio_err(evlist_lock)) {
		ret = EXIT_FAILURE;
		goto out_free_buflist_lock;
	}

	if (WITH_IIOD_USBD && ffs_mountpoint) {
		ret = start_usb_daemon(ctx, ffs_mountpoint,
				(unsigned int) nb_pipes, ep0_fd,
				main_thread_pool, xml_zstd, xml_zstd_len);
		if (ret) {
			IIO_PERROR(ret, "Unable to start USB daemon");
			ret = EXIT_FAILURE;
			goto out_free_evlist_lock;
		}
	}

	if (WITH_IIOD_SERIAL && uart_params) {
		ret = start_serial_daemon(ctx, uart_params,
					  main_thread_pool,
					  xml_zstd, xml_zstd_len);
		if (ret) {
			IIO_PERROR(ret, "Unable to start serial daemon");
			ret = EXIT_FAILURE;
			goto out_thread_pool_stop;
		}
	}

	if (WITH_IIOD_NETWORK) {
		ret = start_network_daemon(ctx, main_thread_pool,
					   xml_zstd, xml_zstd_len, port);
		if (ret) {
			IIO_PERROR(ret, "Unable to start network daemon");
			ret = EXIT_FAILURE;
			goto out_thread_pool_stop;
		}
	}

	thread_pool_wait(main_thread_pool);

out_thread_pool_stop:
	/*
	 * In case we got here through an error in the main thread make sure all
	 * the worker threads are signaled to shutdown.
	 */
	thread_pool_stop_and_wait(main_thread_pool);
out_free_evlist_lock:
	iio_mutex_destroy(evlist_lock);
out_free_buflist_lock:
	iio_mutex_destroy(buflist_lock);
out_free_xml_data:
	free(xml_zstd);
out_destroy_context:
	iio_context_destroy(ctx);

	return ret;
}

int poll_nointr(struct pollfd *pfd, unsigned int num_pfd)
{
	int ret;

	do {
		ret = poll(pfd, num_pfd, -1);
	} while (ret == -1 && errno == EINTR);

	return ret;
}
