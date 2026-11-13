/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "emu_client.h"

/* A plain TCP transport for iiod-client, after daqiri-mgmt.c */
static ssize_t emu_io(struct emu_client *ec, void *data, size_t len, int timeout_ms, bool wr)
{
	struct pollfd pfd = { .fd = ec->fd, .events = wr ? POLLOUT : POLLIN };
	ssize_t ret;

	for (;;) {
		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret < 0)
			return -errno;
		if (!ret)
			return -ETIMEDOUT;

		if (wr)
			ret = send(ec->fd, data, len, 0);
		else
			ret = recv(ec->fd, data, len, 0);
		if (ret > 0)
			return ret;
		if (!ret)
			return -EPIPE;
		if (errno != EINTR && errno != EAGAIN)
			return -errno;
	}
}

static ssize_t emu_read(struct iiod_client_pdata *desc, char *dst, size_t len, int timeout_ms)
{
	return emu_io((struct emu_client *)desc, dst, len, timeout_ms, false);
}

static ssize_t emu_write(
		struct iiod_client_pdata *desc, const char *src, size_t len, int timeout_ms)
{
	return emu_io((struct emu_client *)desc, (void *)src, len, timeout_ms, true);
}

/* Wakes the client's reader thread so that iiod_client_destroy() can join it */
static void emu_cancel(struct iiod_client_pdata *desc)
{
	shutdown(((struct emu_client *)desc)->fd, SHUT_RDWR);
}

static const struct iiod_client_ops emu_ops = {
	.write = emu_write,
	.read = emu_read,
	.cancel = emu_cancel,
};

/* iiod_client_create_context() needs a backend for the context it builds.
 * Nothing in the tests goes through it. */
static const struct iio_backend_ops emu_backend_ops = { 0 };

static const struct iio_backend emu_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "emu-client-test",
	.uri_prefix = "emu-client-test:",
	.ops = &emu_backend_ops,
	.default_timeout_ms = 5000,
};

static const struct iio_context_params emu_params = {
	.log_level = LEVEL_WARNING,
	.timeout_ms = 5000,
};

static int emu_connect(struct emu_client *ec)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(EMU_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	int err, yes = 1;

	ec->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (ec->fd < 0)
		return -errno;

	if (connect(ec->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 ||
			setsockopt(ec->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
		err = -errno;
		close(ec->fd);
		ec->fd = -1;
		return err;
	}

	return 0;
}

int emu_client_open(struct emu_client *ec)
{
	int err;

	/* A server that drops the connection must fail the test, not kill it */
	signal(SIGPIPE, SIG_IGN);

	ec->client = NULL;
	ec->ctx = NULL;

	err = emu_connect(ec);
	if (err) {
		printf("Unable to connect to 127.0.0.1:%d: %d\n", EMU_PORT, err);
		return err;
	}

	ec->client = iiod_client_new(&emu_params, (struct iiod_client_pdata *)ec, &emu_ops);
	err = iio_err(ec->client);
	if (err) {
		printf("Unable to create the iiod client: %d\n", err);
		ec->client = NULL;
		goto err_close;
	}

	ec->ctx = iiod_client_create_context(ec->client, &emu_backend, NULL, NULL, NULL, 0);
	err = iio_err(ec->ctx);
	if (err) {
		printf("Unable to create the context: %d\n", err);
		ec->ctx = NULL;
		goto err_close;
	}

	return 0;

err_close:
	emu_client_close(ec);
	return err;
}

void emu_client_close(struct emu_client *ec)
{
	/* The client logs through the context's params: destroy it first */
	if (ec->client)
		iiod_client_destroy(ec->client);
	if (ec->fd >= 0)
		close(ec->fd);
	if (ec->ctx)
		iio_context_destroy(ec->ctx);

	ec->client = NULL;
	ec->ctx = NULL;
	ec->fd = -1;
}
