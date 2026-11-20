/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <capi_alloc.h>
#include <capi_log.h>
#include <ctype.h>
#include <errno.h>
#include <iio-private.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct iio_capi_device *capi_devices;

/* The buffers with a cyclic block, replayed by iio_capi_backend_idle() */
static struct iio_buffer_pdata *capi_cyclic_buffers;

/* The contexts made of capi_devices that exist: while one does, it holds
 * the devices, so none is unregistered. */
static unsigned int capi_nb_contexts;

/* The context's attributes (iio_capi_set_context_attrs()) */
static const struct iio_capi_ctx_attr *capi_ctx_attrs;
static unsigned int capi_nb_ctx_attrs;

struct iio_buffer_pdata {
	const struct iio_device *dev;
	/* Owned by the buffer stream, valid until the buffer is closed. */
	const struct iio_channels_mask *mask;
	bool enabled;
	/* Of one scan of the channels in mask */
	ssize_t sample_size;
	/* Blocks enqueued while the buffer was disabled, oldest first: their
	 * transfers wait for it to be enabled */
	struct iio_block_pdata *pending;
	/* The block a cyclic output buffer repeats; NULL for none */
	struct iio_block_pdata *cyclic;
	/* The next buffer in capi_cyclic_buffers */
	struct iio_buffer_pdata *next_cyclic;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	size_t size;
	void *data;
	size_t bytes_used;
	int error;
	/* Enqueued and not dequeued since */
	bool enqueued;
	/* Its transfer is done (else it waits in the buffer's pending list) */
	bool done;
	bool cyclic;
	struct iio_block_pdata *next_pending;
};

static struct iio_capi_device *capi_device_of(const struct iio_device *iio_device)
{
	return (struct iio_capi_device *)iio_device_get_pdata(iio_device);
}

/* Whether the device has the sample op of its buffer's direction. */
static bool capi_device_has_buffer(const struct iio_capi_device *dev)
{
	return dev->output ? !!dev->api->write_samples : !!dev->api->read_samples;
}

/* A name the context's XML holds as it is: nothing it would have to
 * escape. */
