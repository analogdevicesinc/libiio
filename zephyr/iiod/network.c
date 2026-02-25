/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <tinyiiod/tinyiiod.h>

LOG_MODULE_REGISTER(libiio, CONFIG_LIBIIO_LOG_LEVEL);

#define MAX_CONNECTIONS	5

/* Structure to hold client data */
struct client_data {
	int fd;
	struct sockaddr_in addr;
	int client_num;
	bool in_use;
	struct k_thread thread;
	k_tid_t thread_id;
};

/* Pool of client data structures */
static struct client_data client_pool[CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX];
static K_MUTEX_DEFINE(client_pool_mutex);

/* Shared IIO context and XML - used by all clients */
static struct iio_context *shared_ctx = NULL;
static const void *shared_xml = NULL;
static size_t shared_xml_len = 0;

/* Thread stacks for client threads */
K_THREAD_STACK_ARRAY_DEFINE(client_thread_stacks,
			    CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX,
			    CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_THREAD_STACK_SIZE);

static ssize_t iiod_network_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct client_data *client = (struct client_data *)pdata;
	uint8_t *buffer = (uint8_t *)buf;
	size_t bytes_read = 0;
	int read = 0;
	int this_fd;
	int i;

	if (!client) {
		return -EPERM;
	}

	this_fd = client->fd;
	if (this_fd < 0) {
		return -ENOENT;
	}

	while (bytes_read < size) {
		read = recv(this_fd, buffer, size - bytes_read, 0);

		if (read > 0) {
			if (read <= 20)
				for(i = 0; i < size; i++) {
					LOG_DBG("%02x ", buffer[i]);
				}
			buffer += read;
			bytes_read += read;

			LOG_DBG("[Client %d] Read %d characters from read_cb; size = %d",
				client->client_num, read, size);
		}
		else if (read < 0) {
			if (errno == ECONNRESET || errno == ENOTCONN) {
				LOG_DBG("[Client %d] Connection closed by peer",
					client->client_num);
			} else {
				LOG_ERR("[Client %d] error: recv: %d",
					client->client_num, errno);
			}
			return -ESRCH;
		}
		else {
			return 0;
		}
	}
	return bytes_read;
}


static ssize_t iiod_network_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct client_data *client = (struct client_data *)pdata;
	const uint8_t *buffer = (const uint8_t *)buf;
	int bytes_sent = 0;
	int sent = 0;
	int this_fd;
	int i;

	if (!client) {
		return -EPERM;
	}

	this_fd = client->fd;
	if (this_fd < 0) {
		return -ENOENT;
	}

	while (bytes_sent < size) {
		sent = send(this_fd, buffer, size - bytes_sent, 0);

		if (sent < 0) {
			LOG_ERR("[Client %d] error: send: %d",
				client->client_num, errno);
			return -ESRCH;
		}

		if (sent <= 20) {
			for(i = 0; i < size; i++) {
				LOG_DBG("%02x ", buffer[i]);
			}
		}

		buffer += sent;
		bytes_sent += sent;

		LOG_DBG("[Client %d] Written %d characters in write_cb; size = %zu; bytes_sent = %d",
			client->client_num, sent, size, bytes_sent);
	}

	return (ssize_t)bytes_sent;
}

static int iiod_network_create_server(void)
{
	int server_fd;
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_addr = { .s_addr = INADDR_ANY },
		.sin_port = htons(CONFIG_LIBIIO_IIOD_NETWORK_PORT),
	};
	int reuse = 1;
	int ret;

	LOG_DBG("Creating test TCP server on port %d", CONFIG_LIBIIO_IIOD_NETWORK_PORT);

	server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_fd < 0) {
		LOG_ERR("Failed to create socket: %d (errno: %d)", server_fd, errno);
		return -EPERM;
	}
	LOG_DBG("Socket created successfully: fd=%d", server_fd);

	ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (ret < 0) {
		LOG_ERR("Failed to set socket options: %d (errno: %d)", ret, errno);
		close(server_fd);
		return -EPERM;
	}
	LOG_DBG("Socket options set successfully");

	ret = bind(server_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind socket: %d (errno: %d)", ret, errno);
		close(server_fd);
		return -EPERM;
	}
	LOG_DBG("Socket bound successfully to port %d", CONFIG_LIBIIO_IIOD_NETWORK_PORT);

	ret = listen(server_fd, MAX_CONNECTIONS);
	if (ret < 0) {
		LOG_ERR("Failed to listen on socket: %d (errno: %d)", ret, errno);
		close(server_fd);
		return -EPERM;
	}
	LOG_DBG("Socket listening successfully: backlog = %d", MAX_CONNECTIONS);

	return server_fd;
}

static void iiod_network_client_thread(void *p1, void *p2, void *p3)
{
	struct client_data *client = (struct client_data *)p1;
	int this_client_num, this_fd;

	if (!client) {
		return;
	}

	this_client_num = client->client_num;
	this_fd = client->fd;

	LOG_DBG("[Client %d] Thread started (fd=%d)", this_client_num, this_fd);

	/* Use iiod_interpreter - locks are already created at startup */
	iiod_interpreter(shared_ctx, (struct iiod_pdata *)client,
			iiod_network_read, iiod_network_write,
			shared_xml, shared_xml_len);

	if (client->fd >= 0) {
		client->fd = -EPERM;
		close(this_fd);
	}

	k_mutex_lock(&client_pool_mutex, K_FOREVER);
	client->in_use = false;
	k_mutex_unlock(&client_pool_mutex);

	LOG_DBG("[Client %d] Thread exiting", this_client_num);
}

