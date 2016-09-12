/*
 * libiio - Dummy IIO streaming example
 *
 * This example libiio program is meant to exercise the features of IIO present
 * in the sample dummy IIO device. For buffered access it relies on the hrtimer
 * trigger but could be modified to use the sysfs trigger. No hardware should
 * be required to run this program.
 *
 * How to setup the sample IIO dummy device and hrtimer trigger:
 *
 *   1. sudo modprobe industrialio kfifo_buf industrialio-sw-trigger
 *   2. sudo modprobe iio_dummy iio-trig-hrtimer
 *   3. sudo mkdir /configfs
 *   4. sudo mount -t configfs none /config
 *   5. sudo mkdir /config/iio/triggers/hrtimer/instance1
 *
 * Copyright (c) 2016, DAQRI. All rights reserved.
 * Author: Lucas Magasweran <lucas.magasweran@daqri.com>
 *
 * Based on AD9361 example:
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
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
 **/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <iio.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static char *name = "iio_dummy_part_no";
static char *trigger_str = "instance1";
#define BUFFER_LENGTH 1

/*
 * libiio supports multiple methods for reading data from a buffer.
 * TODO make command line argument
 */
enum {
	BUFFER_POINTER,
	SAMPLE_CALLBACK,
	CHANNEL_READ_RAW,
	CHANNEL_READ,
};
static int buffer_read_method = BUFFER_POINTER;

// Streaming devices
static struct iio_device *dev;

/* IIO structs required for streaming */
static struct iio_context *ctx;
static struct iio_buffer  *rxbuf;
static struct iio_channel **channels;
static int channel_count;

static bool stop;

/* cleanup and exit */
static void shutdown()
{
	if (channels) { free(channels); }

	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }

	printf("* Disassociate trigger\n");
	if (dev) { iio_device_set_trigger(dev, NULL); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish...\n");
	stop = true;
}

static ssize_t sample_cb(const struct iio_channel *chn, void *src, size_t bytes, void *d)
{
	const struct iio_data_format *fmt = iio_channel_get_data_format(chn);

	printf("%s ", iio_channel_get_id(chn));
	for (int j = 0; j < (fmt->repeat ? fmt->repeat : 1); ++j) {
		if (bytes == sizeof(int16_t))
			printf("%i ", ((int16_t *)src)[j]);
		else if (bytes == sizeof(int64_t))
			printf("%ld ", ((int64_t *)src)[j]);
	}

	return bytes * (fmt->repeat ? fmt->repeat : 1);
}

