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
#include "thread-pool.h"

#include <arpa/inet.h>
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
#include <avahi-common/thread-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/domain.h>
#endif

#define MY_NAME "iiod"

#define IIOD_PORT 30431

struct client_data {
	int fd;
	bool debug;
	struct iio_context *ctx;
};

bool server_demux;

struct thread_pool *main_thread_pool;


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
	  {"ffs", required_argument, 0, 'F'},
	  {"nb-pipes", required_argument, 0, 'n'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Display the version of this program.",
	"Use alternative (incompatible) debug interface.",
	"Demux channels directly on the server.",
	"Run " MY_NAME " in the controlling terminal.",
	"Use asynchronous I/O.",
	"Use the given FunctionFS mountpoint to serve over USB",
	"Specify the number of USB pipes (ep couples) to use",
};

#ifdef HAVE_AVAHI

/***
 * Parts of the avahi code are borrowed from the client-publish-service.c
 * https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 * which is an example in the avahi library. Copyright Lennart Poettering
 * released under the LGPL 2.1 or (at your option) any later version.
 */

/* Global Data */

static struct avahi_data {
	AvahiThreadedPoll *poll;
	AvahiClient *client;
	AvahiEntryGroup *group;
	char * name;
} avahi;

static void create_services(AvahiClient *c);
static AvahiClient * client_new(void);

static void client_free()
{
	/* This also frees the entry group, if any. */
	if (avahi.client) {
		avahi_client_free(avahi.client);
		avahi.client = NULL;
		avahi.group = NULL;
	}
}

static void shutdown_avahi()
{
	/* Stop the avahi client, if it's running. */
	if (avahi.poll)
		avahi_threaded_poll_stop(avahi.poll);

	/* Clean up the avahi objects. The order is significant. */
	client_free();
	if (avahi.poll) {
		avahi_threaded_poll_free(avahi.poll);
		avahi.poll = NULL;
	}
	if (avahi.name) {
		IIO_INFO("Avahi: Removing service '%s'\n", avahi.name);
		avahi_free(avahi.name);
		avahi.name = NULL;
	}
}

static void __avahi_group_cb(AvahiEntryGroup *group,
		AvahiEntryGroupState state, void *d)
{
	/* Called whenever the entry group state changes */
	if (!group)  {
		IIO_ERROR("__avahi_group_cb with no valid group\n");
		return;
	}

	avahi.group = group;

	switch (state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED :
			IIO_INFO("Avahi: Service '%s' successfully established.\n",
					avahi.name);
			break;
		case AVAHI_ENTRY_GROUP_COLLISION : {
			char *n;
			/* A service name collision, pick a new name */
			n = avahi_alternative_service_name(avahi.name);
			avahi_free(avahi.name);
			avahi.name = n;
			IIO_INFO("Avahi: Group Service name collision, renaming service to '%s'\n",
					avahi.name);
			create_services(avahi_entry_group_get_client(group));
			break;
		}
		case AVAHI_ENTRY_GROUP_FAILURE :
			IIO_ERROR("Entry group failure: %s\n",
					avahi_strerror(avahi_client_errno(
						avahi_entry_group_get_client(group))));
			break;
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
			/* This is normal,
			 * since we commit things in the create_services()
			 */
			IIO_DEBUG("Avahi: Group uncommitted\n");
			break;
		case AVAHI_ENTRY_GROUP_REGISTERING:
			IIO_DEBUG("Avahi: Group registering\n");
			break;
	}
}

