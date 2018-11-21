/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#include "debug.h"
#include "iio-private.h"

#include <errno.h>
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
	[IIO_MOD_VOC] = "voc",
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

/*
 * Initializes all auto-detected fields of the channel struct. Must be called
 * after the channel has been otherwise fully initialized.
 */
void iio_channel_init_finalize(struct iio_channel *chn)
{
	unsigned int i;
	size_t len;
	char *mod;

	chn->type = IIO_CHAN_TYPE_UNKNOWN;
	chn->modifier = IIO_NO_MOD;

	for (i = 0; i < ARRAY_SIZE(iio_chan_type_name_spec); i++) {
		len = strlen(iio_chan_type_name_spec[i]);
		if (strncmp(iio_chan_type_name_spec[i], chn->id, len) != 0)
			continue;
		/* Type must be followed by either a '_' or a digit */
		if (chn->id[len] != '_' && (chn->id[len] < '0' || chn->id[len] > '9'))
			continue;

		chn->type = (enum iio_chan_type) i;
	}

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

static char *get_attr_xml(struct iio_channel_attr *attr, size_t *length)
{
	char *str;
	size_t len = strlen(attr->name) + sizeof("<attribute name=\"\" />");
	if (attr->filename)
		len += strlen(attr->filename) + sizeof("filename=\"\"");

	str = malloc(len);
	if (!str)
		return NULL;

	*length = len - 1; /* Skip the \0 */
	if (attr->filename)
		iio_snprintf(str, len, "<attribute name=\"%s\" filename=\"%s\" />",
				attr->name, attr->filename);
	else
		iio_snprintf(str, len, "<attribute name=\"%s\" />", attr->name);
	return str;
}

static char * get_scan_element(const struct iio_channel *chn, size_t *length)
{
	char buf[1024], repeat[12] = "", *str;
	char processed = (chn->format.is_fully_defined ? 'A' - 'a' : 0);

	if (chn->format.repeat > 1)
		iio_snprintf(repeat, sizeof(repeat), "X%u", chn->format.repeat);

	iio_snprintf(buf, sizeof(buf), "<scan-element index=\"%li\" "
			"format=\"%ce:%c%u/%u%s&gt;&gt;%u\" />",
			chn->index, chn->format.is_be ? 'b' : 'l',
			chn->format.is_signed ? 's' + processed : 'u' + processed,
			chn->format.bits, chn->format.length, repeat,
			chn->format.shift);

	if (chn->format.with_scale) {
		char *ptr = strrchr(buf, '\0');
		iio_snprintf(ptr - 2, buf + sizeof(buf) - ptr + 2,
				"scale=\"%f\" />", chn->format.scale);
	}

	str = iio_strdup(buf);
	if (str)
		*length = strlen(str);
	return str;
}

/* Returns a string containing the XML representation of this channel */
char * iio_channel_get_xml(const struct iio_channel *chn, size_t *length)
{
	size_t len = sizeof("<channel id=\"\" name=\"\" "
			"type=\"output\" ></channel>")
		+ strlen(chn->id) + (chn->name ? strlen(chn->name) : 0);
	char *ptr, *str, **attrs, *scan_element = NULL;
	size_t *attrs_len, scan_element_len = 0;
	unsigned int i;

	if (chn->is_scan_element) {
		scan_element = get_scan_element(chn, &scan_element_len);
		if (!scan_element)
			return NULL;
		else
			len += scan_element_len;
	}

	attrs_len = malloc(chn->nb_attrs * sizeof(*attrs_len));
	if (!attrs_len)
		goto err_free_scan_element;

	attrs = malloc(chn->nb_attrs * sizeof(*attrs));
	if (!attrs)
		goto err_free_attrs_len;

	for (i = 0; i < chn->nb_attrs; i++) {
		char *xml = get_attr_xml(&chn->attrs[i], &attrs_len[i]);
		if (!xml)
			goto err_free_attrs;
		attrs[i] = xml;
		len += attrs_len[i];
	}

	str = malloc(len);
	if (!str)
		goto err_free_attrs;

	iio_snprintf(str, len, "<channel id=\"%s\"", chn->id);
	ptr = strrchr(str, '\0');

	if (chn->name) {
		sprintf(ptr, " name=\"%s\"", chn->name);
		ptr = strrchr(ptr, '\0');
	}

	sprintf(ptr, " type=\"%s\" >", chn->is_output ? "output" : "input");
	ptr = strrchr(ptr, '\0');

	if (chn->is_scan_element) {
		strcpy(ptr, scan_element);
		ptr += scan_element_len;
	}

	for (i = 0; i < chn->nb_attrs; i++) {
		strcpy(ptr, attrs[i]);
		ptr += attrs_len[i];
		free(attrs[i]);
	}

	free(scan_element);
	free(attrs);
	free(attrs_len);

	strcpy(ptr, "</channel>");
	*length = ptr - str + sizeof("</channel>") - 1;
	return str;

err_free_attrs:
	while (i--)
		free(attrs[i]);
	free(attrs);
err_free_attrs_len:
	free(attrs_len);
err_free_scan_element:
	if (chn->is_scan_element)
		free(scan_element);
	return NULL;
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
	return chn->nb_attrs;
}

const char * iio_channel_get_attr(const struct iio_channel *chn,
		unsigned int index)
{
	if (index >= chn->nb_attrs)
		return NULL;
	else
		return chn->attrs[index].name;
}

const char * iio_channel_find_attr(const struct iio_channel *chn,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++) {
		const char *attr = chn->attrs[i].name;
		if (!strcmp(attr, name))
			return attr;
	}
	return NULL;
}

ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	if (chn->dev->ctx->ops->read_channel_attr)
		return chn->dev->ctx->ops->read_channel_attr(chn,
				attr, dst, len);
	else
		return -ENOSYS;
}

