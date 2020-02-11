/*
 * libiio - ADXL372 IIO streaming example
 *
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 * Copyright (C) 2020 Analog Devices
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include <stdbool.h>
#include <stdint.h>
#include <strings.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <iio.h>
#include <unistd.h>

#define DRIVER_POLL_TIME_ms 500

#define ASSERT(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
static struct iio_channel *rx_x = NULL;
static struct iio_channel *rx_y = NULL;
static struct iio_channel *rx_z = NULL;
static struct iio_buffer  *rxbuf = NULL;

static bool stop = false;

/* cleanup and exit */
static void shutdown()
{
	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }

	printf("* Disabling streaming channels\n");
	if (rx_x) { iio_channel_disable(rx_x); }
	if (rx_y) { iio_channel_disable(rx_y); }
	if (rx_z) { iio_channel_disable(rx_z); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish...\n");
	stop = true;
}

/* finds ADXL372 IIO device */
static bool get_adxl372_dev(struct iio_context *ctx, struct iio_device **dev)
{
	*dev = iio_context_find_device(ctx, "adxl372");  return *dev != NULL;

	return *dev != NULL;
}

/* finds ADXL372 IIO channels with id chid */
static bool get_axis_chan(struct iio_device *dev, const char *axis, struct iio_channel **chn)
{
	char name[8] = "accel_x\0";

	*chn = iio_device_find_channel(dev, axis, false);

	return *chn != NULL;
}

int config_motion_warning(struct iio_device *dev)
{
	int ret;

	/* set X axis thresholds */
	ret = iio_device_reg_write(dev, 0x32, 0x02);
	if (ret < 0)
		return ret;

	ret = iio_device_reg_write(dev, 0x33, 0x81);
	if (ret < 0)
		return ret;

	/* set Y axis thresholds */
	ret = iio_device_reg_write(dev, 0x34, 0x02);
	if (ret < 0)
		return ret;

	ret = iio_device_reg_write(dev, 0x35, 0x81);
	if (ret < 0)
		return ret;

	/* set Z axis thresholds */
	ret = iio_device_reg_write(dev, 0x36, 0x02);
	if (ret < 0)
		return ret;

	ret = iio_device_reg_write(dev, 0x37, 0x81);
	if (ret < 0)
		return ret;

	/* activate INT2 to fire on activity2 detection */
	ret = iio_device_reg_write(dev, 0x3C, 0x20);
	if (ret < 0)
		return ret;
}

int config_device(struct iio_device *dev)
{
	char buf[100];
	char axis[3] = {'x', 'y', 'z'};
	int ret, i;

	/* set thresholds for actiity and inactivity*/
	for (i = 0; i < 3; i++) {
		sprintf(buf, "in_accel_%c_threshold_activity", axis[i]);
		ret = iio_device_attr_write(dev, buf, "20");
		if (ret < 0)
			return ret;

		sprintf(buf, "in_accel_%c_threshold_inactivity", axis[i]);
		ret = iio_device_attr_write(dev, buf, "1");
		if (ret < 0)
			return ret;
	}

	/* activate motion warning */
	ret = config_motion_warning(dev);
	if (ret < 0) {
		perror("Could not configure motion warning.");
		return ret;
	}

	/* set timings */
	ret = iio_device_attr_write(dev, "time_activity", "10");
	if (ret < 0)
		return ret;

	ret = iio_device_attr_write(dev, "time_inactivity", "1");
	if (ret < 0)
		return ret;

	return 0;
}

int config_fifo(struct iio_device *dev)
{
	int ret;

	ret = iio_device_buffer_attr_write(dev, "length", "1024");
	if (ret < 0)
		return ret;

	ret = iio_device_buffer_attr_write(dev, "watermark", "3");
	if (ret < 0)
		return ret;

	ret = iio_device_attr_write(dev, "peak_fifo_mode_enable", "1");
	if (ret < 0)
		return ret;

	return 0;
}

int main (int argc, char **argv)
{
	short x_axis, y_axis, z_axis;
	struct iio_device *adxl372_dev;
	size_t nrx = 0;
	int ret = 0;

	// Listen to ctrl+c and ASSERT
	signal(SIGINT, handle_sig);

	printf("* Acquiring IIO context\n");
	ASSERT((ctx = iio_create_default_context()) && "No context");
	ASSERT(iio_context_get_devices_count(ctx) > 0 && "No devices");

	printf("* Acquiring IIO Device\n");
	ASSERT(get_adxl372_dev(ctx, &adxl372_dev) && "No dev found");

	printf("* Configuring device\n");
	ret = config_device(adxl372_dev);
	if (ret < 0) {
		perror("Could not configure device.");
		shutdown();
	}

	printf("* Configuring FIFO\n");
	ret = config_fifo(adxl372_dev);
	if (ret < 0) {
		perror("Could not configure FIFO.");
		shutdown();
	}

	printf("* Acquiring all 3 axis channels\n");
	ASSERT(get_axis_chan(adxl372_dev, "accel_x", &rx_x));
	ASSERT(get_axis_chan(adxl372_dev, "accel_y", &rx_y));
	ASSERT(get_axis_chan(adxl372_dev, "accel_z", &rx_z));

	printf("* Enabling IIO axis channels\n");
	iio_channel_enable(rx_x);
	iio_channel_enable(rx_y);
	iio_channel_enable(rx_z);

	printf("* Creating IIO buffer\n");
	rxbuf = iio_device_create_buffer(adxl372_dev, 1024, false);
	if (!rxbuf) {
		perror("Could not create RX buffer");
		shutdown();
	}


	/* Make iio_buffer_refill return -EAGAIN when
	 * trying to run with data_available = 0
	*/
	iio_buffer_set_blocking_mode(rxbuf, false);

	printf("* Starting IO streaming (press CTRL+C to cancel)\n");
	while (!stop)
	{
		ssize_t nbytes_rx;
		char *p_dat, *p_end;
		ptrdiff_t p_inc;

		do {
			usleep(DRIVER_POLL_TIME_ms * 1000);

			if (stop)
				shutdown();

			// Refill RX buffer
			nbytes_rx = iio_buffer_refill(rxbuf);
		} while (nbytes_rx < 6);

		p_dat = (char *)iio_buffer_first(rxbuf, rx_x);

		p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);
		for (p_dat = (char *)iio_buffer_start(rxbuf); p_dat < p_end; p_dat += p_inc) {
			x_axis = 0;
			y_axis = 0;
			z_axis = 0;
			iio_channel_convert(rx_x, &x_axis, p_dat);
			iio_channel_convert(rx_y, &y_axis, p_dat + 2);
			iio_channel_convert(rx_z, &z_axis, p_dat + 4);
		}

		printf("x: %f g, y: %f g, z: %f g\n", x_axis / 10.0f, y_axis / 10.0f,  z_axis / 10.0f);
		nbytes_rx = 0;
	}

	shutdown();

	return 0;
}
