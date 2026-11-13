/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * The standard channels of each device type, for table-driven devices
 * (struct noos_iio_chan, include/iio_device.h): every part of a type then
 * looks the same to a host, with the attribute names and sharing Linux
 * uses for that type.
 */

#ifndef NOOS_INCLUDE_IIO_CHANNELS_H_
#define NOOS_INCLUDE_IIO_CHANNELS_H_

#include <iio_device.h>

/* ---------- ADC inputs (voltage) ---------- */

#define NOOS_IIO_ADC_SHARED (NOOS_IIO_BIT(SAMP_FREQ) | NOOS_IIO_BIT(OVERSAMPLING_RATIO))

#define NOOS_IIO_ADC_MASKS(_info, _avail) \
	.info_mask_separate = (_info) & ~NOOS_IIO_ADC_SHARED, \
	.info_mask_separate_available = (_avail) & ~NOOS_IIO_ADC_SHARED, \
	.info_mask_shared_by_all = (_info) & NOOS_IIO_ADC_SHARED, \
	.info_mask_shared_by_all_available = (_avail) & NOOS_IIO_ADC_SHARED

/*
 * Input @_n: @_scan its scan index (-1: none), @_info the NOOS_IIO_BIT()s
 * it has, @_avail those of them with a list, and last its scan_type, as
 * Linux's: { .sign = 'u', .realbits = 12, .storagebits = 16,
 * .endianness = NOOS_IIO_BE }, or NOOS_IIO_NO_SCAN_TYPE.
 */
#define NOOS_IIO_VOLTAGE_CHANNEL(_n, _scan, _info, _avail, ...) \
	{ \
		.type = NOOS_IIO_VOLTAGE, \
		.indexed = true, \
		.channel = (_n), \
		.scan_index = (_scan), \
		.scan_type = __VA_ARGS__, \
		NOOS_IIO_ADC_MASKS(_info, _avail), \
		.priv = (_n), \
	}

/* Input @_p minus input @_m. */
#define NOOS_IIO_VOLTAGE_DIFF_CHANNEL(_p, _m, _scan, _info, _avail, ...) \
	{ \
		.type = NOOS_IIO_VOLTAGE, \
		.indexed = true, \
		.differential = true, \
		.channel = (_p), \
		.channel2 = (_m), \
		.scan_index = (_scan), \
		.scan_type = __VA_ARGS__, \
		NOOS_IIO_ADC_MASKS(_info, _avail), \
		.priv = (_p), \
	}

/* ---------- Accelerometers ---------- */

#define NOOS_IIO_ACCEL_SHARED \
	(NOOS_IIO_BIT(SCALE) | NOOS_IIO_BIT(SAMP_FREQ) | \
			NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY) | \
			NOOS_IIO_BIT(LOW_PASS_FILTER_3DB_FREQUENCY) | \
			NOOS_IIO_BIT(OVERSAMPLING_RATIO))

/*
 * One axis: @_axis is X, Y or Z, @_scan its scan index (-1: none), @_info
 * the NOOS_IIO_BIT()s it has, @_avail those of them with a list, and last
 * its scan_type, as Linux's: { .sign = 's', .realbits = 20,
 * .storagebits = 32, .endianness = NOOS_IIO_LE }, or NOOS_IIO_NO_SCAN_TYPE.
 */
#define NOOS_IIO_ACCEL_CHANNEL(_axis, _scan, _info, _avail, ...) \
	{ \
		.type = NOOS_IIO_ACCEL, \
		.modified = true, \
		.channel2 = NOOS_IIO_MOD_##_axis, \
		.scan_index = (_scan), \
		.scan_type = __VA_ARGS__, \
		.info_mask_separate = (_info) & ~NOOS_IIO_ACCEL_SHARED, \
		.info_mask_separate_available = (_avail) & ~NOOS_IIO_ACCEL_SHARED, \
		.info_mask_shared_by_type = (_info) & NOOS_IIO_ACCEL_SHARED, \
		.info_mask_shared_by_type_available = (_avail) & NOOS_IIO_ACCEL_SHARED, \
		.priv = NOOS_IIO_MOD_##_axis - NOOS_IIO_MOD_X, \
	}

/* The three axes, scan elements 0 to 2. */
#define NOOS_IIO_ACCEL_XYZ(_info, _avail, ...) \
	NOOS_IIO_ACCEL_CHANNEL(X, 0, _info, _avail, __VA_ARGS__), \
			NOOS_IIO_ACCEL_CHANNEL(Y, 1, _info, _avail, __VA_ARGS__), \
			NOOS_IIO_ACCEL_CHANNEL(Z, 2, _info, _avail, __VA_ARGS__)

/* ---------- Temperature ---------- */

/* The part's temperature: @_scan its scan index (-1: none), @_info the
 * NOOS_IIO_BIT()s it has, @_avail those of them with a list, and last its
 * scan_type (an initializer, as Linux's, or NOOS_IIO_NO_SCAN_TYPE). */
#define NOOS_IIO_TEMP_CHANNEL(_scan, _info, _avail, ...) \
	{ \
		.type = NOOS_IIO_TEMP, \
		.scan_index = (_scan), \
		.scan_type = __VA_ARGS__, \
		.info_mask_separate = (_info), \
		.info_mask_separate_available = (_avail), \
	}

/* Temperature @_n of several; priv is @_n. */
#define NOOS_IIO_TEMP_CHANNEL_N(_n, _scan, _info, _avail, ...) \
	{ \
		.type = NOOS_IIO_TEMP, \
		.indexed = true, \
		.channel = (_n), \
		.scan_index = (_scan), \
		.scan_type = __VA_ARGS__, \
		.info_mask_separate = (_info), \
		.info_mask_separate_available = (_avail), \
		.priv = (_n), \
	}

#endif /* NOOS_INCLUDE_IIO_CHANNELS_H_ */
