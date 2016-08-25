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

#include "../debug.h"
#include "../iio.h"
#include "../iio-config.h"
#include "ops.h"

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef HAVE_AVAHI
#include <avahi-common/simple-watch.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#endif

#define MY_NAME "iiod"

#define IIOD_PORT 30431

#ifndef __bswap_constant_16
#define __bswap_constant_16(x) \
	((unsigned short int) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))
#endif

#ifndef __bswap_constant_32
#define __bswap_constant_32(x) \
	((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
	 (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif

struct client_data {
	int fd;
	bool debug;
	struct iio_context *ctx;
};

bool server_demux;

/*
 * Eventfd that is used to signal all threads that the iiod has been asked to
 * stop and the threads should release all resources and stop.
 */
int stop_fd;

/*
 * This is used to make sure that all active threads have finished cleanup when
 * a STOP event is received. We don't use pthread_join() since for most threads
 * we are OK with them exiting asynchronously and there really is no place to
 * call pthread_join() to free the thread's resources. We only need to
 * synchronize the threads that are still active when the iiod is shutdown to
 * give them a chance to release all resources, disable buffers etc, before
 * iio_context_destroy() is called.
 *
 * In order to avoid race conditions thread_started() must be called before
 * the thread is created and thread_stopped() must be called right before
 * leaving the thread.
 */
static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thread_count_cond = PTHREAD_COND_INITIALIZER;
static unsigned int thread_count;

void thread_started(void)
{
	pthread_mutex_lock(&thread_count_lock);
	thread_count++;
	pthread_mutex_unlock(&thread_count_lock);
}

void thread_stopped(void)
{
	pthread_mutex_lock(&thread_count_lock);
	thread_count--;
	pthread_cond_signal(&thread_count_cond);
	pthread_mutex_unlock(&thread_count_lock);
}

static void wait_for_threads(void)
{
	pthread_mutex_lock(&thread_count_lock);
	while (thread_count)
		pthread_cond_wait(&thread_count_cond, &thread_count_lock);
	pthread_mutex_unlock(&thread_count_lock);
}

static struct sockaddr_in sockaddr = {
	.sin_family = AF_INET,
#if __BYTE_ORDER == __LITTLE_ENDIAN
	.sin_addr.s_addr = __bswap_constant_32(INADDR_ANY),
	.sin_port = __bswap_constant_16(IIOD_PORT),
#else
	.sin_addr.s_addr = INADDR_ANY,
	.sin_port = IIOD_PORT,
#endif
};

#ifdef HAVE_IPV6
static struct sockaddr_in6 sockaddr6 = {
	.sin6_family = AF_INET6,
	.sin6_addr = IN6ADDR_ANY_INIT,
#if __BYTE_ORDER == __LITTLE_ENDIAN
	.sin6_port = __bswap_constant_16(IIOD_PORT),
#else
	.sin6_port = IIOD_PORT,
#endif
};
#endif /* HAVE_IPV6 */

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"version", no_argument, 0, 'V'},
	  {"debug", no_argument, 0, 'd'},
	  {"demux", no_argument, 0, 'D'},
	  {"interactive", no_argument, 0, 'i'},
	  {"aio", no_argument, 0, 'a'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Display the version of this program.",
	"Use alternative (incompatible) debug interface.",
	"Demux channels directly on the server.",
	"Run " MY_NAME " in the controlling terminal.",
	"Use asynchronous I/O.",
};

#ifdef HAVE_AVAHI
static AvahiSimplePoll *avahi_poll;
static AvahiClient *avahi_client;

static void __avahi_group_cb(AvahiEntryGroup *group,
		AvahiEntryGroupState state, void *d)
{
}

static void __avahi_client_cb(AvahiClient *client,
		AvahiClientState state, void *d)
{
	AvahiEntryGroup *group;

	if (state != AVAHI_CLIENT_S_RUNNING)
		return;

	group = avahi_entry_group_new(client, __avahi_group_cb, NULL);

	if (group && !avahi_entry_group_add_service(group,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			0, "iio", "_iio._tcp", NULL, NULL, IIOD_PORT, NULL)) {
		avahi_entry_group_commit(group);
		INFO("Registered to ZeroConf server %s\n",
				avahi_client_get_version_string(client));
	}

	/* NOTE: group is freed by avahi_client_free */
}

static int start_avahi(void)
{
	int ret = ENOMEM;

	avahi_poll = avahi_simple_poll_new();
	if (!avahi_poll)
		return -ENOMEM;

	avahi_client = avahi_client_new(avahi_simple_poll_get(avahi_poll),
			0, __avahi_client_cb, NULL, &ret);
	if (!avahi_client) {
		avahi_simple_poll_free(avahi_poll);
		return -ret;
	}

	return 0;
}

static void stop_avahi(void)
{
	avahi_client_free(avahi_client);
	avahi_simple_poll_free(avahi_poll);
}
#endif /* HAVE_AVAHI */


static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [OPTIONS ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static void * client_thd(void *d)
{
	struct client_data *cdata = d;

	interpreter(cdata->ctx, cdata->fd, cdata->fd, cdata->debug, true, false);

	INFO("Client exited\n");
	close(cdata->fd);
	free(cdata);
	thread_stopped();

	return NULL;
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
	uint64_t ev = 1;
	int ret;

	ret = write(stop_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to shutdown cleanly\n");
		exit(EXIT_FAILURE);
	}
}

static int main_interactive(struct iio_context *ctx, bool verbose, bool use_aio)
{
	int flags;

	/* Specify that we will read sequentially the input FD */
	posix_fadvise(STDIN_FILENO, 0, 0, POSIX_FADV_SEQUENTIAL);

	if (!use_aio) {
		flags = fcntl(STDIN_FILENO, F_GETFL);
		fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
		flags = fcntl(STDOUT_FILENO, F_GETFL);
		fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
	}

	interpreter(ctx, STDIN_FILENO, STDOUT_FILENO, verbose, false, use_aio);
	return EXIT_SUCCESS;
}

static int main_server(struct iio_context *ctx, bool debug)
{
	int ret, fd = -1, yes = 1,
	    keepalive_time = 10,
	    keepalive_intvl = 10,
	    keepalive_probes = 6;
	struct pollfd pfd[2];
	pthread_attr_t attr;
	char err_str[1024];
	bool ipv6;
#ifdef HAVE_AVAHI
	bool avahi_started;
#endif

	INFO("Starting IIO Daemon version %u.%u\n",
			LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR);

#ifdef HAVE_IPV6
	fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
#endif
	ipv6 = (fd >= 0);
	if (!ipv6)
		fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to create socket: %s\n", err_str);
		return EXIT_FAILURE;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef HAVE_IPV6
	if (ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr6,
				sizeof(sockaddr6));
#endif
	if (!ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
	if (ret < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Bind failed: %s\n", err_str);
		goto err_close_socket;
	}

	if (ipv6)
		INFO("IPv6 support enabled\n");

	if (listen(fd, 16) < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to mark as passive socket: %s\n", err_str);
		goto err_close_socket;
	}

#ifdef HAVE_AVAHI
	avahi_started = !start_avahi();
#endif

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = stop_fd;
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	while (true) {
		pthread_t thd;
		struct client_data *cdata;
		struct sockaddr_in caddr;
		sigset_t sigmask, oldsigmask;
		socklen_t addr_len = sizeof(caddr);
		int new;

		poll_nointr(pfd, 2);

		if (pfd[1].revents & POLLIN) /* STOP event */
			break;

		new = accept4(fd, (struct sockaddr *) &caddr, &addr_len,
			SOCK_NONBLOCK);
		if (new == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			iio_strerror(errno, err_str, sizeof(err_str));
			ERROR("Failed to create connection socket: %s\n",
				err_str);
			continue;
		}

		cdata = malloc(sizeof(*cdata));
		if (!cdata) {
			WARNING("Unable to allocate memory for client\n");
			close(new);
			continue;
		}

		/* Configure the socket to send keep-alive packets every 10s,
		 * and disconnect the client if no reply was received for one
		 * minute. */
		setsockopt(new, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
		setsockopt(new, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes,
				sizeof(keepalive_probes));
		setsockopt(new, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time,
				sizeof(keepalive_time));
		setsockopt(new, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl,
				sizeof(keepalive_intvl));
		setsockopt(new, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

		cdata->fd = new;
		cdata->ctx = ctx;
		cdata->debug = debug;

		INFO("New client connected from %s\n",
				inet_ntoa(caddr.sin_addr));
		sigfillset(&sigmask);
		pthread_sigmask(SIG_BLOCK, &sigmask, &oldsigmask);
		thread_started();
		ret = pthread_create(&thd, &attr, client_thd, cdata);
		pthread_sigmask(SIG_SETMASK, &oldsigmask, NULL);
		if (ret) {
			iio_strerror(ret, err_str, sizeof(err_str));
			ERROR("Failed to create new client thread: %s\n",
				err_str);
			close(new);
			free(cdata);
			thread_stopped();
		}
	}

	pthread_attr_destroy(&attr);

	DEBUG("Cleaning up\n");
#ifdef HAVE_AVAHI
	if (avahi_started)
		stop_avahi();
#endif
	close(fd);
	return EXIT_SUCCESS;

err_close_socket:
	close(fd);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	bool debug = false, interactive = false, use_aio = false;
	struct iio_context *ctx;
	int c, option_index = 0;
	char err_str[1024];
	int ret;

	while ((c = getopt_long(argc, argv, "+hVdDia",
					options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			debug = true;
			break;
		case 'D':
			server_demux = true;
			break;
		case 'i':
			interactive = true;
			break;
		case 'a':
#ifdef WITH_AIO
			use_aio = true;
			break;
#else
			ERROR("IIOD was not compiled with AIO support.\n");
			return EXIT_FAILURE;
#endif
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

	stop_fd = eventfd(0, EFD_NONBLOCK);
	if (stop_fd == -1) {
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to create stop eventfd: %s\n", err_str);
		return EXIT_FAILURE;
	}

	set_handler(SIGHUP, sig_handler);
	set_handler(SIGPIPE, sig_handler);
	set_handler(SIGINT, sig_handler);
	set_handler(SIGTERM, sig_handler);

	ctx = iio_create_local_context();
	if (!ctx) {
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to create local context: %s\n", err_str);
		return EXIT_FAILURE;
	}

	if (interactive)
		ret = main_interactive(ctx, debug, use_aio);
	else
		ret = main_server(ctx, debug);

	/*
	 * In case we got here through an error in the main thread make sure all
	 * the worker threads are signaled to shutdown.
	 */
	sig_handler(SIGTERM);

	wait_for_threads();
	iio_context_destroy(ctx);
	close(stop_fd);

	return ret;
}
