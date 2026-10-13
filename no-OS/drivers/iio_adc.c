/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <no_os_print_log.h>
#include <no_os_util.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "iio_adc.h"
#include "iio_adc_hal.h"
#include <iio/iio-backend.h>

static const char *const gain_values[] = {
	[IIO_ADC_GAIN_1_6] = "1/6",
	[IIO_ADC_GAIN_1_5] = "1/5",
	[IIO_ADC_GAIN_1_4] = "1/4",
	[IIO_ADC_GAIN_2_7] = "2/7",
	[IIO_ADC_GAIN_1_3] = "1/3",
	[IIO_ADC_GAIN_2_5] = "2/5",
	[IIO_ADC_GAIN_1_2] = "1/2",
	[IIO_ADC_GAIN_2_3] = "2/3",
	[IIO_ADC_GAIN_4_5] = "4/5",
	[IIO_ADC_GAIN_1]   = "1",
	[IIO_ADC_GAIN_2]   = "2",
	[IIO_ADC_GAIN_3]   = "3",
	[IIO_ADC_GAIN_4]   = "4",
	[IIO_ADC_GAIN_6]   = "6",
	[IIO_ADC_GAIN_8]   = "8",
	[IIO_ADC_GAIN_12]  = "12",
	[IIO_ADC_GAIN_16]  = "16",
	[IIO_ADC_GAIN_24]  = "24",
	[IIO_ADC_GAIN_32]  = "32",
	[IIO_ADC_GAIN_64]  = "64",
	[IIO_ADC_GAIN_128] = "128",
};
#define GAIN_DEFAULT_IDX IIO_ADC_GAIN_1

static const char *const reference_values[] = {
	[IIO_ADC_REF_VDD_1]     = "VDD",
	[IIO_ADC_REF_VDD_1_2]   = "VDD/2",
	[IIO_ADC_REF_VDD_1_3]   = "VDD/3",
	[IIO_ADC_REF_VDD_1_4]   = "VDD/4",
	[IIO_ADC_REF_INTERNAL]  = "Internal",
	[IIO_ADC_REF_EXTERNAL0] = "External0",
	[IIO_ADC_REF_EXTERNAL1] = "External1",
};
#define REFERENCE_DEFAULT_IDX IIO_ADC_REF_INTERNAL

struct adc_channel_state {
	int scale_val;
	int scale_val2;
	unsigned int gain;
	unsigned int reference;
	int differential;
};

static struct adc_channel_state chan_state[IIO_ADC_MAX_CHANNELS];