ssize_t iio_channel_attr_write_raw(const struct iio_channel *chn,
		const char *attr, const void *src, size_t len)
{
	if (chn->dev->ctx->ops->write_channel_attr)
		return chn->dev->ctx->ops->write_channel_attr(chn,
				attr, src, len);
	else
		return -ENOSYS;
}

ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	return iio_channel_attr_write_raw(chn, attr, src, strlen(src) + 1);
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

bool iio_channel_is_enabled(const struct iio_channel *chn)
{
	return chn->index >= 0 && chn->dev->mask &&
		TEST_BIT(chn->dev->mask, chn->number);
}

void iio_channel_enable(struct iio_channel *chn)
{
	if (chn->is_scan_element && chn->index >= 0 && chn->dev->mask)
		SET_BIT(chn->dev->mask, chn->number);
}

void iio_channel_disable(struct iio_channel *chn)
{
	if (chn->index >= 0 && chn->dev->mask)
		CLEAR_BIT(chn->dev->mask, chn->number);
}

void free_channel(struct iio_channel *chn)
{
	size_t i;
	for (i = 0; i < chn->nb_attrs; i++) {
		free(chn->attrs[i].name);
		free(chn->attrs[i].filename);
	}
	if (chn->nb_attrs)
		free(chn->attrs);
	if (chn->name)
		free(chn->name);
	if (chn->id)
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

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	if (!left)
#else
	if (left)
#endif
	{
		if (shift_bytes) {
			memmove(dst, dst + shift_bytes, len - shift_bytes);
			memset(dst + len - shift_bytes, 0, shift_bytes);
		}
		if (shift) {
			for (i = 0; i < len; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
				dst[i] >>= shift;
				if (i < len - 1)
					dst[i] |= dst[i + 1] << (8 - shift);
#else
				dst[i] <<= shift;
				if (i < len - 1)
					dst[i] |= dst[i + 1] >> (8 - shift);
#endif
			}
		}
	} else {
		if (shift_bytes) {
			memmove(dst + shift_bytes, dst, len - shift_bytes);
			memset(dst, 0, shift_bytes);
		}
		if (shift) {
			for (i = len; i > 0; i--) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
				dst[i - 1] <<= shift;
				if (i > 1)
					dst[i - 1] |= dst[i - 2] >> (8 - shift);
#else
				dst[i - 1] >>= shift;
				if (i > 1)
					dst[i - 1] |= dst[i - 2] << (8 - shift);
#endif
			}
		}
	}
}

