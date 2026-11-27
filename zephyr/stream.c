/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#include <iio_stream.h>

LOG_MODULE_REGISTER(iio_stream, CONFIG_LIBIIO_LOG_LEVEL);

/** Runtime state of one open buffer. */
struct iio_buffer_pdata {
	/** libiio's view of the device this buffer belongs to. */
	const struct iio_device *iio_dev;
	/** Zephyr device backing it. */
	const struct device *dev;
	/** Streaming resources of the device; holds their in_use flag. */
	struct iio_device_stream *stream;
	/** The device's RTIO context. */
	struct rtio *rtio;
	/** Serializes SQE acquire..submit; RTIO is single-producer. */
	struct k_mutex lock;
	/** Enabled channels and their place in a sample. */
	struct iio_scan_plan plan;
	/** Paced submission, or RTIO_SQE_HANDLE_INVALID when not armed. */
	rtio_sqe_handle_t stream_handle;
	/** Partly decoded capture, still in the RTIO mempool. */
	uint8_t *held;
	/** Mempool length of @ref held. */
	uint32_t held_len;
	/** Frames of @ref held not yet decoded. */
	uint16_t held_frames;
	/** Per-channel frame iterators into @ref held. */
	uint32_t fit[CONFIG_LIBIIO_MAX_SCAN_ELEMENTS];
	/** Stream ended on a full mempool and has not captured since. */
	bool overflowed;
	/** Set on cancel or disable, cleared on enable. */
	bool cancelled;
	/** A dequeue owns the CQ and @ref held; a stop waits on @ref idle. */
	bool dequeue_active;
	/** Signalled when @ref dequeue_active clears. */
	struct k_condvar idle;
};

/** Runtime state of one block; the payload follows it in the slab block. */
struct iio_block_pdata {
	/** Buffer that owns this block. */
	struct iio_buffer_pdata *buf;
	/** Usable payload size, as requested by the client. */
	size_t size;
	/** Enqueued and not yet dequeued. */
	bool enqueued;
};

BUILD_ASSERT(sizeof(struct iio_block_pdata) <= IIO_DEVICE_BLOCK_HEADER_SIZE);

static inline uint8_t *iio_block_payload(struct iio_block_pdata *block)
{
	return (uint8_t *)block + IIO_DEVICE_BLOCK_HEADER_SIZE;
}

static unsigned int iio_stream_drain_cq(struct iio_buffer_pdata *buf);
static void iio_stream_disarm(struct iio_buffer_pdata *buf);

/* Open buffers across all devices */
K_MEM_SLAB_DEFINE_STATIC(iio_buffer_slab, ROUND_UP(sizeof(struct iio_buffer_pdata),
						  sizeof(void *)),
			 CONFIG_LIBIIO_BUFFER_COUNT, sizeof(void *));

struct iio_buffer_pdata *iio_stream_buffer_open(const struct iio_device *iio_dev,
						unsigned int idx,
						struct iio_channels_mask *mask)
{
	const struct device *dev = (const struct device *)iio_device_get_pdata(iio_dev);
	const struct iio_buffer *buf_desc = iio_device_get_buffer(iio_dev, idx);
	struct iio_device_stream *stream = iio_device_get_stream(dev);
	struct iio_buffer_pdata *buf;
	int ret;

	if (!buf_desc) {
		return iio_ptr(-ENODEV);
	}

	/* Output buffers are not implemented. */
	if (iio_buffer_is_output(buf_desc) || !stream) {
		return iio_ptr(-ENOSYS);
	}

	if (!atomic_cas(&stream->in_use, 0, 1)) {
		LOG_ERR("%s already has a buffer open", dev->name);
		return iio_ptr(-EBUSY);
	}

	ret = k_mem_slab_alloc(&iio_buffer_slab, (void **)&buf, K_NO_WAIT);
	if (ret) {
		LOG_ERR("No buffer descriptor free (%u in use of %u)",
			k_mem_slab_num_used_get(&iio_buffer_slab), CONFIG_LIBIIO_BUFFER_COUNT);
		atomic_clear(&stream->in_use);
		return iio_ptr(-ENOMEM);
	}

