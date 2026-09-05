/*
 * Copyright (c) 2025-2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/types.h>
#include <zephyr/logging/log.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#ifdef CONFIG_LIBIIO_STREAMING
#include <dt-bindings/iio/sensor-triggers.h>

BUILD_ASSERT(IIO_SENSOR_TRIG_TIMER == SENSOR_TRIG_TIMER);
BUILD_ASSERT(IIO_SENSOR_TRIG_DATA_READY == SENSOR_TRIG_DATA_READY);
BUILD_ASSERT(IIO_SENSOR_TRIG_FIFO_WATERMARK == SENSOR_TRIG_FIFO_WATERMARK);
BUILD_ASSERT(IIO_SENSOR_TRIG_FIFO_FULL == SENSOR_TRIG_FIFO_FULL);
BUILD_ASSERT(IIO_SENSOR_TRIG_OVERFLOW == SENSOR_TRIG_OVERFLOW);
#endif

LOG_MODULE_REGISTER(iio_device_sensor, CONFIG_LIBIIO_LOG_LEVEL);

/*
 * The sensor decoder returns Q31 values with a shift:
 * real_SI = q31_value * 2^(shift - 31)
 *
 * IIO convention:  processed_value = raw * scale
 *
 * We present:
 *   raw     – milli-units integer  (SI_value * 1000)
 *   scale   – "0.001"
 *
 * Buffered samples carry the same milli-units, so channels are 32 bits wide
 * to hold the full measuring range.
 */

#define SENSOR_RAW_LEN		12
#define SENSOR_SCALE_LEN	6
#define SENSOR_READ_BUF_SIZE	128

struct sensor_channel_map {
	enum sensor_channel sensor_chan;
	const char *iio_type;
	const char *modifier;
	uint8_t bits;
	bool is_signed;
};

static const struct sensor_channel_map channel_map[] = {

	{SENSOR_CHAN_VOLTAGE,       "voltage",          NULL,      32, true},
	{SENSOR_CHAN_VSHUNT,        "voltage",          "shunt",   32, true},

	{SENSOR_CHAN_CURRENT,       "current",          NULL,      32, true},
	{SENSOR_CHAN_POWER,         "power",            NULL,      32, true},
	{SENSOR_CHAN_RESISTANCE,    "resistance",       NULL,      32, false},

	{SENSOR_CHAN_DIE_TEMP,      "temp",             "die",     32, true},
	{SENSOR_CHAN_AMBIENT_TEMP,  "temp",             "ambient", 32, true},

	{SENSOR_CHAN_ACCEL_X,       "accel",            "x",       32, true},
	{SENSOR_CHAN_ACCEL_Y,       "accel",            "y",       32, true},
	{SENSOR_CHAN_ACCEL_Z,       "accel",            "z",       32, true},

	{SENSOR_CHAN_GYRO_X,        "anglvel",          "x",       32, true},
	{SENSOR_CHAN_GYRO_Y,        "anglvel",          "y",       32, true},
	{SENSOR_CHAN_GYRO_Z,        "anglvel",          "z",       32, true},

	{SENSOR_CHAN_MAGN_X,        "magn",             "x",       32, true},
	{SENSOR_CHAN_MAGN_Y,        "magn",             "y",       32, true},
	{SENSOR_CHAN_MAGN_Z,        "magn",             "z",       32, true},

	{SENSOR_CHAN_PRESS,         "pressure",         NULL,      32, false},
	{SENSOR_CHAN_HUMIDITY,      "humidityrelative", NULL,      32, false},
	{SENSOR_CHAN_LIGHT,         "illuminance",      NULL,      32, false},
	{SENSOR_CHAN_AMBIENT_LIGHT, "illuminance",      NULL,      32, false},
};

struct iio_device_sensor_attr {
	const char *name;
	enum sensor_attribute attr;
};

struct iio_device_sensor_chan_attrs {
	const struct iio_device_sensor_attr *attrs;
	size_t num_attrs;
};

/* chan_idx is always 0, so a device must not declare a channel type twice */
struct iio_device_sensor_config {
	const struct device *sensor_dev;
	struct sensor_chan_spec *chan_specs;
	const struct iio_device_sensor_chan_attrs *chan_attrs;
	size_t num_channels;
	const struct iio_device_sensor_attr *attrs;
	size_t num_attrs;
	const struct rtio_iodev *iodev;
	/* For attribute reads; sensor_read() takes whatever completes first */
	struct rtio *rtio_ctx;
#ifdef CONFIG_LIBIIO_STREAMING
	const struct rtio_iodev *stream_iodev;
	struct iio_device_stream *stream;
#endif
};