static bool capi_name_ok(const char *name)
{
	const char *p;

	if (!name || !*name)
		return false;

	for (p = name; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.')
			return false;

	return true;
}

int iio_capi_device_register(struct iio_capi_device *dev)
{
	struct iio_capi_device **tail = &capi_devices;

	if (!dev || !capi_name_ok(dev->name) || !dev->api || !dev->api->add_channels)
		return -EINVAL;

	if (capi_nb_contexts)
		return -EBUSY;

	/* Keep registration order: it gives the iio:deviceN numbering. */
	while (*tail) {
		if (*tail == dev)
			return -EEXIST;
		tail = &(*tail)->next;
	}

	dev->next = NULL;
	dev->reg_sel = 0;
	*tail = dev;

	return 0;
}

int iio_capi_device_unregister(struct iio_capi_device *dev)
{
	struct iio_capi_device **link = &capi_devices;

	if (!dev)
		return -EINVAL;

	if (capi_nb_contexts)
		return -EBUSY;

	while (*link && *link != dev)
		link = &(*link)->next;
	if (!*link)
		return -ENOENT;

	*link = dev->next;
	dev->next = NULL;

	return 0;
}

/* A context attribute's name a client keeps: not uri, which it sets
 * itself, nor one with a ',' (its backends' keys). */
static bool capi_ctx_attr_name_ok(const char *name)
{
	return capi_name_ok(name) && strcmp(name, "uri");
}

int iio_capi_set_context_attrs(const struct iio_capi_ctx_attr *attrs, unsigned int nb)
{
	unsigned int i, j;

	if (nb && !attrs)
		return -EINVAL;

	if (capi_nb_contexts)
		return -EBUSY;

	for (i = 0; i < nb; i++) {
		if (!capi_ctx_attr_name_ok(attrs[i].name) || !attrs[i].value)
			return -EINVAL;

		for (j = 0; j < i; j++)
			if (!strcmp(attrs[j].name, attrs[i].name))
				return -EINVAL;
	}

	capi_ctx_attrs = nb ? attrs : NULL;
	capi_nb_ctx_attrs = nb;

	return 0;
}

/*
 * direct_reg_access, the debug attribute Linux drivers have for register
 * access: a device with reg_read gets it. Writing "<reg>" selects a
 * register, "<reg> <value>" writes one (with reg_write); reading gives
 * the selected register's value, in hex.
 */
static bool capi_is_reg_attr(const struct iio_capi_device *dev, const struct iio_attr *attr)
{
	return dev->api->reg_read && attr->type == IIO_ATTR_TYPE_DEBUG &&
	       !strcmp(iio_attr_get_name(attr), "direct_reg_access");
}

static int capi_add_reg_attr(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	if (!dev->api->reg_read)
		return 0;

	return iio_device_add_attr(iio_device, "direct_reg_access", IIO_ATTR_TYPE_DEBUG);
}

static ssize_t capi_reg_attr_read(const struct iio_capi_device *dev, char *dst, size_t len)
{
	uint32_t val;
	int ret;

	ret = dev->api->reg_read(dev, dev->reg_sel, &val);
	if (ret < 0)
		return ret;

	ret = snprintf(dst, len, "0x%" PRIX32, val);
	if (ret < 0 || (size_t)ret >= len)
		return -ERANGE;

	return ret + 1;
}

/* "<reg>" selects a register, "<reg> <value>" also writes it. All of it is
 * checked before the register is written. */
static ssize_t capi_reg_attr_write(struct iio_capi_device *dev, const char *src, size_t len)
{
	unsigned long long reg, val = 0;
	char buf[32], *end, *next;
	bool write = false;
	int ret;

	if (len >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, src, len);
	buf[len] = '\0';

	errno = 0;
	reg = strtoull(buf, &end, 0);
	if (end == buf || errno || reg > UINT32_MAX)
		return -EINVAL;

	val = strtoull(end, &next, 0);
	if (next != end) {
		if (errno || val > UINT32_MAX)
			return -EINVAL;

		write = true;
		end = next;
	}

	while (*end == ' ' || *end == '\n')
		end++;
	if (*end)
		return -EINVAL;

	if (write) {
		if (!dev->api->reg_write)
			return -ENOSYS;

		ret = dev->api->reg_write(dev, (uint32_t)reg, (uint32_t)val);
		if (ret < 0)
			return ret;
	}

	dev->reg_sel = (uint32_t)reg;

	return (ssize_t)len;
}

static ssize_t capi_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	const struct iio_capi_device *dev = capi_device_of(iio_device);

	if (capi_is_reg_attr(dev, attr))
		return capi_reg_attr_read(dev, dst, len);

	if (!dev->api->read_attr)
		return -ENOSYS;

	return dev->api->read_attr(dev, iio_device, attr, dst, len);
}

static ssize_t capi_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	struct iio_capi_device *dev = capi_device_of(iio_device);

	if (capi_is_reg_attr(dev, attr))
		return capi_reg_attr_write(dev, src, len);

	if (!dev->api->write_attr)
		return -ENOSYS;

	return dev->api->write_attr(dev, iio_device, attr, src, len);
}

/*
 * The buffer and block ops: libiio opens a buffer, then streams through
 * blocks, and every block transfer is done synchronously by the device's
 * read_samples/write_samples: at enqueue, or for a block enqueued before
 * the buffer is enabled (as libiio's streams do), when it is enabled, in
 * enqueue order. A cyclic block of an output buffer is written once, then
 * again each time the transport is idle (iio_capi_backend_idle()), until
 * the buffer is disabled or closed or the block freed; until then no other
 * block is taken. As in libiio's local backend, a block enqueued twice or
 * dequeued twice is refused.
 */

static void capi_cyclic_start(struct iio_buffer_pdata *buf, struct iio_block_pdata *block)
{
	if (!buf->cyclic) {
		buf->next_cyclic = capi_cyclic_buffers;
		capi_cyclic_buffers = buf;
	}

	buf->cyclic = block;
}

static void capi_cyclic_stop(struct iio_buffer_pdata *buf)
{
	struct iio_buffer_pdata **link = &capi_cyclic_buffers;

	if (!buf->cyclic)
		return;

	while (*link != buf)
		link = &(*link)->next_cyclic;

	*link = buf->next_cyclic;
	buf->next_cyclic = NULL;
	buf->cyclic = NULL;
}

