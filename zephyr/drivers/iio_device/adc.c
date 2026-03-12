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

#define IIO_DEVICE_INT_REF_VOL_LEN 6 /* max voltage is 65535 which is 5 digits + null terminator */
#define IIO_DEVICE_SCALE_LEN 7 /* scale format is xx.xxx + null terminator */
#define IIO_DEVICE_GAIN_LEN 4 /* max gain is 128 which is 3 digits + null terminator */
#define IIO_DEVICE_PROCESS_LEN 12 /* process is int type so it needs 11 digits + null terminator */
#define IIO_DEVICE_REF_LEN 11 /* max reference is External0 so it needs 10 digits + null terminator */
#define IIO_DEVICE_DIFFERENTIAL_LEN 2 /* max differential is 1 so it needs 1 digit + null terminator */

struct iio_device_adc_config {
	const char *name;
	struct adc_dt_spec *channels;
	size_t num_channels;
	uint8_t address;
};

struct iio_device_adc_data {
	enum adc_gain *gains;
	enum adc_reference *references;
	uint8_t *differentials;
	uint8_t num_of_channels;
};

static const char *const raw_name = "raw";
static const char *const scale_name = "scale";
static const char *const internal_ref_voltage_name = "internal_ref_voltage";
static const char *const gain_name = "gain";
static const char *const process_name = "process";
static const char *const reference_name = "reference";
static const char *const differential_name = "differential";

static const char *const gain_values[] = {
	[ADC_GAIN_1_6] = "1/6",
	[ADC_GAIN_1_5] = "1/5",
	[ADC_GAIN_1_4] = "1/4",
	[ADC_GAIN_2_7] = "2/7",
	[ADC_GAIN_1_3] = "1/3",
	[ADC_GAIN_2_5] = "2/5",
	[ADC_GAIN_1_2] = "1/2",
	[ADC_GAIN_2_3] = "2/3",
	[ADC_GAIN_4_5] = "4/5",
	[ADC_GAIN_1] = "1",
	[ADC_GAIN_2] = "2",
	[ADC_GAIN_3] = "3",
	[ADC_GAIN_4] = "4",
	[ADC_GAIN_6] = "6",
	[ADC_GAIN_8] = "8",
	[ADC_GAIN_12] = "12",
	[ADC_GAIN_16] = "16",
	[ADC_GAIN_24] = "24",
	[ADC_GAIN_32] = "32",
	[ADC_GAIN_64] = "64",
	[ADC_GAIN_128] = "128",
};

static const char *const reference_values[] = {
	[ADC_REF_VDD_1] = "VDD",
	[ADC_REF_VDD_1_2] = "VDD/2",
	[ADC_REF_VDD_1_3] = "VDD/3",
	[ADC_REF_VDD_1_4] = "VDD/4",
	[ADC_REF_INTERNAL] = "Internal",
	[ADC_REF_EXTERNAL0] = "External0",
	[ADC_REF_EXTERNAL1] = "External1",
};

static int iio_device_adc_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct iio_device_adc_config *config = dev->config;
	struct iio_channel *iio_channel;
	struct adc_dt_spec *channel;
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

		channel = &config->channels[index];
		if (channel->resolution != 0) {
			if (iio_channel_add_attr(iio_channel, scale_name, filename)) {
				LOG_ERR("Could not add channel %d attribute %s", index, scale_name);
				return -EINVAL;
			}
		}

		if (iio_channel_add_attr(iio_channel, gain_name, filename)) {
			LOG_ERR("Could not add channel %d attribute %s", index, gain_name);
			return -EINVAL;
		}

		if (channel->vref_mv != 0) {
			if (iio_channel_add_attr(iio_channel, process_name, filename)) {
				LOG_ERR("Could not add channel %d attribute %s", index, process_name);
				return -EINVAL;
			}
		}

		if (iio_channel_add_attr(iio_channel, reference_name, filename)) {
			LOG_ERR("Could not add channel %d attribute %s", index, reference_name);
			return -EINVAL;
		}

		if (iio_channel_add_attr(iio_channel, differential_name, filename)) {
			LOG_ERR("Could not add channel %d attribute %s", index, differential_name);
			return -EINVAL;
		}
	}

	if (iio_device_add_attr(iio_device, internal_ref_voltage_name, IIO_ATTR_TYPE_DEVICE)) {
		LOG_ERR("Could not add device %d attribute %s", index, internal_ref_voltage_name);
		return -EINVAL;
	}

	return 0;
}