	memset(buf, 0, sizeof(*buf));
	buf->iio_dev = iio_dev;
	buf->dev = dev;
	buf->stream = stream;
	buf->rtio = stream->rtio;
	buf->stream_handle = RTIO_SQE_HANDLE_INVALID;
	k_mutex_init(&buf->lock);
	k_condvar_init(&buf->idle);

	/* May narrow @mask */
	ret = iio_stream_build_scan_plan(&buf->plan, iio_dev, mask);
	if (ret) {
		iio_stream_buffer_close(buf);
		return iio_ptr(ret);
	}

	return buf;
}

/* Caller holds buf->lock and has set buf->cancelled. */
static void iio_stream_wait_idle(struct iio_buffer_pdata *buf)
{
	while (buf->dequeue_active) {
		k_condvar_wait(&buf->idle, &buf->lock, K_FOREVER);
	}
}

void iio_stream_buffer_close(struct iio_buffer_pdata *buf)
{
	/* The RTIO context outlives the buffer, so leave it idle and empty. */
	k_mutex_lock(&buf->lock, K_FOREVER);
	buf->cancelled = true;
	iio_stream_wait_idle(buf);
	iio_stream_disarm(buf);
	iio_stream_drain_cq(buf);
	k_mutex_unlock(&buf->lock);

	atomic_clear(&buf->stream->in_use);
	k_mem_slab_free(&iio_buffer_slab, (void *)buf);
}

struct iio_block_pdata *iio_stream_block_alloc(struct iio_buffer_pdata *buf, size_t size,
					       void **data)
{
	struct iio_device_stream *stream = buf->stream;
	struct iio_block_pdata *block;
	int ret;

	if (size > stream->block_size_max) {
		LOG_ERR("%s: block of %zu bytes rejected, max is %u", buf->dev->name, size,
			stream->block_size_max);
		return iio_ptr(-ENOMEM);
	}

	ret = k_mem_slab_alloc(stream->block_slab, (void **)&block, K_NO_WAIT);
	if (ret) {
		LOG_ERR("%s: no block free (%u in use of %u)", buf->dev->name,
			k_mem_slab_num_used_get(stream->block_slab),
			stream->block_slab->info.num_blocks);
		return iio_ptr(-ENOMEM);
	}

	memset(block, 0, sizeof(*block));
	block->buf = buf;
	block->size = size;

	*data = iio_block_payload(block);

	return block;
}

void iio_stream_block_free(struct iio_block_pdata *block)
{
	k_mem_slab_free(block->buf->stream->block_slab, (void *)block);
}

/* Must agree with libiio's iio_block_first(). */
static ssize_t iio_stream_chan_offset(const struct iio_device *iio_dev,
				      const struct iio_channels_mask *mask,
				      const struct iio_channel *chn)
{
	unsigned int i, nb_channels = iio_device_get_channels_count(iio_dev);
	long target = iio_channel_get_index(chn);
	const struct iio_data_format *fmt;
	size_t offset = 0, len;

	if (!iio_channel_is_enabled(chn, mask)) {
		return -EINVAL;
	}

	for (i = 0; i < nb_channels; i++) {
		const struct iio_channel *cur = iio_device_get_channel(iio_dev, i);
		long cur_index = iio_channel_get_index(cur);

		fmt = iio_channel_get_data_format(cur);
		len = fmt->length / 8 * fmt->repeat;

		if (cur_index < 0 || cur_index == target) {
			break;
		}

		if (!iio_channel_is_enabled(cur, mask)) {
			continue;
		}

		/* Channels sharing an index share their samples. */
		if (i > 0 &&
		    cur_index == iio_channel_get_index(iio_device_get_channel(iio_dev, i - 1))) {
			continue;
		}

		if (offset % len) {
			offset += len - (offset % len);
		}
		offset += len;
	}

	fmt = iio_channel_get_data_format(chn);
	len = fmt->length / 8;
	if (offset % len) {
		offset += len - (offset % len);
	}

	return (ssize_t)offset;
}