/* simple configuration and streaming */
int main (int argc, char **argv)
{
	// Hardware trigger
	struct iio_device *trigger;

	// Listen to ctrl+c and assert
	signal(SIGINT, handle_sig);

	printf("* Acquiring IIO context\n");
	assert((ctx = iio_create_default_context()) && "No context");
	assert(iio_context_get_devices_count(ctx) > 0 && "No devices");

	printf("* Acquiring device %s\n", name);
	dev = iio_context_find_device(ctx, name);
	if (!dev) {
		perror("No device found");
		shutdown();
	}

	printf("* Initializing IIO streaming channels:\n");
	for (int i = 0; i < iio_device_get_channels_count(dev); ++i) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		const struct iio_data_format *fmt = iio_channel_get_data_format(chn);
		if (iio_channel_is_scan_element(chn)) {
			printf("%s bytes %d repeat %d\n", iio_channel_get_id(chn), fmt->length/8, fmt->repeat);
			channel_count++;
		}
	}
	if (channel_count == 0) {
		printf("No scan elements found (make sure the driver built with 'CONFIG_IIO_SIMPLE_DUMMY_BUFFER=y')\n");
		shutdown();
	}
	channels = calloc(channel_count, sizeof *channels);
	if (!channels) {
		perror("Channel array allocation failed");
		shutdown();
	}
	for (int i = 0; i < channel_count; ++i) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(chn))
			channels[i] = chn;
	}

	printf("* Acquiring trigger %s\n", trigger_str);
	trigger = iio_context_find_device(ctx, trigger_str);
	if (!trigger || !iio_device_is_trigger(trigger)) {
		perror("No trigger found (try setting up the iio-trig-hrtimer module)");
		shutdown();
	}

	printf("* Enabling IIO streaming channels for buffered capture\n");
	for (int i = 0; i < channel_count; ++i)
		iio_channel_enable(channels[i]);

	printf("* Enabling IIO buffer trigger\n");
	if (iio_device_set_trigger(dev, trigger)) {
		perror("Could not set trigger\n");
		shutdown();
	}

	printf("* Creating non-cyclic IIO buffers with %d samples\n", BUFFER_LENGTH);
	rxbuf = iio_device_create_buffer(dev, BUFFER_LENGTH, false);
	if (!rxbuf) {
		perror("Could not create buffer");
		shutdown();
	}

	printf("* Starting IO streaming (press CTRL+C to cancel)\n");
	bool has_ts = strcmp(iio_channel_get_id(channels[channel_count-1]), "timestamp") == 0;
	int64_t last_ts = 0;
	while (!stop)
	{
		ssize_t nbytes_rx;
		void *p_dat, *p_end;
		ptrdiff_t p_inc;
		int64_t now_ts;

		// Refill RX buffer
		nbytes_rx = iio_buffer_refill(rxbuf);
		if (nbytes_rx < 0) {
			printf("Error refilling buf: %d\n", (int)nbytes_rx);
			shutdown();
		}

		p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);

		// Print timestamp delta in ms
		if (has_ts)
			for (p_dat = iio_buffer_first(rxbuf, channels[channel_count-1]); p_dat < p_end; p_dat += p_inc) {
				now_ts = (((int64_t *)p_dat)[0]);
				printf("[%04ld] ", last_ts > 0 ? (now_ts - last_ts)/1000/1000 : 0);
				last_ts = now_ts;
			}

		// Print each captured sample
		switch (buffer_read_method)
		{
		case BUFFER_POINTER:
			for (int i = 0; i < channel_count; ++i) {
				const struct iio_data_format *fmt = iio_channel_get_data_format(channels[i]);
				printf("%s ", iio_channel_get_id(channels[i]));
				for (p_dat = iio_buffer_first(rxbuf, channels[i]); p_dat < p_end; p_dat += p_inc) {
					for (int j = 0; j < (fmt->repeat ? fmt->repeat : 1); ++j) {
						if (fmt->length/8 == sizeof(int16_t))
							printf("%i ", ((int16_t *)p_dat)[j]);
						else if (fmt->length/8 == sizeof(int64_t))
							printf("%ld ", ((int64_t *)p_dat)[j]);
					}
				}
			}
			printf("\n");
			break;

		case SAMPLE_CALLBACK:
			iio_buffer_foreach_sample(rxbuf, sample_cb, NULL);
			printf("\n");
			break;

		case CHANNEL_READ_RAW:
		case CHANNEL_READ:
			for (int i = 0; i < channel_count; ++i) {
				uint8_t *buf;
				size_t bytes;
				const struct iio_data_format *fmt = iio_channel_get_data_format(channels[i]);
				size_t sample_size = fmt->length / 8 * (fmt->repeat ? fmt->repeat : 1);

				buf = malloc(sample_size * BUFFER_LENGTH);

				if (buffer_read_method == CHANNEL_READ_RAW)
					bytes = iio_channel_read_raw(channels[i], rxbuf, buf, sample_size * BUFFER_LENGTH);
				else
					bytes = iio_channel_read(channels[i], rxbuf, buf, sample_size * BUFFER_LENGTH);

				printf("%s ", iio_channel_get_id(channels[i]));
				for (int sample = 0; sample < bytes / sample_size; ++sample) {
					for (int j = 0; j < (fmt->repeat ? fmt->repeat : 1); ++j) {
						if (fmt->length/8 == sizeof(int16_t))
							printf("%i ", ((int16_t *)buf)[sample+j]);
						else if (fmt->length/8 == sizeof(int64_t))
							printf("%ld ", ((int64_t *)buf)[sample+j]);
					}
				}

				free(buf);
			}
			printf("\n");
			break;
		}
	}

	shutdown();

	return 0;
}

