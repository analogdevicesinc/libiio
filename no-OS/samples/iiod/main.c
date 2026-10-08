/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio/iio.h>
#include <iio/iio-backend.h>

#include "iio_adc.h"
#include "iio_device.h"

static int noos_register_devices(void)
{
	struct noos_iio_device_info info;
	int ret;

	ret = iio_adc_init();
	if (ret)
		return ret;

	ret = iio_adc_get_device_info(&info);
	if (ret)
		return ret;

	return noos_iio_register_device(&info);
}

int main(void)
{
	int ret;

	ret = noos_register_devices();
	if (ret)
		return ret;

	return noos_iiod_run();
}
