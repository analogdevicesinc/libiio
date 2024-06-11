// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-private.h"

#include <errno.h>
#include <iio/iio-debug.h>
#include <stdio.h>
#include <string.h>

static const char * const iio_chan_type_name_spec[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_ACCEL] = "accel",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_MAGN] = "magn",
	[IIO_LIGHT] = "illuminance",
	[IIO_INTENSITY] = "intensity",
	[IIO_PROXIMITY] = "proximity",
	[IIO_TEMP] = "temp",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_ANGL] = "angl",
	[IIO_TIMESTAMP] = "timestamp",
	[IIO_CAPACITANCE] = "capacitance",
	[IIO_ALTVOLTAGE] = "altvoltage",
	[IIO_CCT] = "cct",
	[IIO_PRESSURE] = "pressure",
	[IIO_HUMIDITYRELATIVE] = "humidityrelative",
	[IIO_ACTIVITY] = "activity",
	[IIO_STEPS] = "steps",
	[IIO_ENERGY] = "energy",
	[IIO_DISTANCE] = "distance",
	[IIO_VELOCITY] = "velocity",
	[IIO_CONCENTRATION] = "concentration",
	[IIO_RESISTANCE] = "resistance",
	[IIO_PH] = "ph",
	[IIO_UVINDEX] = "uvindex",
	[IIO_ELECTRICALCONDUCTIVITY] = "electricalconductivity",
	[IIO_COUNT] = "count",
	[IIO_INDEX] = "index",
	[IIO_GRAVITY] = "gravity",
	[IIO_POSITIONRELATIVE] = "positionrelative",
	[IIO_PHASE] = "phase",
	[IIO_MASSCONCENTRATION] = "massconcentration",
	[IIO_DELTA_ANGL] = "delta_angl",
	[IIO_DELTA_VELOCITY] = "delta_velocity",
	[IIO_COLORTEMP] = "colortemp",
	[IIO_CHROMATICITY] = "chromaticity",
};

static const char * const hwmon_chan_type_name_spec[] = {
	[HWMON_VOLTAGE] = "in",
	[HWMON_FAN] = "fan",
	[HWMON_PWM] = "pwm",
	[HWMON_TEMP] = "temp",
	[HWMON_CURRENT] = "curr",
	[HWMON_POWER] = "power",
	[HWMON_ENERGY] = "energy",
	[HWMON_HUMIDITY] = "humidity",
	[HWMON_INTRUSION] = "intrusion",
};

static const char * const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_X_AND_Y] = "x&y",
	[IIO_MOD_X_AND_Z] = "x&z",
	[IIO_MOD_Y_AND_Z] = "y&z",
	[IIO_MOD_X_AND_Y_AND_Z] = "x&y&z",
	[IIO_MOD_X_OR_Y] = "x|y",
	[IIO_MOD_X_OR_Z] = "x|z",
	[IIO_MOD_Y_OR_Z] = "y|z",
	[IIO_MOD_X_OR_Y_OR_Z] = "x|y|z",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y] = "sqrt(x^2+y^2)",
	[IIO_MOD_SUM_SQUARED_X_Y_Z] = "x^2+y^2+z^2",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
	[IIO_MOD_LIGHT_CLEAR] = "clear",
	[IIO_MOD_LIGHT_RED] = "red",
	[IIO_MOD_LIGHT_GREEN] = "green",
	[IIO_MOD_LIGHT_BLUE] = "blue",
	[IIO_MOD_LIGHT_UV] = "uv",
	[IIO_MOD_LIGHT_DUV] = "duv",
	[IIO_MOD_QUATERNION] = "quaternion",
	[IIO_MOD_TEMP_AMBIENT] = "ambient",
	[IIO_MOD_TEMP_OBJECT] = "object",
	[IIO_MOD_NORTH_MAGN] = "from_north_magnetic",
	[IIO_MOD_NORTH_TRUE] = "from_north_true",
	[IIO_MOD_NORTH_MAGN_TILT_COMP] = "from_north_magnetic_tilt_comp",
	[IIO_MOD_NORTH_TRUE_TILT_COMP] = "from_north_true_tilt_comp",
	[IIO_MOD_RUNNING] = "running",
	[IIO_MOD_JOGGING] = "jogging",
	[IIO_MOD_WALKING] = "walking",
	[IIO_MOD_STILL] = "still",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z] = "sqrt(x^2+y^2+z^2)",
	[IIO_MOD_I] = "i",
	[IIO_MOD_Q] = "q",
	[IIO_MOD_CO2] = "co2",
	[IIO_MOD_ETHANOL] = "ethanol",
	[IIO_MOD_H2] = "h2",
	[IIO_MOD_O2] = "o2",
	[IIO_MOD_VOC] = "voc",
	[IIO_MOD_PM1] = "pm1",
	[IIO_MOD_PM2P5] = "pm2p5",
	[IIO_MOD_PM4] = "pm4",
	[IIO_MOD_PM10] = "pm10",
	[IIO_MOD_LINEAR_X] = "linear_x",
	[IIO_MOD_LINEAR_Y] = "linear_y",
	[IIO_MOD_LINEAR_Z] = "linear_z",
	[IIO_MOD_PITCH] = "pitch",
	[IIO_MOD_YAW] = "yaw",
	[IIO_MOD_ROLL] = "roll",
	[IIO_MOD_LIGHT_UVA] = "uva",
	[IIO_MOD_LIGHT_UVB] = "uvb",
};

