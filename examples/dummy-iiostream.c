// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - Dummy IIO streaming example
 *
 * This example libiio program is meant to exercise the features of IIO present
 * in the sample dummy IIO device. For buffered access it relies on the hrtimer
 * trigger but could be modified to use the sysfs trigger. No hardware should
 * be required to run this program.
 *
 * Copyright (c) 2016, DAQRI. All rights reserved.
 * Author: Lucas Magasweran <lucas.magasweran@daqri.com>
 *
 * Based on AD9361 example:
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 */

/*
 * How to setup the sample IIO dummy device and hrtimer trigger:
 *
 * 1. Check if `configfs` is already mounted
 *
 * $ mount | grep 'config'
 * configfs on /sys/kernel/config type configfs (rw,relatime)
 *
 * 1.b. Mount `configfs` if it is not already mounted
 *  $ sudo mount -t configfs none /sys/kernel/config
 *
 * 2. Load modules one by one
 *
 * $ sudo modprobe industrialio
 * $ sudo modprobe industrialio-configfs
 * $ sudo modprobe industrialio-sw-device
 * $ sudo modprobe industrialio-sw-trigger
 * $ sudo modprobe iio-trig-hrtimer
 * $ sudo modprobe iio_dummy
 *
 * 3. Create trigger and dummy device under `/sys/kernel/config`
 *
 * $ sudo mkdir /sys/kernel/config/iio/triggers/hrtimer/instance1
 * $ sudo mkdir /sys/kernel/config/iio/devices/dummy/my_dummy_device
 *
 * 4. Run `iio_info` to see that all worked properly
 *
 * $ iio_info
 * Library version: 0.14 (git tag: c9909f2)
 * Compiled with backends: local xml ip
 * IIO context created with local backend.
 * Backend version: 0.14 (git tag: c9909f2)
 * Backend description string: Linux ...
 * IIO context has 1 attributes:
 *         local,kernel: 4.13.0-39-generic
 * IIO context has 2 devices:
 *         iio:device0: my_dummy_device
 *                 10 channels found:
 *                         activity_walking:  (input)
 *                         1 channel-specific attributes found:
 *                                 attr  0: input value: 4
 * ...
 *
 **/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define IIO_ENSURE(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}


static char *name        = "iio_dummy_part_no";
static char *trigger_str = "instance1";
static int buffer_length = 1;
static int count         = -1;

// libiio supports multiple methods for reading data from a buffer
enum {
	BUFFER_POINTER,
	SAMPLE_CALLBACK,
	CHANNEL_READ_RAW,
	CHANNEL_READ,
	MAX_READ_METHOD,
};
static int buffer_read_method = BUFFER_POINTER;

// Streaming devices
static struct iio_device *dev;

/* IIO structs required for streaming */
static struct iio_context *ctx;
static struct iio_buffer  *rxbuf;
static struct iio_channel **channels;
static unsigned int channel_count;
static struct iio_channels_mask *rxmask;
static struct iio_stream *rxstream;

static bool stop;
static bool has_repeat;

/* cleanup and exit */
static void shutdown(void)
{
	int ret;

	if (channels) { free(channels); }

	printf("* Destroying stream\n");
	if (rxstream) { iio_stream_destroy(rxstream); }

	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }

	printf("* Disassociate trigger\n");
	if (dev) {
		ret = iio_device_set_trigger(dev, NULL);
		if (ret < 0) {
			char buf[256];
			iio_strerror(-ret, buf, sizeof(buf));
			fprintf(stderr, "%s while Disassociate trigger\n", buf);
		}
	}

	printf("* Destroying channel masks\n");
	if (rxmask)
		iio_channels_mask_destroy(rxmask);

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish... got signal : %d\n", sig);
	stop = true;
}

static ssize_t sample_cb(const struct iio_channel *chn, void *src, size_t bytes, __notused void *d)
{
	const struct iio_data_format *fmt = iio_channel_get_data_format(chn);
	unsigned int j, repeat = has_repeat ? fmt->repeat : 1;

	printf("%s ", iio_channel_get_id(chn));
	for (j = 0; j < repeat; ++j) {
		if (bytes == sizeof(int16_t))
			printf("%" PRIi16 " ", ((int16_t *)src)[j]);
		else if (bytes == sizeof(int64_t))
			printf("%" PRId64 " ", ((int64_t *)src)[j]);
	}

	return bytes * repeat;
}

