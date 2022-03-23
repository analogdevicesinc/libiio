// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <errno.h>
#include <string.h>

struct iio_channels_mask * iio_create_channels_mask(unsigned int nb_channels)
{
	struct iio_channels_mask *mask;
	size_t nb_words = (nb_channels + 31) / 32;

	if (!nb_words)
		return NULL;

	mask = zalloc(sizeof(*mask) + nb_words * sizeof(uint32_t));
	if (mask)
		mask->words = nb_words;

	return mask;
}

int iio_channels_mask_copy(struct iio_channels_mask *dst,
			   const struct iio_channels_mask *src)
{
	if (dst->words != src->words)
		return -EINVAL;

	memcpy(dst->mask, src->mask, src->words * sizeof(uint32_t));

	return 0;
}

void iio_channels_mask_destroy(struct iio_channels_mask *mask)
{
	free(mask);
}
