/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Test-only sensor whose decoder answers only for SENSOR_CHAN_ACCEL_XYZ, as
 * some real drivers do. It captures no data; the decoder returns the same
 * three known frames every time.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>

#include "xyz_only_accel.h"

#define DT_DRV_COMPAT vnd_xyz_only_accel

static int xyz_only_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec spec,
				    uint16_t *frame_count)
{
	ARG_UNUSED(buffer);

	if (spec.chan_type != SENSOR_CHAN_ACCEL_XYZ || spec.chan_idx != 0) {
		return -ENOTSUP;
	}

	*frame_count = XYZ_ONLY_FRAMES;

	return 0;
}

static int xyz_only_get_size_info(struct sensor_chan_spec spec, size_t *base_size,
				  size_t *frame_size)
{
	if (spec.chan_type != SENSOR_CHAN_ACCEL_XYZ) {
		return -ENOTSUP;
	}

	*base_size = sizeof(struct sensor_three_axis_data);
	*frame_size = sizeof(struct sensor_three_axis_sample_data);

	return 0;
}

static int xyz_only_decode(const uint8_t *buffer, struct sensor_chan_spec spec, uint32_t *fit,
			   uint16_t max_count, void *data_out)
{
	struct sensor_three_axis_data *out = data_out;

	ARG_UNUSED(buffer);

	if (max_count == 0) {
		return -EINVAL;
	}

	if (spec.chan_type != SENSOR_CHAN_ACCEL_XYZ || spec.chan_idx != 0) {
		return -ENOTSUP;
	}

	/* Like the generic RTIO decoder, an exhausted iterator is -EINVAL. */
	if (*fit >= XYZ_ONLY_FRAMES) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->shift = XYZ_ONLY_SHIFT;
	out->header.reading_count = 1;
	out->readings[0].x = XYZ_ONLY_XYZ(*fit, 0);
	out->readings[0].y = XYZ_ONLY_XYZ(*fit, 1);
	out->readings[0].z = XYZ_ONLY_XYZ(*fit, 2);

	(*fit)++;

	return 1;
}

static bool xyz_only_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(trigger);

	return false;
}

static const struct sensor_decoder_api xyz_only_decoder = {
	.get_frame_count = xyz_only_get_frame_count,
	.get_size_info = xyz_only_get_size_info,
	.decode = xyz_only_decode,
	.has_trigger = xyz_only_has_trigger,
};

static int xyz_only_get_decoder(const struct device *dev,
				const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);

	*decoder = &xyz_only_decoder;

	return 0;
}

/* Non-zero defers completion to the work queue. */
static uint32_t xyz_only_delay_ms;
static struct rtio_iodev_sqe *xyz_only_pending;
static struct k_work_delayable xyz_only_dwork;
static K_SEM_DEFINE(xyz_only_submitted, 0, 1);

int xyz_only_await_submit(k_timeout_t timeout)
{
	return k_sem_take(&xyz_only_submitted, timeout);
}

void xyz_only_set_capture_delay(uint32_t ms)
{
	xyz_only_delay_ms = ms;
}

bool xyz_only_capture_pending(void)
{
	return xyz_only_pending != NULL;
}

static void xyz_only_complete(struct rtio_iodev_sqe *iodev_sqe)
{
	uint8_t *buf;
	uint32_t buf_len;
	int ret;

	ret = rtio_sqe_rx_buf(iodev_sqe, 1, 1, &buf, &buf_len);
	if (ret != 0) {
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	rtio_iodev_sqe_ok(iodev_sqe, 0);
}

static void xyz_only_delayed_work(struct k_work *work)
{
	struct rtio_iodev_sqe *iodev_sqe = xyz_only_pending;

	ARG_UNUSED(work);

	xyz_only_pending = NULL;

	if (iodev_sqe) {
		xyz_only_complete(iodev_sqe);
	}
}

void xyz_only_flush(void)
{
	struct k_work_sync sync;
	struct rtio_iodev_sqe *iodev_sqe;

	(void)k_work_cancel_delayable_sync(&xyz_only_dwork, &sync);

	iodev_sqe = xyz_only_pending;
	xyz_only_pending = NULL;

	if (iodev_sqe) {
		rtio_iodev_sqe_err(iodev_sqe, -ECANCELED);
	}

	k_sem_reset(&xyz_only_submitted);
}

static void xyz_only_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	ARG_UNUSED(dev);

	if (xyz_only_delay_ms == 0) {
		xyz_only_complete(iodev_sqe);
		return;
	}

	xyz_only_pending = iodev_sqe;
	k_work_schedule(&xyz_only_dwork, K_MSEC(xyz_only_delay_ms));
	k_sem_give(&xyz_only_submitted);
}

static int xyz_only_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	k_work_init_delayable(&xyz_only_dwork, xyz_only_delayed_work);

	return 0;
}

static DEVICE_API(sensor, xyz_only_driver_api) = {
	.get_decoder = xyz_only_get_decoder,
	.submit = xyz_only_submit,
};

#define XYZ_ONLY_DEFINE(inst)								\
	DEVICE_DT_INST_DEFINE(inst, xyz_only_init, NULL, NULL, NULL, POST_KERNEL,	\
			      CONFIG_SENSOR_INIT_PRIORITY, &xyz_only_driver_api);

DT_INST_FOREACH_STATUS_OKAY(XYZ_ONLY_DEFINE)
