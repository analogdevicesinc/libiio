/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/ztest.h>
#include <iio/iio.h>

#define ADC DEVICE_DT_GET(DT_NODELABEL(adc_emul))

static struct iio_context *ctx;
static const struct iio_device *iio_dev;

static const struct iio_attr *attr(const char *chn_id, const char *name)
{
	const struct iio_channel *chn = iio_device_find_channel(iio_dev, chn_id, false);

	zassert_not_null(chn, "no channel %s", chn_id);
	return iio_channel_find_attr(chn, name);
}

static ssize_t read(const char *chn_id, const char *name, char *buf, size_t len)
{
	return iio_attr_read_raw(attr(chn_id, name), buf, len);
}

static ssize_t write(const char *chn_id, const char *name, const char *val)
{
	return iio_attr_write_string(attr(chn_id, name), val);
}

#define zassert_attr_equal(chn_id, name, expected)					\
	do {										\
		char buf[32];								\
		zassert_true(read(chn_id, name, buf, sizeof(buf)) > 0);			\
		zassert_str_equal(buf, expected, "%s = %s", name, buf);			\
	} while (0)

static void *io_channels_setup(void)
{
	ctx = iio_create_context(NULL, "zephyr:");
	zassert_false(iio_err(ctx), "iio_create_context: %d", iio_err(ctx));

	iio_dev = iio_context_find_device(ctx, "io-test");
	zassert_not_null(iio_dev);

	return NULL;
}

static void io_channels_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	iio_context_destroy(ctx);
}

static void io_channels_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(write("voltage0", "gain", "1") >= 0);
	zassert_true(write("voltage0", "reference", "Internal") >= 0);
	zassert_true(write("voltage0", "differential", "0") >= 0);
}

ZTEST(libiio_io_channels, test_attrs_follow_the_channel_config)
{
	static const char *const adc_attrs[] = {"raw", "scale", "gain", "process", "reference",
						"differential"};

	ARRAY_FOR_EACH(adc_attrs, i) {
		zassert_not_null(attr("voltage0", adc_attrs[i]), "%s", adc_attrs[i]);
	}

	/* process needs a vref, scale needs a resolution */
	zassert_is_null(attr("voltage2", "process"));
	zassert_not_null(iio_device_find_attr(iio_dev, "internal_ref_voltage"));
}

ZTEST(libiio_io_channels, test_adc_raw_reads_the_input)
{
	zassert_ok(adc_emul_const_raw_value_set(ADC, 0, 1234));
	zassert_attr_equal("voltage0", "raw", "1234");
}

ZTEST(libiio_io_channels, test_adc_scale_is_vref_over_full_scale)
{
	zassert_attr_equal("voltage0", "scale", "0.805664");
}

ZTEST(libiio_io_channels, test_adc_scale_keeps_leading_zeros)
{
	zassert_attr_equal("voltage1", "scale", "0.050354");
}

ZTEST(libiio_io_channels, test_adc_scale_does_not_overflow)
{
	zassert_attr_equal("voltage3", "scale", "1.220703");
}

ZTEST(libiio_io_channels, test_adc_process_is_in_millivolts)
{
	zassert_ok(adc_emul_const_raw_value_set(ADC, 0, 2048));
	zassert_attr_equal("voltage0", "process", "1650");
}

ZTEST(libiio_io_channels, test_gain_round_trips)
{
	zassert_attr_equal("voltage0", "gain", "1");
	zassert_true(write("voltage0", "gain", "2") >= 0);
	zassert_attr_equal("voltage0", "gain", "2");
}

ZTEST(libiio_io_channels, test_gain_rejects_unknown_values)
{
	zassert_equal(write("voltage0", "gain", "7"), -EINVAL);
	zassert_attr_equal("voltage0", "gain", "1");
}

ZTEST(libiio_io_channels, test_reference_round_trips)
{
	zassert_attr_equal("voltage0", "reference", "Internal");
	zassert_true(write("voltage0", "reference", "External1") >= 0);
	zassert_attr_equal("voltage0", "reference", "External1");
}

ZTEST(libiio_io_channels, test_reference_rejects_unknown_values)
{
	zassert_equal(write("voltage0", "reference", "VDD/5"), -EINVAL);
	zassert_attr_equal("voltage0", "reference", "Internal");
}

ZTEST(libiio_io_channels, test_differential_halves_the_range)
{
	zassert_ok(adc_emul_const_raw_value_set(ADC, 0, 1024));
	zassert_attr_equal("voltage0", "process", "825");

	zassert_true(write("voltage0", "differential", "1") >= 0);
	zassert_attr_equal("voltage0", "differential", "1");
	zassert_attr_equal("voltage0", "process", "1650");
}

ZTEST(libiio_io_channels, test_differential_rejects_other_values)
{
	zassert_equal(write("voltage0", "differential", "2"), -EINVAL);
	zassert_attr_equal("voltage0", "differential", "0");
}

ZTEST(libiio_io_channels, test_internal_ref_voltage)
{
	char buf[8];

	zassert_true(iio_attr_read_raw(iio_device_find_attr(iio_dev, "internal_ref_voltage"),
				       buf, sizeof(buf)) > 0);
	zassert_str_equal(buf, "3300");
}

ZTEST(libiio_io_channels, test_raw_write_to_an_input_is_refused)
{
	zassert_equal(write("voltage0", "raw", "1"), -EINVAL);
}

ZTEST_SUITE(libiio_io_channels, NULL, io_channels_setup, NULL, io_channels_after,
	    io_channels_teardown);