void iio_capi_backend_idle(void)
{
	struct iio_buffer_pdata *buf, *next;
	const struct iio_capi_device *dev;
	int ret;

	for (buf = capi_cyclic_buffers; buf; buf = next) {
		next = buf->next_cyclic;
		if (!buf->enabled || !buf->cyclic->done)
			continue;

		dev = capi_device_of(buf->dev);
		ret = dev->api->write_samples(dev, buf->dev, buf->mask, buf->cyclic->data,
				buf->cyclic->bytes_used);
		if (ret < 0) {
			/* Only the log tells over IIOD: the responder never
			 * dequeues a cyclic block. */
			CAPI_LOG("%s: cyclic buffer stopped: %d\n", dev->name, ret);
			buf->cyclic->error = ret;
			capi_cyclic_stop(buf);
		}
	}
}

static struct iio_buffer_pdata *capi_open_buffer(
		const struct iio_device *dev, unsigned int idx, struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata;

	(void)idx;

	pdata = capi_calloc(1, sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->mask = mask;
	/* The channels are those of the whole session; libiio's core already
	 * refused a mask with no sample. */
	pdata->sample_size = iio_device_get_sample_size(dev, mask);

	return pdata;
}

/* The block's transfer, now. A cyclic block's error goes back at once:
 * the responder never dequeues it. */
static int capi_block_transfer(struct iio_block_pdata *pdata)
{
	struct iio_buffer_pdata *buf = pdata->buf;
	const struct iio_capi_device *dev = capi_device_of(buf->dev);
	int ret;

	if (dev->output)
		ret = dev->api->write_samples(
				dev, buf->dev, buf->mask, pdata->data, pdata->bytes_used);
	else
		ret = dev->api->read_samples(
				dev, buf->dev, buf->mask, pdata->data, pdata->bytes_used);

	pdata->error = ret < 0 ? ret : 0;

	pdata->done = true;

	if (!pdata->cyclic)
		return 0;

	if (pdata->error) {
		capi_cyclic_stop(buf);
		pdata->enqueued = false;
		return pdata->error;
	}

	capi_cyclic_start(buf, pdata);

	return 0;
}

/* The transfers of the blocks enqueued while the buffer was disabled. */
static int capi_run_pending(struct iio_buffer_pdata *buf)
{
	struct iio_block_pdata *block;
	int ret, first = 0;

	while ((block = buf->pending)) {
		buf->pending = block->next_pending;
		block->next_pending = NULL;

		ret = capi_block_transfer(block);
		if (ret && !first)
			first = ret;
	}

	return first;
}

static void capi_pending_remove(struct iio_block_pdata *pdata)
{
	struct iio_block_pdata **link = &pdata->buf->pending;

	while (*link && *link != pdata)
		link = &(*link)->next_pending;

	if (*link)
		*link = pdata->next_pending;
	pdata->next_pending = NULL;
}

/* Calls the device's enable_buffer only on a change. Enabled, it runs the
 * transfers of the blocks enqueued before; if a cyclic one fails, which the
 * responder never dequeues, the buffer is disabled again and its error
 * returned. */
static int capi_enable_buffer(
		struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable, bool cyclic)
{
	const struct iio_capi_device *dev = capi_device_of(pdata->dev);
	bool was_enabled = pdata->enabled;
	int ret;

	/* Cyclic is per block (capi_enqueue_block()): with the block ops,
	 * libiio passes false here. */
	(void)nb_samples;
	(void)cyclic;

	if (dev->api->enable_buffer && was_enabled != enable) {
		ret = dev->api->enable_buffer(dev, enable);
		if (ret < 0)
			return ret;
	}

	pdata->enabled = enable;

	if (enable) {
		/* Blocks wait only while the buffer is disabled. */
		ret = capi_run_pending(pdata);
		if (ret) {
			if (dev->api->enable_buffer)
				dev->api->enable_buffer(dev, false);
			pdata->enabled = false;
		}

		return ret;
	}

	/* Disabled: the cyclic block stops repeating. One still waiting for
	 * a never enabled buffer keeps it from taking another. */
	if (was_enabled)
		capi_cyclic_stop(pdata);

	return 0;
}

static void capi_close_buffer(struct iio_buffer_pdata *pdata)
{
	/* A client that drops the connection never disables the buffer. */
	if (pdata->enabled)
		capi_enable_buffer(pdata, 0, false, false);

	capi_cyclic_stop(pdata);
	capi_free(pdata);
}

static struct iio_block_pdata *capi_create_block(
		struct iio_buffer_pdata *buf, size_t size, void **data)
{
	struct iio_block_pdata *pdata = capi_calloc(1, sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->data = capi_malloc(size);
	if (!pdata->data) {
		capi_free(pdata);
		return iio_ptr(-ENOMEM);
	}

	pdata->buf = buf;
	pdata->size = size;
	*data = pdata->data;

	return pdata;
}

static void capi_free_block(struct iio_block_pdata *pdata)
{
	if (pdata->buf->cyclic == pdata)
		capi_cyclic_stop(pdata->buf);

	capi_pending_remove(pdata);

	capi_free(pdata->data);
	capi_free(pdata);
}

/* Do the transfer now, or once the buffer is enabled; the result is kept
 * for the dequeue. A cyclic block is then repeated
 * (iio_capi_backend_idle()). */
static int capi_enqueue_block(struct iio_block_pdata *pdata, size_t bytes_used, bool cyclic)
{
	struct iio_buffer_pdata *buf = pdata->buf;
	const struct iio_capi_device *dev = capi_device_of(buf->dev);
	struct iio_block_pdata **tail = &buf->pending;

	if (pdata->enqueued)
		return -EPERM;

	/* The buffer keeps repeating its cyclic block. */
	if (buf->cyclic)
		return -EBUSY;

	if (cyclic && !dev->output)
		return -EINVAL;

	/* Whole scans only, as iio_device.h promises the device. An input
	 * block is filled whole, as libiio's local backend wants: the
	 * responder sends all of it. */
	if (bytes_used % (size_t)buf->sample_size || (!dev->output && bytes_used != pdata->size))
		return -EINVAL;

	pdata->bytes_used = bytes_used;
	pdata->cyclic = cyclic;
	pdata->enqueued = true;
	pdata->done = false;

	if (buf->enabled)
		return capi_block_transfer(pdata);

	while (*tail)
		tail = &(*tail)->next_pending;
	*tail = pdata;

	/* Taken as the buffer's cyclic block now, so that no other is. */
	if (cyclic)
		capi_cyclic_start(buf, pdata);

	return 0;
}

/* The block's transfer is done at enqueue, or when the buffer is enabled:
 * report how it went. */
static int capi_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	if (!pdata->enqueued)
		return -EPERM;

	/* Waiting for the buffer to be enabled; nothing else would run it. */
	if (!pdata->done)
		return nonblock ? -EBUSY : -ETIMEDOUT;

	pdata->enqueued = false;

	return pdata->error;
}

/* An input or output buffer with every scan element, and the device's
 * buffer attributes. */
static int capi_add_buffer(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	struct iio_buffer *buf;
	unsigned int i, nb_channels;
	int ret;

	if (!capi_device_has_buffer(dev))
		return 0;

	buf = iio_device_add_buffer(iio_device, 0);
	if (!buf)
		return -ENOMEM;

	iio_buffer_set_direction(buf, dev->output ? "out" : "in");

	nb_channels = iio_device_get_channels_count(iio_device);
	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *chn = iio_device_get_channel(iio_device, i);

		if (!iio_channel_is_scan_element(chn))
			continue;

		ret = iio_buffer_add_scan_element(buf, chn, NULL);
		if (ret)
			return ret;
	}

	return dev->api->add_buffer_attrs ? dev->api->add_buffer_attrs(dev, buf) : 0;
}

