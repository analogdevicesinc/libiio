/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <iio/iio-backend.h>
#include <iio_device.h>

LOG_MODULE_REGISTER(iio_device_adc, CONFIG_LIBIIO_LOG_LEVEL);

struct iio_device_adc_config {
	const char *name;
	struct adc_dt_spec *channels;
	size_t num_channels;
	uint8_t address;
};

struct iio_device_adc_data {
};

static const char *const raw_name = "raw";
static const char *const scale_name = "scale";

static int iio_device_adc_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct iio_device_adc_config *config = dev->config;
	struct iio_channel *iio_channel;
	bool output = false;
	bool scan_element = false;
	const char *name = NULL;
	const char *label = NULL;
	const char *filename = NULL;
	const struct iio_data_format fmt = {
		.length = 16,
		.bits = 16,
		.is_signed = true,
	};
	char id[32];
	int index;

	for (index = 0; index < config->num_channels; index++) {

		snprintk(id, sizeof(id), "voltage%zu", index);

		iio_channel = iio_device_add_channel(iio_device, index, id,
				name, label, output, scan_element, &fmt);

		if (iio_err(iio_channel)) {
			LOG_ERR("Could not add channel %d", index);
			return -EINVAL;
		}

		iio_channel_set_pdata(iio_channel, (struct iio_channel_pdata *) index);

		if (iio_channel_add_attr(iio_channel, raw_name, filename)) {
			LOG_ERR("Could not add channel %d attribute %s", index, raw_name);
			return -EINVAL;
		}

		if (iio_channel_add_attr(iio_channel, scale_name, filename)) {
			LOG_ERR("Could not add channel %d attribute %s", index, scale_name);
			return -EINVAL;
		}
	}

	return 0;
}

static int iio_device_adc_read_channel_raw(const struct device *dev,
		int index, char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	int raw_len = 12;
	uint32_t tmp_buf = 0;
	struct adc_sequence sequence = {
		.buffer = &tmp_buf,
		.buffer_size = sizeof(tmp_buf),
		.channels = BIT(index),
		.resolution = channel->resolution,
	};
	int ret;

	if (len < raw_len) {
		LOG_ERR("Buffer size %u is too small for raw value, need %u",
			len, raw_len);
		return -ENOMEM;
	}

	ret = adc_read_dt(channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Error reading adc");
		return ret;
	}

	raw_len = snprintk(dst, len, "%u", tmp_buf);

	return raw_len + 1;
}

static int iio_device_adc_read_channel_scale(const struct device *dev,
		int index, char *dst, size_t len)
{
	const char *scale_value = "1";
	int scale_len = strlen(scale_value) + 1;

	if (len < scale_len) {
		LOG_ERR("Buffer size %u is too small for scale value, need %u",
			len, scale_len);
		return -ENOMEM;
	}

	strcpy(dst, scale_value);

	return scale_len;
}

static int iio_device_adc_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	int index = (int) iio_channel_get_pdata(attr->iio.chn);

	if (index >= config->num_channels) {
		LOG_ERR("Invalid index: %d", index);
		return -EINVAL;
	}

	if (!strcmp(attr->name, raw_name)) {
		return iio_device_adc_read_channel_raw(dev, index, dst, len);
	} else if (!strcmp(attr->name, scale_name)) {
		return iio_device_adc_read_channel_scale(dev, index, dst, len);
	}

	LOG_ERR("Invalid attr");
	return -EINVAL;
}

static int iio_device_adc_init(const struct device *dev)
{
	return 0;
}

static DEVICE_API(iio_device, iio_device_adc_driver_api) = {
	.add_channels = iio_device_adc_add_channels,
	.read_attr = iio_device_adc_read_attr,
};

#define DT_DRV_COMPAT iio_adc

#define IIO_DEVICE_ADC_CHANNEL(node_id, prop, idx)					\
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

#define IIO_DEVICE_ADC_INIT(inst)							\
static struct iio_device_adc_data iio_device_adc_data_##inst;				\
											\
static struct adc_dt_spec iio_device_adc_channels_##inst[] = {				\
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, io_channels, IIO_DEVICE_ADC_CHANNEL, (,))	\
};											\
											\
static const struct iio_device_adc_config iio_device_adc_config_##inst = {		\
	.name = DT_INST_PROP_OR(inst, io_name, NULL),					\
	.address = DT_INST_REG_ADDR(inst),						\
	.channels = iio_device_adc_channels_##inst,					\
	.num_channels = ARRAY_SIZE(iio_device_adc_channels_##inst),			\
};											\
											\
IIO_DEVICE_DT_INST_DEFINE(inst, iio_device_adc_init, NULL,				\
	&iio_device_adc_data_##inst, &iio_device_adc_config_##inst,			\
	POST_KERNEL, CONFIG_LIBIIO_IIO_DEVICE_ADC_INIT_PRIORITY,			\
	&iio_device_adc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIO_DEVICE_ADC_INIT)
