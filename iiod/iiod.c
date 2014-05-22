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
#include "ops.h"

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/* For some reason, <netinet/tcp.h> needs this to define SOL_TCP */
#define __USE_MISC
#include <netinet/tcp.h>
#undef __USE_MISC

#ifdef HAVE_AVAHI
#include <avahi-common/simple-watch.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#endif

#define MY_NAME "iiod"

#define IIOD_PORT 30431

struct client_data {
	int fd;
	bool debug;
	struct iio_context *ctx;
};

bool server_demux;

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

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"version", no_argument, 0, 'V'},
	  {"debug", no_argument, 0, 'd'},
	  {"demux", no_argument, 0, 'D'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Display the version of this program.",
	"Use alternative (incompatible) debug interface.",
	"Demux channels directly on the server.",
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
	FILE *f = fdopen(cdata->fd, "r+b");
	if (!f) {
		ERROR("Unable to reopen socket\n");
		return NULL;
	}

	interpreter(cdata->ctx, f, f, cdata->debug);

	INFO("Client exited\n");
	fclose(f);
	close(cdata->fd);
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
	/* This does nothing, but it permits accept() to exit */
}

int main(int argc, char **argv)
{
	int fd;
	struct iio_context *ctx;
	char *backend = getenv("LIBIIO_BACKEND");
	bool debug = false, xml_backend = backend && !strcmp(backend, "xml");
	int c, option_index = 0, arg_index = xml_backend;
	int yes = 1;
	int keepalive_time = 10,
	    keepalive_intvl = 10,
	    keepalive_probes = 6;
#ifdef HAVE_AVAHI
	bool avahi_started;
#endif

	while ((c = getopt_long(argc, argv, "+hVdD",
					options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			debug = true;
			arg_index++;
			break;
		case 'D':
			server_demux = true;
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

	if (arg_index >= argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	if (xml_backend) {
		if (argc < 2) {
			ERROR("The XML backend requires the XML file to be "
					"passed as argument\n");
			return EXIT_FAILURE;
		}

		DEBUG("Creating XML IIO context\n");
		ctx = iio_create_xml_context(argv[arg_index]);
	} else {
		DEBUG("Creating local IIO context\n");
		ctx = iio_create_local_context();
	}
	if (!ctx) {
		ERROR("Unable to create local context\n");
		return EXIT_FAILURE;
	}

	INFO("Starting IIO Daemon version %u.%u\n",
			LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		ERROR("Unable to create socket: %s\n", strerror(errno));
		goto err_close_ctx;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	if (bind(fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
		ERROR("Bind failed: %s\n", strerror(errno));
		goto err_close_socket;
	}

	if (listen(fd, 16) < 0) {
		ERROR("Unable to mark as passive socket: %s\n",
				strerror(errno));
		goto err_close_socket;
	}

	set_handler(SIGHUP, sig_handler);
	set_handler(SIGINT, sig_handler);
	set_handler(SIGSEGV, sig_handler);
	set_handler(SIGTERM, sig_handler);

#ifdef HAVE_AVAHI
	avahi_started = !start_avahi();
#endif

	while (true) {
		pthread_t thd;
		pthread_attr_t attr;
		struct client_data *cdata;
		struct sockaddr_in caddr;
		socklen_t addr_len = sizeof(caddr);
		int new = accept(fd, (struct sockaddr *) &caddr, &addr_len);
		if (new == -1) {
			if (errno == EINTR)
				break;
			ERROR("Failed to create connection socket: %s\n",
					strerror(errno));
			goto err_stop_avahi;
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
		setsockopt(new, SOL_TCP, TCP_KEEPCNT, &keepalive_probes,
				sizeof(keepalive_probes));
		setsockopt(new, SOL_TCP, TCP_KEEPIDLE, &keepalive_time,
				sizeof(keepalive_time));
		setsockopt(new, SOL_TCP, TCP_KEEPINTVL, &keepalive_intvl,
				sizeof(keepalive_intvl));

		cdata->fd = new;
		cdata->ctx = ctx;
		cdata->debug = debug;

		INFO("New client connected from %s\n",
				inet_ntoa(caddr.sin_addr));
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&thd, &attr, client_thd, cdata);
	}

	DEBUG("Cleaning up\n");
#ifdef HAVE_AVAHI
	if (avahi_started)
		stop_avahi();
#endif
	close(fd);
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;

err_stop_avahi:
#ifdef HAVE_AVAHI
	if (avahi_started)
		stop_avahi();
#endif
err_close_socket:
	close(fd);
err_close_ctx:
	iio_context_destroy(ctx);
	return EXIT_FAILURE;
}
