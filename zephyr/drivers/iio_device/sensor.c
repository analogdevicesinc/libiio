/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <iio/iio-backend.h>
#include <iio_device.h>

LOG_MODULE_REGISTER(iio_device_sensor, CONFIG_LIBIIO_LOG_LEVEL);

/*
 * The Zephyr sensor API returns values already converted to SI units
 * via sensor_value (val1 = integer part, val2 = fractional in micro-units).
 *
 * IIO convention:  processed_value = raw * scale
 *
 * We present:
 *   raw     – milli-units integer  (val1 * 1000 + val2 / 1000)
 *   scale   – "0.001"
 */

#define SENSOR_RAW_LEN		12
#define SENSOR_SCALE_LEN	6

struct sensor_channel_map {
	enum sensor_channel sensor_chan;
	const char *iio_type;
	const char *modifier;
	uint8_t bits;
	bool is_signed;
};

static const struct sensor_channel_map channel_map[] = {

	{SENSOR_CHAN_VOLTAGE,       "voltage",          NULL,      16, true},
	{SENSOR_CHAN_VSHUNT,        "voltage",          "shunt",   16, true},

	{SENSOR_CHAN_CURRENT,       "current",          NULL,      16, true},
	{SENSOR_CHAN_POWER,         "power",            NULL,      16, true},
	{SENSOR_CHAN_RESISTANCE,    "resistance",       NULL,      16, false},

	{SENSOR_CHAN_DIE_TEMP,      "temp",             "die",     16, true},
	{SENSOR_CHAN_AMBIENT_TEMP,  "temp",             "ambient", 16, true},

	{SENSOR_CHAN_ACCEL_X,       "accel",            "x",       16, true},
	{SENSOR_CHAN_ACCEL_Y,       "accel",            "y",       16, true},
	{SENSOR_CHAN_ACCEL_Z,       "accel",            "z",       16, true},

	{SENSOR_CHAN_GYRO_X,        "anglvel",          "x",       16, true},
	{SENSOR_CHAN_GYRO_Y,        "anglvel",          "y",       16, true},
	{SENSOR_CHAN_GYRO_Z,        "anglvel",          "z",       16, true},

	{SENSOR_CHAN_MAGN_X,        "magn",             "x",       16, true},
	{SENSOR_CHAN_MAGN_Y,        "magn",             "y",       16, true},
	{SENSOR_CHAN_MAGN_Z,        "magn",             "z",       16, true},

	{SENSOR_CHAN_PRESS,         "pressure",         NULL,      16, false},
	{SENSOR_CHAN_HUMIDITY,      "humidityrelative", NULL,      16, false},
	{SENSOR_CHAN_LIGHT,         "illuminance",      NULL,      16, false},
	{SENSOR_CHAN_AMBIENT_LIGHT, "illuminance",      NULL,      16, false},
};

struct iio_device_sensor_attr {
	const char *name;
	enum sensor_attribute attr;
};

struct iio_device_sensor_chan {
	const enum sensor_channel channel;
	const struct iio_device_sensor_attr *attrs;
	size_t num_attrs;
};

struct iio_device_sensor_config {
	const struct device *sensor_dev;
	const struct iio_device_sensor_chan *channels;
	size_t num_channels;
	const struct iio_device_sensor_attr *attrs;
	size_t num_attrs;
};

/* bulk fetch — one sensor_sample_fetch(SENSOR_CHAN_ALL) per round,
 * then sensor_channel_get() for each configured channel into a cache array.
 * This avoids repeated data-ready waits (e.g. ADXL345 polls STATUS per fetch).
 */
struct iio_device_sensor_data {
	struct sensor_value *cache;
	bool fetched;
};

static const char *const raw_name     = "raw";
static const char *const scale_name   = "scale";

static const struct sensor_channel_map *find_channel_map(enum sensor_channel chan)
{
	for (size_t i = 0; i < ARRAY_SIZE(channel_map); i++) {
		if (channel_map[i].sensor_chan == chan) {
			return &channel_map[i];
		}
	}
	return NULL;
}

