/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <no_os_util.h>
#include "iio_adc_hal.h"

static const uint16_t sine_lut[32] = {
	0x8000, 0x98f8, 0xb0fb, 0xc71c, 0xda82, 0xea6d, 0xf641, 0xfd89,
	0xffff, 0xfd89, 0xf641, 0xea6d, 0xda82, 0xc71c, 0xb0fb, 0x98f8,
	0x8000, 0x6707, 0x4f04, 0x38e3, 0x257d, 0x1592, 0x09be, 0x0276,
	0x0000, 0x0276, 0x09be, 0x1592, 0x257d, 0x38e3, 0x4f04, 0x6707,
};

static const char *const adc_demo_channels[] = {
	"voltage0",
	"voltage1",
};

static unsigned int lut_index[NO_OS_ARRAY_SIZE(adc_demo_channels)];

static int adc_demo_init(void)
{
	unsigned int i;

	for (i = 0; i < NO_OS_ARRAY_SIZE(adc_demo_channels); i++)
		lut_index[i] = 0;

	return 0;
}

static int adc_demo_read_raw(unsigned int channel, int *value)
{
	if (channel >= NO_OS_ARRAY_SIZE(adc_demo_channels))
		return -EINVAL;

	*value = sine_lut[lut_index[channel]];
	lut_index[channel] = (lut_index[channel] + 1) & 0x1F;

	return 0;
}

const struct iio_adc_hal iio_adc_hal = {
	.channels        = adc_demo_channels,
	.num_channels    = NO_OS_ARRAY_SIZE(adc_demo_channels),
	.resolution_bits = 16,
	.ref_voltage_mv  = 2500,
	.init            = adc_demo_init,
	.read_raw        = adc_demo_read_raw,
};
