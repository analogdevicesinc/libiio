/* SPDX-License-Identifier: MIT */
/*
 * bench_common - v1-API-specific discovery helpers for the libiio benchmark
 * suite. See bench_common.h.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <string.h>

/* Checked in order: hw_carrier identifies the carrier board, hw_model the
 * mezzanine/FMC card. */
static const char *const BOARD_ATTRS[] = { "hw_carrier", "hw_model" };

void bench_detect_board(struct iio_context *ctx, struct bench_opts *opts)
{
	unsigned int nb_attrs = iio_context_get_attrs_count(ctx);
	const char *desc;
	unsigned int a, i;

	opts->board[0] = 0;

	for (a = 0; a < sizeof(BOARD_ATTRS) / sizeof(BOARD_ATTRS[0]); a++) {
		for (i = 0; i < nb_attrs; i++) {
			const struct iio_attr *attr = iio_context_get_attr(ctx, i);
			const char *val;

			if (strcmp(iio_attr_get_name(attr), BOARD_ATTRS[a]))
				continue;

			val = iio_attr_get_static_value(attr);
			if (val) {
				strncpy(opts->board, val, sizeof(opts->board) - 1);
				return;
			}
		}
	}

	desc = iio_context_get_description(ctx);
	if (desc && *desc)
		strncpy(opts->board, desc, sizeof(opts->board) - 1);
	else
		strncpy(opts->board, "unknown", sizeof(opts->board) - 1);
}

struct iio_device *bench_find_input_device(struct iio_context *ctx,
					    struct iio_channels_mask **mask_out)
{
	unsigned int nb = iio_context_get_devices_count(ctx);
	unsigned int i, c;

	for (i = 0; i < nb; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		unsigned int nb_chn = iio_device_get_channels_count(dev);
		unsigned int nb_buf_chn = 0;
		struct iio_channels_mask *mask;

		if (!iio_device_get_buffers_count(dev))
			continue;

		mask = iio_create_channels_mask(nb_chn);
		if (!mask)
			continue;

		for (c = 0; c < nb_chn; c++) {
			struct iio_channel *chn = iio_device_get_channel(dev, c);

			if (iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn)) {
				iio_channel_enable(chn, mask);
				nb_buf_chn++;
			}
		}

		if (nb_buf_chn > 0) {
			*mask_out = mask;
			return dev;
		}

		iio_channels_mask_destroy(mask);
	}

	return NULL;
}

const struct iio_attr *bench_find_attr_by_name(struct iio_context *ctx, const char *name)
{
	unsigned int nb_dev = iio_context_get_devices_count(ctx);
	unsigned int d;

	for (d = 0; d < nb_dev; d++) {
		struct iio_device *dev = iio_context_get_device(ctx, d);
		const struct iio_attr *attr = iio_device_find_attr(dev, name);
		unsigned int nb_chn, c;

		if (attr)
			return attr;

		nb_chn = iio_device_get_channels_count(dev);
		for (c = 0; c < nb_chn; c++) {
			struct iio_channel *chn = iio_device_get_channel(dev, c);

			attr = iio_channel_find_attr(chn, name);
			if (attr)
				return attr;
		}
	}

	return NULL;
}