/* Debug register access (iio_reg), forwarded to the device. */
static int capi_reg_read(const struct iio_device *iio_device, uint32_t address, uint32_t *value)
{
	const struct iio_capi_device *dev = capi_device_of(iio_device);
	int ret;

	if (!dev->api->reg_read)
		return -ENOSYS;

	ret = dev->api->reg_read(dev, address, value);

	return ret < 0 ? ret : 0;
}

static int capi_reg_write(const struct iio_device *iio_device, uint32_t address, uint32_t value)
{
	const struct iio_capi_device *dev = capi_device_of(iio_device);
	int ret;

	if (!dev->api->reg_write)
		return -ENOSYS;

	ret = dev->api->reg_write(dev, address, value);

	return ret < 0 ? ret : 0;
}

/* No device has a trigger: -ENODEV, as libiio's local backend gives it. */
static const struct iio_device *capi_get_trigger(const struct iio_device *dev)
{
	(void)dev;

	return iio_ptr(-ENODEV);
}

static int capi_add_ctx_attrs(struct iio_context *ctx)
{
	unsigned int i;
	int ret;

	for (i = 0; i < capi_nb_ctx_attrs; i++) {
		ret = iio_context_add_attr(ctx, capi_ctx_attrs[i].name, capi_ctx_attrs[i].value);
		if (ret < 0) {
			CAPI_LOG("cannot add context attribute %s: %d\n", capi_ctx_attrs[i].name,
					ret);
			return ret;
		}
	}

	return 0;
}