int iio_stream_build_scan_plan(struct iio_scan_plan *plan, const struct iio_device *iio_dev,
			       const struct iio_channels_mask *mask)
{
	unsigned int i, nb_channels = iio_device_get_channels_count(iio_dev);
	ssize_t sample_size, offset;

	plan->nb_scans = 0;

	for (i = 0; i < nb_channels; i++) {
		const struct iio_channel *chn = iio_device_get_channel(iio_dev, i);
		const struct iio_data_format *fmt;
		struct iio_scan_entry *entry;

		if (!iio_channel_is_enabled(chn, mask)) {
			continue;
		}

		if (plan->nb_scans == CONFIG_LIBIIO_MAX_SCAN_ELEMENTS) {
			LOG_ERR("Mask enables more than %d channels",
				CONFIG_LIBIIO_MAX_SCAN_ELEMENTS);
			return -E2BIG;
		}

		offset = iio_stream_chan_offset(iio_dev, mask, chn);
		if (offset < 0) {
			return (int)offset;
		}

		fmt = iio_channel_get_data_format(chn);

		if (fmt->shift != 0) {
			LOG_ERR("Channel %s declares shift %d, which is not implemented",
				iio_channel_get_id(chn), fmt->shift);
			return -ENOTSUP;
		}

		entry = &plan->scan[plan->nb_scans++];
		entry->chan = (uint16_t)i;
		entry->offset = (uint16_t)offset;
		entry->bytes = (uint8_t)(fmt->length / 8);
		entry->is_be = fmt->is_be;
		entry->is_signed = fmt->is_signed;
	}

	if (plan->nb_scans == 0) {
		LOG_ERR("Mask enables no channels");
		return -EINVAL;
	}

	sample_size = iio_device_get_sample_size(iio_dev, mask);
	if (sample_size <= 0) {
		LOG_ERR("Bad sample size %zd", sample_size);
		return -EINVAL;
	}

	plan->sample_size = (size_t)sample_size;

	return 0;
}

void iio_stream_pack(uint8_t *sample, const struct iio_scan_entry *entry, q31_t value,
		     int8_t shift)
{
	unsigned int bits = entry->bytes * 8U;
	uint8_t *dst = sample + entry->offset;
	int64_t v = iio_q31_to_milli(value, shift);
	int64_t min, max;
	uint64_t bytes_out;
	unsigned int i;

	/* Saturate to the field width */
	if (entry->is_signed) {
		max = ((int64_t)1 << (bits - 1U)) - 1;
		min = -((int64_t)1 << (bits - 1U));
	} else {
		max = ((int64_t)1 << bits) - 1;
		min = 0;
	}

	if (v > max) {
		v = max;
	} else if (v < min) {
		v = min;
	}

	bytes_out = (uint64_t)v & (bits >= 64U ? UINT64_MAX : (((uint64_t)1 << bits) - 1U));

	for (i = 0; i < entry->bytes; i++) {
		unsigned int b = entry->is_be ? (entry->bytes - 1U - i) : i;

		dst[i] = (uint8_t)(bytes_out >> (8U * b));
	}
}

/* Release the held capture, dropping any undecoded frames. */
static void iio_stream_release_held(struct iio_buffer_pdata *buf)
{
	if (!buf->held) {
		return;
	}

	if (buf->held_frames) {
		LOG_WRN("%s: dropping %u undecoded frame(s) with the capture",
			buf->dev->name, buf->held_frames);
	}

	rtio_release_buffer(buf->rtio, buf->held, buf->held_len);
	buf->held = NULL;
	buf->held_len = 0;
	buf->held_frames = 0;
}

/* Discard pending completions; returns how many there were. */
static unsigned int iio_stream_drain_cq(struct iio_buffer_pdata *buf)
{
	unsigned int count = 0;
	struct rtio_cqe *cqe;

	iio_stream_release_held(buf);

	while ((cqe = rtio_cqe_consume(buf->rtio)) != NULL) {
		uint8_t *mem;
		uint32_t mem_len;

		if (rtio_cqe_get_mempool_buffer(buf->rtio, cqe, &mem, &mem_len) == 0) {
			rtio_release_buffer(buf->rtio, mem, mem_len);
		}

		rtio_cqe_release(buf->rtio, cqe);
		count++;
	}

	return count;
}

/* Wake a blocked dequeue so it sees buf->cancelled. */
static void iio_stream_wake_dequeue(struct iio_buffer_pdata *buf)
{
#ifdef CONFIG_RTIO_CONSUME_SEM
	k_sem_give(buf->rtio->consume_sem);
#else
	ARG_UNUSED(buf);
#endif
}

#define IIO_STREAM_DISARM_POLL_MS 5

