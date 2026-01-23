/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT iio_adc_emul_generator

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc_emul_generator, CONFIG_ADC_LOG_LEVEL);

struct channel_data {
	uint32_t value;
};

struct generator_config {
	const struct adc_dt_spec *channel_spec;
	struct channel_data *channel_data;
	size_t num_channels;
};

static int generator_value_set(const struct device *dev,
	unsigned int chan, void *data, uint32_t *result)
{
	struct channel_data *channel_data = data;

	channel_data->value += 1;
	*result = channel_data->value;

	return 0;
}

static int generator_init(const struct device *dev)
{
	const struct generator_config *config = dev->config;
	size_t i;
	int err;

	/* Configure channels individually prior to sampling. */
	for (i = 0; i < config->num_channels; i++) {
		if (!adc_is_ready_dt(&config->channel_spec[i])) {
			LOG_ERR("ADC emulator device not ready");
			return -ENODEV;
		}

		err = adc_emul_raw_value_func_set(
				config->channel_spec[i].dev,
				config->channel_spec[i].channel_id,
				generator_value_set,
				&config->channel_data[i]);
		if (err < 0) {
			LOG_ERR("Error setting ADC emulator function");
			return err;
		}
	}

	return 0;
}

#define ADC_EMUL_CHANNEL_INIT(node_id, prop, idx)					\
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

#define ADC_EMUL_GENERATOR_INIT(inst)							\
static const struct adc_dt_spec channel_spec_##inst[] = {				\
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, io_channels, ADC_EMUL_CHANNEL_INIT, (,))	\
};											\
											\
static struct channel_data channel_data_##inst[ARRAY_SIZE(channel_spec_##inst)];	\
											\
static const struct generator_config generator_config_##inst = {			\
	.channel_spec = channel_spec_##inst,						\
	.channel_data = channel_data_##inst,						\
	.num_channels = ARRAY_SIZE(channel_spec_##inst),				\
};											\
											\
DEVICE_DT_INST_DEFINE(inst, generator_init, NULL, NULL,					\
	&generator_config_##inst,							\
	POST_KERNEL, CONFIG_ADC_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ADC_EMUL_GENERATOR_INIT)