/** Bulk-fetch all configured channels in one go.
 *  Uses sensor_sample_fetch(SENSOR_CHAN_ALL) — a single hardware transaction
 *  (one data-ready wait) — then sensor_channel_get() for each channel.
 *  Results are cached; subsequent reads return from cache until invalidated.
 */
static int sensor_fetch_all(const struct device *dev)
{
	const struct iio_device_sensor_config *config = dev->config;
	struct iio_device_sensor_data *data = dev->data;
	int ret;

	if (data->fetched) {
		return 0;
	}

	/* Try bulk fetch first (SENSOR_CHAN_ALL) */
	ret = sensor_sample_fetch(config->sensor_dev);
	if (ret == -ENOTSUP) {
		/* Driver has no SENSOR_CHAN_ALL — fetch each channel individually */
		for (size_t i = 0; i < config->num_channels; i++) {
			int ch_ret = sensor_sample_fetch_chan(config->sensor_dev,
								config->channels[i].channel);
			if (ch_ret < 0) {
				LOG_ERR("Fetch channel %d: %d",
					config->channels[i].channel, ch_ret);
			}
		}
	} else if (ret < 0) {
		LOG_ERR("Error fetching sensor: %d", ret);
		return ret;
	}

	/* Populate cache for every configured channel */
	for (size_t i = 0; i < config->num_channels; i++) {
		ret = sensor_channel_get(config->sensor_dev,
				config->channels[i].channel, &data->cache[i]);
		if (ret < 0) {
			LOG_ERR("Get channel %d: %d",
				config->channels[i].channel, ret);
			data->cache[i].val1 = 0;
			data->cache[i].val2 = 0;
		}
	}

	data->fetched = true;
	return 0;
}

static int sensor_cache_index(const struct device *dev,
		enum sensor_channel sensor_chan)
{
	const struct iio_device_sensor_config *config = dev->config;

	for (size_t i = 0; i < config->num_channels; i++) {
		if (config->channels[i].channel == sensor_chan) {
			return (int)i;
		}
	}
	return -ENOENT;
}

static int iio_device_sensor_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct iio_device_sensor_config *config = dev->config;
	struct iio_channel *iio_channel;
	const bool output = false;
	const bool scan_element = true;
	const char *name = NULL;
	const char *label = NULL;
	enum iio_attr_type type = IIO_ATTR_TYPE_CHANNEL;
	const char *filename = NULL;
	char id[32];
	uint8_t type_counters[SENSOR_CHAN_COMMON_COUNT] = {0};

	for (int index = 0; index < config->num_channels; index++) {
		const struct iio_device_sensor_chan *channel = &config->channels[index];
		const struct sensor_channel_map *map = find_channel_map(channel->channel);

		if (!map) {
			LOG_WRN("No mapping for sensor channel %d, skipping",
				channel->channel);
			continue;
		}

		if (map->modifier) {
			snprintk(id, sizeof(id), "%s_%s",
				 map->iio_type, map->modifier);
		} else {
			uint8_t cnt = type_counters[channel->channel]++;

			snprintk(id, sizeof(id), "%s%d", map->iio_type, cnt);
		}

		const struct iio_data_format fmt = {
			.length    = map->bits,
			.bits      = map->bits,
			.is_signed = map->is_signed,
			.with_scale = true,
			.scale     = 0.001,
			.is_be     = false,
		};

		iio_channel = iio_device_add_channel(iio_device, index, id,
				name, label, output, scan_element, &fmt);
		if (iio_err(iio_channel)) {
			LOG_ERR("Could not add channel %d (%s)", index, id);
			return -EINVAL;
		}

		iio_channel_set_pdata(iio_channel,
			(struct iio_channel_pdata *)(intptr_t)channel);

		if (iio_channel_add_attr(iio_channel, raw_name, type, filename)) {
			LOG_ERR("Could not add channel %d attr %s",
				index, raw_name);
			return -EINVAL;
		}

		if (iio_channel_add_attr(iio_channel, scale_name, type, filename)) {
			LOG_ERR("Could not add channel %d attr %s",
				index, scale_name);
			return -EINVAL;
		}

		for (size_t attr_index = 0; attr_index < channel->num_attrs; attr_index++) {
			if (iio_channel_add_attr(iio_channel, channel->attrs[attr_index].name, type, filename)) {
				LOG_ERR("Could not add channel %d attr %s",
					index, channel->attrs[attr_index].name);
				return -EINVAL;
			}
		}

		LOG_DBG("Added IIO channel %s for sensor channel %d",
			id, channel->channel);
	}

	for (size_t dev_attr_i = 0; dev_attr_i < config->num_attrs; dev_attr_i++) {
		if (iio_device_add_attr(iio_device, config->attrs[dev_attr_i].name, IIO_ATTR_TYPE_DEVICE)) {
			LOG_ERR("Could not add device %d attr %s",
				dev_attr_i, config->attrs[dev_attr_i].name);
			return -EINVAL;
		}
	}

	return 0;
}