static int iio_device_adc_read_channel_raw(const struct device *dev,
		int index, char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	uint32_t tmp_buf = 0;
	struct adc_sequence sequence = {
		.buffer = &tmp_buf,
		.buffer_size = sizeof(tmp_buf),
	};
	int ret;

	ret = adc_sequence_init_dt(channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Error initializing adc sequence");
		return ret;
	}

	if (len < sequence.buffer_size) {
		LOG_ERR("Buffer size %u is too small for process value, need %u",
			len, sequence.buffer_size);
		return -ENOMEM;
	}

	ret = adc_read_dt(channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Error reading adc");
		return ret;
	}

	return snprintk(dst, len, "%u", tmp_buf) + 1;
}

static int iio_device_adc_read_channel_scale(const struct device *dev,
		int index, char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	uint8_t resolution;
	uint32_t scale_uv;
	uint32_t whole;
	uint32_t frac;

	if (len < IIO_DEVICE_SCALE_LEN) {
		LOG_ERR("Buffer size %u is too small for scale value, need %u",
			len, IIO_DEVICE_SCALE_LEN);
		return -ENOMEM;
	}

	resolution = channel->resolution;
	if (channel->channel_cfg_dt_node_exists && channel->channel_cfg.differential) {
		resolution -= 1;
	}

	scale_uv = ((uint32_t)channel->vref_mv * 1000u) / (1u << resolution); /* uV/LSB */
	whole = scale_uv / 1000u;
	frac = scale_uv % 1000u;

	return snprintk(dst, len, "%u.%03u", whole, frac) + 1;
}

static int iio_device_adc_read_channel_gain(const struct device *dev,
		int index, char *dst, size_t len)
{
	struct iio_device_adc_data *data = dev->data;

	if (len < IIO_DEVICE_GAIN_LEN) {
		LOG_ERR("Buffer size %u is too small for gain value, need %u",
			len, IIO_DEVICE_GAIN_LEN);
		return -ENOMEM;
	}

	return snprintk(dst, len, "%s", gain_values[data->gains[index]]) + 1;
}

static int iio_device_adc_parse_gain(const char *src, size_t len, enum adc_gain *gain_out)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(gain_values); i++) {
		const char *compare_gain = gain_values[i];

		if (compare_gain == NULL) {
			continue;
		}

		if (strcmp(src, compare_gain) == 0) {
			*gain_out = (enum adc_gain)i;
			return 0;
		}
	}

	return -EINVAL;
}

struct adc_channel_cfg iio_device_adc_get_channel_cfg(const struct device *dev, int index)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	struct iio_device_adc_data *data = dev->data;

	struct adc_channel_cfg channel_cfg = {0};

	if (channel->channel_cfg_dt_node_exists) {
		channel_cfg.gain = data->gains[index];
		channel_cfg.acquisition_time = channel->channel_cfg.acquisition_time;
		channel_cfg.differential = data->differentials[index];
		channel_cfg.channel_id = channel->channel_id;
		channel_cfg.reference = data->references[index];
	}

	return channel_cfg;
}

static int iio_device_adc_write_channel_gain(const struct device *dev,
		int index, const char *src, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	struct iio_device_adc_data *data = dev->data;

	enum adc_gain gain;
	int ret;

	ret = iio_device_adc_parse_gain(src, len, &gain);
	if (ret) {
		LOG_ERR("Invalid gain value '%.*s'", (int)len, src);
		return ret;
	}

	struct adc_channel_cfg channel_cfg = iio_device_adc_get_channel_cfg(dev, index);
	channel_cfg.gain = gain;

	ret = adc_channel_setup(channel->dev, &channel_cfg);
	if (ret == 0) {
		data->gains[index] = gain;
		ret = len;
	}

	return ret;
}

static int iio_device_adc_int_ref_voltage_read(const struct device *dev,
		char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[0];

	if (len < IIO_DEVICE_INT_REF_VOL_LEN) {
		LOG_ERR("Buffer size %u is too small for internal reference voltage value, need %u",
			len, IIO_DEVICE_INT_REF_VOL_LEN);
		return -ENOMEM;
	}

	uint16_t int_ref_mv = adc_ref_internal(channel->dev);

	return snprintk(dst, len, "%u", int_ref_mv) + 1;
}

