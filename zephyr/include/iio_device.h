/*
 * Copyright (c) 2025-2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZEPHYR_INCLUDE_IIO_DEVICE_H_
#define ZEPHYR_INCLUDE_IIO_DEVICE_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/dsp/types.h>
#include <zephyr/sys/iterable_sections.h>
#ifdef CONFIG_LIBIIO_STREAMING
#include <zephyr/rtio/rtio.h>
#endif
#include <iio/iio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_device_info {
	const char *name;
	const struct device *dev;
};

#define IIO_DEVICE_INFO_INITIALIZER(_dev, _name)				\
	{									\
		.dev = _dev,							\
		.name = _name,							\
	}

#define IIO_DEVICE_INFO_DEFINE(name, ...)					\
	static const STRUCT_SECTION_ITERABLE(iio_device_info, name) =		\
		IIO_DEVICE_INFO_INITIALIZER(__VA_ARGS__)

#define IIO_DEVICE_INFO_DT_NAME(node_id)					\
	_CONCAT(__iio_device_info, DEVICE_DT_NAME_GET(node_id))

#define IIO_DEVICE_INFO_DT_DEFINE(node_id)					\
	IIO_DEVICE_INFO_DEFINE(IIO_DEVICE_INFO_DT_NAME(node_id),		\
			       DEVICE_DT_GET(node_id),				\
			       DT_PROP_OR(node_id, io_name, NULL))

#define IIO_DEVICE_DT_DEFINE(node_id, init_fn, pm_device,			\
			     data_ptr, cfg_ptr, level, prio,			\
			     api_ptr, ...)					\
	DEVICE_DT_DEFINE(node_id, init_fn, pm_device,				\
			 data_ptr, cfg_ptr, level, prio,			\
			 api_ptr, __VA_ARGS__);					\
										\
	IIO_DEVICE_INFO_DT_DEFINE(node_id);


#define IIO_DEVICE_DT_INST_DEFINE(inst, ...)					\
	IIO_DEVICE_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

#ifdef CONFIG_LIBIIO_STREAMING

/** Bytes reserved for the backend ahead of each block payload. */
#define IIO_DEVICE_BLOCK_HEADER_SIZE (4 * sizeof(void *))

/** Streaming resources of a device. */
struct iio_device_stream {
	/** RTIO context the device captures through. */
	struct rtio *rtio;
	/** Buffer blocks, from buffer-block-count. */
	struct k_mem_slab *block_slab;
	/** Largest payload a block accepts, from buffer-block-size-max. */
	uint32_t block_size_max;
	/** Set while a buffer is open; the RTIO context allows one. */
	atomic_t in_use;
};

#define IIO_DEVICE_STREAM_DT_NAME(node_id)					\
	_CONCAT(__iio_device_stream, DEVICE_DT_NAME_GET(node_id))

#define IIO_DEVICE_BLOCK_SLAB_DT_NAME(node_id)					\
	_CONCAT(__iio_block_slab, DEVICE_DT_NAME_GET(node_id))

/** @brief Define the streaming resources of a devicetree node. */
#define IIO_DEVICE_STREAM_DT_DEFINE(node_id, rtio_ctx)				\
	K_MEM_SLAB_DEFINE_STATIC(IIO_DEVICE_BLOCK_SLAB_DT_NAME(node_id),	\
		IIO_DEVICE_BLOCK_HEADER_SIZE +					\
			ROUND_UP(DT_PROP(node_id, buffer_block_size_max),	\
				 sizeof(void *)),				\
		DT_PROP(node_id, buffer_block_count), sizeof(void *));		\
	static struct iio_device_stream IIO_DEVICE_STREAM_DT_NAME(node_id) = {	\
		.rtio = (rtio_ctx),						\
		.block_slab = &IIO_DEVICE_BLOCK_SLAB_DT_NAME(node_id),		\
		.block_size_max = DT_PROP(node_id, buffer_block_size_max),	\
	}

#define IIO_DEVICE_STREAM_DT_GET(node_id) (&IIO_DEVICE_STREAM_DT_NAME(node_id))

#endif /* CONFIG_LIBIIO_STREAMING */

/**
 * @brief Convert a q31 value and shift to milli-units, saturating.
 *
 * @param value Decoded fixed-point value
 * @param shift Exponent reported by the decoder
 * @return Value in milli-units of its SI unit
 */
