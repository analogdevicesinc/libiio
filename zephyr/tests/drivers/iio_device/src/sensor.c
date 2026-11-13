/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/drivers/emul_sensor.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/generic_emul.h>
#include <zephyr/ztest.h>
#include <iio/iio.h>

#define SENSOR0 EMUL_DT_GET(DT_NODELABEL(sensor0))
#define SENSOR1 EMUL_DT_GET(DT_NODELABEL(sensor1))

/* Values must be exact in q31 with this shift */
#define SHIFT 8

static struct iio_context *ctx;

static void set_value(const struct emul *emul, enum sensor_channel chan, double value)
{
	struct sensor_chan_spec spec = {.chan_type = chan};
	q31_t q = (q31_t)(value * (1 << (31 - SHIFT)));

	zassert_ok(emul_sensor_backend_set_channel(emul, spec, &q, SHIFT));
}

static const struct iio_channel *find_channel(const char *dev_name, const char *chn_id)
{
	const struct iio_device *dev = iio_context_find_device(ctx, dev_name);
	const struct iio_channel *chn;

	zassert_not_null(dev, "no device %s", dev_name);
	chn = iio_device_find_channel(dev, chn_id, false);
	zassert_not_null(chn, "no channel %s on %s", chn_id, dev_name);
	return chn;
}

static ssize_t read(const char *dev_name, const char *chn_id, const char *name, char *buf,
		    size_t len)
{
	const struct iio_attr *attr = iio_channel_find_attr(find_channel(dev_name, chn_id), name);

	return iio_attr_read_raw(attr, buf, len);
}

#define zassert_attr_equal(dev_name, chn_id, name, expected)				\
	do {										\
		char buf[32];								\
		zassert_true(read(dev_name, chn_id, name, buf, sizeof(buf)) > 0);	\
		zassert_str_equal(buf, expected, "%s = %s", chn_id, buf);		\
	} while (0)

static void *sensor_setup(void)
{
	ctx = iio_create_context(NULL, "zephyr:");
	zassert_false(iio_err(ctx), "iio_create_context: %d", iio_err(ctx));

	return NULL;
}

static void sensor_before(void *fixture)
{
	ARG_UNUSED(fixture);

	generic_emul_reset(SENSOR0, true);
	generic_emul_reset(SENSOR1, true);
}

static void sensor_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	iio_context_destroy(ctx);
}

ZTEST(libiio_sensor, test_channels_are_named_from_the_sensor_channel)
{
	static const char *const sensor0[] = {"accel_x", "accel_y", "temp_die", "pressure0"};
	static const char *const sensor1[] = {"voltage0", "voltage1", "current0"};

	ARRAY_FOR_EACH(sensor0, i) {
		find_channel("sensor0", sensor0[i]);
	}
	ARRAY_FOR_EACH(sensor1, i) {
		find_channel("sensor1", sensor1[i]);
	}
}

ZTEST(libiio_sensor, test_unmapped_channels_are_skipped)
{
	zassert_equal(iio_device_get_channels_count(iio_context_find_device(ctx, "sensor0")), 4);
}

ZTEST(libiio_sensor, test_channels_are_milli_unit_scan_elements)
{
	const struct iio_data_format *accel = iio_channel_get_data_format(
		find_channel("sensor0", "accel_x"));
	const struct iio_data_format *press = iio_channel_get_data_format(
		find_channel("sensor0", "pressure0"));

	zassert_true(iio_channel_is_scan_element(find_channel("sensor0", "accel_x")));
	zassert_equal(accel->bits, 16);
	zassert_true(accel->is_signed);
	zassert_true(accel->with_scale);
	zassert_within(accel->scale, 0.001, 1e-9);
	zassert_false(press->is_signed);
}

ZTEST(libiio_sensor, test_raw_is_in_milli_units)
{
	set_value(SENSOR0, SENSOR_CHAN_ACCEL_X, 9.5);
	set_value(SENSOR0, SENSOR_CHAN_ACCEL_Y, -1.25);

	zassert_attr_equal("sensor0", "accel_x", "raw", "9500");
	zassert_attr_equal("sensor0", "accel_y", "raw", "-1250");
}

ZTEST(libiio_sensor, test_scale_is_milli)
{
	zassert_attr_equal("sensor0", "accel_x", "scale", "0.001");
}

ZTEST(libiio_sensor, test_first_channel_read_fetches_a_new_sample)
{
	set_value(SENSOR0, SENSOR_CHAN_ACCEL_X, 1.0);
	zassert_attr_equal("sensor0", "accel_x", "raw", "1000");

	set_value(SENSOR0, SENSOR_CHAN_ACCEL_X, 2.0);
	zassert_attr_equal("sensor0", "accel_x", "raw", "2000");
}

ZTEST(libiio_sensor, test_fetch_error_is_returned)
{
	char buf[16];

	generic_emul_func_rc(SENSOR0, 0, 0, -EIO);

	zassert_equal(read("sensor0", "accel_x", "raw", buf, sizeof(buf)), -EIO);
}

ZTEST(libiio_sensor, test_instances_read_their_own_sensor)
{
	set_value(SENSOR0, SENSOR_CHAN_ACCEL_X, 3.0);
	set_value(SENSOR1, SENSOR_CHAN_VOLTAGE, 0.5);

	zassert_attr_equal("sensor0", "accel_x", "raw", "3000");
	zassert_attr_equal("sensor1", "voltage0", "raw", "500");
}

ZTEST(libiio_sensor, test_sensor_that_failed_init_is_not_listed)
{
	zassert_is_null(iio_context_find_device(ctx, "dead"));
	zassert_not_null(iio_context_find_device(ctx, "sensor0"));
	zassert_not_null(iio_context_find_device(ctx, "sensor1"));
}

ZTEST_SUITE(libiio_sensor, NULL, sensor_setup, sensor_before, NULL, sensor_teardown);