/* bulk read — one sensor_read() per round, then decode each channel.
 * This avoids repeated data-ready waits (e.g. ADXL345 polls STATUS per fetch).
 */
struct iio_device_sensor_data {
	int32_t *cache;
	uint8_t read_buf[SENSOR_READ_BUF_SIZE];
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

/* X, Y, Z, then XYZ, in each three-axis group of enum sensor_channel */
static const uint16_t axis_groups[] = {
	SENSOR_CHAN_ACCEL_X, SENSOR_CHAN_GYRO_X, SENSOR_CHAN_MAGN_X,
};

BUILD_ASSERT(SENSOR_CHAN_ACCEL_X + 3 == SENSOR_CHAN_ACCEL_XYZ);
BUILD_ASSERT(SENSOR_CHAN_GYRO_X + 3 == SENSOR_CHAN_GYRO_XYZ);
BUILD_ASSERT(SENSOR_CHAN_MAGN_X + 3 == SENSOR_CHAN_MAGN_XYZ);

/*
 * Some decoders only answer SENSOR_CHAN_*_XYZ. Returns the axis index and sets
 * @p fallback to the XYZ spec, or returns -1 for a channel that is not an axis.
 */
static int axis_fallback(struct sensor_chan_spec spec,
		struct sensor_chan_spec *fallback)
{
	for (size_t i = 0; i < ARRAY_SIZE(axis_groups); i++) {
		if (spec.chan_type >= axis_groups[i] && spec.chan_type < axis_groups[i] + 3) {
			*fallback = (struct sensor_chan_spec){axis_groups[i] + 3, spec.chan_idx};
			return spec.chan_type - axis_groups[i];
		}
	}

