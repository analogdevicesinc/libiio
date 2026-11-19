/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * One bit ADC DAC
 *
 * Input GPIOs are exposed as input voltage channels and output GPIOs as
 * output voltage channels, each indexed from 0 with a single "raw" attribute.
 * Labels come from channel@N nodes, numbered with the inputs first.
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <iio/iio-backend.h>
#include <iio_device.h>

LOG_MODULE_REGISTER(iio_device_one_bit_adc_dac, CONFIG_LIBIIO_LOG_LEVEL);

struct one_bit_adc_dac_label {
	uint32_t reg;
	const char *label;
};

struct one_bit_adc_dac_config {
	const struct gpio_dt_spec *gpios;
	size_t in_num_ch;
	size_t num_ch;
	const struct one_bit_adc_dac_label *labels;
	size_t num_labels;
};

static const char *const raw_name = "raw";

static const char *one_bit_adc_dac_find_label(const struct one_bit_adc_dac_config *config,
		size_t ch)
{
	for (size_t i = 0; i < config->num_labels; i++) {
		if (config->labels[i].reg == ch) {
			return config->labels[i].label;
		}
	}

	return NULL;
}

static int one_bit_adc_dac_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct one_bit_adc_dac_config *config = dev->config;
	const struct iio_data_format fmt = {
		.length = 8,
		.bits = 1,
	};
	struct iio_channel *iio_channel;
	char id[16];

	for (size_t ch = 0; ch < config->num_ch; ch++) {
		bool output = ch >= config->in_num_ch;
		size_t index = output ? ch - config->in_num_ch : ch;

		snprintk(id, sizeof(id), "voltage%zu", index);

		iio_channel = iio_device_add_channel(iio_device, index, id, NULL,
				one_bit_adc_dac_find_label(config, ch), output, false, &fmt);
		if (iio_err(iio_channel)) {
			LOG_ERR("Could not add channel %zu", ch);
			return -EINVAL;
		}

		iio_channel_set_pdata(iio_channel, (struct iio_channel_pdata *) ch);

		if (iio_channel_add_attr(iio_channel, raw_name, IIO_ATTR_TYPE_CHANNEL, NULL)) {
			LOG_ERR("Could not add channel %zu attribute %s", ch, raw_name);
			return -EINVAL;
		}
	}

	return 0;
}

static int one_bit_adc_dac_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct one_bit_adc_dac_config *config = dev->config;
	size_t ch = (size_t) iio_channel_get_pdata(attr->iio.chn);
	int ret;

	ret = gpio_pin_get_dt(&config->gpios[ch]);
	if (ret < 0) {
		LOG_ERR("Failed to get the value of channel %zu (%d)", ch, ret);
		return ret;
	}

	return snprintk(dst, len, "%d", ret) + 1;
}

static int one_bit_adc_dac_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len)
{
	const struct one_bit_adc_dac_config *config = dev->config;
	size_t ch = (size_t) iio_channel_get_pdata(attr->iio.chn);
	char buffer[12] = { 0 };
	char *endptr;
	long val;
	int ret;

	if (ch < config->in_num_ch) {
		return len;
	}

	strncpy(buffer, src, MIN(len, sizeof(buffer) - 1));

	val = strtol(buffer, &endptr, 10);
	if (endptr == buffer || *endptr != '\0') {
		LOG_ERR("Invalid raw value '%.*s'", (int)len, src);
		return -EINVAL;
	}

	ret = gpio_pin_set_dt(&config->gpios[ch], val != 0);
	if (ret < 0) {
		LOG_ERR("Failed to set the value of channel %zu (%d)", ch, ret);
		return ret;
	}

	return len;
}

static int one_bit_adc_dac_init(const struct device *dev)
{
	const struct one_bit_adc_dac_config *config = dev->config;
	int ret;

	for (size_t ch = 0; ch < config->num_ch; ch++) {
		gpio_flags_t flags = ch < config->in_num_ch ? GPIO_INPUT :
				     GPIO_OUTPUT_INACTIVE | GPIO_INPUT;

		if (!gpio_is_ready_dt(&config->gpios[ch])) {
			LOG_ERR("GPIO of channel %zu is not ready", ch);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->gpios[ch], flags);
		if (ret < 0) {
			LOG_ERR("Error setting up channel %zu (%d)", ch, ret);
			return ret;
		}
	}

	return 0;
}

static DEVICE_API(iio_device, one_bit_adc_dac_driver_api) = {
	.add_channels = one_bit_adc_dac_add_channels,
	.read_attr = one_bit_adc_dac_read_attr,
	.write_attr = one_bit_adc_dac_write_attr,
};

#define DT_DRV_COMPAT iio_one_bit_adc_dac

#define ONE_BIT_ADC_DAC_GPIO(node_id, prop, idx)					\
	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define ONE_BIT_ADC_DAC_GPIOS(inst, prop)						\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, prop),					\
		    (DT_INST_FOREACH_PROP_ELEM(inst, prop, ONE_BIT_ADC_DAC_GPIO)), ())

#define ONE_BIT_ADC_DAC_LABEL(node_id)						\
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, label),					\
		    ({ .reg = DT_REG_ADDR(node_id), .label = DT_PROP(node_id, label) },), ())

#define ONE_BIT_ADC_DAC_INIT(inst)							\
static const struct gpio_dt_spec one_bit_adc_dac_gpios_##inst[] = {			\
	ONE_BIT_ADC_DAC_GPIOS(inst, in_gpios)						\
	ONE_BIT_ADC_DAC_GPIOS(inst, out_gpios)						\
};											\
											\
static const struct one_bit_adc_dac_label one_bit_adc_dac_labels_##inst[] = {		\
	DT_INST_FOREACH_CHILD(inst, ONE_BIT_ADC_DAC_LABEL)				\
};											\
											\
static const struct one_bit_adc_dac_config one_bit_adc_dac_config_##inst = {		\
	.gpios = one_bit_adc_dac_gpios_##inst,						\
	.in_num_ch = DT_INST_PROP_LEN_OR(inst, in_gpios, 0),				\
	.num_ch = ARRAY_SIZE(one_bit_adc_dac_gpios_##inst),				\
	.labels = one_bit_adc_dac_labels_##inst,					\
	.num_labels = ARRAY_SIZE(one_bit_adc_dac_labels_##inst),			\
};											\
											\
IIO_DEVICE_DT_INST_DEFINE(inst,								\
	one_bit_adc_dac_init, NULL,							\
	NULL, &one_bit_adc_dac_config_##inst,						\
	POST_KERNEL, CONFIG_LIBIIO_IIO_DEVICE_ONE_BIT_ADC_DAC_INIT_PRIORITY,		\
	&one_bit_adc_dac_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ONE_BIT_ADC_DAC_INIT)
