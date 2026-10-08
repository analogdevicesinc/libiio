/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_ADC_HAL_H
#define IIO_ADC_HAL_H

#define IIO_ADC_MAX_CHANNELS 8

enum iio_adc_gain {
	IIO_ADC_GAIN_1_6,
	IIO_ADC_GAIN_1_5,
	IIO_ADC_GAIN_1_4,
	IIO_ADC_GAIN_2_7,
	IIO_ADC_GAIN_1_3,
	IIO_ADC_GAIN_2_5,
	IIO_ADC_GAIN_1_2,
	IIO_ADC_GAIN_2_3,
	IIO_ADC_GAIN_4_5,
	IIO_ADC_GAIN_1,
	IIO_ADC_GAIN_2,
	IIO_ADC_GAIN_3,
	IIO_ADC_GAIN_4,
	IIO_ADC_GAIN_6,
	IIO_ADC_GAIN_8,
	IIO_ADC_GAIN_12,
	IIO_ADC_GAIN_16,
	IIO_ADC_GAIN_24,
	IIO_ADC_GAIN_32,
	IIO_ADC_GAIN_64,
	IIO_ADC_GAIN_128,
};

enum iio_adc_reference {
	IIO_ADC_REF_VDD_1,
	IIO_ADC_REF_VDD_1_2,
	IIO_ADC_REF_VDD_1_3,
	IIO_ADC_REF_VDD_1_4,
	IIO_ADC_REF_INTERNAL,
	IIO_ADC_REF_EXTERNAL0,
	IIO_ADC_REF_EXTERNAL1,
};

struct iio_adc_hal {
	const char *const *channels;
	unsigned int       num_channels;
	unsigned int       resolution_bits;
	int                ref_voltage_mv;
	int (*init)(void);
	int (*read_raw)(unsigned int channel, int *value);
	int (*set_gain)(unsigned int channel, unsigned int gain);
	int (*set_reference)(unsigned int channel, unsigned int reference);
};

extern const struct iio_adc_hal iio_adc_hal;

#endif /* IIO_ADC_HAL_H */
