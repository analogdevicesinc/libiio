/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Reads a no-OS driver's descriptor for iio_noos_driver.c: everything that
 * touches a no-OS struct iio_device. It must not include libiio's iio.h
 * (see iio_noos_driver_desc.h).
 *
 * Nothing from no-OS's own IIO server (iio/iio.c, iiod.c) or from iio_app
 * is built; only its headers' types are used. What drivers call into that
 * server is provided here instead, under the same names: the four
 * iio_buffer_* functions, which fill the libiio block given to
 * noos_desc_transfer().
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "iio_noos_driver_desc.h"
#include "iio_types.h"
#include "no_os_circular_buffer.h"
#include "no_os_util.h"

/* Same names as no-OS iio.c prints, so the host sees the same channel ids. */
static const char *const chan_type_names[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_ALTVOLTAGE] = "altvoltage",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_TEMP] = "temp",
	[IIO_CAPACITANCE] = "capacitance",
	[IIO_ACCEL] = "accel",
	[IIO_RESISTANCE] = "resistance",
	[IIO_MAGN] = "magn",
	[IIO_INCLI] = "incli",
	[IIO_VELOCITY] = "velocity",
	[IIO_ANGL] = "angl",
	[IIO_ROT] = "rot",
	[IIO_COUNT] = "count",
	[IIO_DELTA_ANGL] = "deltaangl",
	[IIO_DELTA_VELOCITY] = "deltavelocity",
	[IIO_WEIGHT] = "weight",
	[IIO_POWER] = "power",
};

static const char *const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_TEMP_AMBIENT] = "ambient",
	[IIO_MOD_PITCH] = "pitch",
	[IIO_MOD_YAW] = "yaw",
	[IIO_MOD_ROLL] = "roll",
};

/* The no-OS struct iio_device a served driver was declared with. */
static struct iio_device *desc_of(const struct noos_iio_driver *a)
{
	return (struct iio_device *)a->descriptor;
}

/* The channel type's name, as no-OS iio.c prints it. */
static const char *chan_type_name(enum iio_chan_type type)
{
	if ((unsigned int)type >= NO_OS_ARRAY_SIZE(chan_type_names) || !chan_type_names[type])
		return "unknown";

	return chan_type_names[type];
}

/* The modifier's name, as no-OS iio.c prints it. */
static const char *modifier_name(int mod)
{
	if (mod < 0 || (unsigned int)mod >= NO_OS_ARRAY_SIZE(modifier_names) ||
			!modifier_names[mod])
		return "unknown";

	return modifier_names[mod];
}

/* Channel id as no-OS iio.c prints it: voltage0, accel_x, voltage0-voltage1. */
static int chan_id(const struct iio_channel *ch, char *buf, size_t len)
{
	const char *type = chan_type_name(ch->ch_type);
	int ret;

	if (ch->modified)
		ret = snprintf(buf, len, "%s_%s", type, modifier_name(ch->channel2));
	else if (!ch->indexed)
		ret = snprintf(buf, len, "%s", type);
	else if (ch->diferential)
		ret = snprintf(buf, len, "%s%d-%s%d", type, ch->channel, type, ch->channel2);
	else
		ret = snprintf(buf, len, "%s%d", type, ch->channel);

	return (ret < 0 || (size_t)ret >= len) ? -EINVAL : 0;
}

/* Number of channels in the descriptor. */
unsigned int noos_desc_num_channels(const struct noos_iio_driver *a)
{
	struct iio_device *desc = desc_of(a);

	return desc->channels ? desc->num_ch : 0;
}

/* Channel @ch of the descriptor, in plain types. */
int noos_desc_get_chan(const struct noos_iio_driver *a, unsigned int ch, struct noos_desc_chan *out)
{
	const struct iio_channel *c;

	if (ch >= noos_desc_num_channels(a))
		return -EINVAL;

	c = &desc_of(a)->channels[ch];
	*out = (struct noos_desc_chan){
		.output = c->ch_out,
		.scan_element = c->scan_type != NULL,
		.scan_index = c->scan_index,
	};

	if (c->scan_type) {
		out->bits = c->scan_type->realbits;
		out->length = c->scan_type->storagebits;
		out->shift = c->scan_type->shift;
		out->is_signed = c->scan_type->sign == 's';
		out->is_be = c->scan_type->is_big_endian;
	}

	return chan_id(c, out->id, sizeof(out->id));
}

/* As no-OS iio.c: shared-by-all channel attributes belong to the device. */
static bool is_shared(const struct iio_attribute *attr)
{
	return attr->shared == IIO_SHARED_BY_ALL;
}