/** raw: sensor value expressed as milli-units integer.
 *  On the first channel read of a round, triggers a bulk fetch of ALL
 *  configured channels (one data-ready wait).  Subsequent channel reads
 *  within the same round return from the cache.
 *
 *  Cache is invalidated when channel index 0 is read, which marks the
 *  start of a new sample round (readbuf iterates channels in order).
 */
static int iio_device_sensor_read_channel_raw(const struct device *dev,
		enum sensor_channel sensor_chan, char *dst, size_t len)
{
	struct iio_device_sensor_data *data = dev->data;
	int ret, idx;

	if (len < SENSOR_RAW_LEN) {
		LOG_ERR("Buffer too small for raw value");
		return -ENOMEM;
	}

	idx = sensor_cache_index(dev, sensor_chan);
	if (idx < 0) {
		LOG_ERR("Channel %d not in config", sensor_chan);
		return idx;
	}

	if (idx == 0) {
		data->fetched = false;
	}

	ret = sensor_fetch_all(dev);
	if (ret < 0) {
		return ret;
	}

	int32_t raw = data->cache[idx].val1 * 1000
		    + data->cache[idx].val2 / 1000;

	return snprintk(dst, len, "%d", raw) + 1;
}

static int iio_device_sensor_read_channel_scale(char *dst, size_t len)
{
	if (len < SENSOR_SCALE_LEN) {
		LOG_ERR("Buffer too small for scale value");
		return -ENOMEM;
	}

	return snprintk(dst, len, "%u.%03u", 0u, 1u) + 1;
}

static int iio_device_sensor_read_device_attr(const struct device *dev, enum sensor_channel channel, enum sensor_attribute attr, char *dst, size_t len)
{
	const struct iio_device_sensor_config *cfg = dev->config;
	struct sensor_value val = {0};
	int ret;

	ret = sensor_attr_get(cfg->sensor_dev, channel, attr, &val);

	if (ret < 0) {
		return ret;
	}

	return snprintk(dst, len, "%u.%06u", val.val1, val.val2) + 1;
}

static int iio_device_sensor_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_sensor_config *cfg = dev->config;
	struct iio_device_sensor_data *data = dev->data;
	const struct iio_device_sensor_chan *channel;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		channel = (const struct iio_device_sensor_chan *)(intptr_t)
			iio_channel_get_pdata(attr->iio.chn);

		if (channel->channel >= SENSOR_CHAN_COMMON_COUNT) {
			LOG_ERR("Invalid sensor channel: %d", channel->channel);
			return -EINVAL;
		}

		if (!strcmp(attr->name, raw_name)) {
			return iio_device_sensor_read_channel_raw(dev,
					channel->channel, dst, len);
		} else if (!strcmp(attr->name, scale_name)) {
			return iio_device_sensor_read_channel_scale(dst, len);
		} else {
			for (size_t i = 0; i < channel->num_attrs; i++) {
				if (!strcmp(attr->name, channel->attrs[i].name)) {
					return iio_device_sensor_read_device_attr(dev, channel->channel, channel->attrs[i].attr, dst, len);
				}
			}
		}
		break;

	case IIO_ATTR_TYPE_DEVICE:
		for (size_t i = 0; i < cfg->num_attrs; i++) {
			if (!strcmp(attr->name, cfg->attrs[i].name)) {
				return iio_device_sensor_read_device_attr(dev, SENSOR_CHAN_ALL, cfg->attrs[i].attr, dst, len);
			}
		}
		break;

	case IIO_ATTR_TYPE_BUFFER:
		/* Invalidate cache so next sample round re-fetches */
		data->fetched = false;
		dst[0] = '\0';
		return 1;

	default:
		break;
	}

	LOG_ERR("Invalid attr: %s", attr->name);
	return -EINVAL;
}

