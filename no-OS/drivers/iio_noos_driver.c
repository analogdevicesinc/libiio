/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves an unmodified no-OS IIO driver (iio_<part>.c) through libiio: turns
 * what iio_noos_driver_desc.c reads out of the driver's descriptor into
 * libiio channels and attributes, and implements the noos_iio_device_info
 * callbacks backend.c calls. It must not include no-OS's iio_types.h (see
 * iio_noos_driver_desc.h).
 */

#include <errno.h>
#include <iio-private.h>
#include <iio/iio-backend.h>
#include <no_os_print_log.h>
#include <stdint.h>
#include <string.h>

#include "iio_device.h"
#include "iio_noos_driver_desc.h"

/* Matches the responder's attribute cap on this build. */
#define DRV_ATTR_BUF_SIZE 256

/*
 * libiio sorts channels, so a channel's place in iio_dev differs from its
 * place in the descriptor; the descriptor index + 1 is kept in its pdata.
 */
static int drv_chan_index(const struct iio_channel *chn)
{
	return (int)(uintptr_t)iio_channel_get_pdata(chn) - 1;
}

/* Add the device attributes of one kind (device, shared, debug). */
static int drv_add_dev_attrs(const struct noos_iio_driver *a, struct iio_device *iio_dev,
		enum noos_desc_attr_kind kind, enum iio_attr_type type)
{
	const char *name;
	unsigned int k;
	int ret;

	for (k = 0; (name = noos_desc_attr_name(a, kind, 0, k)); k++) {
		/* A device attribute wins over a shared one of the same name. */
		if (type == IIO_ATTR_TYPE_DEVICE && iio_device_find_attr(iio_dev, name))
			continue;

		ret = iio_device_add_attr(iio_dev, name, type);
		if (ret)
			return ret;
	}

	return 0;
}

/* add_channels of a served no-OS driver: its descriptor's channels. */
static int drv_add_channels(void *dev, struct iio_device *iio_dev)
{
	const struct noos_iio_driver *a = dev;
	struct iio_data_format fmt;
	struct noos_desc_chan c;
	const char *name;
	unsigned int i, k;
	int ret;

	for (i = 0; i < noos_desc_num_channels(a); i++) {
		ret = noos_desc_get_chan(a, i, &c);
		if (ret)
			return ret;

		fmt = (struct iio_data_format){
			.length = c.length,
			.bits = c.bits,
			.shift = c.shift,
			.is_signed = c.is_signed,
			.is_be = c.is_be,
		};

		struct iio_channel *chn;

		chn = iio_device_add_channel(iio_dev, c.scan_index, c.id, NULL, NULL, c.output,
				c.scan_element, &fmt);
		if (!chn)
			return -ENOMEM;

		iio_channel_set_pdata(chn, (struct iio_channel_pdata *)(uintptr_t)(i + 1));

		for (k = 0; (name = noos_desc_attr_name(a, NOOS_DESC_ATTR_CHANNEL, i, k)); k++) {
			ret = iio_channel_add_attr(chn, name, IIO_ATTR_TYPE_CHANNEL, NULL);
			if (ret)
				return ret;
		}
	}

	ret = drv_add_dev_attrs(a, iio_dev, NOOS_DESC_ATTR_DEVICE, IIO_ATTR_TYPE_DEVICE);
	if (!ret)
		ret = drv_add_dev_attrs(a, iio_dev, NOOS_DESC_ATTR_SHARED, IIO_ATTR_TYPE_DEVICE);
	if (!ret)
		ret = drv_add_dev_attrs(a, iio_dev, NOOS_DESC_ATTR_DEBUG, IIO_ATTR_TYPE_DEBUG);

	return ret;
}

/* Run show/store for a libiio attribute, wherever no-OS keeps it. */
static int drv_attr_rw(const struct noos_iio_driver *a, const struct iio_attr *attr, char *buf,
		size_t len, bool write)
{
	const char *name = iio_attr_get_name(attr);
	int ret, ch;

	switch (attr->type) {
	case IIO_ATTR_TYPE_DEVICE:
		ret = noos_desc_attr_rw(a, NOOS_DESC_ATTR_DEVICE, 0, name, buf, len, write);
		if (ret == -ENOENT)
			ret = noos_desc_attr_rw(a, NOOS_DESC_ATTR_SHARED, 0, name, buf, len, write);
		return ret;
	case IIO_ATTR_TYPE_DEBUG:
		return noos_desc_attr_rw(a, NOOS_DESC_ATTR_DEBUG, 0, name, buf, len, write);
	case IIO_ATTR_TYPE_CHANNEL:
		ch = attr->iio.chn ? drv_chan_index(attr->iio.chn) : -1;
		if (ch < 0)
			return -EINVAL;
		return noos_desc_attr_rw(
				a, NOOS_DESC_ATTR_CHANNEL, (unsigned int)ch, name, buf, len, write);
	default:
		return -EINVAL;
	}
}

/*
 * After a show into @dst: show returns the text length, the responder wants
 * it with the NUL.
 */
static int attr_read_done(char *dst, size_t len, int ret)
{
	if (ret < 0)
		return ret;
	if ((size_t)ret >= len)
		return -EINVAL;

	dst[ret] = '\0';
	return (int)strlen(dst) + 1;
}