/* First shared-by-all channel attribute called @name, NULL if none. */
static struct iio_attribute *find_shared(const struct noos_iio_driver *a, const char *name)
{
	struct iio_device *desc = desc_of(a);
	struct iio_attribute *attrs;
	unsigned int i, k;

	for (i = 0; i < noos_desc_num_channels(a); i++) {
		attrs = desc->channels[i].attributes;
		for (k = 0; attrs && attrs[k].name; k++)
			if (is_shared(&attrs[k]) && !strcmp(attrs[k].name, name))
				return &attrs[k];
	}

	return NULL;
}

/* k-th distinct shared-by-all attribute name, NULL past the end. */
static const char *shared_name(const struct noos_iio_driver *a, unsigned int k)
{
	struct iio_device *desc = desc_of(a);
	struct iio_attribute *attrs;
	unsigned int i, j;

	for (i = 0; i < noos_desc_num_channels(a); i++) {
		attrs = desc->channels[i].attributes;
		for (j = 0; attrs && attrs[j].name; j++) {
			if (!is_shared(&attrs[j]) || find_shared(a, attrs[j].name) != &attrs[j])
				continue;
			if (!k--)
				return attrs[j].name;
		}
	}

	return NULL;
}

/* k-th entry of a NULL-name-terminated array; @chan_only skips shared ones. */
static struct iio_attribute *nth_attr(struct iio_attribute *attrs, unsigned int k, bool chan_only)
{
	unsigned int i;

	for (i = 0; attrs && attrs[i].name; i++) {
		if (chan_only && is_shared(&attrs[i]))
			continue;
		if (!k--)
			return &attrs[i];
	}

	return NULL;
}

/* The descriptor's attribute array of one kind (device, debug, channel). */
static struct iio_attribute *attr_array(
		const struct noos_iio_driver *a, enum noos_desc_attr_kind kind, unsigned int ch)
{
	struct iio_device *desc = desc_of(a);

	switch (kind) {
	case NOOS_DESC_ATTR_DEVICE:
		return desc->attributes;
	case NOOS_DESC_ATTR_DEBUG:
		return desc->debug_attributes;
	case NOOS_DESC_ATTR_CHANNEL:
		if (ch >= noos_desc_num_channels(a))
			return NULL;
		return desc->channels[ch].attributes;
	default:
		return NULL;
	}
}

/* Name of the k-th attribute of one kind; see iio_noos_driver_desc.h. */
const char *noos_desc_attr_name(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, unsigned int k)
{
	struct iio_attribute *attr;

	if (kind == NOOS_DESC_ATTR_SHARED)
		return shared_name(a, k);

	attr = nth_attr(attr_array(a, kind, ch), k, kind == NOOS_DESC_ATTR_CHANNEL);

	return attr ? attr->name : NULL;
}

/* Attribute called @name in an array, NULL if none. */
static struct iio_attribute *find_attr(struct iio_attribute *attrs, const char *name)
{
	unsigned int i;

	for (i = 0; attrs && attrs[i].name; i++)
		if (!strcmp(attrs[i].name, name))
			return &attrs[i];

	return NULL;
}

/* Run an attribute's show or store; see iio_noos_driver_desc.h. */
int noos_desc_attr_rw(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, const char *name, char *buf, size_t len, bool write)
{
	struct iio_ch_info info, *pinfo = NULL;
	struct iio_attribute *attr;

	if (kind == NOOS_DESC_ATTR_SHARED) {
		attr = find_shared(a, name);
	} else {
		attr = find_attr(attr_array(a, kind, ch), name);
		if (kind == NOOS_DESC_ATTR_CHANNEL) {
			const struct iio_channel *c = &desc_of(a)->channels[ch];

			info = (struct iio_ch_info){
				.ch_num = c->channel,
				.ch_out = c->ch_out,
				.type = c->ch_type,
				.differential = c->diferential,
				.address = c->address,
			};
			pinfo = &info;
		}
	}

	if (!attr)
		return -ENOENT;

	if (write)
		return attr->store ? attr->store(a->instance, buf, len, pinfo, attr->priv) : -EPERM;

	return attr->show ? attr->show(a->instance, buf, len, pinfo, attr->priv) : -EPERM;
}

/* True if the driver gives register access. */
bool noos_desc_has_reg(const struct noos_iio_driver *a)
{
	return desc_of(a)->debug_reg_read != NULL;
}

/* Read a register through the driver's debug_reg_read. */
int noos_desc_reg_read(const struct noos_iio_driver *a, uint32_t reg, uint32_t *val)
{
	struct iio_device *desc = desc_of(a);

	if (!desc->debug_reg_read)
		return -ENOSYS;

	return desc->debug_reg_read(a->instance, reg, val);
}