/*
 * Looks for a IIO channel modifier at the beginning of the string s. If a
 * modifier was found the symbolic constant (IIO_MOD_*) is returned, otherwise
 * IIO_NO_MOD is returned. If a modifier was found len_p will be updated with
 * the length of the modifier.
 */
unsigned int find_channel_modifier(const char *s, size_t *len_p)
{
	unsigned int i;
	size_t len;

	for (i = 0; i < ARRAY_SIZE(modifier_names); i++) {
		if (!modifier_names[i])
			continue;
		len = strlen(modifier_names[i]);
		if (strncmp(s, modifier_names[i], len) == 0 &&
				(s[len] == '\0' || s[len] == '_')) {
			if (len_p)
				*len_p = len;
			return i;
		}
	}

	return IIO_NO_MOD;
}

static int iio_channel_find_type(const char *id,
			const char *const *name_spec, size_t size)
{
	unsigned int i;
	size_t len;

	for (i = 0; i < size; i++) {
		len = strlen(name_spec[i]);
		if (strncmp(name_spec[i], id, len) != 0)
		      continue;

		/* Type must be followed by one of a '\0', a '_', or a digit */
		if (id[len] != '\0' && id[len] != '_' &&
				(id[len] < '0' || id[len] > '9'))
			continue;

		return i;
	}

	return -EINVAL;
}

#if WITH_HWMON
bool iio_channel_is_hwmon(const char *id)
{
	return iio_channel_find_type(id, hwmon_chan_type_name_spec,
				ARRAY_SIZE(hwmon_chan_type_name_spec)) >= 0;
}
#endif /* WITH_HWMON */

/*
 * Initializes all auto-detected fields of the channel struct. Must be called
 * after the channel has been otherwise fully initialized.
 */
void iio_channel_init_finalize(struct iio_channel *chn)
{
	unsigned int i;
	size_t len;
	char *mod;
	int type;

	if (iio_device_is_hwmon(chn->dev)) {
		type = iio_channel_find_type(chn->id, hwmon_chan_type_name_spec,
					ARRAY_SIZE(hwmon_chan_type_name_spec));
	} else {
		type = iio_channel_find_type(chn->id, iio_chan_type_name_spec,
					ARRAY_SIZE(iio_chan_type_name_spec));
	}

	chn->type = (type >= 0) ? type : IIO_CHAN_TYPE_UNKNOWN;
	chn->modifier = IIO_NO_MOD;

	mod = strchr(chn->id, '_');
	if (!mod)
		return;

	mod++;

	for (i = 0; i < ARRAY_SIZE(modifier_names); i++) {
		if (!modifier_names[i])
			continue;
		len = strlen(modifier_names[i]);
		if (strncmp(modifier_names[i], mod, len) != 0)
			continue;

		chn->modifier = (enum iio_modifier) i;
		break;
	}
}

static ssize_t iio_snprintf_chan_attr_xml(const struct iio_attr *attr,
					  char *str, ssize_t len)
{
	ssize_t ret, alen = 0;

	if (!attr->filename)
		return iio_snprintf(str, len, "<attribute name=\"%s\" />", attr->name);

	ret = iio_snprintf(str, len, "<attribute name=\"%s\" ", attr->name);
	if (ret < 0)
		return ret;

	iio_update_xml_indexes(ret, &str, &len, &alen);

	ret = iio_xml_print_and_sanitized_param(str, len, "filename=\"",
						attr->filename, "\" />");
	if (ret < 0)
		return ret;

	return alen + ret;
}