static struct client_data* iiod_network_alloc_client_data(void)
{
	struct client_data *client = NULL;
	int i;

	k_mutex_lock(&client_pool_mutex, K_FOREVER);
	for (i = 0; i < CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX; i++) {
		if (!client_pool[i].in_use) {
			client = &client_pool[i];
			client->in_use = true;
			client->client_num = i;
			break;
		}
	}
	k_mutex_unlock(&client_pool_mutex);

	if (!client) {
		LOG_ERR("Reached maximum number of clients! (max = %d)",
			CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX);
	}
	return client;
}

static void iiod_network_free_client_data(struct client_data *client)
{
	if (!client) {
		LOG_ERR("No such client");
		return;
	}

	LOG_DBG("[Client %d] free_client_data called", client->client_num);

	k_mutex_lock(&client_pool_mutex, K_FOREVER);
	if (client->fd >= 0) {
		LOG_DBG("[Client %d] Closing socket in free_client_data",
			client->client_num);
		close(client->fd);
		client->fd = -EPERM;
	}

	client->in_use = false;
	k_mutex_unlock(&client_pool_mutex);

	LOG_DBG("[Client %d] free_client_data completed", client->client_num);
}

static void iiod_network_server_thread(void *p1, void *p2, void *p3)
{
	int server_fd, new_fd, i;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;
	char addr_str[32];
	struct iio_context_params ctx_params = {0};
	struct client_data *client;
	static int counter;

	LOG_DBG("*** Simple TCP Test Server for %s ***", CONFIG_BOARD_TARGET);
	LOG_DBG("Waiting for network to initialize...");

	/* Wait for network to be ready */
	k_sleep(K_SECONDS(5));

	LOG_DBG("Starting simplified TCP test server...");

	server_fd = iiod_network_create_server();
	if (server_fd < 0) {
		LOG_ERR("Failed to create test server");
		return;
	}

	LOG_DBG("Test server ready, waiting for connections on port %d...",
		CONFIG_LIBIIO_IIOD_NETWORK_PORT);

	LOG_DBG("Maximum concurrent clients: %d",
		CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX);

	/* Initialize tinyiiod global resources */
	LOG_DBG("Initializing tinyiiod resources...");
	if (iiod_init() < 0) {
		LOG_ERR("Failed to initialize tinyiiod resources");
		return;
	}

	/* Create the shared IIO context */
	LOG_DBG("Creating shared IIO context...");
	shared_ctx = iio_create_context(&ctx_params, "zephyr:");
	if (iio_err(shared_ctx)) {
		LOG_ERR("Context creation failed");
		close(server_fd);
		iiod_cleanup();
		return;
	}

	/* Get XML */
	LOG_DBG("Getting xml data");
	shared_xml = iio_context_get_xml(shared_ctx);
	if (!shared_xml) {
		LOG_ERR("Error getting context XML");
		iio_context_destroy(shared_ctx);
		close(server_fd);
		iiod_cleanup();
		return;
	}

	shared_xml_len = strlen(shared_xml) + 1;
	LOG_DBG("XML ready, length: %zu bytes", shared_xml_len);

	/* Initialize client pool */
	for (i = 0; i < CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX; i++) {
		client_pool[i].fd = -EPERM;
		client_pool[i].in_use = false;
		client_pool[i].client_num = i;
	}

	while (1) {
		client_addr_len = sizeof(client_addr);
		LOG_DBG("Calling accept() (connections served: %d)...", counter);

		new_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (new_fd < 0) {
			LOG_ERR("Accept failed: %d (errno: %d)", new_fd, errno);
			k_sleep(K_SECONDS(1));
			continue;
		}
		LOG_DBG("Accept successful! Client fd: %d", new_fd);

		inet_ntop(client_addr.sin_family, &client_addr.sin_addr,
				addr_str, sizeof(addr_str));
		LOG_DBG("Connection #%d from %s", counter++, addr_str);

		client = iiod_network_alloc_client_data();
		if (!client) {
			LOG_ERR("Reached maximum number of clients!");
			close(new_fd);
			continue;
		}
		client->fd = new_fd;
		memcpy(&client->addr, &client_addr, sizeof(client_addr));

		client->thread_id = k_thread_create(&client->thread,
				client_thread_stacks[client->client_num],
				CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_THREAD_STACK_SIZE,
				iiod_network_client_thread,
				client, NULL, NULL,
				CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_THREAD_PRIORITY,
				0, K_NO_WAIT);
		if (!client->thread_id) {
			LOG_ERR("[Client %d] Failed to create thread", client->client_num);
			iiod_network_free_client_data(client);
			continue;
		}
	}

	/* Cleanup resources if server loop exits */
	iio_context_destroy(shared_ctx);
	close(server_fd);
	iiod_cleanup();

	LOG_DBG("Network server thread exiting");
}

K_THREAD_DEFINE(iiod_network, CONFIG_LIBIIO_IIOD_NETWORK_SERVER_THREAD_STACK_SIZE,
		iiod_network_server_thread, NULL, NULL, NULL,
		CONFIG_LIBIIO_IIOD_NETWORK_SERVER_THREAD_PRIORITY, 0, 1);
