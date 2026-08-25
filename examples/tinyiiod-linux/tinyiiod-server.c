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
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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
 * ADC SIMULATION BACKEND
 * ======================================================================== */

#define ADC_NUM_CHANNELS 4
#define ADC_RESOLUTION_BITS 12
#define ADC_MAX_VALUE ((1 << ADC_RESOLUTION_BITS) - 1)  /* 4095 for 12-bit */
#define ADC_VREF_MV 3300  /* 3.3V reference in millivolts */

/* ADC channel state */
struct adc_channel {
	int channel_num;
	uint16_t raw_value;
	uint32_t read_count;  /* Increments on each read for simulation */
};

/* ADC device state */
struct adc_device {
	char name[64];
	uint32_t sampling_frequency;  /* Hz */
	struct adc_channel channels[ADC_NUM_CHANNELS];
	time_t start_time;
};

/* Buffer state - tracks active streaming configuration */
struct adc_buffer {
	const struct iio_device *dev;
	bool enabled;
	bool cyclic;
	size_t nb_samples;
	uint64_t sample_count;  /* Total samples generated */
	bool channel_enabled[ADC_NUM_CHANNELS];
	int num_enabled_channels;
};

static struct adc_device g_adc = {
	.name = "ADC Simulator",
	.sampling_frequency = 1000,  /* 1 kHz default */
};

/* Protects g_adc, accessed concurrently by per-client threads */
static pthread_mutex_t g_adc_lock = PTHREAD_MUTEX_INITIALIZER;

/* Initialize ADC channels with different simulation patterns */
static void adc_init_channels(void)
{
	int i;

	g_adc.start_time = time(NULL);

	for (i = 0; i < ADC_NUM_CHANNELS; i++) {
		g_adc.channels[i].channel_num = i;
		g_adc.channels[i].raw_value = 0;
		g_adc.channels[i].read_count = 0;
	}
}

/* Simulate ADC readings - different pattern per channel */
static uint16_t adc_simulate_reading(int channel_num)
{
	struct adc_channel *ch;
	time_t elapsed;
	uint16_t value;

	pthread_mutex_lock(&g_adc_lock);

	ch = &g_adc.channels[channel_num];
	elapsed = time(NULL) - g_adc.start_time;

	ch->read_count++;

	/* Different simulation pattern per channel:
	 * Channel 0: Sine wave
	 * Channel 1: Sawtooth ramp
	 * Channel 2: Square wave
	 * Channel 3: Random noise
	 */
	switch (channel_num) {
	case 0:
		/* Sine wave: varies between 0 and ADC_MAX_VALUE */
		value = (uint16_t)((sin(elapsed * 0.5) + 1.0) * (ADC_MAX_VALUE / 2.0));
		break;
	case 1:
		/* Sawtooth: ramps from 0 to max over 10 seconds */
		value = (uint16_t)((elapsed % 10) * (ADC_MAX_VALUE / 10));
		break;
	case 2:
		/* Square wave: alternates between 25% and 75% every 3 seconds */
		value = ((elapsed / 3) % 2) ? (ADC_MAX_VALUE * 3 / 4) : (ADC_MAX_VALUE / 4);
		break;
	case 3:
		/* Pseudo-random using read count */
		value = (uint16_t)((ch->read_count * 1103515245 + 12345) % (ADC_MAX_VALUE + 1));
		break;
	default:
		value = 0;
	}

	ch->raw_value = value;

	pthread_mutex_unlock(&g_adc_lock);

	return value;
}

static ssize_t adc_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const char *attr_name = iio_attr_get_name(attr);
	const struct iio_channel *chn = NULL;
	ssize_t ret;

	/* Check if this is a channel attribute */
	if (attr->type == IIO_ATTR_TYPE_CHANNEL) {
		chn = attr->iio.chn;
	}

	/* Device attributes */
	if (!chn) {
		pthread_mutex_lock(&g_adc_lock);
		if (strcmp(attr_name, "name") == 0) {
			ret = snprintf(dst, len, "%s", g_adc.name);
		} else if (strcmp(attr_name, "sampling_frequency") == 0) {
			ret = snprintf(dst, len, "%u", g_adc.sampling_frequency);
		} else {
			pthread_mutex_unlock(&g_adc_lock);
			return -ENOENT;
		}
		pthread_mutex_unlock(&g_adc_lock);
	} else {
		/* Channel attributes */
		const char *chn_id = iio_channel_get_id(chn);
		int channel_num;

		/* Extract channel number from id (e.g., "voltage0" -> 0) */
		if (sscanf(chn_id, "voltage%d", &channel_num) != 1 ||
		    channel_num < 0 || channel_num >= ADC_NUM_CHANNELS) {
			return -EINVAL;
		}

		if (strcmp(attr_name, "raw") == 0) {
			uint16_t value = adc_simulate_reading(channel_num);
			ret = snprintf(dst, len, "%u", value);
		} else if (strcmp(attr_name, "scale") == 0) {
			/* Scale factor: millivolts per LSB */
			double scale = (double)ADC_VREF_MV / (double)(ADC_MAX_VALUE + 1);
			ret = snprintf(dst, len, "%.6f", scale);
		} else if (strcmp(attr_name, "offset") == 0) {
			ret = snprintf(dst, len, "0");
		} else {
			return -ENOENT;
		}
	}

	/* Include null terminator in length (protocol requirement) */
	if (ret >= 0 && ret < (ssize_t)len - 1) {
		return ret + 1;
	}

	return ret;
}