static void usage(__notused int argc, char *argv[])
{
	printf("Usage: %s [OPTION]\n", argv[0]);
	printf("  -d\tdevice name (default \"iio_dummy_part_no\")\n");
	printf("  -t\ttrigger name (default \"instance1\")\n");
	printf("  -b\tbuffer length (default 1)\n");
	printf("  -r\tread method (default 0 pointer, 1 callback, 2 read raw, 3 read)\n");
	printf("  -c\tread count (default no limit)\n");
}

static void parse_options(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "d:t:b:r:c:h")) != -1) {
		switch (c)
		{
		case 'd':
			name = optarg;
			break;
		case 't':
			trigger_str = optarg;
			break;
		case 'b':
			buffer_length = atoi(optarg);
			break;
		case 'r':
			if (atoi(optarg) >= 0 && atoi(optarg) < MAX_READ_METHOD) {
				buffer_read_method = atoi(optarg);
			} else {
				usage(argc, argv);
				exit(1);
			}
			break;
		case 'c':
			if (atoi(optarg) > 0) {
				count = atoi(optarg);
			} else {
				usage(argc, argv);
				exit(1);
			}
			break;
		case 'h':
		default:
			usage(argc, argv);
			exit(1);
		}
	}
}

/* simple configuration and streaming */
int main (int argc, char **argv)
{
	unsigned int i, j,
		     major = iio_context_get_version_major(NULL),
		     minor = iio_context_get_version_minor(NULL);
	int err;

	// Hardware trigger
	struct iio_device *trigger;

	parse_options(argc, argv);

	// Listen to ctrl+c and assert
	signal(SIGINT, handle_sig);

	printf("Library version: %u.%u (git tag: %s)\n", major, minor,
	       iio_context_get_version_tag(NULL));

	/* check for struct iio_data_format.repeat support
	 * 0.8 has repeat support, so anything greater than that */
	has_repeat = ((major * 10000) + minor) >= 8 ? true : false;

	printf("* Acquiring IIO context\n");
	IIO_ENSURE((ctx = iio_create_context(NULL, NULL)) && "No context");
	IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

	printf("* Acquiring device %s\n", name);
	dev = iio_context_find_device(ctx, name);
	if (!dev) {
		fprintf(stderr, "No device found");
		shutdown();
	}

	printf("* Initializing IIO streaming channels:\n");
	rxmask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!rxmask) {
		fprintf(stderr, "Unable to allocate channels mask\n");
		shutdown();
	}

	for (i = 0; i < iio_device_get_channels_count(dev); ++i) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(chn)) {
			printf("%s\n", iio_channel_get_id(chn));
			channel_count++;
		}
	}
	if (channel_count == 0) {
		printf("No scan elements found (make sure the driver built with 'CONFIG_IIO_SIMPLE_DUMMY_BUFFER=y')\n");
		shutdown();
	}
	channels = calloc(channel_count, sizeof *channels);
	if (!channels) {
		fprintf(stderr, "Channel array allocation failed");
		shutdown();
	}
	for (i = 0; i < channel_count; ++i) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(chn))
			channels[i] = chn;
	}

	printf("* Acquiring trigger %s\n", trigger_str);
	trigger = iio_context_find_device(ctx, trigger_str);
	if (!trigger || !iio_device_is_trigger(trigger)) {
		fprintf(stderr, "No trigger found (try setting up the iio-trig-hrtimer module)");
		shutdown();
	}

	printf("* Enabling IIO streaming channels for buffered capture\n");
	for (i = 0; i < channel_count; ++i)
		iio_channel_enable(channels[i], rxmask);

	printf("* Enabling IIO buffer trigger\n");
	if (iio_device_set_trigger(dev, trigger)) {
		fprintf(stderr, "Could not set trigger\n");
		shutdown();
	}

	printf("* Creating non-cyclic RX IIO buffer\n");
	rxbuf = iio_device_create_buffer(dev, 0, rxmask);
	err = iio_err(rxbuf);
	if (err) {
		rxbuf = NULL;
		ctx_perror(ctx, err, "Could not create buffer");
		shutdown();
	}

	printf("* Creating IIO blocks of %u bytes\n", buffer_length);
	rxstream = iio_buffer_create_stream(rxbuf, 4, buffer_length);
	if (iio_err(rxstream)) {
		rxstream = NULL;
		ctx_perror(ctx, iio_err(rxstream), "Could not create RX stream");
		shutdown();
	}

	printf("* Starting IO streaming (press CTRL+C to cancel)\n");
	bool has_ts = strcmp(iio_channel_get_id(channels[channel_count-1]), "timestamp") == 0;
	int64_t last_ts = 0;
	while (!stop)
	{
		const struct iio_block *rxblock;
		/* we use a char pointer, rather than a void pointer, for p_dat & p_end
		 * to ensure the compiler understands the size is a byte, and then we
		 * can do math on it.
		 */
		char *p_dat, *p_end;
		ptrdiff_t p_inc;
		int64_t now_ts;

		rxblock = iio_stream_get_next_block(rxstream);
		err = iio_err(rxblock);
		if (err) {
			ctx_perror(ctx, err, "Unable to receive block");
			shutdown();
		}

		p_inc = iio_device_get_sample_size(dev, rxmask);
		p_end = iio_block_end(rxblock);

		// Print timestamp delta in ms
		if (has_ts)
			for (p_dat = iio_block_first(rxblock, channels[channel_count-1]); p_dat < p_end; p_dat += p_inc) {
				now_ts = (((int64_t *)p_dat)[0]);
				printf("[%04" PRId64 "] ", last_ts > 0 ? (now_ts - last_ts)/1000/1000 : 0);
				last_ts = now_ts;
			}

		// Print each captured sample
		switch (buffer_read_method)
		{
		case BUFFER_POINTER:
			for (i = 0; i < channel_count; ++i) {
				const struct iio_data_format *fmt = iio_channel_get_data_format(channels[i]);
				unsigned int repeat = has_repeat ? fmt->repeat : 1;

				printf("%s ", iio_channel_get_id(channels[i]));
				for (p_dat = iio_block_first(rxblock, channels[i]); p_dat < p_end; p_dat += p_inc) {
					for (j = 0; j < repeat; ++j) {
						if (fmt->length/8 == sizeof(int16_t))
							printf("%" PRIi16 " ", ((int16_t *)p_dat)[j]);
						else if (fmt->length/8 == sizeof(int64_t))
							printf("%" PRId64 " ", ((int64_t *)p_dat)[j]);
					}
				}
			}
			printf("\n");
			break;

		case SAMPLE_CALLBACK: {
			int ret;
			ret = iio_block_foreach_sample(rxblock, rxmask, sample_cb, NULL);
			if (ret < 0)
				dev_perror(dev, ret, "Unable to process buffer");
			printf("\n");
			break;
		}
		case CHANNEL_READ_RAW:
		case CHANNEL_READ:
			for (i = 0; i < channel_count; ++i) {
				uint8_t *buf;
				size_t sample, bytes;
				const struct iio_data_format *fmt = iio_channel_get_data_format(channels[i]);
				unsigned int repeat = has_repeat ? fmt->repeat : 1;
				size_t sample_size = fmt->length / 8 * repeat;

				buf = malloc(sample_size * buffer_length);
				if (!buf) {
					perror("trying to allocate memory for buffer\n");
					shutdown();
				}

				bytes = iio_channel_read(channels[i], rxblock, buf,
							 sample_size * buffer_length,
							 buffer_read_method == CHANNEL_READ_RAW);

				printf("%s ", iio_channel_get_id(channels[i]));
				for (sample = 0; sample < bytes / sample_size; ++sample) {
					for (j = 0; j < repeat; ++j) {
						if (fmt->length / 8 == sizeof(int16_t))
							printf("%" PRIi16 " ", ((int16_t *)buf)[sample+j]);
						else if (fmt->length / 8 == sizeof(int64_t))
							printf("%" PRId64 " ", ((int64_t *)buf)[sample+j]);
					}
				}

				free(buf);
			}
			printf("\n");
			break;
		}

		if (count != -1 && --count == 0)
			break;
	}

	shutdown();

	return 0;
}

