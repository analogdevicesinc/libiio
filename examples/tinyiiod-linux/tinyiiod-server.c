// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Dan Nechita <dan.nechita@analog.com>
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <tinyiiod/tinyiiod.h>
#include <unistd.h>

#define IIOD_PORT 30431
#define BACKLOG 5

/* Global flag for graceful shutdown */
static volatile bool running = true;

/* Signal handler for graceful shutdown */
static void signal_handler(int signum)
{
	(void)signum;
	running = false;
}

/* ========================================================================
 * SIMPLE BACKEND IMPLEMENTATION
 * ======================================================================== */

struct simple_device {
	char name[64];
	int test_value;
};

static struct simple_device g_simple = {
	.name = "Simple IIO Device",
	.test_value = 42,
};

static ssize_t simple_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const char *attr_name = iio_attr_get_name(attr);
	ssize_t ret;

	if (strcmp(attr_name, "name") == 0) {
		ret = snprintf(dst, len, "%s", g_simple.name);
	} else if (strcmp(attr_name, "test_value") == 0) {
		ret = snprintf(dst, len, "%d", g_simple.test_value);
	} else {
		return -ENOENT;
	}

	if (ret >= 0 && ret < (ssize_t)len - 1) {
		return ret + 1;  /* Include the null terminator */
	}

	return ret;
}

static ssize_t simple_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const char *attr_name = iio_attr_get_name(attr);

	if (strcmp(attr_name, "test_value") == 0) {
		int value;
		if (sscanf(src, "%d", &value) != 1) {
			return -EINVAL;
		}
		g_simple.test_value = value;
		return len;
	}

	return -ENOENT;
}

static struct iio_context *simple_create_context(
		const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	int ret;

	(void)args;

	ctx = iio_context_create_from_backend(
			params, &iio_external_backend, "Linux tinyIIOD reference", 1, 0, 0, "v1.0.0");
	if (iio_err(ctx)) {
		return ctx;
	}

	dev = iio_context_add_device(ctx, "iio:device0", "simple0", NULL);
	if (iio_err(dev)) {
		iio_context_destroy(ctx);
		return iio_err_cast(dev);
	}

	ret = iio_device_add_attr(dev, "name", IIO_ATTR_TYPE_DEVICE);
	if (ret < 0) {
		iio_context_destroy(ctx);
		return iio_ptr(ret);
	}

	ret = iio_device_add_attr(dev, "test_value", IIO_ATTR_TYPE_DEVICE);
	if (ret < 0) {
		iio_context_destroy(ctx);
		return iio_ptr(ret);
	}

	return ctx;
}

/* Backend operations */
static const struct iio_backend_ops simple_ops = {
	.create = simple_create_context,
	.read_attr = simple_read_attr,
	.write_attr = simple_write_attr,
};

/* Backend definition */
const struct iio_backend iio_external_backend = {
	.name = "simple",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 5000,
	.uri_prefix = "simple:",
	.ops = &simple_ops,
};

/* ========================================================================
 * NETWORK TRANSPORT
 * ======================================================================== */

struct client_data {
	int fd;
	struct sockaddr_in addr;
};

/* Network read callback - blocks until data arrives */
static ssize_t network_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct client_data *client = (struct client_data *)pdata;
	uint8_t *buffer = (uint8_t *)buf;
	size_t bytes_read = 0;
	ssize_t ret;

	while (bytes_read < size) {
		ret = recv(client->fd, buffer + bytes_read, size - bytes_read, 0);
		if (ret > 0) {
			bytes_read += ret;
		} else if (ret == 0) {
			/* Connection closed */
			return 0;
		} else {
			if (errno == ECONNRESET || errno == ENOTCONN) {
				fprintf(stderr, "Connection closed by peer\n");
			} else {
				perror("recv");
			}
			return -1;
		}
	}

	return bytes_read;
}