/* Before a store: it parses a writable, NUL-terminated string. */
static int attr_write_buf(char *buf, size_t size, const char *src, size_t *len)
{
	*len = strnlen(src, *len);
	if (*len >= size)
		return -EINVAL;

	memcpy(buf, src, *len);
	buf[*len] = '\0';
	return 0;
}

/* read_attr of a served no-OS driver: its show function. */
static int drv_read_attr(void *dev, const struct iio_device *iio_dev, const struct iio_attr *attr,
		char *dst, size_t len)
{
	if (!len)
		return -EINVAL;

	return attr_read_done(dst, len, drv_attr_rw(dev, attr, dst, len, false));
}

/* write_attr of a served no-OS driver: its store function. */
static int drv_write_attr(void *dev, const struct iio_device *iio_dev, const struct iio_attr *attr,
		const char *src, size_t len)
{
	char buf[DRV_ATTR_BUF_SIZE];
	int ret;

	ret = attr_write_buf(buf, sizeof(buf), src, &len);
	if (!ret)
		ret = drv_attr_rw(dev, attr, buf, len, true);

	return ret < 0 ? ret : (int)len;
}

/* The client's enabled channels as no-OS's mask: bit i = channel i. */
static int drv_mask(const struct iio_device *iio_dev, const struct iio_channels_mask *mask,
		uint32_t *out)
{
	unsigned int i, nb = iio_device_get_channels_count(iio_dev);
	int idx;

	*out = 0;
	for (i = 0; i < nb; i++) {
		const struct iio_channel *chn = iio_device_get_channel(iio_dev, i);

		if (!iio_channel_is_scan_element(chn) || !iio_channel_is_enabled(chn, mask))
			continue;

		idx = drv_chan_index(chn);
		if (idx < 0 || idx >= 32)
			return -EINVAL;
		*out |= 1u << idx;
	}

	return *out ? 0 : -EINVAL;
}

/* pre_enable on the first transfer, when the mask is known. */
static int drv_transfer(struct noos_iio_driver *a, const struct iio_device *iio_dev,
		const struct iio_channels_mask *mask, void *data, size_t bytes, bool output)
{
	ssize_t lib_size;
	uint32_t m;
	int ret;

	ret = drv_mask(iio_dev, mask, &m);
	if (ret)
		return ret;

	/* The host lays samples out as libiio does, the driver as no-OS does. */
	lib_size = iio_device_get_sample_size(iio_dev, mask);
	if (lib_size <= 0 || (size_t)lib_size != noos_desc_bytes_per_scan(a, m)) {
		pr_err("%s: libiio and no-OS disagree on the scan size\n", a->name);
		return -EINVAL;
	}

	if (!a->enabled || a->mask != m) {
		ret = noos_desc_enable(a, m, true);
		if (ret)
			return ret;
		a->enabled = true;
		a->mask = m;
	}

	return noos_desc_transfer(a, m, data, bytes, output);
}

/* read_samples of a served no-OS driver (input buffer). */
static int drv_read_samples(void *dev, const struct iio_device *iio_dev,
		const struct iio_channels_mask *mask, void *data, size_t bytes)
{
	return drv_transfer(dev, iio_dev, mask, data, bytes, false);
}

/* write_samples of a served no-OS driver (output buffer). */
static int drv_write_samples(void *dev, const struct iio_device *iio_dev,
		const struct iio_channels_mask *mask, const void *data, size_t bytes)
{
	/* The driver only pops scans out of the block, never writes it. */
	return drv_transfer(dev, iio_dev, mask, (void *)data, bytes, true);
}

/* enable_buffer of a served no-OS driver: post_disable. */
static int drv_enable_buffer(void *dev, bool enable)
{
	struct noos_iio_driver *a = dev;

	if (enable || !a->enabled)
		return 0;

	a->enabled = false;
	return noos_desc_enable(a, a->mask, false);
}

/* reg_read of a served no-OS driver: its debug_reg_read. */
static int drv_reg_read(void *dev, uint32_t reg, uint32_t *val)
{
	return noos_desc_reg_read(dev, reg, val);
}

/* reg_write of a served no-OS driver: its debug_reg_write. */
static int drv_reg_write(void *dev, uint32_t reg, uint32_t val)
{
	return noos_desc_reg_write(dev, reg, val);
}

/* Describe a served no-OS driver; see iio_noos_driver.h. */
int noos_iio_driver_get_device_info(struct noos_iio_driver *drv, struct noos_iio_device_info *info)
{
	struct noos_desc_chan c;
	bool output = false;
	unsigned int i;

	if (!drv || !drv->descriptor || !info)
		return -EINVAL;

	/* The buffer's direction follows its scan elements. */
	for (i = 0; i < noos_desc_num_channels(drv); i++)
		if (!noos_desc_get_chan(drv, i, &c) && c.scan_element)
			output = c.output;

	*info = (struct noos_iio_device_info){
		.name = drv->name,
		.dev = drv,
		.direction = output,
		.add_channels = drv_add_channels,
		.read_attr = drv_read_attr,
		.write_attr = drv_write_attr,
		.read_samples = output ? NULL : drv_read_samples,
		.write_samples = output ? drv_write_samples : NULL,
		.enable_buffer = drv_enable_buffer,
		.reg_read = noos_desc_has_reg(drv) ? drv_reg_read : NULL,
		.reg_write = noos_desc_has_reg(drv) ? drv_reg_write : NULL,
	};

	return 0;
}
