// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio-lock.h>
#include <iio/iio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

#include "network.h"

#define DEFAULT_PORT 30431
#define BACKLOG 16

struct client {
	struct client *next;
	struct iiod_emu *emu;
	struct iio_thrd *thrd;
	emu_socket sock;
	char peer[32];
	bool done;
};

struct iiod_emu {
	struct iio_context *ctx;
	const char *xml;
	size_t xml_len;
	struct iio_mutex *lock;
	struct client *clients;
};

static const struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ NULL, 0, NULL, 0 },
};

static void usage(void)
{
	printf("Usage: iiod-emu [OPTIONS] <device.xml>\n"
	       "Options:\n"
	       "\t-h, --help\t\tDisplay this help message\n"
	       "\t-p, --port <port>\tPort to listen on (default: %u)\n",
			DEFAULT_PORT);
}

static bool parse_port(const char *str, uint16_t *port)
{
	unsigned long val;
	char *end;

	errno = 0;
	val = strtoul(str, &end, 10);

	if (end == str || *end != '\0' || errno == ERANGE || !val || val > UINT16_MAX)
		return false;

	*port = (uint16_t)val;

	return true;
}

static ssize_t emu_read_cb(struct iiod_pdata *pdata, void *buf, size_t len)
{
	struct client *client = (struct client *)pdata;

	return emu_socket_read(client->sock, buf, len);
}

static ssize_t emu_write_cb(struct iiod_pdata *pdata, const void *buf, size_t len)
{
	struct client *client = (struct client *)pdata;

	return emu_socket_write(client->sock, buf, len);
}

static int client_thrd(void *d)
{
	struct client *client = d;
	struct iiod_emu *emu = client->emu;

	printf("Client %s connected\n", client->peer);

	iiod_interpreter(emu->ctx, (struct iiod_pdata *)client, emu_read_cb, emu_write_cb, emu->xml,
			emu->xml_len);

	printf("Client %s disconnected\n", client->peer);

	emu_socket_close(client->sock);

	iio_mutex_lock(emu->lock);
	client->done = true;
	iio_mutex_unlock(emu->lock);

	return 0;
}

/* Join and free every client thread that has finished. */
static void reap_clients(struct iiod_emu *emu, bool wait_all)
{
	struct client **prev, *client, *reap = NULL;

	iio_mutex_lock(emu->lock);
	for (prev = &emu->clients; (client = *prev);) {
		if (!wait_all && !client->done) {
			prev = &client->next;
			continue;
		}

		*prev = client->next;
		client->next = reap;
		reap = client;
	}
	iio_mutex_unlock(emu->lock);

	while ((client = reap)) {
		reap = client->next;
		iio_thrd_join_and_destroy(client->thrd);
		free(client);
	}
}

static int spawn_client(struct iiod_emu *emu, emu_socket sock, const char *peer)
{
	struct client *client;
	int err;

	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->emu = emu;
	client->sock = sock;
	snprintf(client->peer, sizeof(client->peer), "%s", peer);

	iio_mutex_lock(emu->lock);
	client->next = emu->clients;
	emu->clients = client;
	iio_mutex_unlock(emu->lock);

	client->thrd = iio_thrd_create(client_thrd, client, "iiod-emu-client");
	err = iio_err(client->thrd);
	if (err) {
		iio_mutex_lock(emu->lock);
		emu->clients = client->next;
		iio_mutex_unlock(emu->lock);
		free(client);
		return err;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct iio_context_params params = { .log_level = LEVEL_INFO };
	struct iiod_emu emu = { 0 };
	emu_socket srv = EMU_INVALID_SOCKET;
	uint16_t port = DEFAULT_PORT;
	int ret = EXIT_FAILURE;
	char err_str[256];
	char *uri = NULL;
	size_t uri_len;
	int c, err;

	/*
	 * Our progress messages are useless if they sit in a buffer until we
	 * exit, which is what happens as soon as stdout is not a tty. MSVC
	 * treats _IOLBF as _IOFBF, so go fully unbuffered.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	while ((c = getopt_long(argc, argv, "+hp:", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'p':
			if (!parse_port(optarg, &port)) {
				fprintf(stderr, "Invalid port: %s\n\n", optarg);
				usage();
				return EXIT_FAILURE;
			}
			break;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	if (optind + 1 != argc) {
		fprintf(stderr, "Expected exactly one XML description file.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	/* No PATH_MAX on MSVC, and this also lifts the length limit. */
	uri_len = sizeof("emu:") + strlen(argv[optind]);
	uri = malloc(uri_len);
	if (!uri)
		return EXIT_FAILURE;

	snprintf(uri, uri_len, "emu:%s", argv[optind]);

	if (emu_network_init())
		goto out_free_uri;

	err = iiod_init();
	if (err) {
		fprintf(stderr, "Unable to initialize tinyiiod\n");
		goto out_network_deinit;
	}

	emu.lock = iio_mutex_create();
	if (iio_err(emu.lock)) {
		fprintf(stderr, "Unable to create mutex\n");
		emu.lock = NULL;
		goto out_iiod_cleanup;
	}

	emu.ctx = iio_create_context(&params, uri);
	err = iio_err(emu.ctx);
	if (err) {
		iio_strerror(-err, err_str, sizeof(err_str));
		fprintf(stderr, "Unable to create context from %s: %s\n", uri, err_str);
		emu.ctx = NULL;
		goto out_mutex_destroy;
	}

	emu.xml = iio_context_get_xml(emu.ctx);
	if (!emu.xml) {
		fprintf(stderr, "Unable to serialize the context\n");
		goto out_ctx_destroy;
	}
	emu.xml_len = strlen(emu.xml) + 1;

	srv = emu_socket_listen(port, BACKLOG);
	if (srv == EMU_INVALID_SOCKET)
		goto out_ctx_destroy;

	printf("Emulating %u device(s) from %s\n", iio_context_get_devices_count(emu.ctx),
			argv[optind]);
	printf("Listening on port %u; connect with: iio_info -u ip:127.0.0.1:%u\n", port, port);

	for (;;) {
		char peer[32];
		emu_socket sock;

		sock = emu_socket_accept(srv, peer, sizeof(peer));
		if (sock == EMU_INVALID_SOCKET)
			break;

		reap_clients(&emu, false);

		err = spawn_client(&emu, sock, peer);
		if (err) {
			iio_strerror(-err, err_str, sizeof(err_str));
			fprintf(stderr, "Unable to handle client %s: %s\n", peer, err_str);
			emu_socket_close(sock);
		}
	}

	/*
	 * The only way out of the loop is a fatal accept() failure, which
	 * emu_socket_accept() has already reported. Leave <ret> at EXIT_FAILURE;
	 * a graceful shutdown will have to set success explicitly.
	 */
	emu_socket_close(srv);
	reap_clients(&emu, true);
out_ctx_destroy:
	iio_context_destroy(emu.ctx);
out_mutex_destroy:
	iio_mutex_destroy(emu.lock);
out_iiod_cleanup:
	iiod_cleanup();
out_network_deinit:
	emu_network_deinit();
out_free_uri:
	free(uri);

	return ret;
}