static int adc_emit_int(char *dst, size_t len, int val)
{
	int ret = snprintf(dst, len, "%d", val);

	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int adc_emit_str(char *dst, size_t len, const char *val)
{
	int ret = snprintf(dst, len, "%s", val);

	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int adc_emit_micro(char *dst, size_t len, int val, int val2)
{
	int ret;

	if (val2 < 0)
		ret = snprintf(dst, len, "-%d.%06u", abs(val),
			       (unsigned int)-val2);
	else
		ret = snprintf(dst, len, "%d.%06u", val, (unsigned int)val2);

	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int adc_parse_int(const char *src, int *val)
{
	char *end;
	long parsed;

	if (!(*src == '-' || *src == '+' || (*src >= '0' && *src <= '9')))
		return -EINVAL;

	errno = 0;
	parsed = strtol(src, &end, 10);
	if (end == src || errno == ERANGE || (long)(int)parsed != parsed)
		return -EINVAL;

	if (*end == '\n')
		end++;
	if (*end != '\0')
		return -EINVAL;

	*val = (int)parsed;

	return 0;
}

static int adc_parse_micro(const char *src, int *val, int *val2)
{
	int integer = 0, fract = 0, mult = 100000;
	bool negative = false, integer_part = true;

	if (*src == '-') {
		negative = true;
		src++;
	} else if (*src == '+') {
		src++;
	}

	if (*src == '\0')
		return -EINVAL;

	while (*src) {
		if (*src >= '0' && *src <= '9') {
			if (integer_part) {
				integer = integer * 10 + (*src - '0');
			} else if (mult) {
				fract += mult * (*src - '0');
				mult /= 10;
			}
		} else if (*src == '\n' && src[1] == '\0') {
			break;
		} else if (*src == '.' && integer_part) {
			integer_part = false;
		} else {
			return -EINVAL;
		}
		src++;
	}

	if (negative) {
		if (integer)
			integer = -integer;
		else
			fract = -fract;
	}

	*val = integer;
	*val2 = fract;

	return 0;
}

static int adc_lookup_str(const char *const *table, size_t count,
			  const char *src, size_t len)
{
	size_t i, slen = len;

	while (slen > 0 && (src[slen - 1] == '\n' || src[slen - 1] == '\0'))
		slen--;

	for (i = 0; i < count; i++) {
		if (table[i] && strlen(table[i]) == slen && /* Flawfinder: ignore */
		    strncmp(src, table[i], slen) == 0)
			return (int)i;
	}

	return -EINVAL;
}

static int adc_channel_index(const char *id)
{
	unsigned int i, n = iio_adc_hal.num_channels;

	for (i = 0; i < n; i++) {
		const char *cid = iio_adc_hal.channels[i];

		if (cid && strcmp(cid, id) == 0)
			return (int)i;
	}

	return -EINVAL;
}

static void iio_adc_state_init(void)
{
	unsigned int i;

	for (i = 0; i < IIO_ADC_MAX_CHANNELS; i++) {
		chan_state[i].scale_val = 1;
		chan_state[i].scale_val2 = 0;
		chan_state[i].gain = GAIN_DEFAULT_IDX;
		chan_state[i].reference = REFERENCE_DEFAULT_IDX;
		chan_state[i].differential = 0;
	}
}

/*
 * Per-channel format: the storage is the resolution rounded up to whole
 * bytes, big endian, signed for a differential channel, and the scale is one
 * LSB in millivolts. The format is published in the context XML, so it
 * follows the channel's differential setting at context creation.
 */
static void adc_channel_format(unsigned int idx, struct iio_data_format *fmt)
{
	unsigned int res = iio_adc_hal.resolution_bits;
	bool is_signed = chan_state[idx].differential;

	*fmt = (struct iio_data_format) {
		.length = NO_OS_DIV_ROUND_UP(res, 8) * 8,
		.bits = res,
		.is_signed = is_signed,
		.with_scale = true,
		.scale = (double)iio_adc_hal.ref_voltage_mv /
			 (double)(1ull << (res - (is_signed ? 1 : 0))),
		.is_be = true,
	};
}

static int iio_adc_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_data_format fmt;
	struct iio_channel *ch;
	unsigned int i, n = iio_adc_hal.num_channels;

	if (n > IIO_ADC_MAX_CHANNELS)
		return -EINVAL;

	for (i = 0; i < n; i++) {
		adc_channel_format(i, &fmt);

		ch = iio_device_add_channel(iio_dev, (long)i,
					    iio_adc_hal.channels[i],
					    NULL, NULL,
					    false, false, &fmt);
		if (!ch)
			return -ENOMEM;

		iio_channel_add_attr(ch, "raw", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "scale", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "gain", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "process", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "reference", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "differential", IIO_ATTR_TYPE_CHANNEL,
				     NULL);
	}

	iio_device_add_attr(iio_dev, "internal_ref_voltage",
			    IIO_ATTR_TYPE_DEVICE);

	return 0;
}

static int iio_adc_read_attr(void *dev,
			     const struct iio_device *iio_dev,
			     const struct iio_attr *attr,
			     char *dst, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	int idx;
	int raw_value;
	int ret;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	if (attr->type == IIO_ATTR_TYPE_DEVICE) {
		if (strcmp(attr_name, "internal_ref_voltage") == 0)
			return adc_emit_int(dst, len,
					    iio_adc_hal.ref_voltage_mv);
		return -EINVAL;
	}

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	idx = adc_channel_index(ch_id);
	if (idx < 0)
		return idx;

	if (strcmp(attr_name, "scale") == 0)
		return adc_emit_micro(dst, len, chan_state[idx].scale_val,
				      chan_state[idx].scale_val2);

	if (strcmp(attr_name, "raw") == 0) {
		ret = iio_adc_hal.read_raw((unsigned int)idx, &raw_value);
		if (ret)
			return ret;

		return adc_emit_int(dst, len, raw_value);
	}

	if (strcmp(attr_name, "gain") == 0)
		return adc_emit_str(dst, len, gain_values[chan_state[idx].gain]);

	if (strcmp(attr_name, "reference") == 0)
		return adc_emit_str(dst, len,
				    reference_values[chan_state[idx].reference]);

	if (strcmp(attr_name, "differential") == 0)
		return adc_emit_int(dst, len, chan_state[idx].differential);

	if (strcmp(attr_name, "process") == 0) {
		int64_t scale_uv;

		ret = iio_adc_hal.read_raw((unsigned int)idx, &raw_value);
		if (ret)
			return ret;

		scale_uv = (int64_t)chan_state[idx].scale_val * 1000000 +
			   chan_state[idx].scale_val2;

		return adc_emit_int(dst, len,
				    (int)((raw_value * scale_uv) / 1000000));
	}

	return -EINVAL;
}

static int iio_adc_write_attr(void *dev,
			      const struct iio_device *iio_dev,
			      const struct iio_attr *attr,
			      const char *src, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	int idx;
	int integer, fract;
	int str_idx;
	int ret;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	if (attr->type == IIO_ATTR_TYPE_DEVICE)
		return -EPERM;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	idx = adc_channel_index(ch_id);
	if (idx < 0)
		return idx;

	if (strcmp(attr_name, "scale") == 0) {
		ret = adc_parse_micro(src, &integer, &fract);
		if (ret)
			return ret;

		chan_state[idx].scale_val = integer;
		chan_state[idx].scale_val2 = fract;
		return len;
	}

	if (strcmp(attr_name, "gain") == 0) {
		str_idx = adc_lookup_str(gain_values,
					 NO_OS_ARRAY_SIZE(gain_values),
					 src, len);
		if (str_idx < 0)
			return str_idx;

		if (iio_adc_hal.set_gain) {
			ret = iio_adc_hal.set_gain((unsigned int)idx,
						   (unsigned int)str_idx);
			if (ret)
				return ret;
		}

		chan_state[idx].gain = (unsigned int)str_idx;
		return len;
	}

	if (strcmp(attr_name, "reference") == 0) {
		str_idx = adc_lookup_str(reference_values,
					 NO_OS_ARRAY_SIZE(reference_values),
					 src, len);
		if (str_idx < 0)
			return str_idx;

		if (iio_adc_hal.set_reference) {
			ret = iio_adc_hal.set_reference((unsigned int)idx,
							(unsigned int)str_idx);
			if (ret)
				return ret;
		}

		chan_state[idx].reference = (unsigned int)str_idx;
		return len;
	}

	if (strcmp(attr_name, "differential") == 0) {
		ret = adc_parse_int(src, &integer);
		if (ret)
			return ret;

		if (integer != 0 && integer != 1)
			return -EINVAL;
		chan_state[idx].differential = integer;
		return len;
	}

	if (strcmp(attr_name, "raw") == 0 ||
	    strcmp(attr_name, "process") == 0)
		return -EPERM;

	return -EINVAL;
}

#define ADC_NUM_REGS 16
static uint32_t adc_regs[ADC_NUM_REGS];

static int iio_adc_reg_read(void *dev, uint32_t reg, uint32_t *val)
{
	if (reg >= ADC_NUM_REGS)
		return -EINVAL;

	*val = adc_regs[reg];

	return 0;
}

static int iio_adc_reg_write(void *dev, uint32_t reg, uint32_t val)
{
	if (reg >= ADC_NUM_REGS)
		return -EINVAL;

	adc_regs[reg] = val;

	return 0;
}

int iio_adc_init(void)
{
	int ret;

	ret = iio_adc_hal.init();
	if (ret)
		return ret;

	if (!iio_adc_hal.resolution_bits || iio_adc_hal.resolution_bits > 32)
		return -EINVAL;

	iio_adc_state_init();

	return 0;
}

int iio_adc_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	*info = (struct noos_iio_device_info) {
		.name = "iio-adc",
		.add_channels = iio_adc_add_channels,
		.read_attr = iio_adc_read_attr,
		.write_attr = iio_adc_write_attr,
		.reg_read = iio_adc_reg_read,
		.reg_write = iio_adc_reg_write,
	};

	return 0;
}
