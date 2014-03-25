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

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static char *get_attr_xml(const char *attr, size_t *length)
{
	size_t len = sizeof("<attribute name=\"\" />") + strlen(attr);
	char *str = malloc(len);
	if (!str) {
		ERROR("Unable to allocate memory\n");
		return NULL;
	}

	*length = len - 1; /* Skip the \0 */
	sprintf(str, "<attribute name=\"%s\" />", attr);
	return str;
}

/* Returns a string containing the XML representation of this channel */
char * iio_channel_get_xml(const struct iio_channel *chn, size_t *length)
{
	size_t len = sizeof("<channel id=\"\" name=\"\" "
			"type=\"output\" ></channel>");
	char *ptr, *str, *attrs[chn->nb_attrs];
	size_t attrs_len[chn->nb_attrs];
	unsigned int i;

	for (i = 0; i < chn->nb_attrs; i++) {
		char *xml = get_attr_xml(chn->attrs[i], &attrs_len[i]);
		if (!xml)
			goto err_free_attrs;
		attrs[i] = xml;
		len += attrs_len[i];
	}

	len += strlen(chn->id);
	if (chn->name)
		len += strlen(chn->name);

	str = malloc(len);
	if (!str)
		goto err_free_attrs;

	sprintf(str, "<channel id=\"%s\"", chn->id);
	ptr = strrchr(str, '\0');

	if (chn->name) {
		sprintf(ptr, " name=\"%s\"", chn->name);
		ptr = strrchr(ptr, '\0');
	}

	sprintf(ptr, " type=\"%s\" >", chn->is_output ? "output" : "input");
	ptr = strrchr(ptr, '\0');

	for (i = 0; i < chn->nb_attrs; i++) {
		strcpy(ptr, attrs[i]);
		ptr += attrs_len[i];
		free(attrs[i]);
	}

	strcpy(ptr, "</channel>");
	*length = ptr - str + sizeof("</channel>") - 1;
	return str;

err_free_attrs:
	while (i--)
		free(attrs[i]);
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
		return chn->attrs[index];
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

ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	if (chn->dev->ctx->ops->write_channel_attr)
		return chn->dev->ctx->ops->write_channel_attr(chn, attr, src);
	else
		return -ENOSYS;
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
	return chn->enabled;
}

void iio_channel_enable(struct iio_channel *chn)
{
	chn->enabled = true;
}

void iio_channel_disable(struct iio_channel *chn)
{
	chn->enabled = false;
}

void free_channel(struct iio_channel *chn)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++)
		free((char *) chn->attrs[i]);
	if (chn->nb_attrs)
		free(chn->attrs);
	if (chn->name)
		free((char *) chn->name);
	if (chn->id)
		free((char *) chn->id);
	free(chn);
}

static void byte_swap(uint8_t *dst, const uint8_t *src, size_t len)
{
	unsigned int i;
	for (i = 0; i < len; i++)
		dst[i] = src[len - i - 1];
}

static void shift_bits(uint8_t *dst, size_t shift, size_t len)
{
	unsigned int i;
	size_t shift_bytes = shift / 8;
	shift %= 8;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	if (shift_bytes) {
		memmove(dst, dst + shift_bytes, len - shift_bytes);
		memset(dst + len - shift_bytes, 0, shift_bytes);
	}
	if (shift) {
		for (i = 0; i < len; i++) {
			dst[i] >>= shift;
			if (i < len - 1)
				dst[i] |= dst[i + 1] << (8 - shift);
		}
	}
#else
	/* XXX: untested */
	if (shift_bytes) {
		memmove(dst + shift_bytes, dst, len - shift_bytes);
		memset(dst, 0, shift_bytes);
	}
	if (shift) {
		for (i = len; i > 0; i--) {
			dst[i - 1] >>= shift;
			if (i > 1)
				dst[i - 1] |= dst[i - 2] << (8 - shift);
		}
	}
#endif
}

static void sign_extend(uint8_t *dst, size_t bits, size_t len)
{
	size_t upper_bytes = ((len * 8 - bits) / 8);
	uint8_t msb, msb_bit = 1 << ((bits - 1) % 8);

#if __BYTE_ORDER == __LITTLE_ENDIAN
	msb = dst[len - 1 - upper_bytes] & msb_bit;
	if (upper_bytes)
		memset(dst + len - upper_bytes, msb ? 0xff : 0x00, upper_bytes);
	if (msb)
		dst[len - 1 - upper_bytes] |= ~(msb_bit - 1);
#else
	/* XXX: untested */
	msb = dst[upper_bytes] & msb_bit;
	if (upper_bytes)
		memset(dst, msb ? 0xff : 0x00, upper_bytes);
	if (msb)
		dst[upper_bytes] |= ~(msb_bit - 1);
#endif
}

void iio_channel_convert(const struct iio_channel *chn,
		void *dst, const void *src)
{
	unsigned int len = chn->format.length / 8;
#if __BYTE_ORDER == __LITTLE_ENDIAN
	bool swap = chn->format.is_be;
#else
	bool swap = !chn->format.is_be;
#endif

	if (len == 1 || !swap)
		memcpy(dst, src, len);
	else
		byte_swap(dst, src, len);

	if (chn->format.shift)
		shift_bits(dst, chn->format.shift, len);
	if (chn->format.is_signed)
		sign_extend(dst, chn->format.bits, len);
}
