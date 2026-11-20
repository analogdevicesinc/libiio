/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#include <iio_stream.h>

#include "xyz_only_accel.h"

DEFINE_FFF_GLOBALS;

#define XYZ_IIO_NODE DT_PATH(iio_context, iio_sensors, xyz_only)

static const struct device *xyz_iio_dev = DEVICE_DT_GET(XYZ_IIO_NODE);

static const char *const axes[] = { "accel_x", "accel_y", "accel_z" };

/* Checked before each test, since ztest does not fail a test from its after phase. */
static void pools_drained(const struct ztest_unit_test *test, void *data)
{
	const struct device *devs[] = { xyz_iio_dev };

	ARG_UNUSED(data);

	ARRAY_FOR_EACH(devs, i) {
		struct iio_device_stream *stream = iio_device_get_stream(devs[i]);

		zassert_equal(atomic_get(&stream->in_use), 0, "%s was open before %s",
			      devs[i]->name, test->name);
		zassert_equal(k_mem_slab_num_used_get(stream->block_slab), 0,
			      "a block of %s was claimed before %s", devs[i]->name, test->name);
	}
}

ZTEST_RULE(libiio_stream_pools_drained, pools_drained, NULL);

/* A libiio context with the three axes of one device enabled. */
struct fixture {
	struct iio_context *ctx;
	const struct iio_device *dev;
	struct iio_buffer *buffer;
	struct iio_channels_mask *mask;
	const struct iio_channel *chan[ARRAY_SIZE(axes)];
	size_t sample_size;
	struct iio_buffer_stream *stream;
	struct iio_block *block;
};

static const struct iio_device *find_iio_dev(struct iio_context *ctx, const struct device *zdev)
{
	for (unsigned int i = 0; i < iio_context_get_devices_count(ctx); i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);

		if (iio_device_get_pdata(dev) == (struct iio_device_pdata *)zdev) {
			return dev;
		}
	}

	return NULL;
}

static void fixture_setup(struct fixture *f, const struct device *zdev)
{
	ssize_t size;

	*f = (struct fixture){0};

	f->ctx = iio_create_context(NULL, "zephyr:");
	zassert_equal(iio_err(f->ctx), 0, "context: %d", iio_err(f->ctx));

	f->dev = find_iio_dev(f->ctx, zdev);
	zassert_not_null(f->dev, "no iio device for %s", zdev->name);

	f->buffer = iio_device_get_buffer(f->dev, 0);
	zassert_not_null(f->buffer, "%s advertises no buffer", zdev->name);

	f->mask = iio_create_channels_mask(iio_device_get_channels_count(f->dev));
	zassert_not_null(f->mask, "mask alloc");

	ARRAY_FOR_EACH(axes, i) {
		f->chan[i] = iio_device_find_channel(f->dev, axes[i], false);
		zassert_not_null(f->chan[i], "no channel %s", axes[i]);
		iio_channel_enable(f->chan[i], f->mask);
	}

	size = iio_device_get_sample_size(f->dev, f->mask);
	zassert_true(size > 0, "sample size %zd", size);
	f->sample_size = (size_t)size;
}

/* Open a stream with one block of @p samples. */
static void fixture_open(struct fixture *f, size_t samples)
{
	f->stream = iio_buffer_open(f->buffer, f->mask);
	zassert_equal(iio_err(f->stream), 0, "buffer_open: %d", iio_err(f->stream));

	f->block = iio_buffer_stream_create_block(f->stream, samples * f->sample_size);
	zassert_equal(iio_err(f->block), 0, "create_block: %d", iio_err(f->block));
}

/* Also cleans up after a test that failed part way. */
static void fixture_close(struct fixture *f)
{
	if (!f->stream) {
		return;
	}

	(void)iio_buffer_stream_stop(f->stream);

	if (f->block && !iio_err(f->block)) {
		iio_block_destroy(f->block);
	}

	iio_buffer_close(f->stream);
	f->stream = NULL;
	f->block = NULL;
}

static void fixture_teardown(struct fixture *f)
{
	fixture_close(f);

	if (f->mask) {
		iio_channels_mask_destroy(f->mask);
	}

	if (f->ctx) {
		iio_context_destroy(f->ctx);
	}

	*f = (struct fixture){0};
}