static struct iio_context *capi_create_context(
		const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_capi_device *dev;
	struct iio_device *iio_device;
	unsigned int i = 0;
	char id[32];
	int ret;

	/* The backend is CAPI: its version and commit are the context's.
	 * libiio's own commit, which a client has its own of, is in the
	 * description. */
	const char *description = "CAPI " CAPI_BACKEND_VERSION " libiio " LIBIIO_VERSION_GIT;

	(void)args;

	ctx = iio_context_create_from_backend(params, &iio_external_backend, description,
			CAPI_BACKEND_VERSION_MAJOR, CAPI_BACKEND_VERSION_MINOR,
			CAPI_BACKEND_VERSION_PATCH, CAPI_BACKEND_VERSION_GIT);
	if (iio_err(ctx))
		return iio_err_cast(ctx);
	/* From here on, iio_context_destroy() runs capi_shutdown() */
	capi_nb_contexts++;

	ret = capi_add_ctx_attrs(ctx);
	if (ret < 0)
		goto err_destroy;

	for (dev = capi_devices; dev; dev = dev->next) {
		snprintf(id, sizeof(id), "iio:device%u", i);

		/* Returns NULL, not an error pointer, when it fails. */
		iio_device = iio_context_add_device(ctx, id, dev->name, NULL);
		if (!iio_device) {
			ret = -ENOMEM;
			CAPI_LOG("%s: cannot add the device\n", dev->name);
			goto err_destroy;
		}

		iio_device_set_pdata(iio_device, (struct iio_device_pdata *)dev);

		ret = dev->api->add_channels(dev, iio_device);
		if (ret < 0) {
			CAPI_LOG("%s: add_channels failed: %d\n", dev->name, ret);
			goto err_destroy;
		}

		ret = capi_add_buffer(dev, iio_device);
		if (ret < 0) {
			CAPI_LOG("%s: cannot add the buffer: %d\n", dev->name, ret);
			goto err_destroy;
		}

		ret = capi_add_reg_attr(dev, iio_device);
		if (ret < 0) {
			CAPI_LOG("%s: cannot add direct_reg_access: %d\n", dev->name, ret);
			goto err_destroy;
		}

		i++;
	}

	return ctx;

err_destroy:
	iio_context_destroy(ctx);
	return iio_ptr(ret);
}

static void capi_shutdown(struct iio_context *ctx)
{
	(void)ctx;

	if (capi_nb_contexts)
		capi_nb_contexts--;
}

static const struct iio_backend_ops capi_ops = {
	.create = capi_create_context,
	.shutdown = capi_shutdown,
	.read_attr = capi_read_attr,
	.write_attr = capi_write_attr,
	.get_trigger = capi_get_trigger,

	.open_buffer = capi_open_buffer,
	.close_buffer = capi_close_buffer,
	.enable_buffer = capi_enable_buffer,

	.create_block = capi_create_block,
	.free_block = capi_free_block,
	.enqueue_block = capi_enqueue_block,
	.dequeue_block = capi_dequeue_block,

	.reg_read = capi_reg_read,
	.reg_write = capi_reg_write,
};

const struct iio_backend iio_external_backend = {
	.name = "capi",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 5000,
	.uri_prefix = "capi:",
	.ops = &capi_ops,
};