static ssize_t adc_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const char *attr_name = iio_attr_get_name(attr);

	/* Only device attributes are writable - check if this is a channel attribute */
	if (attr->type == IIO_ATTR_TYPE_CHANNEL) {
		return -EPERM;  /* Channel attributes are read-only */
	}

	if (strcmp(attr_name, "sampling_frequency") == 0) {
		unsigned int freq;
		if (sscanf(src, "%u", &freq) != 1) {
			return -EINVAL;
		}
		/* Validate range: 1 Hz to 100 kHz */
		if (freq < 1 || freq > 100000) {
			return -EINVAL;
		}
		pthread_mutex_lock(&g_adc_lock);
		g_adc.sampling_frequency = freq;
		pthread_mutex_unlock(&g_adc_lock);
		return len;
	}

	return -ENOENT;
}

/* Buffer operations for streaming data */

/* Open a buffer for streaming */
static struct iio_buffer_pdata *adc_open_buffer(const struct iio_device *dev, unsigned int idx,
		struct iio_channels_mask *mask)
{
	struct adc_buffer *buf;
	int i;

	(void)idx;  /* We only support one buffer per device */

	buf = calloc(1, sizeof(*buf));
	if (!buf) {
		return iio_ptr(-ENOMEM);
	}

	buf->dev = dev;
	buf->enabled = false;
	buf->cyclic = false;
	buf->nb_samples = 0;
	buf->sample_count = 0;
	buf->num_enabled_channels = 0;

	/* Determine which channels are enabled */
	for (i = 0; i < ADC_NUM_CHANNELS; i++) {
		const struct iio_channel *chn = iio_device_get_channel(dev, i);
		if (chn && iio_channel_is_enabled(chn, mask)) {
			buf->channel_enabled[i] = true;
			buf->num_enabled_channels++;
		} else {
			buf->channel_enabled[i] = false;
		}
	}

	return (struct iio_buffer_pdata *)buf;
}

/* Close buffer and free resources */
static void adc_close_buffer(struct iio_buffer_pdata *pdata)
{
	struct adc_buffer *buf = (struct adc_buffer *)pdata;
	if (buf) {
		free(buf);
	}
}

/* Enable or disable the buffer */
static int adc_enable_buffer(struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable,
		bool cyclic)
{
	struct adc_buffer *buf = (struct adc_buffer *)pdata;

	buf->enabled = enable;
	buf->cyclic = cyclic;
	buf->nb_samples = nb_samples;

	if (enable) {
		buf->sample_count = 0;
	}

	return 0;
}

/* Cancel buffer operation */
static void adc_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	struct adc_buffer *buf = (struct adc_buffer *)pdata;
	buf->enabled = false;
}

/* Read buffer data - generate simulated samples */
static ssize_t adc_readbuf(struct iio_buffer_pdata *pdata, void *dst, size_t len)
{
	struct adc_buffer *buf = (struct adc_buffer *)pdata;
	uint16_t *samples = (uint16_t *)dst;
	size_t bytes_per_sample = sizeof(uint16_t);
	size_t samples_requested = len / bytes_per_sample;
	size_t sample_idx = 0;
	int ch;

	if (!buf->enabled) {
		return -EPERM;
	}

	if (buf->num_enabled_channels == 0) {
		return 0;
	}

	/* Generate interleaved samples for all enabled channels */
	while (sample_idx < samples_requested) {
		for (ch = 0; ch < ADC_NUM_CHANNELS; ch++) {
			if (!buf->channel_enabled[ch]) {
				continue;
			}

			if (sample_idx >= samples_requested) {
				break;
			}

			/* Generate simulated sample for this channel */
			samples[sample_idx] = adc_simulate_reading(ch);
			sample_idx++;
		}

		buf->sample_count++;
	}

	return sample_idx * bytes_per_sample;
}

