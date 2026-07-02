// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include <errno.h>
#include <string.h>

#include "iio-private.h"

int iio_parse_format_string(const char *fmt_str, struct iio_data_format *fmt)
{
	char endian, sign;
	int err;

	if (!fmt_str || !fmt)
		return -EINVAL;

	/* Parse format string with repeat count: "le:s16/16X4>>0" */
	if (strchr(fmt_str, 'X')) {
		err = iio_sscanf(fmt_str, "%ce:%c%u/%uX%u>>%u",
#ifdef _MSC_BUILD
				&endian, sizeof(endian), &sign, sizeof(sign),
#else
				&endian, &sign,
#endif
				&fmt->bits, &fmt->length, &fmt->repeat, &fmt->shift);
		if (err != 6)
			return -EINVAL;
	} else {
		/* Parse format string without repeat count: "le:s16/16>>0" */
		fmt->repeat = 1;
		err = iio_sscanf(fmt_str, "%ce:%c%u/%u>>%u",
#ifdef _MSC_BUILD
				&endian, sizeof(endian), &sign, sizeof(sign),
#else
				&endian, &sign,
#endif
				&fmt->bits, &fmt->length, &fmt->shift);
		if (err != 5)
			return -EINVAL;
	}

	/* Set format flags */
	fmt->is_be = (endian == 'b');
	fmt->is_signed = (sign == 's' || sign == 'S');
	fmt->is_fully_defined = (sign == 'S' || sign == 'U' || fmt->bits == fmt->length);

	return 0;
}
