// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "../debug.h"
#include "ops.h"
#include "thread-pool.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>

struct serial_pdata {
	struct iio_context *ctx;
	bool debug;
	int fd;
	const void *xml_zstd;
	size_t xml_zstd_len;
};

static char *get_uart_params(const char *str,
			     unsigned int *bps, unsigned int *bits,
			     char *parity, unsigned int *stop, char *flow)
{
	const char *ptr;
	char *dev_name;

	/* Default values when unspecified */
	*bps = 57600;
	*bits = 8;
	*parity = 'n';
	*stop = 1;
	*flow = '\0';

	ptr = strchr(str, ',');
	if (!ptr) {
		dev_name = strdup(str);
	} else {
		dev_name = strndup(str, ptr - str);
		sscanf(ptr, ",%u,%u%c%u%c", bps, bits, parity, stop, flow);
	}

	return dev_name;
}

static void serial_main(struct thread_pool *pool, void *d)
{
	struct serial_pdata *pdata = d;

	do {
		interpreter(pdata->ctx, pdata->fd, pdata->fd, pdata->debug,
			    false, false, false, pool,
			    pdata->xml_zstd, pdata->xml_zstd_len);
	} while (!thread_pool_is_stopped(pool));

	close(pdata->fd);
	free(pdata);
}

static int serial_configure(int fd, unsigned int uart_bps,
			    unsigned int uart_bits,
			    char uart_parity,
			    unsigned int uart_stop,
			    char uart_flow)
{
	struct termios tty_attrs;
	int err;

	err = tcgetattr(fd, &tty_attrs);
	if (err == -1) {
		IIO_ERROR("tcgetattr failed\n");
		return -errno;
	}

	tty_attrs.c_lflag &= ~(ISIG | ICANON | ECHO | IEXTEN);
	tty_attrs.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONOCR | ONLRET);

	tty_attrs.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
				IGNCR | ICRNL | IMAXBEL | IXON | IXOFF);

	tty_attrs.c_cflag |= CLOCAL | CREAD | PARENB;
	tty_attrs.c_cflag &= ~(CSIZE | CBAUD | CRTSCTS);
#ifdef CMSPAR
	tty_attrs.c_cflag &= ~CMSPAR;
#endif

	tty_attrs.c_cc[VMIN] = 1;
	tty_attrs.c_cc[VTIME] = 0;