/*
 * Cancel a paced capture and wait for its terminal completion, which comes at
 * the device's next trigger. Caller holds buf->lock with no dequeue running.
 * A timeout only warns, since iio_buffer_stream_stop() treats errors as fatal.
 */
static void iio_stream_disarm(struct iio_buffer_pdata *buf)
{
	unsigned int waited;

	if (buf->stream_handle == RTIO_SQE_HANDLE_INVALID) {
		return;
	}

	/* Drop stale completions first; the next one is terminal. */
	iio_stream_drain_cq(buf);

	rtio_sqe_cancel(buf->rtio, buf->stream_handle);

	/* The executor frees the submission; never cancel it twice. */
	buf->stream_handle = RTIO_SQE_HANDLE_INVALID;

	for (waited = 0; waited < CONFIG_LIBIIO_STREAM_DISARM_TIMEOUT_MS;
	     waited += IIO_STREAM_DISARM_POLL_MS) {
		if (iio_stream_drain_cq(buf) > 0) {
			return;
		}

		k_msleep(IIO_STREAM_DISARM_POLL_MS);
	}

	LOG_WRN("%s did not release its stream within %u ms", buf->dev->name,
		CONFIG_LIBIIO_STREAM_DISARM_TIMEOUT_MS);
}

/* Arm a capture if not already armed. Caller holds buf->lock. */
static int iio_stream_arm(struct iio_buffer_pdata *buf)
{
	int ret;

	if (buf->stream_handle != RTIO_SQE_HANDLE_INVALID) {
		return 0;
	}

	/* -ENOSYS: no trigger, so blocks are filled by on-demand reads. */
	ret = iio_device_arm_stream(buf->dev, &buf->stream_handle);
	if (ret == -ENOSYS) {
		LOG_DBG("%s paces no capture of its own; reading on demand", buf->dev->name);
		buf->stream_handle = RTIO_SQE_HANDLE_INVALID;
		return 0;
	} else if (ret) {
		LOG_ERR("Could not arm a capture on %s: %d", buf->dev->name, ret);
		buf->stream_handle = RTIO_SQE_HANDLE_INVALID;
	}

	return ret;
}

int iio_stream_buffer_enable(struct iio_buffer_pdata *buf, size_t nb_samples, bool enable,
			     bool cyclic)
{
	int ret = 0;

	ARG_UNUSED(nb_samples);

	if (cyclic) {
		LOG_ERR("Cyclic buffers are not implemented");
		return -ENOSYS;
	}

	k_mutex_lock(&buf->lock, K_FOREVER);

	if (enable) {
		/* A running dequeue owns the CQ */
		if (!buf->dequeue_active) {
			iio_stream_drain_cq(buf);
		}
		buf->cancelled = false;
		ret = iio_stream_arm(buf);
	} else {
		/* iio_buffer_stream_stop() disables without cancelling. */
		buf->cancelled = true;
		iio_stream_wake_dequeue(buf);
		iio_stream_wait_idle(buf);
		iio_stream_disarm(buf);
	}

	k_mutex_unlock(&buf->lock);

	return ret;
}

void iio_stream_buffer_cancel(struct iio_buffer_pdata *buf)
{
	k_mutex_lock(&buf->lock, K_FOREVER);
	buf->cancelled = true;
	iio_stream_wake_dequeue(buf);
	iio_stream_wait_idle(buf);
	iio_stream_disarm(buf);
	k_mutex_unlock(&buf->lock);
}

int iio_stream_block_enqueue(struct iio_block_pdata *block, size_t bytes_used, bool cyclic)
{
	struct iio_buffer_pdata *buf = block->buf;
	int ret = 0;

	ARG_UNUSED(bytes_used);

	if (cyclic) {
		LOG_ERR("Cyclic blocks are not implemented");
		return -ENOSYS;
	}

	/* The core enqueues all blocks before enabling the buffer. */
	k_mutex_lock(&buf->lock, K_FOREVER);

	if (block->enqueued) {
		/* The core does not check for this before calling us. */
		LOG_ERR("Block already enqueued");
		ret = -EPERM;
	} else {
		block->enqueued = true;
	}

	k_mutex_unlock(&buf->lock);

	return ret;
}

