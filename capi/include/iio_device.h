/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CAPI_INCLUDE_IIO_DEVICE_H_
#define CAPI_INCLUDE_IIO_DEVICE_H_

#include <iio/iio-backend.h>
#include <iio/iio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_capi_device;

typedef int (*iio_capi_device_add_channels_t)(
		const struct iio_capi_device *dev, struct iio_device *iio_device);

typedef int (*iio_capi_device_read_attr_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr, char *dst,
		size_t len);

typedef int (*iio_capi_device_write_attr_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr, const char *src,
		size_t len);

/* Fill (read) or consume (write) bytes of samples, whole scans, packed as
 * libiio lays them out for the channels enabled in mask. */
typedef int (*iio_capi_device_read_samples_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		void *data, size_t bytes);

typedef int (*iio_capi_device_write_samples_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		const void *data, size_t bytes);

/* Buffer enabled or disabled by the client. */
typedef int (*iio_capi_device_enable_buffer_t)(const struct iio_capi_device *dev, bool enable);

/* Debug register access (iio_reg, direct_reg_access). */
typedef int (*iio_capi_device_reg_read_t)(
		const struct iio_capi_device *dev, uint32_t reg, uint32_t *val);

typedef int (*iio_capi_device_reg_write_t)(
		const struct iio_capi_device *dev, uint32_t reg, uint32_t val);

typedef int (*iio_capi_device_add_buffer_attrs_t)(
		const struct iio_capi_device *dev, struct iio_buffer *buf);

/* What a device does for libiio clients. add_channels is required; the
 * others are optional. A device with the sample op of its direction
 * (read_samples for an input one, write_samples for an output one) gets a
 * buffer; one with reg_read gets register access. add_buffer_attrs adds
 * the buffer's attributes, once the buffer is there; read_attr and
 * write_attr serve them. */
struct iio_capi_device_api {
	iio_capi_device_add_channels_t add_channels;
	iio_capi_device_read_attr_t read_attr;
	iio_capi_device_write_attr_t write_attr;
	iio_capi_device_read_samples_t read_samples;
	iio_capi_device_write_samples_t write_samples;
	iio_capi_device_enable_buffer_t enable_buffer;
	iio_capi_device_reg_read_t reg_read;
	iio_capi_device_reg_write_t reg_write;
	iio_capi_device_add_buffer_attrs_t add_buffer_attrs;
};

/* One device served to libiio clients: a name, the part's handle and a
 * table of callbacks. CAPI has no device model, so the application fills
 * one of these per device and registers it before the server starts. */
struct iio_capi_device {
	/* IIO device name */
	const char *name;
	/* What api works on, e.g. the part's CAPI handle */
	void *handle;
	const struct iio_capi_device_api *api;
	/* Its buffer is an output one (samples go to write_samples), else an
	 * input one (samples come from read_samples) */
	bool output;
	/* Set by iio_capi_device_register() */
	struct iio_capi_device *next;
	/* Set by the backend: the register direct_reg_access reads */
	uint32_t reg_sel;
};

/* Add a device to the context. Call before the context is created; dev
 * must stay valid for as long as the context exists. Devices are numbered
 * iio:device0, 1 ... in the order they are registered. Returns 0, -EINVAL
 * without a name, an api or its add_channels, or -EEXIST for a device
 * already registered. */
int iio_capi_device_register(struct iio_capi_device *dev);

/* Take a device out of the context to come: before the context is created
 * or once it is gone (iiod_<transport>_run() returned), so that the part's
 * IIO support can be undone. Devices after it are numbered one less.
 * Returns 0, -EINVAL for no dev, -EBUSY while a context exists, or -ENOENT
 * for a device not registered. */
int iio_capi_device_unregister(struct iio_capi_device *dev);

/* A context attribute: what the firmware says of itself (hw_model,
 * fw_version ...), as no-OS's iio_ctx_attr. iio_info lists them, sorted by
 * name as libiio sorts every attribute; iio_attr -C reads them. */
struct iio_capi_ctx_attr {
	const char *name;
	const char *value;
};

/*
 * Set the context's attributes, as no-OS's iio_init_param.ctx_attrs. Call
 * before the context is created, as iio_capi_device_register(); attrs is
 * kept, not copied, and replaces what an earlier call set (NULL and 0 for
 * none). Returns 0, or -EINVAL for attrs NULL with nb, a value NULL, a
 * name used twice, or a name a client would not keep: empty, "uri" or of
 * other characters than letters, digits, '_', '-' and '.' (a client sets
 * uri itself, and the keys of its backends have a ',': serial,port ...).
 */
int iio_capi_set_context_attrs(const struct iio_capi_ctx_attr *attrs, unsigned int nb);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_DEVICE_H_ */