/* Write a register through the driver's debug_reg_write. */
int noos_desc_reg_write(const struct noos_iio_driver *a, uint32_t reg, uint32_t val)
{
	struct iio_device *desc = desc_of(a);

	if (!desc->debug_reg_write)
		return -ENOSYS;

	return desc->debug_reg_write(a->instance, reg, val);
}

/* Same layout rule as no-OS iio.c, which is what the driver writes. */
size_t noos_desc_bytes_per_scan(const struct noos_iio_driver *a, uint32_t mask)
{
	struct iio_device *desc = desc_of(a);
	size_t cnt = 0, length, largest = 1;
	unsigned int i;

	for (i = 0; mask && i < noos_desc_num_channels(a); i++, mask >>= 1) {
		if (!(mask & 1) || !desc->channels[i].scan_type)
			continue;

		length = desc->channels[i].scan_type->storagebits / 8;
		if (!length)
			continue;
		if (length > largest)
			largest = length;

		if (cnt % length)
			cnt += 2 * length - (cnt % length);
		else
			cnt += length;
	}

	if (cnt % largest)
		cnt += largest - (cnt % largest);

	return cnt;
}

/* Buffer on (pre_enable with the mask) or off (post_disable). */
int noos_desc_enable(const struct noos_iio_driver *a, uint32_t mask, bool enable)
{
	struct iio_device *desc = desc_of(a);

	if (enable)
		return desc->pre_enable ? desc->pre_enable(a->instance, mask) : 0;

	return desc->post_disable ? desc->post_disable(a->instance) : 0;
}

/* One whole transfer through submit or read/write_dev. */
int noos_desc_transfer(const struct noos_iio_driver *a, uint32_t mask, void *data, size_t bytes,
		bool output)
{
	struct iio_device *desc = desc_of(a);
	struct no_os_circular_buffer cb;
	struct iio_buffer buf = { 0 };
	struct iio_device_data dev_data;
	size_t bps = noos_desc_bytes_per_scan(a, mask);
	int ret;

	if (!bps || bytes % bps)
		return -EINVAL;

	ret = no_os_cb_cfg(&cb, (int8_t *)data, bytes);
	if (ret)
		return ret;

	/* An output block arrives full: the driver pops it scan by scan. */
	if (output)
		cb.write.spin_count = 1;

	buf = (struct iio_buffer){
		.active_mask = mask,
		.size = bytes,
		.bytes_per_scan = bps,
		.samples = bytes / bps,
		.dir = output ? IIO_DIRECTION_OUTPUT : IIO_DIRECTION_INPUT,
		.buf = &cb,
	};
	dev_data = (struct iio_device_data){
		.dev = a->instance,
		.buffer = &buf,
	};

	if (desc->submit)
		ret = desc->submit(&dev_data);
	else if (!output && desc->read_dev)
		ret = desc->read_dev(a->instance, data, buf.samples);
	else if (output && desc->write_dev)
		ret = desc->write_dev(a->instance, data, buf.samples);
	else
		ret = -ENOSYS;

	return ret < 0 ? ret : 0;
}

/* --- what drivers call from submit; see the comment at the top --- */

/* no-OS API for drivers: the block to fill or drain in one go. */
int iio_buffer_get_block(struct iio_buffer *buffer, void **addr)
{
	uint32_t size;

	if (!buffer)
		return -EINVAL;

	if (buffer->dir == IIO_DIRECTION_INPUT)
		return no_os_cb_prepare_async_write(buffer->buf, buffer->size, addr, &size);

	return no_os_cb_prepare_async_read(buffer->buf, buffer->size, addr, &size);
}

/* no-OS API for drivers: the block from iio_buffer_get_block() is done. */
int iio_buffer_block_done(struct iio_buffer *buffer)
{
	if (!buffer)
		return -EINVAL;

	if (buffer->dir == IIO_DIRECTION_INPUT)
		return no_os_cb_end_async_write(buffer->buf);

	return no_os_cb_end_async_read(buffer->buf);
}

/* no-OS API for drivers: append one scan to an input block. */
int iio_buffer_push_scan(struct iio_buffer *buffer, void *data)
{
	if (!buffer)
		return -EINVAL;

	return no_os_cb_write(buffer->buf, data, buffer->bytes_per_scan);
}

/* no-OS API for drivers: take one scan from an output block. */
int iio_buffer_pop_scan(struct iio_buffer *buffer, void *data)
{
	if (!buffer)
		return -EINVAL;

	return no_os_cb_read(buffer->buf, data, buffer->bytes_per_scan);
}
