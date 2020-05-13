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
#include <getopt.h>

#include "iio_common.h"

#define MY_NAME "iio_reg"

static const struct option options[] = {
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"<device> <register> [<value>]\n"
};

static int write_reg(struct iio_device *dev, uint32_t addr, uint32_t val)
{
	int ret;

	ret = iio_device_reg_write(dev, addr, val);
	if (ret < 0) {
		errno = -ret;
		perror("Unable to write register");
		goto err_destroy_context;
	}

	return EXIT_SUCCESS;

err_destroy_context:
	return EXIT_FAILURE;
}

static int read_reg(struct iio_device *dev, unsigned long addr)
{
	uint32_t val;
	int ret;

	ret = iio_device_reg_read(dev, addr, &val);
	if (ret < 0) {
		errno = -ret;
		perror("Unable to read register");
		goto err_destroy_context;
	}

	printf("0x%x\n", val);
	return EXIT_SUCCESS;

err_destroy_context:
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	char **argw;
	unsigned long addr;
	struct iio_context *ctx;
	struct iio_device *dev;
	int c, option_index = 0;
	char * name;

	argw = dup_argv(MY_NAME, argc, argv);

	ctx = handle_common_opts(MY_NAME, argc, argw, options, options_descriptions);

	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS, /* Flawfinder: ignore */
			options, &option_index)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'n':
		case 'x':
		case 'S':
		case 'u':
		case 'a':
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}

	if ((argc - optind) < 2 || (argc - optind) > 3) {
		usage(MY_NAME, options, options_descriptions);
		return EXIT_SUCCESS;
	}

	name = cmn_strndup(argw[optind], NAME_MAX);
	dev = iio_context_find_device(ctx, name);
	if (!dev) {
		perror("Unable to find device");
		goto err_destroy_context;
	}

	addr = sanitize_clamp("register address", argw[optind + 1], 0, UINT32_MAX);

	if ((argc - optind) == 2) {
		return read_reg(dev, addr);
	} else {
		uint32_t val = sanitize_clamp("register value", argw[optind + 2], 0, UINT32_MAX);
		return write_reg(dev, addr, val);
	}

err_destroy_context:
	free(name);
	iio_context_destroy(ctx);
	free_argw(argc, argw);
	return EXIT_SUCCESS;
}