static int iio_device_sensor_write_device_attr(const struct device *dev, enum sensor_channel channel, enum sensor_attribute attr, const char *src, size_t len)
{
	// TODO: Configurable max RAW size
	char buffer[32] = { 0 };
	char *endptr = NULL;

	const struct iio_device_sensor_config *cfg = dev->config;
	struct sensor_value val = {0};
	int ret;

	/* Be extra cautious src is properly terminated before calling strtol */

	strncpy(buffer, src, MIN(len, ARRAY_SIZE(buffer)) - 1);
	buffer[31] = '\0';

	val.val1 = strtol(buffer, &endptr, 10);

	if (*endptr == '.') {
		char *start = endptr + 1;
		uint32_t frac = strtol(start, &endptr, 10);
		if (*endptr != '\0') {
			return -EINVAL;
		}

		size_t places = endptr - start;
		val.val2 = (uint32_t)(((uint64_t)frac) * 1000000 / (pow(10, places)));
	} else if (*endptr != '\0') {
		return -EINVAL;
	}

	ret = sensor_attr_set(cfg->sensor_dev, channel, attr, &val);
	if (ret < 0) {
		return ret;
	}

	return len;
}

static int iio_device_sensor_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len)
{
	const struct iio_device_sensor_config *cfg = dev->config;
	const struct iio_device_sensor_chan *channel;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		channel = (const struct iio_device_sensor_chan *)(intptr_t)
			iio_channel_get_pdata(attr->iio.chn);

		if (channel->channel >= SENSOR_CHAN_COMMON_COUNT) {
			LOG_ERR("Invalid sensor channel: %d", channel->channel);
			return -EINVAL;
		}

		for (size_t i = 0; i < channel->num_attrs; i++) {
			if (!strcmp(attr->name, channel->attrs[i].name)) {
				return iio_device_sensor_write_device_attr(dev, channel->channel, channel->attrs[i].attr, src, len);
			}
		}
		break;

	case IIO_ATTR_TYPE_DEVICE:
		for (size_t i = 0; i < cfg->num_attrs; i++) {
			if (!strcmp(attr->name, cfg->attrs[i].name)) {
				return iio_device_sensor_write_device_attr(dev, SENSOR_CHAN_ALL, cfg->attrs[i].attr, src, len);
			}
		}
		break;

	default:
		break;
	}

	LOG_ERR("Invalid attr");
	return -EINVAL;
}

static int iio_device_sensor_init(const struct device *dev)
{
	const struct iio_device_sensor_config *config = dev->config;

	if (!device_is_ready(config->sensor_dev)) {
		LOG_ERR("Sensor device %s is not ready",
			config->sensor_dev->name);
		return -ENODEV;
	}

	return 0;
}

static DEVICE_API(iio_device, iio_device_sensor_driver_api) = {
	.add_channels   = iio_device_sensor_add_channels,
	.read_attr      = iio_device_sensor_read_attr,
	.write_attr     = iio_device_sensor_write_attr,
};

#define DT_DRV_COMPAT iio_sensor