static inline int32_t iio_q31_to_milli(q31_t value, int8_t shift)
{
	int64_t scaled = (int64_t)value * 1000;
	int right_shift = 31 - shift;

	if (right_shift >= 63) {
		return 0;
	} else if (right_shift > 0) {
		int64_t half = INT64_C(1) << (right_shift - 1);

		scaled = scaled < 0 ? -((half - scaled) >> right_shift)
				    : (scaled + half) >> right_shift;
	} else if (scaled != 0) {
		/* |scaled| < 2^41 */
		if (-right_shift > 22) {
			return scaled < 0 ? INT32_MIN : INT32_MAX;
		}

		scaled *= INT64_C(1) << -right_shift;
	}

	return (int32_t)CLAMP(scaled, INT32_MIN, INT32_MAX);
}

typedef int (*iio_device_add_channels_t)(const struct device *dev,
		struct iio_device *iio_device);

typedef int (*iio_device_read_attr_t)(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len);

typedef int (*iio_device_write_attr_t)(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len);

#ifdef CONFIG_LIBIIO_STREAMING

/**
 * @brief Get the device's streaming resources, or NULL if it cannot stream.
 *
 * Drivers that implement this must implement submit_read, frame_count and decode.
 */
typedef struct iio_device_stream *(*iio_device_get_stream_t)(const struct device *dev);

/** @brief Submit one non-blocking capture into the device's RTIO context. */
typedef int (*iio_device_submit_read_t)(const struct device *dev);

/**
 * @brief Arm a multishot capture paced by the device's own trigger.
 *
 * @param handle Set to the submission; cancel it with rtio_sqe_cancel()
 * @return 0 on success, -ENOSYS if the device has no trigger
 */
typedef int (*iio_device_arm_stream_t)(const struct device *dev, rtio_sqe_handle_t *handle);

/** @brief Get the number of frames for @p chn in a completed RTIO buffer. */
typedef int (*iio_device_frame_count_t)(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint16_t *count);

/**
 * @brief Decode one frame of @p chn from a completed RTIO buffer.
 *
 * @param fit Frame iterator, advanced on success
 * @return 1 if a frame was decoded, 0 if none remain, or a negative errno
 */
typedef int (*iio_device_decode_t)(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint32_t *fit, q31_t *value, int8_t *shift);

#endif /* CONFIG_LIBIIO_STREAMING */

__subsystem struct iio_device_driver_api {
	iio_device_add_channels_t add_channels;
	iio_device_read_attr_t read_attr;
	iio_device_write_attr_t write_attr;
#ifdef CONFIG_LIBIIO_STREAMING
	iio_device_get_stream_t get_stream;
	iio_device_submit_read_t submit_read;
	iio_device_arm_stream_t arm_stream;
	iio_device_frame_count_t frame_count;
	iio_device_decode_t decode;
#endif
};

__syscall int iio_device_add_channels(const struct device *dev,
		struct iio_device *iio_device);

static inline int z_impl_iio_device_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	return api->add_channels(dev, iio_device);
}

__syscall int iio_device_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len);

static inline int z_impl_iio_device_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	if (api->read_attr == NULL) {
		return -ENOSYS;
	}

	return api->read_attr(dev, iio_device, attr, dst, len);
}

__syscall int iio_device_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len);

static inline int z_impl_iio_device_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	if (api->write_attr == NULL) {
		return -ENOSYS;
	}

	return api->write_attr(dev, iio_device, attr, src, len);
}

#ifdef CONFIG_LIBIIO_STREAMING

static inline struct iio_device_stream *iio_device_get_stream(const struct device *dev)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	return api->get_stream ? api->get_stream(dev) : NULL;
}

static inline int iio_device_submit_read(const struct device *dev)
{
	return DEVICE_API_GET(iio_device, dev)->submit_read(dev);
}

static inline int iio_device_arm_stream(const struct device *dev, rtio_sqe_handle_t *handle)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	return api->arm_stream ? api->arm_stream(dev, handle) : -ENOSYS;
}

static inline int iio_device_frame_count(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint16_t *count)
{
	return DEVICE_API_GET(iio_device, dev)->frame_count(dev, buf, chn, count);
}

static inline int iio_device_decode(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint32_t *fit, q31_t *value, int8_t *shift)
{
	return DEVICE_API_GET(iio_device, dev)->decode(dev, buf, chn, fit, value, shift);
}

#endif /* CONFIG_LIBIIO_STREAMING */

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/iio_device.h>

#endif  /* ZEPHYR_INCLUDE_IIO_DEVICE_H_ */