/* Dequeue in a thread of its own, for tests that block it on purpose. */
static struct k_thread bg_thread;
static K_THREAD_STACK_DEFINE(bg_stack, 2048);
static K_SEM_DEFINE(bg_done, 0, 1);
static struct iio_block *bg_block;
static int bg_ret;

static void bg_dequeue_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	bg_ret = iio_block_dequeue(bg_block, false);
	k_sem_give(&bg_done);
}

static void bg_dequeue_start(struct iio_block *block)
{
	k_sem_reset(&bg_done);
	bg_block = block;
	k_thread_create(&bg_thread, bg_stack, K_THREAD_STACK_SIZEOF(bg_stack), bg_dequeue_fn,
			NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
}

/* Returns the dequeue's result, or -EAGAIN if it is still blocked. */
static int bg_dequeue_wait(k_timeout_t timeout)
{
	if (k_sem_take(&bg_done, timeout)) {
		return -EAGAIN;
	}

	k_thread_join(&bg_thread, K_FOREVER);

	return bg_ret;
}

/* Block and buffer budgets, through libiio's API. */

#define XYZ_BLOCK_COUNT    DT_PROP(XYZ_IIO_NODE, buffer_block_count)
#define XYZ_BLOCK_SIZE_MAX DT_PROP(XYZ_IIO_NODE, buffer_block_size_max)

static struct fixture pool;

static void *pool_setup(void)
{
	fixture_setup(&pool, xyz_iio_dev);

	return NULL;
}

static void pool_after(void *unused)
{
	ARG_UNUSED(unused);

	fixture_close(&pool);
}

static void pool_teardown(void *unused)
{
	ARG_UNUSED(unused);

	fixture_teardown(&pool);
}

ZTEST(libiio_stream_pool, test_blocks_are_bounded_by_the_slab)
{
	struct iio_block *blocks[XYZ_BLOCK_COUNT];
	struct iio_block *extra;

	pool.stream = iio_buffer_open(pool.buffer, pool.mask);
	zassert_equal(iio_err(pool.stream), 0, "buffer_open: %d", iio_err(pool.stream));

	ARRAY_FOR_EACH(blocks, i) {
		blocks[i] = iio_buffer_stream_create_block(pool.stream, XYZ_BLOCK_SIZE_MAX);
		zassert_equal(iio_err(blocks[i]), 0, "block %zu: %d", i, iio_err(blocks[i]));
	}

	extra = iio_buffer_stream_create_block(pool.stream, XYZ_BLOCK_SIZE_MAX);
	zassert_equal(iio_err(extra), -ENOMEM, "block past the budget: %d", iio_err(extra));

	ARRAY_FOR_EACH(blocks, i) {
		iio_block_destroy(blocks[i]);
	}
}

ZTEST(libiio_stream_pool, test_an_oversized_block_is_refused)
{
	pool.stream = iio_buffer_open(pool.buffer, pool.mask);
	zassert_equal(iio_err(pool.stream), 0, "buffer_open: %d", iio_err(pool.stream));

	pool.block = iio_buffer_stream_create_block(pool.stream, XYZ_BLOCK_SIZE_MAX + 1);
	zassert_equal(iio_err(pool.block), -ENOMEM, "got %d", iio_err(pool.block));
}

ZTEST_SUITE(libiio_stream_pool, NULL, pool_setup, NULL, pool_after, pool_teardown);

/*
 * The scan plan against libiio's iio_block_first(), on a synthetic device with
 * mixed channel widths and a shared index.
 */

#define SYNTH_SAMPLES 4

struct synth_chan {
	const char *id;
	long index;
	unsigned int bits;
};

/* voltage3 and voltage4 share an index */
static const struct synth_chan synth_chans[] = {
	{ "voltage0", 0, 8 },
	{ "voltage1", 1, 32 },
	{ "voltage2", 2, 16 },
	{ "voltage3", 3, 16 },
	{ "voltage4", 3, 16 },
	/* Enough channels to exceed LIBIIO_MAX_SCAN_ELEMENTS */
	{ "voltage5", 4, 16 },
	{ "voltage6", 5, 16 },
	{ "voltage7", 6, 16 },
	{ "voltage8", 7, 16 },
	{ "voltage9", 8, 16 },
};

static struct iio_context *synth_ctx;
static struct iio_device *synth_dev;
static struct iio_buffer *synth_buf;

static void *synth_setup(void)
{
	synth_ctx = iio_create_context(NULL, "zephyr:");
	zassert_equal(iio_err(synth_ctx), 0, "context: %d", iio_err(synth_ctx));

	/* Borrow a streaming device's pdata so the buffer can open */
	synth_dev = iio_context_add_device(synth_ctx, "iio:device9", "synthetic", NULL);
	zassert_equal(iio_err(synth_dev), 0, "add_device: %d", iio_err(synth_dev));
	iio_device_set_pdata(synth_dev, (struct iio_device_pdata *)xyz_iio_dev);

	ARRAY_FOR_EACH(synth_chans, i) {
		const struct iio_data_format fmt = {
			.length = synth_chans[i].bits,
			.bits = synth_chans[i].bits,
			.is_signed = true,
		};
		struct iio_channel *chn;

		chn = iio_device_add_channel(synth_dev, synth_chans[i].index, synth_chans[i].id,
					     NULL, NULL, false, true, &fmt);
		zassert_equal(iio_err(chn), 0, "add_channel %zu: %d", i, iio_err(chn));
	}

	synth_buf = iio_device_add_buffer(synth_dev, 0);
	zassert_not_null(synth_buf, "add_buffer failed");
	iio_buffer_set_direction(synth_buf, "in");

	for (unsigned int i = 0; i < iio_device_get_channels_count(synth_dev); i++) {
		zassert_ok(iio_buffer_add_scan_element(
				   synth_buf, iio_device_get_channel(synth_dev, i), NULL),
			   "add_scan_element %u", i);
	}

	return NULL;
}

static void synth_teardown(void *unused)
{
	ARG_UNUSED(unused);

	if (synth_ctx) {
		iio_context_destroy(synth_ctx);
		synth_ctx = NULL;
	}
}

static struct iio_channels_mask *synth_mask(uint32_t enabled_bits)
{
	unsigned int nb = iio_device_get_channels_count(synth_dev);
	struct iio_channels_mask *mask = iio_create_channels_mask(nb);

	zassert_not_null(mask, "mask alloc");

	for (unsigned int i = 0; i < nb; i++) {
		if (enabled_bits & BIT(i)) {
			iio_channel_enable(iio_device_get_channel(synth_dev, i), mask);
		}
	}

	return mask;
}

static void check_mask(uint32_t enabled_bits)
{
	struct iio_channels_mask *mask = synth_mask(enabled_bits);
	struct iio_buffer_stream *buf_stream;
	struct iio_scan_plan plan;
	struct iio_block *block;
	uintptr_t start;
	int ret;

	ret = iio_stream_build_scan_plan(&plan, synth_dev, mask);
	zassert_ok(ret, "mask 0x%x: build_scan_plan %d", enabled_bits, ret);
	zassert_equal(plan.sample_size, (size_t)iio_device_get_sample_size(synth_dev, mask),
		      "mask 0x%x: sample size", enabled_bits);
	zassert_equal(plan.nb_scans, __builtin_popcount(enabled_bits), "mask 0x%x: %u entries",
		      enabled_bits, plan.nb_scans);

	/* iio_block_first() only needs a block, not a filled one */
	buf_stream = iio_buffer_open(synth_buf, mask);
	zassert_equal(iio_err(buf_stream), 0, "mask 0x%x: buffer_open %d", enabled_bits,
		      iio_err(buf_stream));

	block = iio_buffer_stream_create_block(buf_stream, SYNTH_SAMPLES * plan.sample_size);
	zassert_equal(iio_err(block), 0, "mask 0x%x: create_block %d", enabled_bits,
		      iio_err(block));

	start = (uintptr_t)iio_block_start(block);

	for (unsigned int i = 0; i < plan.nb_scans; i++) {
		const struct iio_scan_entry *entry = &plan.scan[i];
		const struct iio_channel *chn = iio_device_get_channel(synth_dev, entry->chan);
		size_t theirs = (uintptr_t)iio_block_first(block, chn) - start;

		zassert_equal(entry->offset, theirs,
			      "mask 0x%x %s: offset %u, iio_block_first %zu", enabled_bits,
			      iio_channel_get_id(chn), entry->offset, theirs);
		zassert_equal(entry->bytes, synth_chans[entry->chan].bits / 8,
			      "mask 0x%x %s: width", enabled_bits, iio_channel_get_id(chn));
		zassert_true(entry->offset + entry->bytes <= plan.sample_size,
			     "mask 0x%x %s: runs past the sample", enabled_bits,
			     iio_channel_get_id(chn));
	}

	iio_block_destroy(block);
	iio_buffer_close(buf_stream);
	iio_channels_mask_destroy(mask);
}

ZTEST(libiio_stream_scan, test_the_plan_matches_iio_block_first)
{
	/* All, each alone, pairs around the 32-bit channel, the shared index */
	static const uint32_t masks[] = {
		0x1f, 0x01, 0x02, 0x04, 0x08, 0x10,
		0x03, 0x05, 0x06, 0x07, 0x0c, 0x14, 0x18, 0x1c,
	};

	ARRAY_FOR_EACH(masks, i) {
		check_mask(masks[i]);
	}
}

BUILD_ASSERT(ARRAY_SIZE(synth_chans) > CONFIG_LIBIIO_MAX_SCAN_ELEMENTS);

ZTEST(libiio_stream_scan, test_too_many_channels_refused)
{
	struct iio_channels_mask *mask = synth_mask(BIT_MASK(ARRAY_SIZE(synth_chans)));
	struct iio_scan_plan plan;

	int ret = iio_stream_build_scan_plan(&plan, synth_dev, mask);

	zassert_equal(ret, -E2BIG, "an oversized mask gave %d, %u channels", ret,
		      iio_device_get_channels_count(synth_dev));

	iio_channels_mask_destroy(mask);
}

ZTEST_SUITE(libiio_stream_scan, NULL, synth_setup, NULL, NULL, synth_teardown);

/* Sample packing and the milli-unit conversion */

static uint8_t packbuf[16];

static const struct iio_scan_entry *entry_of(uint8_t bytes, bool is_signed, bool is_be,
					     uint16_t offset)
{
	static struct iio_scan_entry e;

	e = (struct iio_scan_entry){
		.chan = 0, .offset = offset, .bytes = bytes,
		.is_be = is_be, .is_signed = is_signed,
	};

	return &e;
}

static uint64_t packed(uint8_t bytes, bool is_be)
{
	uint64_t v = 0;

	for (unsigned int i = 0; i < bytes; i++) {
		unsigned int b = is_be ? (bytes - 1U - i) : i;

		v |= (uint64_t)packbuf[i] << (8U * b);
	}

	return v;
}

static int64_t packed_signed(uint8_t bytes, bool is_be)
{
	unsigned int bits = bytes * 8U;
	uint64_t v = packed(bytes, is_be);

	if (bits < 64U && (v & (UINT64_C(1) << (bits - 1U)))) {
		v |= ~((UINT64_C(1) << bits) - 1U);
	}

	return (int64_t)v;
}

static int64_t pack_one(uint8_t bytes, bool is_signed, q31_t value, int8_t shift)
{
	memset(packbuf, 0, sizeof(packbuf));
	iio_stream_pack(packbuf, entry_of(bytes, is_signed, false, 0), value, shift);

	return is_signed ? packed_signed(bytes, false) : (int64_t)packed(bytes, false);
}

ZTEST(libiio_stream_pack, test_the_exponent_scales_the_reading)
{
	/* 2^28 at shift 7 is 16.0 */
	static const struct {
		int8_t shift;
		int32_t want;
	} cases[] = {
		{ 5, 4000 }, { 6, 8000 }, { 7, 16000 }, { 8, 32000 },
	};

	ARRAY_FOR_EACH(cases, i) {
		zassert_equal(pack_one(4, true, BIT(28), cases[i].shift), cases[i].want,
			      "shift %d gave %lld, expected %d", cases[i].shift,
			      (long long)pack_one(4, true, BIT(28), cases[i].shift),
			      cases[i].want);
	}
}

ZTEST(libiio_stream_pack, test_negative_readings_keep_their_sign)
{
	zassert_equal(pack_one(4, true, -BIT(28), 7), -16000, "got %lld",
		      (long long)pack_one(4, true, -BIT(28), 7));

	/* An unsigned field clamps a negative reading to zero */
	zassert_equal(pack_one(4, false, -BIT(28), 7), 0,
		      "a negative reading in an unsigned field gave %lld",
		      (long long)pack_one(4, false, -BIT(28), 7));
}

ZTEST(libiio_stream_pack, test_a_reading_past_the_field_saturates)
{
	/* +/-16 g in milli-m/s^2 fits 32 bits but not 16 */
	const q31_t full_scale = 157 * BIT(23); /* 157.0 at shift 8 */

	zassert_equal(pack_one(4, true, full_scale, 8), 157000, "32-bit gave %lld",
		      (long long)pack_one(4, true, full_scale, 8));
	zassert_equal(pack_one(2, true, full_scale, 8), INT16_MAX, "16-bit gave %lld",
		      (long long)pack_one(2, true, full_scale, 8));
	zassert_equal(pack_one(2, true, -full_scale, 8), INT16_MIN, "16-bit gave %lld",
		      (long long)pack_one(2, true, -full_scale, 8));
}

ZTEST(libiio_stream_pack, test_endianness)
{
	/* 0x12345663 at shift 21: four distinct, non-zero bytes */
	const q31_t value = 2443359 * BIT(7);

	memset(packbuf, 0, sizeof(packbuf));
	iio_stream_pack(packbuf, entry_of(4, true, false, 0), value, 21);
	zassert_equal(packbuf[0], 0x63, "LE byte 0");
	zassert_equal(packbuf[3], 0x12, "LE byte 3");

	memset(packbuf, 0, sizeof(packbuf));
	iio_stream_pack(packbuf, entry_of(4, true, true, 0), value, 21);
	zassert_equal(packbuf[0], 0x12, "BE byte 0");
	zassert_equal(packbuf[3], 0x63, "BE byte 3");
}

ZTEST(libiio_stream_pack, test_offset_is_honoured)
{
	/* 66 steps of 125 milli-units */
	memset(packbuf, 0xAA, sizeof(packbuf));
	iio_stream_pack(packbuf, entry_of(2, true, false, 4), 66 * BIT(21), 7);

	zassert_equal(packbuf[3], 0xAA, "wrote before the offset");
	zassert_equal(packbuf[4] | (packbuf[5] << 8), 8250, "offset field holds %d",
		      packbuf[4] | (packbuf[5] << 8));
	zassert_equal(packbuf[6], 0xAA, "wrote past the field");
}

ZTEST(libiio_stream_pack, test_the_conversion_rounds_half_away_from_zero)
{
	/* At shift 27 one q31 step is exactly 62.5 milli-units */
	zassert_equal(iio_q31_to_milli(1, 27), 63, "got %d", iio_q31_to_milli(1, 27));
	zassert_equal(iio_q31_to_milli(-1, 27), -63, "got %d", iio_q31_to_milli(-1, 27));
}

ZTEST(libiio_stream_pack, test_the_conversion_saturates_rather_than_wrapping)
{
	zassert_equal(iio_q31_to_milli(BIT(30), 31), INT32_MAX, "got %d",
		      iio_q31_to_milli(BIT(30), 31));
	zassert_equal(iio_q31_to_milli(-BIT(30), 31), INT32_MIN, "got %d",
		      iio_q31_to_milli(-BIT(30), 31));
	zassert_equal(iio_q31_to_milli(1, 127), INT32_MAX, "got %d",
		      iio_q31_to_milli(1, 127));
	zassert_equal(iio_q31_to_milli(INT32_MAX, -128), 0, "got %d",
		      iio_q31_to_milli(INT32_MAX, -128));
	zassert_equal(iio_q31_to_milli(0, 31), 0, "got %d", iio_q31_to_milli(0, 31));
	zassert_equal(iio_q31_to_milli(0, 7), 0, "got %d", iio_q31_to_milli(0, 7));
}

ZTEST_SUITE(libiio_stream_pack, NULL, NULL, NULL, NULL, NULL);

/* One-shot capture, against the xyz-only fixture's known values */

#define CAPTURE_SAMPLES 8

static struct fixture cap;

static void *cap_setup(void)
{
	fixture_setup(&cap, xyz_iio_dev);

	return NULL;
}

static void cap_teardown(void *unused)
{
	ARG_UNUSED(unused);

	fixture_teardown(&cap);
}

static void cap_before(void *unused)
{
	ARG_UNUSED(unused);

	zassert_false(xyz_only_capture_pending(), "an earlier test left a capture pending");
	fixture_open(&cap, CAPTURE_SAMPLES);
}

static void cap_after(void *unused)
{
	ARG_UNUSED(unused);

	xyz_only_flush();
	xyz_only_set_capture_delay(0);
	fixture_close(&cap);
}

ZTEST(libiio_stream_capture, test_capture_fills_a_block_with_known_values)
{
	int ret;

	zassert_ok(iio_block_enqueue(cap.block, 0, false), "enqueue");
	zassert_ok(iio_buffer_stream_start(cap.stream), "stream_start");

	ret = iio_block_dequeue(cap.block, false);
	zassert_ok(ret, "dequeue: %d", ret);

	/* Captures repeat the fixture's frames: sample s is frame s % 3 */
	for (unsigned int s = 0; s < CAPTURE_SAMPLES; s++) {
		unsigned int frame = s % XYZ_ONLY_FRAMES;

		ARRAY_FOR_EACH(axes, axis) {
			const uint8_t *at = (const uint8_t *)iio_block_first(cap.block,
									     cap.chan[axis]) +
					    s * cap.sample_size;
			int32_t got = (int32_t)sys_get_le32(at);
			int32_t want = XYZ_ONLY_MILLI(frame, axis);

			zassert_equal(got, want, "sample %u %s: got %d, expected %d (frame %u)",
				      s, axes[axis], got, want, frame);
		}
	}
}

ZTEST(libiio_stream_capture, test_cyclic_is_refused)
{
	zassert_equal(iio_block_enqueue(cap.block, 0, true), -ENOSYS,
		      "a cyclic enqueue should be refused");
}

ZTEST(libiio_stream_capture, test_double_enqueue_is_refused)
{
	zassert_ok(iio_block_enqueue(cap.block, 0, false), "first enqueue");
	zassert_equal(iio_block_enqueue(cap.block, 0, false), -EPERM,
		      "a second enqueue of the same block should be refused");

	zassert_ok(iio_buffer_stream_start(cap.stream), "stream_start");
	zassert_ok(iio_block_dequeue(cap.block, false), "dequeue");
}

ZTEST(libiio_stream_capture, test_dequeue_without_enqueue_is_refused)
{
	zassert_equal(iio_block_dequeue(cap.block, false), -EPERM,
		      "dequeuing a block that was never enqueued should be refused");
}

ZTEST(libiio_stream_capture, test_nonblocking_dequeue_reports_busy)
{
	int ret;

	zassert_ok(iio_block_enqueue(cap.block, 0, false), "enqueue");
	zassert_ok(iio_buffer_stream_start(cap.stream), "stream_start");

	zassert_equal(iio_block_dequeue(cap.block, true), -EBUSY,
		      "a non-blocking dequeue should report not-ready");

	/* -EBUSY must not consume the enqueue */
	ret = iio_block_dequeue(cap.block, false);
	zassert_ok(ret, "blocking dequeue after -EBUSY: %d", ret);
}

/* Far beyond the capture timeout, so only a cancel can end the dequeue */
#define CANCEL_DELAY_MS 10000

BUILD_ASSERT(CONFIG_LIBIIO_STREAM_CAPTURE_TIMEOUT_MS < CANCEL_DELAY_MS);

ZTEST(libiio_stream_capture, test_cancel_wakes_a_blocked_dequeue)
{
	int ret;

	xyz_only_set_capture_delay(CANCEL_DELAY_MS);

	zassert_ok(iio_block_enqueue(cap.block, 0, false), "enqueue");
	zassert_ok(iio_buffer_stream_start(cap.stream), "stream_start");

	bg_dequeue_start(cap.block);

	zassert_ok(xyz_only_await_submit(K_SECONDS(1)), "the dequeue submitted no capture");
	zassert_equal(bg_dequeue_wait(K_NO_WAIT), -EAGAIN,
		      "the dequeue returned before anything cancelled it");

	iio_buffer_stream_cancel(cap.stream);

	ret = bg_dequeue_wait(K_SECONDS(1));
	zassert_equal(ret, -ECANCELED, "dequeue returned %d, expected -ECANCELED", ret);
}

ZTEST(libiio_stream_capture, test_missing_capture_times_out)
{
	xyz_only_set_capture_delay(CANCEL_DELAY_MS);

	zassert_ok(iio_block_enqueue(cap.block, 0, false), "enqueue");
	zassert_ok(iio_buffer_stream_start(cap.stream), "stream_start");
	zassert_equal(iio_block_dequeue(cap.block, false), -ETIMEDOUT);
}

ZTEST_SUITE(libiio_stream_capture, NULL, cap_setup, cap_before, cap_after, cap_teardown);