static ssize_t iio_snprintf_scan_element_xml(char *str, ssize_t len,
					     const struct iio_channel *chn)
{
	char processed = (chn->format.is_fully_defined ? 'A' - 'a' : 0);
	char repeat[12] = "", scale[48] = "";

	if (chn->format.repeat > 1)
		iio_snprintf(repeat, sizeof(repeat), "X%u", chn->format.repeat);

	if (chn->format.with_scale)
		iio_snprintf(scale, sizeof(scale), "scale=\"%f\" ", chn->format.scale);

	return iio_snprintf(str, len,
			"<scan-element index=\"%li\" format=\"%ce:%c%u/%u%s&gt;&gt;%u\" %s/>",
			chn->index, chn->format.is_be ? 'b' : 'l',
			chn->format.is_signed ? 's' + processed : 'u' + processed,
			chn->format.bits, chn->format.length, repeat,
			chn->format.shift, scale);
}

ssize_t iio_snprintf_channel_xml(char *ptr, ssize_t len,
				 const struct iio_channel *chn)
{
	ssize_t ret, alen = 0;
	unsigned int i;


	ret = iio_xml_print_and_sanitized_param(ptr, len, "<channel id=\"",
						chn->id, "\"");
	if (ret < 0)
		return ret;
	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	if (chn->name) {
		ret = iio_snprintf(ptr, len, " name=\"%s\"", chn->name);
		if (ret < 0)
			return ret;
		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	ret = iio_snprintf(ptr, len, " type=\"%s\" >", chn->is_output ? "output" : "input");
	if (ret < 0)
		return ret;
	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	if (chn->is_scan_element) {
		ret = iio_snprintf_scan_element_xml(ptr, len, chn);
		if (ret < 0)
			return ret;
		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	for (i = 0; i < chn->attrlist.num; i++) {
		ret = iio_snprintf_chan_attr_xml(&chn->attrlist.attrs[i], ptr, len);
		if (ret < 0)
			return ret;
		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	ret = iio_snprintf(ptr, len, "</channel>");
	if (ret < 0)
		return ret;

	return alen + ret;
}

const char * iio_channel_get_id(const struct iio_channel *chn)
{
	return chn->id;
}

const char * iio_channel_get_name(const struct iio_channel *chn)
{
	return chn->name;
}

bool iio_channel_is_output(const struct iio_channel *chn)
{
	return chn->is_output;
}

bool iio_channel_is_scan_element(const struct iio_channel *chn)
{
	return chn->is_scan_element;
}

enum iio_modifier iio_channel_get_modifier(const struct iio_channel *chn)
{
	return chn->modifier;
}

enum iio_chan_type iio_channel_get_type(const struct iio_channel *chn)
{
	return chn->type;
}

unsigned int iio_channel_get_attrs_count(const struct iio_channel *chn)
{
	return chn->attrlist.num;
}

const struct iio_attr *
iio_channel_get_attr(const struct iio_channel *chn, unsigned int index)
{
	return iio_attr_get(&chn->attrlist, index);
}

const struct iio_attr *
iio_channel_find_attr(const struct iio_channel *chn, const char *name)
{
	const struct iio_attr *attr;
	size_t len;

	attr = iio_attr_find(&chn->attrlist, name);
	if (attr)
		return attr;

	/* Support attribute names that start with the channel's label to avoid
	 * breaking compatibility with old kernels, which did not offer a
	 * 'label' attribute, and caused Libiio to sometimes misdetect the
	 * channel's extended name as being part of the attribute name. */
	if (chn->name) {
		len = strlen(chn->name);

		if (!strncmp(chn->name, name, len) && name[len] == '_') {
			name += len + 1;
			return iio_attr_find(&chn->attrlist, name);
		}
	}

	return NULL;
}

void iio_channel_set_data(struct iio_channel *chn, void *data)
{
	chn->userdata = data;
}

void * iio_channel_get_data(const struct iio_channel *chn)
{
	return chn->userdata;
}

long iio_channel_get_index(const struct iio_channel *chn)
{
	return chn->index;
}

const struct iio_data_format * iio_channel_get_data_format(
		const struct iio_channel *chn)
{
	return &chn->format;
}

bool iio_channel_is_enabled(const struct iio_channel *chn,
			    const struct iio_channels_mask *mask)
{
	return chn->index >= 0 && iio_channels_mask_test_bit(mask, chn->number);
}

void iio_channel_enable(const struct iio_channel *chn,
			struct iio_channels_mask *mask)
{
	if (chn->is_scan_element && chn->index >= 0)
		iio_channels_mask_set_bit(mask, chn->number);
}

void iio_channel_disable(const struct iio_channel *chn,
			 struct iio_channels_mask *mask)
{
	if (chn->is_scan_element && chn->index >= 0)
		iio_channels_mask_clear_bit(mask, chn->number);
}

void free_channel(struct iio_channel *chn)
{
	iio_free_attrs(&chn->attrlist);
	free(chn->name);
	free(chn->id);
	free(chn);
}

static void byte_swap(uint8_t *dst, const uint8_t *src, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		dst[i] = src[len - i - 1];
}

static void shift_bits(uint8_t *dst, size_t shift, size_t len, bool left)
{
	size_t i, shift_bytes = shift / 8;
	shift %= 8;

	if (is_little_endian() ^ left)
	{
		if (shift_bytes) {
			memmove(dst, dst + shift_bytes, len - shift_bytes);
			memset(dst + len - shift_bytes, 0, shift_bytes);
		}
		if (shift) {
			for (i = 0; i < len; i++) {
				if (is_little_endian()) {
					dst[i] >>= shift;
					if (i < len - 1)
						dst[i] |= dst[i + 1] << (8 - shift);
				} else {
					dst[i] <<= shift;
					if (i < len - 1)
						dst[i] |= dst[i + 1] >> (8 - shift);
				}
			}
		}
	} else {
		if (shift_bytes) {
			memmove(dst + shift_bytes, dst, len - shift_bytes);
			memset(dst, 0, shift_bytes);
		}
		if (shift) {
			for (i = len; i > 0; i--) {
				if (is_little_endian()) {
					dst[i - 1] <<= shift;
					if (i > 1)
						dst[i - 1] |= dst[i - 2] >> (8 - shift);
				} else {
					dst[i - 1] >>= shift;
					if (i > 1)
						dst[i - 1] |= dst[i - 2] << (8 - shift);
				}
			}
		}
	}
}

static void sign_extend(uint8_t *dst, size_t bits, size_t len)
{
	size_t upper_bytes = ((len * 8 - bits) / 8);
	uint8_t msb, msb_bit = 1 << ((bits - 1) % 8);

	if (is_little_endian()) {
		msb = dst[len - 1 - upper_bytes] & msb_bit;
		if (upper_bytes)
			memset(dst + len - upper_bytes, msb ? 0xff : 0x00, upper_bytes);
		if (msb)
			dst[len - 1 - upper_bytes] |= ~(msb_bit - 1);
		else
			dst[len - 1 - upper_bytes] &= (msb_bit - 1);
	} else {
		/* XXX: untested */
		msb = dst[upper_bytes] & msb_bit;
		if (upper_bytes)
			memset(dst, msb ? 0xff : 0x00, upper_bytes);
		if (msb)
			dst[upper_bytes] |= ~(msb_bit - 1);
	}
}

static void mask_upper_bits(uint8_t *dst, size_t bits, size_t len)
{
	size_t i;

	/* Clear upper bits */
	if (bits % 8)
		dst[bits / 8] &= (1 << (bits % 8)) - 1;

	/* Clear upper bytes */
	for (i = (bits + 7) / 8; i < len; i++)
		dst[i] = 0;
}

void iio_channel_convert(const struct iio_channel *chn,
		void *dst, const void *src)
{
	uintptr_t src_ptr = (uintptr_t) src, dst_ptr = (uintptr_t) dst;
	unsigned int len = chn->format.length / 8;
	ptrdiff_t end = len * chn->format.repeat;
	uintptr_t end_ptr = src_ptr + end;
	bool swap = is_little_endian() ^ !chn->format.is_be;

	for (src_ptr = (uintptr_t) src; src_ptr < end_ptr;
			src_ptr += len, dst_ptr += len) {
		if (len == 1 || !swap)
			memcpy((void *) dst_ptr, (const void *) src_ptr, len);
		else
			byte_swap((void *) dst_ptr, (const void *) src_ptr,
				len);

		if (chn->format.shift)
			shift_bits((void *) dst_ptr, chn->format.shift, len,
				false);

		if (!chn->format.is_fully_defined) {
			if (chn->format.is_signed)
				sign_extend((void *) dst_ptr,
					chn->format.bits, len);
			else
				mask_upper_bits((void *) dst_ptr,
					chn->format.bits, len);
		}
	}
}

void iio_channel_convert_inverse(const struct iio_channel *chn,
		void *dst, const void *src)
{
	uintptr_t src_ptr = (uintptr_t) src, dst_ptr = (uintptr_t) dst;
	unsigned int len = chn->format.length / 8;
	ptrdiff_t end = len * chn->format.repeat;
	uintptr_t end_ptr = dst_ptr + end;
	bool swap = is_little_endian() ^ !chn->format.is_be;
	uint8_t buf[1024];

	/* Somehow I doubt we will have samples of 8192 bits each. */
	if (len > sizeof(buf))
		return;

	for (dst_ptr = (uintptr_t) dst; dst_ptr < end_ptr;
			src_ptr += len, dst_ptr += len) {
		memcpy(buf, (const void *) src_ptr, len);
		mask_upper_bits(buf, chn->format.bits, len);

		if (chn->format.shift)
			shift_bits(buf, chn->format.shift, len, true);

		if (len == 1 || !swap)
			memcpy((void *) dst_ptr, buf, len);
		else
			byte_swap((void *) dst_ptr, buf, len);
	}
}

static void chn_memcpy(const struct iio_channel *chn,
		       void *dst, const void *src)
{
	unsigned int length = chn->format.length / 8 * chn->format.repeat;

	memcpy(dst, src, length);
}

size_t iio_channel_read(const struct iio_channel *chn,
			const struct iio_block *block,
			void *dst, size_t len, bool raw)
{
	const struct iio_buffer *buf = iio_block_get_buffer(block);
	const struct iio_device *dev = buf->dev;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t src_ptr, dst_ptr = (uintptr_t) dst, end = dst_ptr + len;
	uintptr_t block_end = (uintptr_t) iio_block_end(block);
	size_t step = iio_device_get_sample_size(dev, buf->mask);
	void (*cb)(const struct iio_channel *, void *, const void *);
	size_t block_len;
	const void *src;

	if (raw && step == length) {
		src = iio_block_start(block);
		block_len = (uintptr_t) iio_block_end(block) - (uintptr_t) src;

		if (block_len < len)
			len = block_len;

		memcpy(dst, src, len);

		return len;
	}

	if (raw)
		cb = chn_memcpy;
	else
		cb = iio_channel_convert;

	for (src_ptr = (uintptr_t) iio_block_first(block, chn);
	     src_ptr < block_end && dst_ptr + length <= end;
	     src_ptr += step, dst_ptr += length) {
		(*cb)(chn, (void *) dst_ptr, (const void *) src_ptr);
	}

	return dst_ptr - (uintptr_t) dst;

}

size_t iio_channel_write(const struct iio_channel *chn,
			 struct iio_block *block,
			 const void *src, size_t len, bool raw)
{
	const struct iio_buffer *buf = iio_block_get_buffer(block);
	const struct iio_device *dev = buf->dev;
	uintptr_t dst_ptr, src_ptr = (uintptr_t) src, end = src_ptr + len;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t block_end = (uintptr_t) iio_block_end(block);
	size_t step = iio_device_get_sample_size(dev, buf->mask);
	void (*cb)(const struct iio_channel *, void *, const void *);
	size_t block_len;
	void *dst;

	if (raw && step == length) {
		dst = iio_block_start(block);
		block_len = (uintptr_t) iio_block_end(block) - (uintptr_t) dst;

		if (block_len < len)
			len = block_len;

		memcpy(dst, src, len);

		return len;
	}

	if (raw)
		cb = chn_memcpy;
	else
		cb = iio_channel_convert_inverse;

	for (dst_ptr = (uintptr_t) iio_block_first(block, chn);
	     dst_ptr < block_end && src_ptr + length <= end;
	     dst_ptr += step, src_ptr += length) {
		(*cb)(chn, (void *) dst_ptr, (const void *) src_ptr);
	}

	return src_ptr - (uintptr_t) src;
}

const struct iio_device * iio_channel_get_device(const struct iio_channel *chn)
{
	return chn->dev;
}

struct iio_channel_pdata * iio_channel_get_pdata(const struct iio_channel *chn)
{
	return chn->pdata;
}

void iio_channel_set_pdata(struct iio_channel *chn, struct iio_channel_pdata *d)
{
	chn->pdata = d;
}