static int iio_device_adc_read_channel_process(const struct device *dev,
		int index, char *dst, size_t len)
{
	struct iio_device_adc_data *data = dev->data;
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	uint32_t tmp_buf = 0;
	struct adc_sequence sequence = {
		.buffer = &tmp_buf,
		.buffer_size = sizeof(tmp_buf),
	};
	uint8_t resolution = channel->resolution;
	int ret;

	if (len < IIO_DEVICE_PROCESS_LEN) {
		LOG_ERR("Buffer size %u is too small for process value, need %u",
			len, IIO_DEVICE_PROCESS_LEN);
		return -ENOMEM;
	}

	ret = adc_sequence_init_dt(channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Error initializing adc sequence");
		return ret;
	}

	ret = adc_read_dt(channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Error reading adc");
		return ret;
	}

	if (data->differentials[index]) {
		resolution -= 1;
	}

	ret = adc_raw_to_millivolts(channel->vref_mv, data->gains[index], resolution,
					&tmp_buf);
	if (ret < 0) {
		LOG_ERR("Error converting raw to millivolts");
		return ret;
	}

	return snprintk(dst, len, "%d", tmp_buf) + 1;
}

static int iio_device_adc_read_channel_reference(const struct device *dev,
		int index, char *dst, size_t len)
{
	struct iio_device_adc_data *data = dev->data;

	if (len < IIO_DEVICE_REF_LEN) {
		LOG_ERR("Buffer size %u is too small for reference value, need %u",
			len, IIO_DEVICE_REF_LEN);
		return -ENOMEM;
	}

	return snprintk(dst, len, "%s", reference_values[data->references[index]]) + 1;
}

static int iio_device_adc_parse_reference(const char *src, size_t len, enum adc_reference *reference_out)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(reference_values); i++) {
		const char *compare_reference = reference_values[i];

		if (compare_reference == NULL) {
			continue;
		}

		if (strcmp(src, compare_reference) == 0) {
			*reference_out = (enum adc_reference)i;
			return 0;
		}
	}

	return -EINVAL;
}

static int iio_device_adc_write_channel_reference(const struct device *dev,
	int index, const char *src, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	struct iio_device_adc_data *data = dev->data;

	enum adc_reference reference;
	int ret;

	ret = iio_device_adc_parse_reference(src, len, &reference);
	if (ret) {
		LOG_ERR("Invalid reference value '%.*s'", (int)len, src);
		return ret;
	}

	struct adc_channel_cfg channel_cfg = iio_device_adc_get_channel_cfg(dev, index);
	channel_cfg.reference = reference;

	ret = adc_channel_setup(channel->dev, &channel_cfg);
	if (ret == 0) {
		data->references[index] = reference;
		ret = len;
	}

	return ret;
}

static int iio_device_adc_read_channel_differential(const struct device *dev,
		int index, char *dst, size_t len)
{
	struct iio_device_adc_data *data = dev->data;

	if (len < IIO_DEVICE_DIFFERENTIAL_LEN) {
		LOG_ERR("Buffer size %u is too small for differential value, need %u",
			len, IIO_DEVICE_DIFFERENTIAL_LEN);
		return -ENOMEM;
	}

	return snprintk(dst, len, "%d", data->differentials[index]) + 1;
}
	
static int iio_device_adc_write_channel_differential(const struct device *dev,
	int index, const char *src, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	struct adc_dt_spec *channel = &config->channels[index];
	struct iio_device_adc_data *data = dev->data;
	uint8_t differential;
	int ret;

	if (len != 2 || (src[0] != '0' && src[0] != '1')) {
		LOG_ERR("Invalid differential value '%.*s'", (int)len, src);
		return -EINVAL;
	}

	differential = src[0] == '1' ? 1 : 0; /* Convert char '0' or '1' to unsigned integer */
	struct adc_channel_cfg channel_cfg = iio_device_adc_get_channel_cfg(dev, index);
	channel_cfg.differential = differential;

	ret = adc_channel_setup(channel->dev, &channel_cfg);
	if (ret == 0) {
		data->differentials[index] = differential;
		ret = len;
	}

	return ret;
}