static struct iio_context *adc_create_context(
		const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_channel *chn;
	char channel_id[16];
	int ret;
	int i;

	(void)args;

	/* Initialize ADC simulation */
	adc_init_channels();

	ctx = iio_context_create_from_backend(
			params, &iio_external_backend, "Linux tinyIIOD reference", 1, 0, 0, "v1.0.0");
	if (iio_err(ctx)) {
		return ctx;
	}

	/* Add ADC device */
	dev = iio_context_add_device(ctx, "iio:device0", "adc0", NULL);
	if (iio_err(dev)) {
		iio_context_destroy(ctx);
		return iio_err_cast(dev);
	}

	ret = iio_device_add_attr(dev, "name", IIO_ATTR_TYPE_DEVICE);
	if (ret < 0) {
		iio_context_destroy(ctx);
		return iio_ptr(ret);
	}

	ret = iio_device_add_attr(dev, "sampling_frequency", IIO_ATTR_TYPE_DEVICE);
	if (ret < 0) {
		iio_context_destroy(ctx);
		return iio_ptr(ret);
	}

	/* Add voltage input channels */
	for (i = 0; i < ADC_NUM_CHANNELS; i++) {
		struct iio_data_format fmt = {
			.length = 16,  /* 16-bit storage */
			.bits = ADC_RESOLUTION_BITS,  /* 12 bits of data */
			.shift = 0,
			.is_signed = false,
			.is_fully_defined = true,
		};

		snprintf(channel_id, sizeof(channel_id), "voltage%d", i);

		chn = iio_device_add_channel(dev, i, channel_id, NULL, NULL,
					     false, true, &fmt);
		if (iio_err(chn)) {
			iio_context_destroy(ctx);
			return iio_err_cast(chn);
		}

		/* Add channel attributes */
		ret = iio_channel_add_attr(chn, "raw", IIO_ATTR_TYPE_CHANNEL, NULL);
		if (ret >= 0) {
			ret = iio_channel_add_attr(chn, "scale", IIO_ATTR_TYPE_CHANNEL, NULL);
		}
		if (ret >= 0) {
			ret = iio_channel_add_attr(chn, "offset", IIO_ATTR_TYPE_CHANNEL, NULL);
		}
		if (ret < 0) {
			iio_context_destroy(ctx);
			return iio_ptr(ret);
		}
	}

	/* Add buffer for streaming data */
	struct iio_buffer *buf = iio_device_add_buffer(dev, 0);
	if (!buf) {
		iio_context_destroy(ctx);
		return iio_ptr(-ENOMEM);
	}

	/* Add scan elements for all channels */
	for (i = 0; i < ADC_NUM_CHANNELS; i++) {
		chn = iio_device_get_channel(dev, i);
		ret = iio_buffer_add_scan_element(buf, chn, NULL);
		if (ret < 0) {
			iio_context_destroy(ctx);
			return iio_ptr(ret);
		}
	}

	return ctx;
}
/* Backend operations */
static const struct iio_backend_ops adc_ops = {
	.create = adc_create_context,
	.read_attr = adc_read_attr,
	.write_attr = adc_write_attr,
	.open_buffer = adc_open_buffer,
	.close_buffer = adc_close_buffer,
	.enable_buffer = adc_enable_buffer,
	.cancel_buffer = adc_cancel_buffer,
	.readbuf = adc_readbuf,
};

/* Backend definition */
const struct iio_backend iio_external_backend = {
	.name = "adc",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 5000,
	.uri_prefix = "adc:",
	.ops = &adc_ops,
};

/* ========================================================================
 * NETWORK TRANSPORT
 * ======================================================================== */

struct client_data {
	int fd;
	struct sockaddr_in addr;
};

/* Thread data for handling client connections */
struct client_thread_data {
	int client_fd;
	struct sockaddr_in client_addr;
	struct iio_context *ctx;
	const void *xml;
	size_t xml_len;
};

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

static void *client_thread(void *arg)
{
	struct client_thread_data *thread_data = (struct client_thread_data *)arg;
	struct client_data client = {
		.fd = thread_data->client_fd,
		.addr = thread_data->client_addr,
	};
	char client_ip[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &thread_data->client_addr.sin_addr, client_ip, sizeof(client_ip));
	printf("Client connected from %s:%d\n", client_ip, ntohs(thread_data->client_addr.sin_port));

	iiod_interpreter(thread_data->ctx, (struct iiod_pdata *)&client, network_read, network_write,
			thread_data->xml, thread_data->xml_len);

	printf("Client disconnected from %s:%d\n", client_ip, ntohs(thread_data->client_addr.sin_port));

	close(thread_data->client_fd);
	free(thread_data);
	return NULL;
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
	ctx = iio_create_context(&params, "adc:");
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

	/* Accept and handle client connections (multi-threaded) */
	while (running) {
		struct sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		struct client_thread_data *thread_data;
		pthread_t thread_id;
		int client_fd;
		int err;

		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
		if (client_fd < 0) {
			if (running) {
				perror("accept");
			}
			continue;
		}

		/* Allocate thread data */
		thread_data = malloc(sizeof(*thread_data));
		if (!thread_data) {
			fprintf(stderr, "Failed to allocate thread data\n");
			close(client_fd);
			continue;
		}

		thread_data->client_fd = client_fd;
		thread_data->client_addr = client_addr;
		thread_data->ctx = ctx;
		thread_data->xml = xml;
		thread_data->xml_len = xml_len;

		/* Create thread to handle client */
		err = pthread_create(&thread_id, NULL, client_thread, thread_data);
		if (err) {
			fprintf(stderr, "Failed to create thread: %s\n", strerror(err));
			free(thread_data);
			close(client_fd);
			continue;
		}

		/* Detach thread - we don't need to join it */
		pthread_detach(thread_id);
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