/* Samples in one capture: the fewest frames of any enabled channel. */
static int iio_stream_capture_frames(struct iio_buffer_pdata *buf, const uint8_t *capture,
				     uint16_t *frames)
{
	uint16_t least = UINT16_MAX;
	unsigned int i;

	for (i = 0; i < buf->plan.nb_scans; i++) {
		const struct iio_channel *chn =
			iio_device_get_channel(buf->iio_dev, buf->plan.scan[i].chan);
		uint16_t count = 0;
		int ret;

		ret = iio_device_frame_count(buf->dev, capture, chn, &count);
		if (ret) {
			LOG_ERR("No frame count for channel %s: %d", iio_channel_get_id(chn),
				ret);
			return ret;
		}

		least = MIN(least, count);
	}

	*frames = least;

	return 0;
}

/*
 * Decode up to @p budget frames of the held capture into IIO samples.
 * Returns samples added, or a negative errno.
 */
static int iio_stream_decode_held(struct iio_buffer_pdata *buf, uint8_t *block_data,
				  size_t first, size_t budget)
{
	size_t filled = first;
	unsigned int i;

	while (filled < first + budget) {
		uint8_t *sample = block_data + filled * buf->plan.sample_size;

		for (i = 0; i < buf->plan.nb_scans; i++) {
			const struct iio_scan_entry *entry = &buf->plan.scan[i];
			const struct iio_channel *chn =
				iio_device_get_channel(buf->iio_dev, entry->chan);
			q31_t value;
			int8_t shift;
			int ret;

			ret = iio_device_decode(buf->dev, buf->held, chn, &buf->fit[i], &value,
						&shift);
			if (ret < 0) {
				LOG_ERR("Decode failed for channel %s: %d",
					iio_channel_get_id(chn), ret);
				return ret;
			}

			if (ret == 0) {
				/* A partial sample is not counted. */
				LOG_WRN("Channel %s ran out of frames early",
					iio_channel_get_id(chn));
				buf->held_frames = 0;
				return (int)(filled - first);
			}

			iio_stream_pack(sample, entry, value, shift);
		}

		filled++;
	}

	return (int)(filled - first);
}

#define IIO_STREAM_CAPTURE_POLL_MS 5

/* Wait for the next completion. Returns 0, -ECANCELED or -ETIMEDOUT. */
static int iio_stream_wait_cqe(struct iio_buffer_pdata *buf, struct rtio_cqe **cqe)
{
	k_timepoint_t end = sys_timepoint_calc(K_MSEC(CONFIG_LIBIIO_STREAM_CAPTURE_TIMEOUT_MS));

	while (true) {
		*cqe = rtio_cqe_consume(buf->rtio);
		if (*cqe) {
			return 0;
		}

		if (buf->cancelled) {
			return -ECANCELED;
		}

#ifdef CONFIG_RTIO_CONSUME_SEM
		if (k_sem_take(buf->rtio->consume_sem, sys_timepoint_timeout(end))) {
			break;
		}

		/* Only waited on; rtio_cqe_consume() takes it. */
		k_sem_give(buf->rtio->consume_sem);
#else
		if (sys_timepoint_expired(end)) {
			break;
		}

		k_msleep(IIO_STREAM_CAPTURE_POLL_MS);
#endif
	}

	LOG_ERR("%s produced no capture within %u ms", buf->dev->name,
		CONFIG_LIBIIO_STREAM_CAPTURE_TIMEOUT_MS);

	return -ETIMEDOUT;
}