#define IIO_DEVICE_SENSOR_ATTR_INST(n, prop, idx)				\
	(struct iio_device_sensor_attr) { 					\
		.attr = DT_PROP_BY_IDX(n, prop, idx),				\
		.name = DT_PROP_BY_IDX(n, attr_names, idx),			\
	}

#define IIO_DEVICE_SENSOR_CHANNEL_PROP(n, prop, idx)				\
	(struct iio_device_sensor_chan) { 					\
		.channel = DT_PROP_BY_IDX(n, prop, idx),			\
	}

#define IIO_DEVICE_SENSOR_CHANNEL_CHILD_ATTRS(n)				\
	static const struct iio_device_sensor_attr				\
		iio_device_sensor_attrs_child_##n[] =				\
	{ DT_FOREACH_PROP_ELEM_SEP(n, attrs, IIO_DEVICE_SENSOR_ATTR_INST, (,)) };

#define IIO_DEVICE_SENSOR_CHANNEL_CHILD(n)					\
	(struct iio_device_sensor_chan) { 					\
		.channel = DT_PROP(n, channel),					\
		.attrs = iio_device_sensor_attrs_child_##n,			\
		.num_attrs = ARRAY_SIZE(iio_device_sensor_attrs_child_##n),	\
	}


#define IIO_DEVICE_SENSOR_INIT(inst)						\
										\
COND_CODE_1(DT_INST_CHILD_NUM_STATUS_OKAY(inst), (), (				\
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst,					\
		IIO_DEVICE_SENSOR_CHANNEL_CHILD_ATTRS)				\
))										\
BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, sensor_channels, 0)			\
		!= DT_INST_CHILD_NUM_STATUS_OKAY(inst),				\
	     "Set either sensor-channels or assign channel child nodes");	\
static const struct iio_device_sensor_chan iio_device_sensor_channels_##inst[] =\
	{									\
		COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, sensor_channels), (	\
		DT_INST_FOREACH_PROP_ELEM_SEP(inst, sensor_channels, IIO_DEVICE_SENSOR_CHANNEL_PROP, (,))),	\
		(DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(inst, IIO_DEVICE_SENSOR_CHANNEL_CHILD, (,))))		\
	};									\
BUILD_ASSERT(ARRAY_SIZE(iio_device_sensor_channels_##inst) > 0,			\
	     "One or more sensor channels must be set");			\
										\
BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, attrs, 0)				\
		== DT_INST_PROP_LEN_OR(inst, attr_names, 0),			\
	     "The attr and attr-names property lengths must match");		\
static const struct iio_device_sensor_attr iio_device_sensor_attrs_##inst[] =	\
	{ DT_INST_FOREACH_PROP_ELEM_SEP(inst, attrs, IIO_DEVICE_SENSOR_ATTR_INST, ( , )) };	\
										\
static struct sensor_value							\
	iio_device_sensor_cache_##inst[ARRAY_SIZE(				\
		iio_device_sensor_channels_##inst)];				\
										\
static struct iio_device_sensor_data iio_device_sensor_data_##inst = {		\
	.cache        = iio_device_sensor_cache_##inst,				\
	.fetched      = false,							\
};										\
										\
static const struct iio_device_sensor_config					\
		iio_device_sensor_config_##inst = {				\
	.sensor_dev  = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor_device)),	\
	.channels    = iio_device_sensor_channels_##inst,			\
	.num_channels = ARRAY_SIZE(iio_device_sensor_channels_##inst),		\
	.attrs       = iio_device_sensor_attrs_##inst,				\
	.num_attrs   = ARRAY_SIZE(iio_device_sensor_attrs_##inst),		\
};										\
										\
IIO_DEVICE_DT_INST_DEFINE(inst,							\
	iio_device_sensor_init, NULL,						\
	&iio_device_sensor_data_##inst,						\
	&iio_device_sensor_config_##inst,					\
	POST_KERNEL, CONFIG_LIBIIO_IIO_DEVICE_SENSOR_INIT_PRIORITY,		\
	&iio_device_sensor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIO_DEVICE_SENSOR_INIT)