#define CASE_BPS(bps, attr) case bps: (attr)->c_cflag |= B##bps; break
	switch (uart_bps) {
	CASE_BPS(50, &tty_attrs);
	CASE_BPS(75, &tty_attrs);
	CASE_BPS(110, &tty_attrs);
	CASE_BPS(134, &tty_attrs);
	CASE_BPS(150, &tty_attrs);
	CASE_BPS(200, &tty_attrs);
	CASE_BPS(300, &tty_attrs);
	CASE_BPS(600, &tty_attrs);
	CASE_BPS(1200, &tty_attrs);
	CASE_BPS(1800, &tty_attrs);
	CASE_BPS(2400, &tty_attrs);
	CASE_BPS(4800, &tty_attrs);
	CASE_BPS(9600, &tty_attrs);
	CASE_BPS(19200, &tty_attrs);
	CASE_BPS(38400, &tty_attrs);
	CASE_BPS(57600, &tty_attrs);
	CASE_BPS(115200, &tty_attrs);
	CASE_BPS(230400, &tty_attrs);
	CASE_BPS(460800, &tty_attrs);
	CASE_BPS(500000, &tty_attrs);
	CASE_BPS(576000, &tty_attrs);
	CASE_BPS(921600, &tty_attrs);
	CASE_BPS(1000000, &tty_attrs);
	CASE_BPS(1152000, &tty_attrs);
	CASE_BPS(1500000, &tty_attrs);
	CASE_BPS(2000000, &tty_attrs);
	CASE_BPS(2500000, &tty_attrs);
	CASE_BPS(3000000, &tty_attrs);
	CASE_BPS(3500000, &tty_attrs);
	CASE_BPS(4000000, &tty_attrs);
	default:
		IIO_ERROR("Invalid baud rate\n");
		return -EINVAL;
	}

	switch (uart_bits) {
	case 5:
		tty_attrs.c_cflag |= CS5;
		break;
	case 6:
		tty_attrs.c_cflag |= CS6;
		break;
	case 7:
		tty_attrs.c_cflag |= CS7;
		break;
	case 8:
		tty_attrs.c_cflag |= CS8;
		break;
	default:
		IIO_ERROR("Invalid serial configuration\n");
		return -EINVAL;
	}

	switch (uart_parity) {
	case 'n':
		tty_attrs.c_cflag &= ~PARENB;
		break;
	case 'm':
#ifndef CMSPAR
		IIO_ERROR("\"mark\" parity not supported on this system.\n");
		return -EINVAL;
#else
		tty_attrs.c_cflag |= CMSPAR;
#endif
		/* falls through */
	case 'o':
		tty_attrs.c_cflag |= PARODD;
		break;
	case 's':
#ifndef CMSPAR
		IIO_ERROR("\"space\" parity not supported on this system.\n");
		return -EINVAL;
#else
		tty_attrs.c_cflag |= CMSPAR;
#endif
		/* falls through */
	case 'e':
		tty_attrs.c_cflag &= ~PARODD;
		break;
	default:
		IIO_ERROR("Invalid serial configuration\n");
		return -EINVAL;
	}

	switch (uart_stop) {
	case 1:
		tty_attrs.c_cflag &= ~CSTOPB;
		break;
	case 2:
		tty_attrs.c_cflag |= CSTOPB;
		break;
	default:
		IIO_ERROR("Invalid serial configuration\n");
		return -EINVAL;
	}

	switch (uart_flow) {
	case '\0':
		break;
	case 'x':
		tty_attrs.c_iflag |= IXON | IXOFF;
		break;
	case 'r':
		tty_attrs.c_cflag |= CRTSCTS;
		break;
	case 'd':
		IIO_ERROR("DTR/SDR is unsupported\n");
		return -EINVAL;
	default:
		IIO_ERROR("Invalid serial configuration\n");
		return -EINVAL;
	}

	err = tcsetattr(fd, TCSANOW, &tty_attrs);
	if (err == -1) {
		IIO_ERROR("Unable to apply serial settings\n");
		return -errno;
	}

	return 0;
}

int start_serial_daemon(struct iio_context *ctx, const char *uart_params,
			bool debug, struct thread_pool *pool,
			const void *xml_zstd, size_t xml_zstd_len)
{
	struct serial_pdata *pdata;
	char *dev, uart_parity, uart_flow;
	unsigned int uart_bps, uart_bits, uart_stop;
	int fd, err = -ENOMEM;

	dev = get_uart_params(uart_params, &uart_bps, &uart_bits,
			      &uart_parity, &uart_stop, &uart_flow);
	if (!dev)
		return -ENOMEM;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		goto err_free_dev;

	fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd == -1) {
		err = -errno;
		goto err_free_pdata;
	}

	err = serial_configure(fd, uart_bps, uart_bits,
			       uart_parity, uart_stop, uart_flow);
	if (err)
		goto err_close_fd;

	pdata->ctx = ctx;
	pdata->debug = debug;
	pdata->fd = fd;
	pdata->xml_zstd = xml_zstd;
	pdata->xml_zstd_len = xml_zstd_len;

	IIO_DEBUG("Serving over UART on %s at %u bps, %u bits\n",
		  dev, uart_bps, uart_bits);

	return thread_pool_add_thread(pool, serial_main, pdata, "iiod_serial_thd");

err_close_fd:
	close(fd);
err_free_pdata:
	free(pdata);
err_free_dev:
	free(dev);
	return err;
}