static void __avahi_client_cb(AvahiClient *client,
		AvahiClientState state, void *d)
{
	if (!client) {
		IIO_ERROR("__avahi_client_cb with no valid client\n");
		return;
	}

	switch (state) {
		case AVAHI_CLIENT_S_RUNNING:
			/* Same as AVAHI_SERVER_RUNNING */
			IIO_DEBUG("Avahi: create services\n");
			/* The server has startup successfully, so create our services */
			create_services(client);
			break;
		case AVAHI_CLIENT_FAILURE:
			if (avahi_client_errno(client) != AVAHI_ERR_DISCONNECTED) {
				IIO_ERROR("Avahi: Client failure: %s\n",
					avahi_strerror(avahi_client_errno(client)));
				break;
			}
			IIO_INFO("Avahi: server disconnected\n");
			avahi_client_free(client);
			avahi.group = NULL;
			avahi.client = client_new();
			break;
		case AVAHI_CLIENT_S_COLLISION:
			/* Same as AVAHI_SERVER_COLLISION */
			/* When the server is back in AVAHI_SERVER_RUNNING state
			 * we will register them again with the new host name. */
			IIO_DEBUG("Avahi: Client collision\n");
			/* Let's drop our registered services.*/
			if (avahi.group)
				avahi_entry_group_reset(avahi.group);
			break;
		case AVAHI_CLIENT_S_REGISTERING:
			/* Same as AVAHI_SERVER_REGISTERING */
			IIO_DEBUG("Avahi: Client group reset\n");
			if (avahi.group)
				avahi_entry_group_reset(avahi.group);
			break;
		case AVAHI_CLIENT_CONNECTING:
			IIO_DEBUG("Avahi: Client Connecting\n");
			break;
	}

	/* NOTE: group is freed by avahi_client_free */
}

static AvahiClient * client_new(void)
{
	int ret;
	AvahiClient * client;

	client = avahi_client_new(avahi_threaded_poll_get(avahi.poll),
			AVAHI_CLIENT_NO_FAIL, __avahi_client_cb, NULL, &ret);

	/* No Daemon is handled by the avahi_start thread */
	if (!client && ret != AVAHI_ERR_NO_DAEMON) {
		IIO_ERROR("Avahi: failure creating client: %s (%d)\n",
				avahi_strerror(ret), ret);
	}

	return client;
}


static void create_services(AvahiClient *c)
{
	int ret;

	if (!c) {
		IIO_ERROR("create_services called with no valid client\n");
		goto fail;
	}

	if (!avahi.group) {
		avahi.group = avahi_entry_group_new(c, __avahi_group_cb, NULL);
		if (!avahi.group) {
			IIO_ERROR("avahi_entry_group_new() failed: %s\n",
					avahi_strerror(avahi_client_errno(c)));
			goto fail;
		}
	}

	if (!avahi_entry_group_is_empty(avahi.group)) {
		IIO_DEBUG("Avahi group not empty\n");
		return;
	}

	ret = avahi_entry_group_add_service(avahi.group,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			0, avahi.name, "_iio._tcp", NULL, NULL, IIOD_PORT, NULL);
	if (ret < 0) {
		if (ret == AVAHI_ERR_COLLISION) {
			char *n;
			n = avahi_alternative_service_name(avahi.name);
			avahi_free(avahi.name);
			avahi.name = n;
			IIO_DEBUG("Service name collision, renaming service to '%s'\n",
					avahi.name);
			avahi_entry_group_reset(avahi.group);
			create_services(c);
			return;
		}
		IIO_ERROR("Failed to add _iio._tcp service: %s\n", avahi_strerror(ret));
		goto fail;
	}

	ret = avahi_entry_group_commit(avahi.group);
	if (ret < 0) {
		IIO_ERROR("Failed to commit entry group: %s\n", avahi_strerror(ret));
		goto fail;
	}

	IIO_INFO("Avahi: Registered '%s' to ZeroConf server %s\n",
			avahi.name, avahi_client_get_version_string(c));

	return;

fail:
	avahi_entry_group_reset(avahi.group);
}

#define IIOD_ON "iiod on "