static void sign_extend(uint8_t *dst, size_t bits, size_t len)
{
	size_t upper_bytes = ((len * 8 - bits) / 8);
	uint8_t msb, msb_bit = 1 << ((bits - 1) % 8);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	msb = dst[len - 1 - upper_bytes] & msb_bit;
	if (upper_bytes)
		memset(dst + len - upper_bytes, msb ? 0xff : 0x00, upper_bytes);
	if (msb)
		dst[len - 1 - upper_bytes] |= ~(msb_bit - 1);
	else
		dst[len - 1 - upper_bytes] &= (msb_bit - 1);
#else
	/* XXX: untested */
	msb = dst[upper_bytes] & msb_bit;
	if (upper_bytes)
		memset(dst, msb ? 0xff : 0x00, upper_bytes);
	if (msb)
		dst[upper_bytes] |= ~(msb_bit - 1);
#endif
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
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	bool swap = chn->format.is_be;
#else
	bool swap = !chn->format.is_be;
#endif

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
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	bool swap = chn->format.is_be;
#else
	bool swap = !chn->format.is_be;
#endif
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

size_t iio_channel_read_raw(const struct iio_channel *chn,
		struct iio_buffer *buf, void *dst, size_t len)
{
	uintptr_t src_ptr, dst_ptr = (uintptr_t) dst, end = dst_ptr + len;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t buf_end = (uintptr_t) iio_buffer_end(buf);
	ptrdiff_t buf_step = iio_buffer_step(buf);

	for (src_ptr = (uintptr_t) iio_buffer_first(buf, chn);
			src_ptr < buf_end && dst_ptr + length <= end;
			src_ptr += buf_step, dst_ptr += length)
		memcpy((void *) dst_ptr, (const void *) src_ptr, length);
	return dst_ptr - (uintptr_t) dst;
}

size_t iio_channel_read(const struct iio_channel *chn,
		struct iio_buffer *buf, void *dst, size_t len)
{
	uintptr_t src_ptr, dst_ptr = (uintptr_t) dst, end = dst_ptr + len;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t buf_end = (uintptr_t) iio_buffer_end(buf);
	ptrdiff_t buf_step = iio_buffer_step(buf);

	for (src_ptr = (uintptr_t) iio_buffer_first(buf, chn);
			src_ptr < buf_end && dst_ptr + length <= end;
			src_ptr += buf_step, dst_ptr += length)
		iio_channel_convert(chn,
				(void *) dst_ptr, (const void *) src_ptr);
	return dst_ptr - (uintptr_t) dst;
}

size_t iio_channel_write_raw(const struct iio_channel *chn,
		struct iio_buffer *buf, const void *src, size_t len)
{
	uintptr_t dst_ptr, src_ptr = (uintptr_t) src, end = src_ptr + len;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t buf_end = (uintptr_t) iio_buffer_end(buf);
	ptrdiff_t buf_step = iio_buffer_step(buf);

	for (dst_ptr = (uintptr_t) iio_buffer_first(buf, chn);
			dst_ptr < buf_end && src_ptr + length <= end;
			dst_ptr += buf_step, src_ptr += length)
		memcpy((void *) dst_ptr, (const void *) src_ptr, length);
	return src_ptr - (uintptr_t) src;
}

size_t iio_channel_write(const struct iio_channel *chn,
		struct iio_buffer *buf, const void *src, size_t len)
{
	uintptr_t dst_ptr, src_ptr = (uintptr_t) src, end = src_ptr + len;
	unsigned int length = chn->format.length / 8 * chn->format.repeat;
	uintptr_t buf_end = (uintptr_t) iio_buffer_end(buf);
	ptrdiff_t buf_step = iio_buffer_step(buf);

	for (dst_ptr = (uintptr_t) iio_buffer_first(buf, chn);
			dst_ptr < buf_end && src_ptr + length <= end;
			dst_ptr += buf_step, src_ptr += length)
		iio_channel_convert_inverse(chn,
				(void *) dst_ptr, (const void *) src_ptr);
	return src_ptr - (uintptr_t) src;
}

int iio_channel_attr_read_longlong(const struct iio_channel *chn,
		const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	value = strtoll(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_channel_attr_read_bool(const struct iio_channel *chn,
		const char *attr, bool *val)
{
	long long value;
	int ret = iio_channel_attr_read_longlong(chn, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_channel_attr_read_double(const struct iio_channel *chn,
		const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_channel_attr_write_longlong(const struct iio_channel *chn,
		const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];
	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_channel_attr_write(chn, attr, buf);
	return ret < 0 ? ret : 0;
}

int iio_channel_attr_write_double(const struct iio_channel *chn,
		const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_channel_attr_write(chn, attr, buf);
	return ret < 0 ? ret : 0;
}

int iio_channel_attr_write_bool(const struct iio_channel *chn,
		const char *attr, bool val)
{
	ssize_t ret;
	if (val)
		ret = iio_channel_attr_write_raw(chn, attr, "1", 2);
	else
		ret = iio_channel_attr_write_raw(chn, attr, "0", 2);
	return ret < 0 ? ret : 0;
}

const char * iio_channel_attr_get_filename(
		const struct iio_channel *chn, const char *attr)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++) {
		if (!strcmp(chn->attrs[i].name, attr))
			return chn->attrs[i].filename;
	}
	return NULL;
}

int iio_channel_attr_read_all(struct iio_channel *chn,
		int (*cb)(struct iio_channel *chn,
			const char *attr, const char *val, size_t len, void *d),
		void *data)
{
	int ret, buf_size;
	char *buf, *ptr;
	unsigned int i;

	/* We need a big buffer here; 1 MiB should be enough */
	buf = malloc(0x100000);
	if (!buf)
		return -ENOMEM;

	ret = (int) iio_channel_attr_read(chn, NULL, buf, 0x100000);
	if (ret < 0)
		goto err_free_buf;

	ptr = buf;
	buf_size = ret;

	for (i = 0; i < iio_channel_get_attrs_count(chn); i++) {
		const char *attr = iio_channel_get_attr(chn, i);
		int32_t len;

		if (buf_size < 4) {
			ret = -EPROTO;
			break;
		}

		len = (int32_t) iio_be32toh(*(uint32_t *) ptr);
		ptr += 4;
		buf_size -= 4;

		if (len > 0 && buf_size < len) {
			ret = -EPROTO;
			break;
		}

		if (len > 0) {
			ret = cb(chn, attr, ptr, (size_t) len, data);
			if (ret < 0)
				goto err_free_buf;

			if (len & 0x3)
				len = ((len >> 2) + 1) << 2;
			ptr += len;
			if (len >= buf_size)
				buf_size = 0;
			else
				buf_size -= len;
		}
	}

err_free_buf:
	free(buf);
	return ret < 0 ? ret : 0;
}

int iio_channel_attr_write_all(struct iio_channel *chn,
		ssize_t (*cb)(struct iio_channel *chn,
			const char *attr, void *buf, size_t len, void *d),
		void *data)
{
	char *buf, *ptr;
	unsigned int i;
	size_t len = 0x100000;
	int ret;

	/* We need a big buffer here; 1 MiB should be enough */
	buf = malloc(len);
	if (!buf)
		return -ENOMEM;

	ptr = buf;

	for (i = 0; i < iio_channel_get_attrs_count(chn); i++) {
		const char *attr = iio_channel_get_attr(chn, i);

		ret = (int) cb(chn, attr, ptr + 4, len - 4, data);
		if (ret < 0)
			goto err_free_buf;

		*(int32_t *) ptr = (int32_t) iio_htobe32((uint32_t) ret);
		ptr += 4;
		len -= 4;

		if (ret > 0) {
			if (ret & 0x3)
				ret = ((ret >> 2) + 1) << 2;
			ptr += ret;
			len -= ret;
		}
	}

	ret = (int) iio_channel_attr_write_raw(chn, NULL, buf, ptr - buf);

err_free_buf:
	free(buf);
	return ret < 0 ? ret : 0;
}

const struct iio_device * iio_channel_get_device(const struct iio_channel *chn)
{
	return chn->dev;
}