/* Take the next completion and hold it for decoding. */
static int iio_stream_take_capture(struct iio_buffer_pdata *buf)
{
	struct rtio_cqe *cqe;
	uint16_t frames = 0;
	int ret;

	ret = iio_stream_wait_cqe(buf, &cqe);
	if (ret) {
		return ret;
	}

	ret = cqe->result;
	if (ret < 0) {
		bool streaming = buf->stream_handle != RTIO_SQE_HANDLE_INVALID;

		/* Terminal for a multishot; the executor freed the SQE. */
		buf->stream_handle = RTIO_SQE_HANDLE_INVALID;
		rtio_cqe_release(buf->rtio, cqe);

		/* Mempool full: re-arm once; twice in a row means it cannot fit. */
		if (ret == -ENOMEM && streaming && !buf->overflowed) {
			LOG_WRN("%s: RTIO mempool full, samples dropped; re-arming",
				buf->dev->name);
			buf->overflowed = true;
			return -EAGAIN;
		}

		LOG_ERR("Capture on %s failed: %d", buf->dev->name, ret);
		return ret;
	}

	ret = rtio_cqe_get_mempool_buffer(buf->rtio, cqe, &buf->held, &buf->held_len);
	rtio_cqe_release(buf->rtio, cqe);
	if (ret) {
		LOG_ERR("Completion carried no capture buffer: %d", ret);
		buf->held = NULL;
		return ret;
	}

	memset(buf->fit, 0, sizeof(buf->fit));

	ret = iio_stream_capture_frames(buf, buf->held, &frames);
	if (ret) {
		iio_stream_release_held(buf);
		return ret;
	}

	if (frames == 0) {
		LOG_ERR("%s produced a capture with no frames", buf->dev->name);
		iio_stream_release_held(buf);
		return -ENODATA;
	}

	buf->held_frames = frames;
	buf->overflowed = false;

	return 0;
}

/* Decode as much of the held capture as the block still has room for. */
static int iio_stream_consume_held(struct iio_buffer_pdata *buf, uint8_t *block_data,
				   size_t *filled, size_t total)
{
	size_t budget = MIN((size_t)buf->held_frames, total - *filled);
	int ret;

	ret = iio_stream_decode_held(buf, block_data, *filled, budget);
	if (ret < 0) {
		iio_stream_release_held(buf);
		return ret;
	}

	*filled += (size_t)ret;
	buf->held_frames -= (uint16_t)ret;

	if (buf->held_frames == 0) {
		iio_stream_release_held(buf);
	}

	return 0;
}

int iio_stream_block_dequeue(struct iio_block_pdata *block, bool nonblock)
{
	struct iio_buffer_pdata *buf = block->buf;
	uint8_t *data = iio_block_payload(block);
	size_t total, filled = 0;
	int ret;

	k_mutex_lock(&buf->lock, K_FOREVER);
	if (!block->enqueued) {
		k_mutex_unlock(&buf->lock);
		return -EPERM;
	}

	/* A block is filled completely or not at all. */
	if (nonblock) {
		k_mutex_unlock(&buf->lock);
		return -EBUSY;
	}

	buf->dequeue_active = true;
	k_mutex_unlock(&buf->lock);

	total = buf->plan.sample_size ? block->size / buf->plan.sample_size : 0;
	if (total == 0) {
		LOG_ERR("Block of %zu bytes holds no whole sample of %zu", block->size,
			buf->plan.sample_size);
		ret = -EINVAL;
		goto out;
	}

	/* The whole block goes on the wire; don't leak a stale tail. */
	memset(data + total * buf->plan.sample_size, 0,
	       block->size - total * buf->plan.sample_size);

	while (filled < total) {
		if (buf->cancelled) {
			ret = -ECANCELED;
			goto out;
		}

		if (buf->held_frames == 0) {
			/* Arm here, not at open: unread captures fill the mempool */
			if (buf->stream_handle == RTIO_SQE_HANDLE_INVALID && !buf->cancelled) {
				k_mutex_lock(&buf->lock, K_FOREVER);
				(void)iio_stream_arm(buf);
				k_mutex_unlock(&buf->lock);
			}

			if (buf->stream_handle == RTIO_SQE_HANDLE_INVALID) {
				if (filled == 0) {
					LOG_DBG("%s: filling this block by reading on demand",
						buf->dev->name);
				}

				k_mutex_lock(&buf->lock, K_FOREVER);
				ret = iio_device_submit_read(buf->dev);
				k_mutex_unlock(&buf->lock);

				if (ret) {
					LOG_ERR("Could not submit a read to %s: %d",
						buf->dev->name, ret);
					goto out;
				}
			}

			ret = iio_stream_take_capture(buf);
			if (ret == -EAGAIN) {
				continue;
			} else if (ret) {
				goto out;
			}
		}

		ret = iio_stream_consume_held(buf, data, &filled, total);
		if (ret) {
			goto out;
		}
	}

	ret = 0;
out:
	k_mutex_lock(&buf->lock, K_FOREVER);
	block->enqueued = false;
	buf->dequeue_active = false;
	k_condvar_broadcast(&buf->idle);
	k_mutex_unlock(&buf->lock);

	return ret;
}
