/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <iio-private.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#include <no_os_print_log.h>
#include <no_os_uart.h>
#include <noos_iiod.h>

#define NOOS_BACKEND_VERSION "no-OS 1.0 " __DATE__ " " __TIME__

struct noos_iio_device_info noos_iio_devices[NOOS_IIO_MAX_DEVICES];
unsigned int noos_iio_device_count;

struct iio_buffer_pdata {
	const struct iio_device *dev;
	/* Owned by the buffer stream, valid until the buffer is closed. */
	const struct iio_channels_mask *mask;
	bool enabled;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	size_t size;
	void *data;
	size_t bytes_used;
	int error;
};

/*
 * libiio opens a device buffer: remember the device and its channel mask
 * for the block transfers that follow.
 */
static struct iio_buffer_pdata *noos_open_buffer(
		const struct iio_device *dev, unsigned int idx, struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->mask = mask;

	return pdata;
}

/* Get the driver info (callbacks + dev) a device was registered with. */
static struct noos_iio_device_info *noos_buffer_info(const struct iio_buffer_pdata *pdata)
{
	return (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
}

/* Start/stop streaming; calls the driver's enable_buffer only on a change. */
static int noos_enable_buffer(
		struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable, bool cyclic)
{
	struct noos_iio_device_info *info = noos_buffer_info(pdata);
	int ret;

	if (info && info->enable_buffer && pdata->enabled != enable) {
		ret = info->enable_buffer(info->dev, enable);
		if (ret)
			return ret;
	}

	pdata->enabled = enable;

	return 0;
}

/* Buffer closed by the client: disable it if still running, then free it. */
static void noos_close_buffer(struct iio_buffer_pdata *pdata)
{
	/* A client that drops the connection never disables the buffer. */
	if (pdata->enabled)
		noos_enable_buffer(pdata, 0, false, false);

	free(pdata);
}

/* Nothing to cancel: every transfer here finishes before the call returns. */
static void noos_cancel_buffer(struct iio_buffer_pdata *pdata)
{
}

/* Direct read path: fill dst from the driver's read_samples. */
static ssize_t noos_readbuf(struct iio_buffer_pdata *pdata, void *dst, size_t len)
{
	struct noos_iio_device_info *info;
	int ret;

	if (!pdata || !pdata->dev)
		return -EINVAL;

	info = (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
	if (!info || !info->read_samples)
		return -ENOSYS;

	ret = info->read_samples(info->dev, pdata->dev, pdata->mask, dst, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

/*
 * Direct write path: pass src to the driver's write_samples (output devices).
 */
static ssize_t noos_writebuf(struct iio_buffer_pdata *pdata, const void *src, size_t len)
{
	struct noos_iio_device_info *info;
	int ret;

	if (!pdata || !pdata->dev)
		return -EINVAL;

	info = (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
	if (!info || !info->write_samples)
		return -ENOSYS;

	ret = info->write_samples(info->dev, pdata->dev, pdata->mask, src, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

/* Allocate one block of sample memory; iiod streams through these blocks. */
static struct iio_block_pdata *noos_create_block(
		struct iio_buffer_pdata *buf, size_t size, void **data)
{
	struct iio_block_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->data = malloc(size);
	if (!pdata->data) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	pdata->buf = buf;
	pdata->size = size;
	*data = pdata->data;

	return pdata;
}

/* Release a block made by noos_create_block(). */
static void noos_free_block(struct iio_block_pdata *pdata)
{
	free(pdata->data);
	free(pdata);
}

/*
 * Do the transfer now: an input block is filled by read_samples, an output
 * block goes to write_samples. The result is kept for the dequeue.
 */
static int noos_enqueue_block(struct iio_block_pdata *pdata, size_t bytes_used, bool cyclic)
{
	struct iio_buffer_pdata *buf = pdata->buf;
	struct noos_iio_device_info *info = noos_buffer_info(buf);

	if (!info)
		return -ENOSYS;

	/*
	 * Enqueue runs the transfer inline, so a block cannot repeat until the
	 * buffer is disabled: refuse cyclic buffers instead of sending it once.
	 */
	if (cyclic)
		return -ENOSYS;

	pdata->bytes_used = bytes_used;

	if (info->direction) {
		if (!info->write_samples)
			return -ENOSYS;
		pdata->error = info->write_samples(
				info->dev, buf->dev, buf->mask, pdata->data, bytes_used);
	} else {
		if (!info->read_samples)
			return -ENOSYS;
		pdata->error = info->read_samples(
				info->dev, buf->dev, buf->mask, pdata->data, bytes_used);
	}

	return 0;
}

/* The block was done at enqueue time, so just report how it went. */
static int noos_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	return pdata->error;
}

/*
 * Called from main() before the context exists: adds a driver to the list
 * noos_create_context() turns into IIO devices.
 */
int noos_iio_register_device(const struct noos_iio_device_info *info)
{
	int ret;

	if (!info)
		return -EINVAL;

	if (noos_iio_device_count >= NOOS_IIO_MAX_DEVICES)
		return -ENOMEM;

	/* A table-driven device gets its callbacks made from its tables here. */
	noos_iio_devices[noos_iio_device_count] = *info;
	ret = noos_iio_table_install(&noos_iio_devices[noos_iio_device_count]);
	if (ret)
		return ret;

	noos_iio_device_count++;
	return 0;
}

/*
 * Point the logs at the project's console, then init and register every
 * row of its device table. A device that fails is logged and left out; the
 * rest are still served.
 */
void noos_iiod_register(const struct noos_iiod_param *param)
{
	static struct no_os_uart_desc *console;
	struct noos_iio_device_info info;
	unsigned int i;
	int ret;

	/* First, so the device inits below can already log. */
	if (param->console && !console) {
		ret = no_os_uart_init(&console, param->console);
		if (!ret)
			no_os_uart_stdio(console);
	}

	for (i = 0; i < param->nb_devices; i++) {
		const struct noos_iiod_device *d = &param->devices[i];

		if (d->init) {
			ret = d->init(d->param);
			if (ret) {
				pr_err("device %u: init failed: %d\n", i, ret);
				continue;
			}
		}

		info = (struct noos_iio_device_info){ 0 };
		ret = d->get_info(&info);
		if (!ret)
			ret = noos_iio_register_device(&info);
		if (ret)
			pr_err("device %u: not registered: %d\n", i, ret);
	}
}

/* Route an attribute read (e.g. iio_attr) to the owning driver's read_attr. */
static ssize_t noos_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
			(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (!info || !info->read_attr)
		return -ENOSYS;

	return info->read_attr(info->dev, iio_dev, attr, dst, len);
}

/* Route an attribute write to the owning driver's write_attr. */
static ssize_t noos_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
			(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (!info || !info->write_attr)
		return -ENOSYS;

	return info->write_attr(info->dev, iio_dev, attr, src, len);
}

/* No triggers yet: the devices pace their own captures. */
static const struct iio_device *noos_get_trigger(const struct iio_device *dev)
{
	return iio_ptr(-ENODEV);
}

/* Debug register read (iio_reg), forwarded to the driver's reg_read. */
static int noos_reg_read(const struct iio_device *dev, uint32_t address, uint32_t *value)
{
	struct noos_iio_device_info *info =
			(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_read)
		return -ENOSYS;

	return info->reg_read(info->dev, address, value);
}

/* Debug register write (iio_reg), forwarded to the driver's reg_write. */
static int noos_reg_write(const struct iio_device *dev, uint32_t address, uint32_t value)
{
	struct noos_iio_device_info *info =
			(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_write)
		return -ENOSYS;

	return info->reg_write(info->dev, address, value);
}

/* The device's buffer, holding all its scan elements; none without channels. */
static int noos_add_buffer(struct iio_device *iio_dev, const struct noos_iio_device_info *info)
{
	unsigned int c, nb_channels = iio_device_get_channels_count(iio_dev);
	struct iio_buffer *buf;
	int ret;

	if (!nb_channels)
		return 0;

	buf = iio_device_add_buffer(iio_dev, 0);
	if (!buf)
		return -ENOMEM;

	iio_buffer_set_direction(buf, info->direction ? "out" : "in");
	for (c = 0; c < nb_channels; c++) {
		struct iio_channel *chn = iio_device_get_channel(iio_dev, c);

		if (!iio_channel_is_scan_element(chn))
			continue;

		ret = iio_buffer_add_scan_element(buf, chn, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Build the context behind the "no-os:" URI: one IIO device per registered
 * driver, with its channels and a buffer holding the scan elements.
 */
static struct iio_context *noos_create_context(
		const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	unsigned int i, nb_devices = 0;
	char id[32];
	int ret;

	ctx = iio_context_create_from_backend(
			params, &iio_external_backend, NOOS_BACKEND_VERSION, 1, 0, 0, "v1.0");
	if (iio_err(ctx))
		return iio_err_cast(ctx);

	for (i = 0; i < noos_iio_device_count; i++) {
		struct noos_iio_device_info *info = &noos_iio_devices[i];

		snprintf(id, sizeof(id), "iio:device%u", nb_devices++);

		iio_dev = iio_context_add_device(ctx, id, info->name, NULL);
		if (!iio_dev) {
			ret = -ENOMEM;
			goto err_destroy;
		}

		iio_device_set_pdata(iio_dev, (struct iio_device_pdata *)info);

		if (info->add_channels) {
			ret = info->add_channels(info->dev, iio_dev);
			if (ret)
				goto err_destroy;
		}

		ret = noos_add_buffer(iio_dev, info);
		if (ret)
			goto err_destroy;
	}

	return ctx;

err_destroy:
	pr_err("%s: not added to the context: %d\n", noos_iio_devices[i].name, ret);
	iio_context_destroy(ctx);
	return iio_ptr(ret);
}

static const struct iio_backend_ops noos_ops = {
	.create = noos_create_context,
	.get_trigger = noos_get_trigger,
	.read_attr = noos_read_attr,
	.write_attr = noos_write_attr,

	.open_buffer = noos_open_buffer,
	.close_buffer = noos_close_buffer,
	.enable_buffer = noos_enable_buffer,
	.cancel_buffer = noos_cancel_buffer,

	.readbuf = noos_readbuf,
	.writebuf = noos_writebuf,

	.create_block = noos_create_block,
	.free_block = noos_free_block,
	.enqueue_block = noos_enqueue_block,
	.dequeue_block = noos_dequeue_block,

	.reg_read = noos_reg_read,
	.reg_write = noos_reg_write,
};

const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