static int iio_device_adc_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	int index;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		index = (int) iio_channel_get_pdata(attr->iio.chn);

		if (index >= config->num_channels) {
			LOG_ERR("Invalid index: %d", index);
			return -EINVAL;
		}

		if (!strcmp(attr->name, raw_name)) {
			return iio_device_adc_read_channel_raw(dev, index, dst, len);
		} else if (!strcmp(attr->name, scale_name)) {
			return iio_device_adc_read_channel_scale(dev, index, dst, len);
		} else if (!strcmp(attr->name, gain_name)) {
			return iio_device_adc_read_channel_gain(dev, index, dst, len);
		} else if (!strcmp(attr->name, process_name)) {
			return iio_device_adc_read_channel_process(dev, index, dst, len);
		} else if (!strcmp(attr->name, reference_name)) {
			return iio_device_adc_read_channel_reference(dev, index, dst, len);
		} else if (!strcmp(attr->name, differential_name)) {
			return iio_device_adc_read_channel_differential(dev, index, dst, len);
		}
		break;

	case IIO_ATTR_TYPE_DEVICE:
		if (!strcmp(attr->name, internal_ref_voltage_name)) {
			return iio_device_adc_int_ref_voltage_read(dev, dst, len);
		}
		break;

	default:
		break;
	}

	LOG_ERR("Invalid attr");
	return -EINVAL;
}

static int iio_device_adc_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len)
{
	const struct iio_device_adc_config *config = dev->config;
	int index = 0;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		index = (int) iio_channel_get_pdata(attr->iio.chn);

		if (index >= config->num_channels) {
			LOG_ERR("Invalid index: %d", index);
			return -EINVAL;
		}

		if (!strcmp(attr->name, gain_name)) {
			return iio_device_adc_write_channel_gain(dev, index, src, len);
		} else if (!strcmp(attr->name, reference_name)) {
			return iio_device_adc_write_channel_reference(dev, index, src, len);
		} else if (!strcmp(attr->name, differential_name)) {
			return iio_device_adc_write_channel_differential(dev, index, src, len);
		}
		break;

	case IIO_ATTR_TYPE_DEVICE:
		break;

	default:
		break;
	}

	LOG_ERR("Invalid attr");
	return -EINVAL;
}

static int iio_device_adc_init(const struct device *dev)
{
	const struct iio_device_adc_config *config = dev->config;
	struct iio_device_adc_data *data = dev->data;
	int ret = 0;

	__ASSERT(data->gains != NULL, "gains not set");
	__ASSERT(data->references != NULL, "references not set");
	__ASSERT(data->num_of_channels == config->num_channels, "channel count mismatch");

	for (size_t i = 0; i < config->num_channels; i++) {
		if (config->channels[i].channel_cfg_dt_node_exists) {
			data->gains[i] = config->channels[i].channel_cfg.gain;
			data->references[i] = config->channels[i].channel_cfg.reference;
			data->differentials[i] = config->channels[i].channel_cfg.differential;
		}

		ret = adc_channel_setup_dt(&config->channels[i]);
		if (ret < 0) {
			LOG_ERR("Error setting up channel %zu", i);
			break;
		}
	}

	return ret;
}

static DEVICE_API(iio_device, iio_device_adc_driver_api) = {
	.add_channels = iio_device_adc_add_channels,
	.read_attr = iio_device_adc_read_attr,
	.write_attr = iio_device_adc_write_attr,
};

#define DT_DRV_COMPAT iio_adc

#define IIO_DEVICE_ADC_CHANNEL(node_id, prop, idx)					\
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

#define IIO_DEVICE_ADC_INIT(inst)							\
static enum adc_gain iio_device_adc_gains_##inst[DT_INST_PROP_LEN(inst, io_channels)];	\
static enum adc_reference iio_device_adc_references_##inst				\
			[DT_INST_PROP_LEN(inst, io_channels)];				\
static uint8_t iio_device_adc_differentials_##inst					\
			[DT_INST_PROP_LEN(inst, io_channels)];				\
static struct iio_device_adc_data iio_device_adc_data_##inst = {			\
	.gains = iio_device_adc_gains_##inst,						\
	.references = iio_device_adc_references_##inst,					\
	.differentials = iio_device_adc_differentials_##inst,				\
	.num_of_channels = ARRAY_SIZE(iio_device_adc_gains_##inst),			\
};											\
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