static void start_avahi_thd(struct thread_pool *pool, void *d)
{

	struct pollfd pfd[2];
	int ret = ENOMEM;
	char label[AVAHI_LABEL_MAX];
	char host[AVAHI_LABEL_MAX - sizeof(IIOD_ON)];
	struct timespec ts;
	ts.tv_nsec = 0;
	ts.tv_sec = 1;

	pfd[1].fd = thread_pool_get_poll_fd(main_thread_pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	while(true) {
		if (pfd[1].revents & POLLIN) /* STOP event */
			break;

		ret = gethostname(host, sizeof(host));
		IIO_ERROR("host %s\n", host);
		if (ret || !strncmp(host, "none", sizeof("none") - 1))
			goto again;

		iio_snprintf(label, sizeof(label), "%s%s", IIOD_ON, host);

		if (!avahi.name)
			avahi.name = avahi_strdup(label);
		if (!avahi.name)
			break;

		if (!avahi.poll)
			avahi.poll = avahi_threaded_poll_new();
		if (!avahi.poll) {
			goto again;
		}

		if (!avahi.client)
			avahi.client = client_new();
		if (avahi.client)
			break;
again:
		IIO_INFO("Avahi didn't start, try again later\n");
		nanosleep(&ts, NULL);
		ts.tv_sec++;
		/* If it hasn't started in 10 times over 60 seconds,
		 * it is not going to, so stop
		 */
		if (ts.tv_sec >= 11)
			break;
	}

	if (avahi.client && avahi.poll) {
		avahi_threaded_poll_start(avahi.poll);
		IIO_INFO("Avahi: Started.\n");
	} else  {
		shutdown_avahi();
		IIO_INFO("Avahi: Failed to start.\n");
	}
}

static void start_avahi(void)
{
	int ret;
	char err_str[1024];

	IIO_INFO("Attempting to start Avahi\n");

	avahi.poll = NULL;
	avahi.client = NULL;
	avahi.group = NULL;
	avahi.name = NULL;

	/* In case dbus, or avahi deamon are not started, we create a thread
	 * that tries a few times to attach, if it can't within the first
	 * minute, it gives up.
	 */
	ret = thread_pool_add_thread(main_thread_pool, start_avahi_thd, NULL, "avahi_thd");
	if (ret) {
		iio_strerror(ret, err_str, sizeof(err_str));
		IIO_ERROR("Failed to create new Avahi thread: %s\n",
				err_str);
	}
}
static void stop_avahi(void)
{
	shutdown_avahi();
	IIO_INFO("Avahi: Stopped\n");
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

static void client_thd(struct thread_pool *pool, void *d)
{
	struct client_data *cdata = d;

	interpreter(cdata->ctx, cdata->fd, cdata->fd, cdata->debug,
			true, false, pool);

	IIO_INFO("Client exited\n");
	close(cdata->fd);
	free(cdata);
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

static int main_interactive(struct iio_context *ctx, bool verbose, bool use_aio)
{
	int flags;

	if (!use_aio) {
		flags = fcntl(STDIN_FILENO, F_GETFL);
		if (flags >= 0)
			flags = fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
		if (flags < 0) {
			char err_str[1024];
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_ERROR("Could not get/set O_NONBLOCK on STDIN_FILENO"
					" %s\n", err_str);
		}

		flags = fcntl(STDOUT_FILENO, F_GETFL);
		if (flags >= 0)
			flags = fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
		if (flags < 0) {
			char err_str[1024];
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_ERROR("Could not get/set O_NONBLOCK on STDOUT_FILENO"
					" %s\n", err_str);
		}
	}

	interpreter(ctx, STDIN_FILENO, STDOUT_FILENO, verbose,
			false, use_aio, main_thread_pool);
	return EXIT_SUCCESS;
}

static int main_server(struct iio_context *ctx, bool debug)
{
	int ret, fd = -1, yes = 1,
	    keepalive_time = 10,
	    keepalive_intvl = 10,
	    keepalive_probes = 6;
	struct pollfd pfd[2];
	char err_str[1024];
	bool ipv6;

	IIO_INFO("Starting IIO Daemon version %u.%u.%s\n",
			LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR,
			LIBIIO_VERSION_GIT);

#ifdef HAVE_IPV6
	fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
#endif
	ipv6 = (fd >= 0);
	if (!ipv6)
		fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Unable to create socket: %s\n", err_str);
		return EXIT_FAILURE;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	if (ret < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_WARNING("setsockopt SO_REUSEADDR : %s\n", err_str);
	}

#ifdef HAVE_IPV6
	if (ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr6,
				sizeof(sockaddr6));
#endif
	if (!ipv6)
		ret = bind(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
	if (ret < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Bind failed: %s\n", err_str);
		goto err_close_socket;
	}

	if (ipv6)
		IIO_INFO("IPv6 support enabled\n");

	if (listen(fd, 16) < 0) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Unable to mark as passive socket: %s\n", err_str);
		goto err_close_socket;
	}

#ifdef HAVE_AVAHI
	start_avahi();
#endif

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(main_thread_pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	while (true) {
		struct client_data *cdata;
		struct sockaddr_in caddr;
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
			IIO_ERROR("Failed to create connection socket: %s\n",
				err_str);
			continue;
		}

		cdata = malloc(sizeof(*cdata));
		if (!cdata) {
			IIO_WARNING("Unable to allocate memory for client\n");
			close(new);
			continue;
		}

		/* Configure the socket to send keep-alive packets every 10s,
		 * and disconnect the client if no reply was received for one
		 * minute. */
		ret = setsockopt(new, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
		if (ret < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_WARNING("setsockopt SO_KEEPALIVE : %s", err_str);
		}
		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_probes,
				sizeof(keepalive_probes));
		if (ret < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_WARNING("setsockopt TCP_KEEPCNT : %s", err_str);
		}
		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPIDLE, &keepalive_time,
				sizeof(keepalive_time));
		if (ret < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_WARNING("setsockopt TCP_KEEPIDLE : %s", err_str);
		}
		ret = setsockopt(new, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_intvl,
				sizeof(keepalive_intvl));
		if (ret < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_WARNING("setsockopt TCP_KEEPINTVL : %s", err_str);
		}
		ret = setsockopt(new, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
		if (ret < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_WARNING("setsockopt TCP_NODELAY : %s", err_str);
		}

		cdata->fd = new;
		cdata->ctx = ctx;
		cdata->debug = debug;

		IIO_INFO("New client connected from %s\n",
				inet_ntoa(caddr.sin_addr));

		ret = thread_pool_add_thread(main_thread_pool, client_thd, cdata, "net_client_thd");
		if (ret) {
			iio_strerror(ret, err_str, sizeof(err_str));
			IIO_ERROR("Failed to create new client thread: %s\n",
				err_str);
			close(new);
			free(cdata);
		}
	}

	IIO_DEBUG("Cleaning up\n");
#ifdef HAVE_AVAHI
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
#ifdef WITH_IIOD_USBD
	long nb_pipes = 3;
	char *end;
#endif
	struct iio_context *ctx;
	int c, option_index = 0;
	char *ffs_mountpoint = NULL;
	char err_str[1024];
	int ret;

	while ((c = getopt_long(argc, argv, "+hVdDiaF:n:",
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
			IIO_ERROR("IIOD was not compiled with AIO support.\n");
			return EXIT_FAILURE;
#endif
		case 'F':
#ifdef WITH_IIOD_USBD
			ffs_mountpoint = optarg;
			break;
#else
			IIO_ERROR("IIOD was not compiled with USB support.\n");
			return EXIT_FAILURE;
#endif
		case 'n':
#ifdef WITH_IIOD_USBD
			errno = 0;
			nb_pipes = strtol(optarg, &end, 10);
			if (optarg == end || nb_pipes < 1 || errno == ERANGE) {
				IIO_ERROR("--nb-pipes: Invalid parameter\n");
				return EXIT_FAILURE;
			}
			break;
#else
			IIO_ERROR("IIOD was not compiled with USB support.\n");
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

	ctx = iio_create_local_context();
	if (!ctx) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Unable to create local context: %s\n", err_str);
		return EXIT_FAILURE;
	}

	main_thread_pool = thread_pool_new();
	if (!main_thread_pool) {
		iio_strerror(errno, err_str, sizeof(err_str));
		IIO_ERROR("Unable to create thread pool: %s\n", err_str);
		ret = EXIT_FAILURE;
		goto out_destroy_context;
	}

	set_handler(SIGHUP, sig_handler);
	set_handler(SIGPIPE, sig_handler);
	set_handler(SIGINT, sig_handler);
	set_handler(SIGTERM, sig_handler);

	if (ffs_mountpoint) {
#ifdef WITH_IIOD_USBD
		/* We pass use_aio == true directly, this is ensured to be true
		 * by the CMake script. */
		ret = start_usb_daemon(ctx, ffs_mountpoint,
				debug, true, (unsigned int) nb_pipes,
				main_thread_pool);
		if (ret) {
			iio_strerror(-ret, err_str, sizeof(err_str));
			IIO_ERROR("Unable to start USB daemon: %s\n", err_str);
			ret = EXIT_FAILURE;
			goto out_destroy_thread_pool;
		}
#endif
	}

	if (interactive)
		ret = main_interactive(ctx, debug, use_aio);
	else
		ret = main_server(ctx, debug);

	/*
	 * In case we got here through an error in the main thread make sure all
	 * the worker threads are signaled to shutdown.
	 */

#ifdef WITH_IIOD_USBD
out_destroy_thread_pool:
#endif
	thread_pool_stop_and_wait(main_thread_pool);
	thread_pool_destroy(main_thread_pool);

out_destroy_context:
	iio_context_destroy(ctx);

	return ret;
}
