/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZEPHYR_INCLUDE_IIO_STREAM_H_
#define ZEPHYR_INCLUDE_IIO_STREAM_H_

#include <zephyr/kernel.h>
#include <zephyr/dsp/types.h>
#include <iio/iio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_buffer_pdata;
struct iio_block_pdata;

/**
 * @name Backend buffer and block ops
 * @{
 */

/**
 * @brief Open buffer @p idx of @p iio_dev, possibly narrowing @p mask.
 *
 * @return the buffer, or an error pointer (-ENOSYS, -EBUSY, -ENOMEM)
 */
struct iio_buffer_pdata *iio_stream_buffer_open(const struct iio_device *iio_dev,
						unsigned int idx,
						struct iio_channels_mask *mask);

/** @brief Disarm @p buf and release it. */
void iio_stream_buffer_close(struct iio_buffer_pdata *buf);

/**
 * @brief Arm or disarm capture for @p buf.
 *
 * Disarming also wakes a blocked dequeue.
 *
 * @retval -ENOSYS @p cyclic is set
 */
int iio_stream_buffer_enable(struct iio_buffer_pdata *buf, size_t nb_samples, bool enable,
			     bool cyclic);

/** @brief Wake a blocked dequeue and stop further capture. */
void iio_stream_buffer_cancel(struct iio_buffer_pdata *buf);

/**
 * @brief Claim a block and its payload from @p buf's device slab.
 *
 * @param data set to the payload on success
 * @return the block, or an error pointer
 */
struct iio_block_pdata *iio_stream_block_alloc(struct iio_buffer_pdata *buf, size_t size,
					       void **data);

/** @brief Release a block claimed by iio_stream_block_alloc(). */
void iio_stream_block_free(struct iio_block_pdata *block);

/**
 * @brief Mark @p block as the next to fill; does no I/O.
 *
 * @retval -ENOSYS @p cyclic is set
 * @retval -EPERM @p block is already enqueued
 */
int iio_stream_block_enqueue(struct iio_block_pdata *block, size_t bytes_used, bool cyclic);

/**
 * @brief Capture into @p block until it is full.
 *
 * @retval -EPERM @p block was not enqueued
 * @retval -EBUSY @p nonblock is set
 * @retval -ECANCELED the buffer was cancelled
 * @retval -ETIMEDOUT no capture within LIBIIO_STREAM_CAPTURE_TIMEOUT_MS
 */
int iio_stream_block_dequeue(struct iio_block_pdata *block, bool nonblock);

/** @} */

/**
 * @name Sample layout, exposed for tests
 * @{
 */

/** @brief Location of one enabled channel within an IIO sample. */
struct iio_scan_entry {
	/** Channel position within the device's channel list. */
	uint16_t chan;
	/** Byte offset of this channel's sample within one IIO sample. */
	uint16_t offset;
	/** Bytes this channel occupies. */
	uint8_t bytes;
	/** Sample is big-endian on the wire. */
	bool is_be;
	/** Sample is signed. */
	bool is_signed;
};

/** @brief Enabled channels of a buffer and where each sits in a sample. */
struct iio_scan_plan {
	/** Bytes in one IIO sample across all enabled channels. */
	size_t sample_size;
	/** Entries used in @ref scan. */
	uint8_t nb_scans;
	/** Enabled channels, in device order. */
	struct iio_scan_entry scan[CONFIG_LIBIIO_MAX_SCAN_ELEMENTS];
};

/**
 * @brief Build the scan plan of @p iio_dev for @p mask.
 *
 * @retval -E2BIG @p mask enables more than LIBIIO_MAX_SCAN_ELEMENTS channels
 * @retval -ENOTSUP a channel declares a shift
 * @retval -EINVAL @p mask enables no channels
 */
int iio_stream_build_scan_plan(struct iio_scan_plan *plan, const struct iio_device *iio_dev,
			       const struct iio_channels_mask *mask);

/**
 * @brief Pack one channel's value, in clamped milli-units, into a sample.
 *
 * @param sample first byte of the sample within the block
 * @param shift exponent of @p value, as the decoder reported it
 */
void iio_stream_pack(uint8_t *sample, const struct iio_scan_entry *entry, q31_t value,
		     int8_t shift);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_IIO_STREAM_H_ */