	return -1;
}

/*
 * Decode one frame of a channel, with the XYZ fallback. get_size_info() guards
 * the q31 attempt because some decoders write a full three-axis frame even for
 * a single-axis spec, which would overrun a sensor_q31_data.
 *
 * Returns 1 when a value was decoded, otherwise the decoder's own return.
 */
static int decode_channel_q31(const struct sensor_decoder_api *decoder,
		const uint8_t *buf, struct sensor_chan_spec spec,
		uint32_t *fit, q31_t *value, int8_t *shift)
{
	struct sensor_chan_spec xyz_spec;
	size_t base_size, frame_size;
	int axis;
	int ret;

	/* Silence -Wmaybe-uninitialized in callers */
	*value = 0;
	*shift = 0;

	ret = decoder->get_size_info(spec, &base_size, &frame_size);
	if (ret == 0 && frame_size == sizeof(struct sensor_q31_sample_data)) {
		struct sensor_q31_data q31_data = {0};

		ret = decoder->decode(buf, spec, fit, 1, &q31_data);
		if (ret == 1) {
			*value = q31_data.readings[0].value;
			*shift = q31_data.shift;
			return 1;
		}
		return ret;
	}

	axis = axis_fallback(spec, &xyz_spec);
	if (axis >= 0) {
		struct sensor_three_axis_data axis_data = {0};

		ret = decoder->decode(buf, xyz_spec, fit, 1, &axis_data);
		if (ret == 1) {
			*value = axis_data.readings[0].values[axis];
			*shift = axis_data.shift;
			return 1;
		}
		return ret;
	}

	return ret;
}

static int32_t decode_channel_milli(const struct sensor_decoder_api *decoder,
		const uint8_t *buf, struct sensor_chan_spec spec)
{
	uint32_t fit = 0;
	q31_t value;
	int8_t shift;
	int ret;

	ret = decode_channel_q31(decoder, buf, spec, &fit, &value, &shift);
	if (ret != 1) {
		LOG_DBG("No value for channel (type %u, index %u): %d",
			spec.chan_type, spec.chan_idx, ret);
		return 0;
	}

	return iio_q31_to_milli(value, shift);
}

static int sensor_read_all(const struct device *dev)
{
	const struct iio_device_sensor_config *config = dev->config;
	struct iio_device_sensor_data *data = dev->data;
	const struct sensor_decoder_api *decoder;
	int ret;

	if (data->fetched) {
		return 0;
	}

	ret = sensor_read(config->iodev, config->rtio_ctx,
			  data->read_buf, sizeof(data->read_buf));
	if (ret < 0) {
		LOG_ERR("sensor_read failed: %d", ret);
		return ret;
	}

	ret = sensor_get_decoder(config->sensor_dev, &decoder);
	if (ret < 0) {
		LOG_ERR("sensor_get_decoder failed: %d", ret);
		return ret;
	}

	for (size_t i = 0; i < config->num_channels; i++) {
		data->cache[i] = decode_channel_milli(decoder, data->read_buf,
						      config->chan_specs[i]);
	}

	data->fetched = true;
	return 0;
}

/* add_channels() stores each channel's spec in its pdata */
static const struct sensor_chan_spec *chan_spec_of(const struct iio_channel *chn)
{
	return (const struct sensor_chan_spec *)iio_channel_get_pdata(chn);
}

/* The cache is in chan_specs order, so the slot is the spec's position. */
static int sensor_cache_index(const struct device *dev,
		const struct sensor_chan_spec *spec)
{
	const struct iio_device_sensor_config *config = dev->config;

	if (spec < config->chan_specs ||
	    (size_t)(spec - config->chan_specs) >= config->num_channels) {
		return -ENOENT;
	}

	return (int)(spec - config->chan_specs);
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
		const struct sensor_chan_spec *spec = &config->chan_specs[index];
		const struct iio_device_sensor_chan_attrs *chan_attrs =
			&config->chan_attrs[index];
		enum sensor_channel sensor_chan =
			(enum sensor_channel)spec->chan_type;
		const struct sensor_channel_map *map = find_channel_map(sensor_chan);

		if (!map) {
			LOG_WRN("No mapping for sensor channel %d, skipping",
				sensor_chan);
			continue;
		}

		if (map->modifier) {
			snprintk(id, sizeof(id), "%s_%s",
				 map->iio_type, map->modifier);
		} else {
			uint8_t cnt = type_counters[sensor_chan]++;

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
			(struct iio_channel_pdata *)spec);

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

		for (size_t attr_index = 0; attr_index < chan_attrs->num_attrs; attr_index++) {
			if (iio_channel_add_attr(iio_channel, chan_attrs->attrs[attr_index].name, type, filename)) {
				LOG_ERR("Could not add channel %d attr %s",
					index, chan_attrs->attrs[attr_index].name);
				return -EINVAL;
			}
		}

		LOG_DBG("Added IIO channel %s for sensor channel (type %u, index %u)",
			id, spec->chan_type, spec->chan_idx);
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

static int iio_device_sensor_read_channel_raw(const struct device *dev,
		const struct sensor_chan_spec *spec, char *dst, size_t len)
{
	struct iio_device_sensor_data *data = dev->data;
	int ret, idx;

	if (len < SENSOR_RAW_LEN) {
		LOG_ERR("Buffer too small for raw value");
		return -ENOMEM;
	}

	idx = sensor_cache_index(dev, spec);
	if (idx < 0) {
		LOG_ERR("Channel (type %u, index %u) not in config",
			spec->chan_type, spec->chan_idx);
		return idx;
	}

	if (idx == 0) {
		data->fetched = false;
	}

	ret = sensor_read_all(dev);
	if (ret < 0) {
		return ret;
	}

	return snprintk(dst, len, "%d", data->cache[idx]) + 1;
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

/* chan_attrs is in chan_specs order, like the cache */
static const struct iio_device_sensor_chan_attrs *sensor_chan_attrs(
		const struct device *dev, const struct sensor_chan_spec *spec)
{
	const struct iio_device_sensor_config *cfg = dev->config;
	int idx = sensor_cache_index(dev, spec);

	return idx < 0 ? NULL : &cfg->chan_attrs[idx];
}

static int iio_device_sensor_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_sensor_config *cfg = dev->config;
	struct iio_device_sensor_data *data = dev->data;
	const struct iio_device_sensor_chan_attrs *chan_attrs;
	const struct sensor_chan_spec *spec;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		spec = chan_spec_of(attr->iio.chn);

		if (!spec || spec->chan_type >= SENSOR_CHAN_COMMON_COUNT) {
			LOG_ERR("Invalid sensor channel: %d",
				spec ? spec->chan_type : -1);
			return -EINVAL;
		}

		if (!strcmp(attr->name, raw_name)) {
			return iio_device_sensor_read_channel_raw(dev, spec,
					dst, len);
		} else if (!strcmp(attr->name, scale_name)) {
			return iio_device_sensor_read_channel_scale(dst, len);
		}

		chan_attrs = sensor_chan_attrs(dev, spec);
		for (size_t i = 0; chan_attrs && i < chan_attrs->num_attrs; i++) {
			if (!strcmp(attr->name, chan_attrs->attrs[i].name)) {
				return iio_device_sensor_read_device_attr(dev, (enum sensor_channel)spec->chan_type, chan_attrs->attrs[i].attr, dst, len);
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
	const struct iio_device_sensor_chan_attrs *chan_attrs;
	const struct sensor_chan_spec *spec;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		spec = chan_spec_of(attr->iio.chn);

		if (!spec || spec->chan_type >= SENSOR_CHAN_COMMON_COUNT) {
			LOG_ERR("Invalid sensor channel: %d",
				spec ? spec->chan_type : -1);
			return -EINVAL;
		}

		chan_attrs = sensor_chan_attrs(dev, spec);
		for (size_t i = 0; chan_attrs && i < chan_attrs->num_attrs; i++) {
			if (!strcmp(attr->name, chan_attrs->attrs[i].name)) {
				return iio_device_sensor_write_device_attr(dev, (enum sensor_channel)spec->chan_type, chan_attrs->attrs[i].attr, src, len);
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

#ifdef CONFIG_LIBIIO_STREAMING

static int iio_device_sensor_decoder(const struct device *dev,
		const struct sensor_decoder_api **decoder)
{
	const struct iio_device_sensor_config *config = dev->config;

	return sensor_get_decoder(config->sensor_dev, decoder);
}

static struct iio_device_stream *iio_device_sensor_get_stream(const struct device *dev)
{
	const struct iio_device_sensor_config *config = dev->config;

	return config->stream;
}

static int iio_device_sensor_submit_read(const struct device *dev)
{
	const struct iio_device_sensor_config *config = dev->config;

	/* Sensors without a submit op use the RTIO workqueue fallback */
	return sensor_read_async_mempool(config->iodev, config->stream->rtio, NULL);
}

static int iio_device_sensor_arm_stream(const struct device *dev, rtio_sqe_handle_t *handle)
{
	const struct iio_device_sensor_config *config = dev->config;
	int ret;

	if (!config->stream_iodev) {
		return -ENOSYS;
	}

	ret = sensor_stream(config->stream_iodev, config->stream->rtio, NULL, handle);
	if (ret) {
		LOG_ERR("Could not stream %s: %d", dev->name, ret);
	}

	return ret;
}

static int iio_device_sensor_frame_count(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint16_t *count)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_chan_spec spec = *chan_spec_of(chn);
	struct sensor_chan_spec xyz_spec;
	int ret;

	ret = iio_device_sensor_decoder(dev, &decoder);
	if (ret) {
		return ret;
	}

	ret = decoder->get_frame_count(buf, spec, count);
	if (ret == 0) {
		return 0;
	}

	/* Same XYZ fallback as decode_channel_q31() */
	if (axis_fallback(spec, &xyz_spec) >= 0) {
		return decoder->get_frame_count(buf, xyz_spec, count);
	}

	return ret;
}

static int iio_device_sensor_decode(const struct device *dev, const uint8_t *buf,
		const struct iio_channel *chn, uint32_t *fit, q31_t *value, int8_t *shift)
{
	const struct sensor_decoder_api *decoder;
	int ret;

	ret = iio_device_sensor_decoder(dev, &decoder);
	if (ret) {
		return ret;
	}

	return decode_channel_q31(decoder, buf, *chan_spec_of(chn), fit, value, shift);
}

#endif /* CONFIG_LIBIIO_STREAMING */

static DEVICE_API(iio_device, iio_device_sensor_driver_api) = {
	.add_channels   = iio_device_sensor_add_channels,
	.read_attr      = iio_device_sensor_read_attr,
	.write_attr     = iio_device_sensor_write_attr,
#ifdef CONFIG_LIBIIO_STREAMING
	.get_stream     = iio_device_sensor_get_stream,
	.submit_read    = iio_device_sensor_submit_read,
	.arm_stream     = iio_device_sensor_arm_stream,
	.frame_count    = iio_device_sensor_frame_count,
	.decode         = iio_device_sensor_decode,
#endif
};

#define DT_DRV_COMPAT iio_sensor

#define IIO_SENSOR_CHAN_TO_SPEC(node_id, prop, idx)				\
	{ (uint16_t)DT_PROP_BY_IDX(node_id, prop, idx), 0 }

#define IIO_SENSOR_CHAN_NO_ATTRS(node_id, prop, idx)				\
	{ NULL, 0 }

#define IIO_DEVICE_SENSOR_ATTR_INST(n, prop, idx)				\
	(struct iio_device_sensor_attr) { 					\
		.attr = DT_PROP_BY_IDX(n, prop, idx),				\
		.name = DT_PROP_BY_IDX(n, attr_names, idx),			\
	}

#define IIO_SENSOR_CHILD_TO_SPEC(n)						\
	{ (uint16_t)DT_PROP(n, channel), 0 }

#define IIO_DEVICE_SENSOR_CHANNEL_CHILD_ATTRS(n)				\
	static const struct iio_device_sensor_attr				\
		iio_device_sensor_attrs_child_##n[] =				\
	{ DT_FOREACH_PROP_ELEM_SEP(n, attrs, IIO_DEVICE_SENSOR_ATTR_INST, (,)) };

#define IIO_DEVICE_SENSOR_CHANNEL_CHILD(n)					\
	{									\
		.attrs = iio_device_sensor_attrs_child_##n,			\
		.num_attrs = ARRAY_SIZE(iio_device_sensor_attrs_child_##n),	\
	}

/* The executor posts the completion before it frees the SQE */
#define IIO_DEVICE_SENSOR_RTIO_DEFINE(inst)					\
	RTIO_DEFINE(iio_device_sensor_rtio_##inst, 2, 1)

#ifdef CONFIG_LIBIIO_STREAMING
#define IIO_DEVICE_SENSOR_STREAM_RTIO_DEFINE(inst)				\
	RTIO_DEFINE_WITH_MEMPOOL(iio_device_sensor_stream_rtio_##inst, 3,	\
		DT_INST_PROP(inst, rtio_block_count) + 1,			\
		DT_INST_PROP(inst, rtio_block_count),				\
		DT_INST_PROP(inst, rtio_block_size), sizeof(void *));		\
	IIO_DEVICE_STREAM_DT_DEFINE(DT_DRV_INST(inst),				\
		&iio_device_sensor_stream_rtio_##inst);

#define IIO_SENSOR_STREAM_TRIGGER(node_id, prop, idx)				\
	{									\
		.trigger = (enum sensor_trigger_type)				\
			DT_PROP_BY_IDX(node_id, prop, idx),			\
		.opt = SENSOR_STREAM_DATA_INCLUDE,				\
	}

#define IIO_DEVICE_SENSOR_STREAM_IODEV_DEFINE(inst)				\
	static struct sensor_stream_trigger					\
		iio_device_sensor_triggers_##inst[] = {				\
		DT_INST_FOREACH_PROP_ELEM_SEP(inst, stream_triggers,		\
			IIO_SENSOR_STREAM_TRIGGER, (,))				\
	};									\
	static struct sensor_read_config					\
			iio_device_sensor_stream_cfg_##inst = {			\
		.sensor = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor_device)),	\
		.is_streaming = true,						\
		.triggers = iio_device_sensor_triggers_##inst,			\
		.count = ARRAY_SIZE(iio_device_sensor_triggers_##inst),		\
		.max = ARRAY_SIZE(iio_device_sensor_triggers_##inst),		\
	};									\
	RTIO_IODEV_DEFINE(iio_device_sensor_stream_iodev_##inst,		\
		&__sensor_iodev_api, &iio_device_sensor_stream_cfg_##inst)

#define IIO_DEVICE_SENSOR_STREAM_IODEV(inst)					\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, stream_triggers),		\
		(IIO_DEVICE_SENSOR_STREAM_IODEV_DEFINE(inst);), ())

#define IIO_DEVICE_SENSOR_STREAM_INITIALIZER(inst)				\
	.stream = IIO_DEVICE_STREAM_DT_GET(DT_DRV_INST(inst)),			\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, stream_triggers),		\
		(.stream_iodev = &iio_device_sensor_stream_iodev_##inst,), ())
#else
#define IIO_DEVICE_SENSOR_STREAM_RTIO_DEFINE(inst)
#define IIO_DEVICE_SENSOR_STREAM_IODEV(inst)
#define IIO_DEVICE_SENSOR_STREAM_INITIALIZER(inst)
#endif

#define IIO_DEVICE_SENSOR_INIT(inst)						\
										\
COND_CODE_1(DT_INST_CHILD_NUM_STATUS_OKAY(inst), (), (				\
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst,					\
		IIO_DEVICE_SENSOR_CHANNEL_CHILD_ATTRS)				\
))										\
BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, sensor_channels, 0)			\
		!= DT_INST_CHILD_NUM_STATUS_OKAY(inst),				\
	     "Set either sensor-channels or assign channel child nodes");	\
static struct sensor_chan_spec iio_device_sensor_chan_specs_##inst[] = {	\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, sensor_channels), (		\
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, sensor_channels,			\
		IIO_SENSOR_CHAN_TO_SPEC, (,))),					\
	(DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(inst,				\
		IIO_SENSOR_CHILD_TO_SPEC, (,))))				\
};										\
BUILD_ASSERT(ARRAY_SIZE(iio_device_sensor_chan_specs_##inst) > 0,		\
	     "One or more sensor channels must be set");			\
										\
static const struct iio_device_sensor_chan_attrs				\
		iio_device_sensor_chan_attrs_##inst[] = {			\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, sensor_channels), (		\
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, sensor_channels,			\
		IIO_SENSOR_CHAN_NO_ATTRS, (,))),				\
	(DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(inst,				\
		IIO_DEVICE_SENSOR_CHANNEL_CHILD, (,))))				\
};										\
										\
BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, attrs, 0)				\
		== DT_INST_PROP_LEN_OR(inst, attr_names, 0),			\
	     "The attr and attr-names property lengths must match");		\
static const struct iio_device_sensor_attr iio_device_sensor_attrs_##inst[] =	\
	{ DT_INST_FOREACH_PROP_ELEM_SEP(inst, attrs, IIO_DEVICE_SENSOR_ATTR_INST, ( , )) };	\
										\
static struct sensor_read_config iio_device_sensor_read_cfg_##inst = {		\
	.sensor = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor_device)),		\
	.is_streaming = false,							\
	.channels = iio_device_sensor_chan_specs_##inst,			\
	.count = ARRAY_SIZE(iio_device_sensor_chan_specs_##inst),		\
	.max = ARRAY_SIZE(iio_device_sensor_chan_specs_##inst),			\
};										\
										\
RTIO_IODEV_DEFINE(iio_device_sensor_iodev_##inst,				\
	&__sensor_iodev_api, &iio_device_sensor_read_cfg_##inst);		\
										\
IIO_DEVICE_SENSOR_RTIO_DEFINE(inst);						\
										\
IIO_DEVICE_SENSOR_STREAM_RTIO_DEFINE(inst)					\
										\
IIO_DEVICE_SENSOR_STREAM_IODEV(inst)						\
										\
static int32_t									\
	iio_device_sensor_cache_##inst[ARRAY_SIZE(				\
		iio_device_sensor_chan_specs_##inst)];				\
										\
static struct iio_device_sensor_data iio_device_sensor_data_##inst = {		\
	.cache        = iio_device_sensor_cache_##inst,				\
	.fetched      = false,							\
};										\
										\
static const struct iio_device_sensor_config					\
		iio_device_sensor_config_##inst = {				\
	.sensor_dev  = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor_device)),	\
	.chan_specs   = iio_device_sensor_chan_specs_##inst,			\
	.chan_attrs   = iio_device_sensor_chan_attrs_##inst,			\
	.num_channels = ARRAY_SIZE(iio_device_sensor_chan_specs_##inst),	\
	.attrs        = iio_device_sensor_attrs_##inst,				\
	.num_attrs    = ARRAY_SIZE(iio_device_sensor_attrs_##inst),		\
	.iodev        = &iio_device_sensor_iodev_##inst,			\
	.rtio_ctx     = &iio_device_sensor_rtio_##inst,				\
	IIO_DEVICE_SENSOR_STREAM_INITIALIZER(inst)				\
};										\
										\
IIO_DEVICE_DT_INST_DEFINE(inst,							\
	iio_device_sensor_init, NULL,						\
	&iio_device_sensor_data_##inst,						\
	&iio_device_sensor_config_##inst,					\
	POST_KERNEL, CONFIG_LIBIIO_IIO_DEVICE_SENSOR_INIT_PRIORITY,		\
	&iio_device_sensor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIO_DEVICE_SENSOR_INIT)
