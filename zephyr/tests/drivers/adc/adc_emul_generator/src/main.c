/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/ztest.h>

static const struct adc_dt_spec chan4 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec chan8 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

static uint32_t sample(const struct adc_dt_spec *spec)
{
	uint32_t value = 0;
	struct adc_sequence seq = {
		.buffer = &value,
		.buffer_size = sizeof(value),
	};

	zassert_ok(adc_sequence_init_dt(spec, &seq));
	zassert_ok(adc_read_dt(spec, &seq));

	return value;
}

static void *generator_setup(void)
{
	zassert_ok(adc_channel_setup_dt(&chan4));
	zassert_ok(adc_channel_setup_dt(&chan8));

	return NULL;
}

ZTEST(adc_emul_generator, test_each_read_advances_by_one)
{
	uint32_t prev = sample(&chan8);

	for (int i = 0; i < 10; i++) {
		uint32_t next = sample(&chan8);

		zassert_equal(next, (prev + 1) % BIT(8));
		prev = next;
	}
}

ZTEST(adc_emul_generator, test_value_wraps_at_the_resolution)
{
	int i;

	for (i = 0; i < BIT(4); i++) {
		if (sample(&chan4) == BIT(4) - 1) {
			break;
		}
	}

	zassert_true(i < BIT(4), "never reached full scale");
	zassert_equal(sample(&chan4), 0);
	zassert_equal(sample(&chan4), 1);
}

ZTEST(adc_emul_generator, test_channels_count_independently)
{
	uint32_t a = sample(&chan4);
	uint32_t b = sample(&chan8);

	zassert_equal(sample(&chan8), (b + 1) % BIT(8));
	zassert_equal(sample(&chan4), (a + 1) % BIT(4));
}

ZTEST_SUITE(adc_emul_generator, NULL, generator_setup, NULL, NULL, NULL);
