/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
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
 * */

#include <errno.h>
#include <iio.h>
#include <stdio.h>
#include <stdlib.h>

static int write_reg(const char *name, unsigned long addr, unsigned long val)
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

	addr = strtoul(argv[2], NULL, 0);

	if (argc == 3) {
		return read_reg(argv[1], addr);
	} else {
		unsigned long val = strtoul(argv[3], NULL, 0);
		return write_reg(argv[1], addr, val);
	}
}