static ssize_t network_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct client_data *client = (struct client_data *)pdata;
	const uint8_t *buffer = (const uint8_t *)buf;
	size_t bytes_sent = 0;
	ssize_t ret;

	while (bytes_sent < size) {
		ret = send(client->fd, buffer + bytes_sent, size - bytes_sent, 0);
		if (ret > 0) {
			bytes_sent += ret;
		} else {
			perror("send");
			return -1;
		}
	}

	return bytes_sent;
}

static int create_server_socket(void)
{
	int server_fd;
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_addr = { .s_addr = INADDR_ANY },
		.sin_port = htons(IIOD_PORT),
	};
	int reuse = 1;
	int ret;

	server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_fd < 0) {
		perror("socket");
		return -1;
	}

	ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (ret < 0) {
		perror("setsockopt");
		close(server_fd);
		return -1;
	}

	ret = bind(server_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		perror("bind");
		close(server_fd);
		return -1;
	}

	ret = listen(server_fd, BACKLOG);
	if (ret < 0) {
		perror("listen");
		close(server_fd);
		return -1;
	}

	return server_fd;
}

static void handle_client(int client_fd, struct sockaddr_in *client_addr, struct iio_context *ctx,
		const void *xml, size_t xml_len)
{
	struct client_data client = {
		.fd = client_fd,
		.addr = *client_addr,
	};
	char client_ip[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
	printf("Client connected from %s:%d\n", client_ip, ntohs(client_addr->sin_port));

	/* Run tinyIIOD interpreter */
	iiod_interpreter(ctx, (struct iiod_pdata *)&client, network_read, network_write, xml,
			xml_len);

	printf("Client disconnected from %s:%d\n", client_ip, ntohs(client_addr->sin_port));
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(void)
{
	struct iio_context_params params = {
		.log_level = LEVEL_INFO,
	};
	struct iio_context *ctx = NULL;
	const char *xml = NULL;
	size_t xml_len;
	int server_fd = -1;
	int ret = EXIT_FAILURE;

	printf("tinyIIOD Linux reference server\n");
	printf("================================\n\n");

	/* Set up signal handlers */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Initialize tinyIIOD */
	if (iiod_init() < 0) {
		fprintf(stderr, "Failed to initialize tinyIIOD\n");
		goto cleanup;
	}

	/* Create IIO context */
	ctx = iio_create_context(&params, "simple:");
	if (iio_err(ctx)) {
		char err_buf[256];
		iio_strerror(-iio_err(ctx), err_buf, sizeof(err_buf));
		fprintf(stderr, "Failed to create IIO context: %s\n", err_buf);
		ctx = NULL;
		goto cleanup;
	}

	/* Get XML representation */
	xml = iio_context_get_xml(ctx);
	if (!xml) {
		fprintf(stderr, "Failed to get context XML\n");
		goto cleanup;
	}
	xml_len = strlen(xml) + 1;

	printf("IIO context created:\n");
	printf("  Backend: %s\n", iio_context_get_name(ctx));
	printf("  Description: %s\n", iio_context_get_description(ctx));
	printf("  Devices: %u\n", iio_context_get_devices_count(ctx));
	printf("  XML size: %zu bytes\n\n", xml_len);

	/* Create server socket */
	server_fd = create_server_socket();
	if (server_fd < 0) {
		fprintf(stderr, "Failed to create server socket\n");
		goto cleanup;
	}

	printf("Server listening on port %d\n", IIOD_PORT);
	printf("Connect with: iio_info -u ip:127.0.0.1\n");
	printf("Press Ctrl+C to stop\n\n");

	/* Accept and handle client connections */
	while (running) {
		struct sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		int client_fd;

		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
		if (client_fd < 0) {
			if (running) {
				perror("accept");
			}
			continue;
		}

		handle_client(client_fd, &client_addr, ctx, xml, xml_len);
		close(client_fd);
	}

	printf("\nShutting down...\n");
	ret = EXIT_SUCCESS;

cleanup:
	if (server_fd >= 0) {
		close(server_fd);
	}
	if (ctx) {
		iio_context_destroy(ctx);
	}
	iiod_cleanup();

	return ret;
}
