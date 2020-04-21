/*
 * iio_reg - Part of the industrial I/O (IIO) utilities
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
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
 * */

#include <errno.h>
#include <iio.h>
#include <stdio.h>
#include <stdlib.h>

#include "iio_common.h"

static int write_reg(const char *name, uint32_t addr, uint32_t val)
{
	struct iio_device *dev;
	struct iio_context *ctx;
	int ret;

	ctx = iio_create_default_context();
	if (!ctx) {
		perror("Unable to create context");
		return EXIT_FAILURE;
	}

	dev = iio_context_find_device(ctx, name);
	if (!dev) {
		errno = ENODEV;
		perror("Unable to find device");
		goto err_destroy_context;
	}

	ret = iio_device_reg_write(dev, addr, val);
	if (ret < 0) {
		errno = -ret;
		perror("Unable to write register");
		goto err_destroy_context;
	}

	iio_context_destroy(ctx);
	return EXIT_SUCCESS;

err_destroy_context:
	iio_context_destroy(ctx);
	return EXIT_FAILURE;
}

static int read_reg(const char *name, unsigned long addr)
{
	struct iio_device *dev;
	struct iio_context *ctx;
	uint32_t val;
	int ret;

	ctx = iio_create_default_context();
	if (!ctx) {
		perror("Unable to create context");
		return EXIT_FAILURE;
	}

	dev = iio_context_find_device(ctx, name);
	if (!dev) {
		errno = ENODEV;
		perror("Unable to find device");
		goto err_destroy_context;
	}

	ret = iio_device_reg_read(dev, addr, &val);
	if (ret < 0) {
		errno = -ret;
		perror("Unable to read register");
		goto err_destroy_context;
	}

	printf("0x%x\n", val);
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;

err_destroy_context:
	iio_context_destroy(ctx);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	unsigned long addr;

	if (argc < 3 || argc > 4) {
		printf("Usage:\n\niio_reg <device> <register> [<value>]\n");
		return 0;
	}

	addr = sanitize_clamp("register address", argv[2], 0, UINT32_MAX);

	if (argc == 3) {
		return read_reg(argv[1], addr);
	} else {
		uint32_t val = sanitize_clamp("register value", argv[3], 0, UINT32_MAX);
		return write_reg(argv[1], addr, val);
	}
}
