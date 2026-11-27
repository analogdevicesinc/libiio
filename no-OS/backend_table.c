/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * The generic add_channels/read_attr/write_attr/read_samples of a
 * table-driven device (include/iio_device.h): the device describes its
 * channels in a noos_iio_chan table and handles values, as a Linux driver
 * does; this file creates the channels and attributes, names them as Linux
 * does, serves the attributes that are data (struct noos_iio_value), sends
 * the others to read_raw/write_raw, formats and parses the strings and
 * packs the samples.
 */

#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio.h>
#include <iio_device.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- attribute value helpers (noos_value_*, see iio_device.h) --- */

/* The length a read_attr returns: the string's, plus its NUL. */
static ssize_t value_len(ssize_t ret, size_t len)
{
	if (!len)
		return -ERANGE;

	return ret < 0 ? ret : ret + 1;
}

/* 10 to the power @digits. */
static unsigned int value_pow10(unsigned int digits)
{
	unsigned int p = 1;

	while (digits--)
		p *= 10;

	return p;
}

/* Format an integer value; see iio_device.h. */
ssize_t noos_value_format_int(char *dst, size_t len, long long val)
{
	return value_len(iio_snprintf(dst, len, "%lld", val), len);
}

/* Format a string value; see iio_device.h. */
ssize_t noos_value_format_str(char *dst, size_t len, const char *val)
{
	return value_len(iio_snprintf(dst, len, "%s", val), len);
}

/* Format val + val2 / 10^digits as fixed point; see iio_device.h. */
ssize_t noos_value_format_fixed(char *dst, size_t len, int val, int val2, unsigned int digits)
{
	long long ival = val, fval = val2;
	bool negative = ival < 0 || fval < 0;

	if (digits < 1 || digits > 9)
		return -EINVAL;

	if (ival < 0)
		ival = -ival;
	if (fval < 0)
		fval = -fval;
	if (fval >= value_pow10(digits))
		return -EINVAL;

	return value_len(iio_snprintf(dst, len, "%s%lld.%0*lld", negative ? "-" : "", ival,
					 (int)digits, fval),
			len);
}

/* At the end of a written value: optionally one newline, then the NUL. */
static bool value_end(const char *end)
{
	if (*end == '\n')
		end++;

	return *end == '\0';
}

/* Parse an integer, allowing one trailing newline; see iio_device.h. */
int noos_value_parse_int(const char *src, long long *val)
{
	long long parsed;
	char *end;

	if (!src || !val)
		return -EINVAL;

	if (!(*src == '-' || *src == '+' || (*src >= '0' && *src <= '9')))
		return -EINVAL;

	errno = 0;
	parsed = strtoll(src, &end, 10);
	if (end == src || errno == ERANGE || !value_end(end))
		return -EINVAL;

	*val = parsed;

	return 0;
}

/* Parse a fixed-point value into val and val2; see iio_device.h. */
int noos_value_parse_fixed(const char *src, int *val, int *val2, unsigned int digits)
{
	long long integer = 0;
	unsigned int mult, fract = 0;
	bool negative = false, fraction = false, digit = false;

	if (!src || !val || !val2 || digits < 1 || digits > 9)
		return -EINVAL;

	mult = value_pow10(digits - 1);

	if (*src == '-' || *src == '+')
		negative = *src++ == '-';

	for (; *src && *src != '\n'; src++) {
		if (*src >= '0' && *src <= '9') {
			digit = true;
			if (!fraction) {
				integer = integer * 10 + (*src - '0');
				if (integer > INT_MAX)
					return -EINVAL;
			} else if (mult) {
				fract += mult * (unsigned int)(*src - '0');
				mult /= 10;
			}
		} else if (*src == '.' && !fraction) {
			fraction = true;
		} else {
			return -EINVAL;
		}
	}

	if (!digit || !value_end(src))
		return -EINVAL;

	*val = (int)integer;
	*val2 = (int)fract;

	/* -0.25 has no sign on its integer part, so the fraction carries it. */
	if (negative) {
		if (integer)
			*val = -*val;
		else
			*val2 = -*val2;
	}

	return 0;
}

/* Index of a name in a table of names; see iio_device.h. */
int noos_value_match_str(const char *const *table, size_t count, const char *src, size_t len)
{
	size_t i;

	if (!table || !src)
		return -EINVAL;

	while (len && (src[len - 1] == '\n' || src[len - 1] == '\0'))
		len--;

	for (i = 0; i < count; i++) {
		if (table[i] && strlen(table[i]) == len && !strncmp(src, table[i], len))
			return (int)i;
	}

	return -EINVAL;
}

#define TABLE_MAX_CHANNELS 32

/* Room for an id, and for a Linux attribute file name. */
#define TABLE_ID_LEN 64
#define TABLE_NAME_LEN 128

/* Linux's iio_chan_info_postfix[]. */
static const char *const info_names[NOOS_IIO_INFO_COUNT] = {
	[NOOS_IIO_INFO_RAW] = "raw",
	[NOOS_IIO_INFO_PROCESSED] = "input",
	[NOOS_IIO_INFO_SCALE] = "scale",
	[NOOS_IIO_INFO_OFFSET] = "offset",
	[NOOS_IIO_INFO_CALIBSCALE] = "calibscale",
	[NOOS_IIO_INFO_CALIBBIAS] = "calibbias",
	[NOOS_IIO_INFO_PEAK] = "peak_raw",
	[NOOS_IIO_INFO_PEAK_SCALE] = "peak_scale",
	[NOOS_IIO_INFO_QUADRATURE_CORRECTION_RAW] = "quadrature_correction_raw",
	[NOOS_IIO_INFO_AVERAGE_RAW] = "mean_raw",
	[NOOS_IIO_INFO_LOW_PASS_FILTER_3DB_FREQUENCY] = "filter_low_pass_3db_frequency",
	[NOOS_IIO_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY] = "filter_high_pass_3db_frequency",
	[NOOS_IIO_INFO_SAMP_FREQ] = "sampling_frequency",
	[NOOS_IIO_INFO_FREQUENCY] = "frequency",
	[NOOS_IIO_INFO_PHASE] = "phase",
	[NOOS_IIO_INFO_HARDWAREGAIN] = "hardwaregain",
	[NOOS_IIO_INFO_HYSTERESIS] = "hysteresis",
	[NOOS_IIO_INFO_HYSTERESIS_RELATIVE] = "hysteresis_relative",
	[NOOS_IIO_INFO_INT_TIME] = "integration_time",
	[NOOS_IIO_INFO_ENABLE] = "en",
	[NOOS_IIO_INFO_CALIBHEIGHT] = "calibheight",
	[NOOS_IIO_INFO_CALIBWEIGHT] = "calibweight",
	[NOOS_IIO_INFO_DEBOUNCE_COUNT] = "debounce_count",
	[NOOS_IIO_INFO_DEBOUNCE_TIME] = "debounce_time",
	[NOOS_IIO_INFO_CALIBEMISSIVITY] = "calibemissivity",
	[NOOS_IIO_INFO_OVERSAMPLING_RATIO] = "oversampling_ratio",
	[NOOS_IIO_INFO_THERMOCOUPLE_TYPE] = "thermocouple_type",
	[NOOS_IIO_INFO_CALIBAMBIENT] = "calibambient",
	[NOOS_IIO_INFO_ZEROPOINT] = "zeropoint",
	[NOOS_IIO_INFO_TROUGH] = "trough_raw",
	[NOOS_IIO_INFO_CONVDELAY] = "convdelay",
	[NOOS_IIO_INFO_POWERFACTOR] = "powerfactor",
};

/* Linux's iio_chan_type_name_spec[]. */
static const char *const type_names[NOOS_IIO_CHAN_TYPE_COUNT] = {
	[NOOS_IIO_VOLTAGE] = "voltage",
	[NOOS_IIO_CURRENT] = "current",
	[NOOS_IIO_POWER] = "power",
	[NOOS_IIO_ACCEL] = "accel",
	[NOOS_IIO_ANGL_VEL] = "anglvel",
	[NOOS_IIO_MAGN] = "magn",
	[NOOS_IIO_LIGHT] = "illuminance",
	[NOOS_IIO_INTENSITY] = "intensity",
	[NOOS_IIO_PROXIMITY] = "proximity",
	[NOOS_IIO_TEMP] = "temp",
	[NOOS_IIO_INCLI] = "incli",
	[NOOS_IIO_ROT] = "rot",
	[NOOS_IIO_ANGL] = "angl",
	[NOOS_IIO_TIMESTAMP] = "timestamp",
	[NOOS_IIO_CAPACITANCE] = "capacitance",
	[NOOS_IIO_ALTVOLTAGE] = "altvoltage",
	[NOOS_IIO_CCT] = "cct",
	[NOOS_IIO_PRESSURE] = "pressure",
	[NOOS_IIO_HUMIDITYRELATIVE] = "humidityrelative",
	[NOOS_IIO_ACTIVITY] = "activity",
	[NOOS_IIO_STEPS] = "steps",
	[NOOS_IIO_ENERGY] = "energy",
	[NOOS_IIO_DISTANCE] = "distance",
	[NOOS_IIO_VELOCITY] = "velocity",
	[NOOS_IIO_CONCENTRATION] = "concentration",
	[NOOS_IIO_RESISTANCE] = "resistance",
	[NOOS_IIO_PH] = "ph",
	[NOOS_IIO_UVINDEX] = "uvindex",
	[NOOS_IIO_ELECTRICALCONDUCTIVITY] = "electricalconductivity",
	[NOOS_IIO_COUNT] = "count",
	[NOOS_IIO_INDEX] = "index",
	[NOOS_IIO_GRAVITY] = "gravity",
	[NOOS_IIO_POSITIONRELATIVE] = "positionrelative",
	[NOOS_IIO_PHASE] = "phase",
	[NOOS_IIO_MASSCONCENTRATION] = "massconcentration",
	[NOOS_IIO_DELTA_ANGL] = "deltaangl",
	[NOOS_IIO_DELTA_VELOCITY] = "deltavelocity",
	[NOOS_IIO_COLORTEMP] = "colortemp",
	[NOOS_IIO_CHROMATICITY] = "chromaticity",
	[NOOS_IIO_ATTENTION] = "attention",
	[NOOS_IIO_ALTCURRENT] = "altcurrent",
	[NOOS_IIO_COVERAGE] = "coverage",
	[NOOS_IIO_VOLUMEFLOW] = "volumeflow",
};

/* Linux's iio_modifier_names[]. */
static const char *const modifier_names[NOOS_IIO_MOD_COUNT] = {
	[NOOS_IIO_MOD_X] = "x",
	[NOOS_IIO_MOD_Y] = "y",
	[NOOS_IIO_MOD_Z] = "z",
	[NOOS_IIO_MOD_X_AND_Y] = "x&y",
	[NOOS_IIO_MOD_X_AND_Z] = "x&z",
	[NOOS_IIO_MOD_Y_AND_Z] = "y&z",
	[NOOS_IIO_MOD_X_AND_Y_AND_Z] = "x&y&z",
	[NOOS_IIO_MOD_X_OR_Y] = "x|y",
	[NOOS_IIO_MOD_X_OR_Z] = "x|z",
	[NOOS_IIO_MOD_Y_OR_Z] = "y|z",
	[NOOS_IIO_MOD_X_OR_Y_OR_Z] = "x|y|z",
	[NOOS_IIO_MOD_ROOT_SUM_SQUARED_X_Y] = "sqrt(x^2+y^2)",
	[NOOS_IIO_MOD_SUM_SQUARED_X_Y_Z] = "x^2+y^2+z^2",
	[NOOS_IIO_MOD_LIGHT_BOTH] = "both",
	[NOOS_IIO_MOD_LIGHT_IR] = "ir",
	[NOOS_IIO_MOD_LIGHT_CLEAR] = "clear",
	[NOOS_IIO_MOD_LIGHT_RED] = "red",
	[NOOS_IIO_MOD_LIGHT_GREEN] = "green",
	[NOOS_IIO_MOD_LIGHT_BLUE] = "blue",
	[NOOS_IIO_MOD_LIGHT_UV] = "uv",
	[NOOS_IIO_MOD_LIGHT_UVA] = "uva",
	[NOOS_IIO_MOD_LIGHT_UVB] = "uvb",
	[NOOS_IIO_MOD_LIGHT_DUV] = "duv",
	[NOOS_IIO_MOD_QUATERNION] = "quaternion",
	[NOOS_IIO_MOD_TEMP_AMBIENT] = "ambient",
	[NOOS_IIO_MOD_TEMP_OBJECT] = "object",
	[NOOS_IIO_MOD_NORTH_MAGN] = "from_north_magnetic",
	[NOOS_IIO_MOD_NORTH_TRUE] = "from_north_true",
	[NOOS_IIO_MOD_NORTH_MAGN_TILT_COMP] = "from_north_magnetic_tilt_comp",
	[NOOS_IIO_MOD_NORTH_TRUE_TILT_COMP] = "from_north_true_tilt_comp",
	[NOOS_IIO_MOD_RUNNING] = "running",
	[NOOS_IIO_MOD_JOGGING] = "jogging",
	[NOOS_IIO_MOD_WALKING] = "walking",
	[NOOS_IIO_MOD_STILL] = "still",
	[NOOS_IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z] = "sqrt(x^2+y^2+z^2)",
	[NOOS_IIO_MOD_I] = "i",
	[NOOS_IIO_MOD_Q] = "q",
	[NOOS_IIO_MOD_CO2] = "co2",
	[NOOS_IIO_MOD_VOC] = "voc",
	[NOOS_IIO_MOD_PM1] = "pm1",
	[NOOS_IIO_MOD_PM2P5] = "pm2p5",
	[NOOS_IIO_MOD_PM4] = "pm4",
	[NOOS_IIO_MOD_PM10] = "pm10",
	[NOOS_IIO_MOD_ETHANOL] = "ethanol",
	[NOOS_IIO_MOD_H2] = "h2",
	[NOOS_IIO_MOD_O2] = "o2",
	[NOOS_IIO_MOD_LINEAR_X] = "linear_x",
	[NOOS_IIO_MOD_LINEAR_Y] = "linear_y",
	[NOOS_IIO_MOD_LINEAR_Z] = "linear_z",
	[NOOS_IIO_MOD_PITCH] = "pitch",
	[NOOS_IIO_MOD_YAW] = "yaw",
	[NOOS_IIO_MOD_ROLL] = "roll",
	[NOOS_IIO_MOD_RMS] = "rms",
	[NOOS_IIO_MOD_ACTIVE] = "active",
	[NOOS_IIO_MOD_REACTIVE] = "reactive",
	[NOOS_IIO_MOD_APPARENT] = "apparent",
	[NOOS_IIO_MOD_QUATERNION_AXIS] = "quaternionaxis",
};

static const char avail_suffix[] = "_available";

/* A device attribute: a standard one of a channel, or an attrs entry. */
struct dev_attr {
	const struct noos_iio_ext_attr *ext;
	const struct noos_iio_chan *ch;
	int info;
	bool avail;
};

static const struct noos_iio_ext_attr *find_ext(
		const struct noos_iio_ext_attr *ext, const char *name);

/* The device info a table device registered with, from its libiio device. */
static const struct noos_iio_device_info *table_info(const struct iio_device *iio_dev)
{
	return (const struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);
}

/* The table entry a libiio channel was made from; see iio_device.h. */
const struct noos_iio_chan *noos_iio_chan_of(const struct iio_channel *chn)
{
	return (const struct noos_iio_chan *)iio_channel_get_pdata(chn);
}

/* Its own and its type's: the standard attributes of the channel itself. */
static uint64_t chan_mask(const struct noos_iio_chan *ch)
{
	return ch->info_mask_separate | ch->info_mask_shared_by_type;
}

/* The _available mask that goes with one of the channel's info masks. */
static uint64_t chan_avail(const struct noos_iio_chan *ch)
{
	return ch->info_mask_separate_available | ch->info_mask_shared_by_type_available;
}

/* "in" or "out", as Linux prefixes a channel's attribute files. */
static const char *dir_prefix(const struct noos_iio_chan *ch)
{
	return ch->output ? "out_" : "in_";
}

/* The channel's id, as Linux names the channel: its own @id, or made from
 * what it is in @buf. NULL if it does not fit. */
static const char *chan_id(const struct noos_iio_chan *ch, char *buf, size_t len)
{
	const char *type;
	ssize_t ret;

	if (ch->id)
		return ch->id;

	type = type_names[ch->type];

	if (ch->differential)
		ret = iio_snprintf(buf, len, "%s%d-%s%d", type, ch->channel, type, ch->channel2);
	else if (ch->modified && ch->indexed)
		ret = iio_snprintf(buf, len, "%s%d_%s", type, ch->channel,
				modifier_names[ch->channel2]);
	else if (ch->modified)
		ret = iio_snprintf(buf, len, "%s_%s", type, modifier_names[ch->channel2]);
	else if (ch->indexed)
		ret = iio_snprintf(buf, len, "%s%d", type, ch->channel);
	else
		ret = iio_snprintf(buf, len, "%s", type);

	return ret < 0 || (size_t)ret >= len ? NULL : buf;
}

/* Whether @name is @prefix and the name of @info, with "_available" or
 * not. */
static bool info_name_is(const char *name, const char *prefix, int info, bool *avail)
{
	size_t n = strlen(prefix);

	if (strncmp(name, prefix, n))
		return false;
	name += n;

	n = strlen(info_names[info]);
	if (strncmp(name, info_names[info], n))
		return false;
	name += n;

	*avail = *name != '\0';

	return !*avail || !strcmp(name, avail_suffix);
}

/* The NOOS_IIO_INFO_* @name stands for in @mask (with "_available" for
 * those in @avail_mask) after @prefix; -ENOENT if none. */
static int find_info_in(const char *name, const char *prefix, uint64_t mask, uint64_t avail_mask,
		bool *avail)
{
	int i;

	for (i = 0; i < NOOS_IIO_INFO_COUNT; i++) {
		if (!(mask & NOOS_IIO_INFO_BIT(i)) || !info_name_is(name, prefix, i, avail))
			continue;

		if (!*avail || (avail_mask & NOOS_IIO_INFO_BIT(i)))
			return i;
	}

	return -ENOENT;
}

/* The standard attribute of @ch that is the channel attribute @name. */
static int find_info(const struct noos_iio_chan *ch, const char *name, bool *avail)
{
	return find_info_in(name, "", chan_mask(ch), chan_avail(ch), avail);
}

/* The standard attribute of @ch that is the device attribute @name: one of
 * its direction's ("in_sampling_frequency") or the device's
 * ("sampling_frequency"). */
static int find_shared_info(const struct noos_iio_chan *ch, const char *name, bool *avail)
{
	int i;

	i = find_info_in(name, dir_prefix(ch), ch->info_mask_shared_by_dir,
			ch->info_mask_shared_by_dir_available, avail);
	if (i >= 0)
		return i;

	return find_info_in(name, "", ch->info_mask_shared_by_all,
			ch->info_mask_shared_by_all_available, avail);
}

/*
 * A device attribute: the standard ones the channels share, then the
 * device's attrs. The first channel that has it serves it, as
 * table_add_channels added them.
 */
static int find_dev_attr(
		const struct noos_iio_device_info *info, const char *name, struct dev_attr *da)
{
	unsigned int c;
	int i;

	for (c = 0; c < info->num_channels; c++) {
		i = find_shared_info(&info->channels[c], name, &da->avail);
		if (i >= 0) {
			da->ext = NULL;
			da->ch = &info->channels[c];
			da->info = i;
			return 0;
		}
	}

	da->ext = find_ext(info->attrs, name);
	da->ch = NULL;

	return da->ext ? 0 : -ENOENT;
}

/* The channel extra attribute (or device attribute) called @name. */
static const struct noos_iio_ext_attr *find_ext(
		const struct noos_iio_ext_attr *ext, const char *name)
{
	for (; ext && ext->name; ext++)
		if (!strcmp(ext->name, name))
			return ext;

	return NULL;
}

/* A standard attribute of a channel, with the file name Linux gives it:
 * in_accel_x_scale for its own, in_accel_scale for its type's. */
static int add_info_attr(struct iio_channel *chn, const struct noos_iio_chan *ch, const char *id,
		int info, bool avail)
{
	const char *suffix = avail ? avail_suffix : "";
	/* Checked at registration for a channel without an id of its own. */
	const char *type = ch->id ? NULL : type_names[ch->type];
	char name[TABLE_NAME_LEN], file[TABLE_NAME_LEN];
	const char *filename = file;
	ssize_t ret;

	ret = iio_snprintf(name, sizeof(name), "%s%s", info_names[info], suffix);
	if (ret < 0)
		return (int)ret;

	if (ch->info_mask_separate & NOOS_IIO_INFO_BIT(info))
		ret = iio_snprintf(file, sizeof(file), "%s%s_%s", dir_prefix(ch), id, name);
	else if (ch->id)
		filename = NULL; /* its type is not known */
	else if (ch->differential)
		ret = iio_snprintf(
				file, sizeof(file), "%s%s-%s_%s", dir_prefix(ch), type, type, name);
	else
		ret = iio_snprintf(file, sizeof(file), "%s%s_%s", dir_prefix(ch), type, name);
	if (ret < 0)
		return (int)ret;

	return iio_channel_add_attr(chn, name, IIO_ATTR_TYPE_CHANNEL, filename);
}

/* Add a channel's standard and extra attributes to its libiio channel. */
static int add_chan_attrs(struct iio_channel *chn, const struct noos_iio_chan *ch, const char *id)
{
	uint64_t mask = chan_mask(ch), avail = chan_avail(ch);
	const struct noos_iio_ext_attr *ext;
	int i, ret;

	for (i = 0; i < NOOS_IIO_INFO_COUNT; i++) {
		if (!(mask & NOOS_IIO_INFO_BIT(i)))
			continue;

		ret = add_info_attr(chn, ch, id, i, false);
		if (ret)
			return ret;

		if (avail & NOOS_IIO_INFO_BIT(i)) {
			ret = add_info_attr(chn, ch, id, i, true);
			if (ret)
				return ret;
		}
	}

	for (ext = ch->ext; ext && ext->name; ext++) {
		ret = iio_channel_add_attr(chn, ext->name, IIO_ATTR_TYPE_CHANNEL, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

/* A device attribute, unless an earlier channel added it. */
static int add_dev_attr(struct iio_device *iio_dev, const char *name)
{
	if (iio_device_find_attr(iio_dev, name))
		return 0;

	return iio_device_add_attr(iio_dev, name, IIO_ATTR_TYPE_DEVICE);
}

/* The standard attributes @ch shares with its direction and the device:
 * device attributes "in_<name>" and "<name>". */
static int add_shared_attrs(struct iio_device *iio_dev, const struct noos_iio_chan *ch)
{
	const struct {
		const char *prefix;
		uint64_t mask, avail;
	} shared[] = {
		{ dir_prefix(ch), ch->info_mask_shared_by_dir,
				ch->info_mask_shared_by_dir_available },
		{ "", ch->info_mask_shared_by_all, ch->info_mask_shared_by_all_available },
	};
	char name[TABLE_NAME_LEN];
	unsigned int s;
	ssize_t ret;
	int i;

	for (s = 0; s < 2; s++) {
		for (i = 0; i < NOOS_IIO_INFO_COUNT; i++) {
			if (!(shared[s].mask & NOOS_IIO_INFO_BIT(i)))
				continue;

			ret = iio_snprintf(name, sizeof(name), "%s%s", shared[s].prefix,
					info_names[i]);
			if (ret < 0)
				return (int)ret;

			ret = add_dev_attr(iio_dev, name);
			if (ret)
				return (int)ret;

			if (!(shared[s].avail & NOOS_IIO_INFO_BIT(i)))
				continue;

			ret = iio_snprintf(name, sizeof(name), "%s%s%s", shared[s].prefix,
					info_names[i], avail_suffix);
			if (ret < 0)
				return (int)ret;

			ret = add_dev_attr(iio_dev, name);
			if (ret)
				return (int)ret;
		}
	}

	return 0;
}

/* The board's own byte order, for NOOS_IIO_CPU. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define TABLE_CPU_IS_BE true
#else
#define TABLE_CPU_IS_BE false
#endif

/* A Linux scan_type as libiio's format, the one clients decode with. */
static struct iio_data_format scan_format(const struct noos_iio_scan_type *st)
{
	return (struct iio_data_format){
		.length = st->storagebits,
		.bits = st->realbits,
		.shift = st->shift,
		.is_signed = st->sign == NOOS_IIO_SCAN_FORMAT_SIGNED_INT,
		.is_be = st->endianness == NOOS_IIO_BE ||
			 (st->endianness == NOOS_IIO_CPU && TABLE_CPU_IS_BE),
		.repeat = st->repeat ? st->repeat : 1,
	};
}

/* add_channels of a table device: its channels and device attributes. */
static int table_add_channels(void *dev, struct iio_device *iio_dev)
{
	const struct noos_iio_device_info *info = table_info(iio_dev);
	const struct iio_data_format none = { 0 };
	const struct noos_iio_ext_attr *ext;
	struct iio_data_format fmt;
	char buf[TABLE_ID_LEN];
	struct iio_channel *chn;
	unsigned int c;
	const char *id;
	int ret;

	for (c = 0; c < info->num_channels; c++) {
		const struct noos_iio_chan *ch = &info->channels[c];
		bool scan = ch->scan_index >= 0;

		id = chan_id(ch, buf, sizeof(buf));
		if (!id)
			return -EINVAL;

		fmt = scan_format(&ch->scan_type);
		chn = iio_device_add_channel(iio_dev, ch->scan_index, id, NULL, NULL, ch->output,
				scan, scan ? &fmt : &none);
		if (!chn)
			return -ENOMEM;

		iio_channel_set_pdata(chn, (struct iio_channel_pdata *)(uintptr_t)ch);

		ret = add_chan_attrs(chn, ch, id);
		if (ret)
			return ret;

		ret = add_shared_attrs(iio_dev, ch);
		if (ret)
			return ret;
	}

	for (ext = info->attrs; ext && ext->name; ext++) {
		ret = add_dev_attr(iio_dev, ext->name);
		if (ret)
			return ret;
	}

	return 0;
}

/* Move past a piece an noos_value_* helper wrote, whose length counts the
 * NUL. */
static int advance(ssize_t ret, size_t *pos)
{
	if (ret < 0)
		return (int)ret;

	*pos += (size_t)ret - 1;

	return 0;
}

/* q + r / 10^9 of a 64-bit value in billionths, as Linux prints it. */
static ssize_t format_billionths(char *dst, size_t len, int64_t v)
{
	return noos_value_format_fixed(dst, len, (int)(v / 1000000000), (int)(v % 1000000000), 9);
}

/* A value of a NOOS_IIO_VAL_* type, from @size ints, as Linux's
 * __iio_format_value() prints it. */
static ssize_t format_val(char *dst, size_t len, int type, int size, const int *v)
{
	char c[2] = { 0, 0 };
	size_t pos = 0;
	int64_t x;
	int i, ret;

	switch (type) {
	case NOOS_IIO_VAL_INT:
		return noos_value_format_int(dst, len, v[0]);
	case NOOS_IIO_VAL_INT_PLUS_MICRO:
		return noos_value_format_fixed(dst, len, v[0], v[1], 6);
	case NOOS_IIO_VAL_INT_PLUS_MICRO_DB:
		ret = advance(noos_value_format_fixed(dst, len, v[0], v[1], 6), &pos);
		if (ret)
			return ret;

		ret = advance(noos_value_format_str(dst + pos, len - pos, " dB"), &pos);
		return ret ? ret : (ssize_t)pos + 1;
	case NOOS_IIO_VAL_INT_PLUS_NANO:
		return noos_value_format_fixed(dst, len, v[0], v[1], 9);
	case NOOS_IIO_VAL_FRACTIONAL:
		if (!v[1])
			return -EINVAL;

		return format_billionths(dst, len, (int64_t)v[0] * 1000000000 / v[1]);
	case NOOS_IIO_VAL_FRACTIONAL_LOG2:
		if (v[1] < 0 || v[1] > 62)
			return -EINVAL;

		/* Shifted as a magnitude, so it rounds toward zero. */
		x = (int64_t)v[0] * 1000000000;
		x = x < 0 ? -(-x >> v[1]) : x >> v[1];

		return format_billionths(dst, len, x);
	case NOOS_IIO_VAL_INT_MULTIPLE:
		if (!len)
			return -ERANGE;

		dst[0] = '\0';

		for (i = 0; i < size; i++) {
			ret = advance(noos_value_format_int(dst + pos, len - pos, v[i]), &pos);
			if (!ret)
				ret = advance(noos_value_format_str(dst + pos, len - pos, " "),
						&pos);
			if (ret)
				return ret;
		}

		return (ssize_t)pos + 1;
	case NOOS_IIO_VAL_INT_64:
		x = (int64_t)(((uint64_t)(uint32_t)v[1] << 32) | (uint32_t)v[0]);

		return noos_value_format_int(dst, len, x);
	case NOOS_IIO_VAL_CHAR:
		c[0] = (char)v[0];

		return noos_value_format_str(dst, len, c);
	default:
		return -EINVAL;
	}
}

/* Ints per value in an "_available" list of @type, as Linux counts them. */
static int avail_stride(int type)
{
	return type == NOOS_IIO_VAL_INT || type == NOOS_IIO_VAL_CHAR ? 1 : 2;
}

/* An "_available" list, "a b c", or a range, "[min step max]". */
static ssize_t format_avail(char *dst, size_t len, int kind, int type, const int *vals, int length)
{
	int step = avail_stride(type);
	size_t pos = 0;
	int i, ret;

	if (length < 0 || length % step || (kind == NOOS_IIO_AVAIL_RANGE && length != 3 * step))
		return -EINVAL;

	if (kind != NOOS_IIO_AVAIL_LIST && kind != NOOS_IIO_AVAIL_RANGE)
		return -EINVAL;

	if (!len)
		return -ERANGE;

	dst[0] = '\0';

	for (i = 0; i < length; i += step) {
		const char *sep = i ? " " : kind == NOOS_IIO_AVAIL_RANGE ? "[" : "";

		ret = advance(noos_value_format_str(dst + pos, len - pos, sep), &pos);
		if (ret)
			return ret;

		ret = advance(format_val(dst + pos, len - pos, type, step, &vals[i]), &pos);
		if (ret)
			return ret;
	}

	if (kind == NOOS_IIO_AVAIL_RANGE) {
		ret = advance(noos_value_format_str(dst + pos, len - pos, "]"), &pos);
		if (ret)
			return ret;
	}

	return (ssize_t)pos + 1;
}

/* The value entry that serves @info on @ch, or NULL. */
static const struct noos_iio_value *find_value(
		const struct noos_iio_device_info *info, enum noos_iio_chan_type type, int i)
{
	unsigned int v;

	for (v = 0; v < info->nb_values; v++)
		if (info->values[v].chan_type == type && (int)info->values[v].info == i)
			return &info->values[v];

	return NULL;
}

/* An entry's values now, and their count. */
static const int *value_table(const struct noos_iio_value *val, void *dev, unsigned int *nb)
{
	if (val->avail)
		return val->avail(dev, nb);

	*nb = val->nb;

	return val->vals;
}

/* Serve a standard attribute that is a struct noos_iio_value entry. */
static int value_read(const struct noos_iio_value *val, void *dev, int *v)
{
	unsigned int nb = 0, step = NOOS_IIO_VAL_STRIDE(val->type);
	const int *table = value_table(val, dev, &nb);
	int idx = val->get ? val->get(dev) : 0;

	if (idx < 0)
		return idx;
	if (!table || (unsigned int)idx >= nb)
		return -EINVAL;

	v[0] = table[step * idx];
	if (step == 2)
		v[1] = table[step * idx + 1];

	return val->type;
}

/* Switch to the row holding @v, @v2; -EINVAL if none does. */
static int value_write(const struct noos_iio_value *val, void *dev, int v, int v2)
{
	unsigned int i, nb = 0, step = NOOS_IIO_VAL_STRIDE(val->type);
	const int *table;

	if (!val->set)
		return -EPERM;

	table = value_table(val, dev, &nb);
	if (!table)
		return -EINVAL;

	for (i = 0; i < nb; i++) {
		const int *row = &table[step * i];

		if (row[0] == v && (step == 1 || row[1] == v2))
			return val->set(dev, i);
	}

	return -EINVAL;
}

/* Read one standard attribute: a value entry, read_avail or read_raw. */
static int read_info(const struct noos_iio_device_info *info, const struct noos_iio_chan *ch, int i,
		bool avail, char *dst, size_t len)
{
	const struct noos_iio_value *val = find_value(info, ch->type, i);
	int v[NOOS_IIO_MAX_RAW_ELEMENTS] = { 0 };
	int type, length, size = 2, ret;
	unsigned int nb = 0;
	const int *vals;

	if (avail && val) {
		vals = value_table(val, info->dev, &nb);
		if (!vals)
			return -EINVAL;

		return (int)format_avail(dst, len, NOOS_IIO_AVAIL_LIST, val->type, vals,
				(int)(NOOS_IIO_VAL_STRIDE(val->type) * nb));
	}

	if (avail) {
		if (!info->read_avail)
			return -ENOSYS;

		ret = info->read_avail(info->dev, ch, i, &vals, &type, &length);
		if (ret < 0)
			return ret;

		return (int)format_avail(dst, len, ret, type, vals, length);
	}

	if (val)
		ret = value_read(val, info->dev, v);
	else if (info->read_raw_multi)
		ret = info->read_raw_multi(info->dev, ch, NOOS_IIO_MAX_RAW_ELEMENTS, v, &size, i);
	else if (info->read_raw)
		ret = info->read_raw(info->dev, ch, i, &v[0], &v[1]);
	else
		return -ENOSYS;
	if (ret < 0)
		return ret;

	if (size < 0 || size > NOOS_IIO_MAX_RAW_ELEMENTS)
		return -EINVAL;

	return (int)format_val(dst, len, ret, size, v);
}

/* read_attr of a table device: route by attribute kind and name. */
static int table_read_attr(void *dev, const struct iio_device *iio_dev, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct noos_iio_device_info *info = table_info(iio_dev);
	const char *name = iio_attr_get_name(attr);
	const struct noos_iio_ext_attr *ext;
	const struct noos_iio_chan *ch = NULL;
	struct dev_attr da;
	bool avail;
	int i;

	if (!name)
		return -EINVAL;

	switch (attr->type) {
	case IIO_ATTR_TYPE_DEVICE:
		if (find_dev_attr(info, name, &da))
			return -ENOENT;
		if (!da.ext)
			return read_info(info, da.ch, da.info, da.avail, dst, len);

		ext = da.ext;
		break;
	case IIO_ATTR_TYPE_CHANNEL:
		ch = attr->iio.chn ? noos_iio_chan_of(attr->iio.chn) : NULL;
		if (!ch)
			return -EINVAL;

		i = find_info(ch, name, &avail);
		if (i >= 0)
			return read_info(info, ch, i, avail, dst, len);

		ext = find_ext(ch->ext, name);
		break;
	default:
		return -EINVAL;
	}

	if (!ext)
		return -ENOENT;
	if (!ext->read)
		return -EPERM;

	return ext->read(info->dev, ch, ext->priv, dst, len);
}

/* The value without the " dB" (or "dB") a dB one may end with. */
static int strip_db(const char *src, char *buf, size_t len)
{
	size_t n = strlen(src);

	if (n && src[n - 1] == '\n')
		n--;
	if (n >= len)
		return -EINVAL;

	memcpy(buf, src, n);
	buf[n] = '\0';

	if (n >= 2 && !strcmp(buf + n - 2, "dB")) {
		n -= 2;
		if (n && buf[n - 1] == ' ')
			n--;
		buf[n] = '\0';
	}

	return 0;
}

/* Parse @src as a value of NOOS_IIO_VAL_* @type, as Linux's
 * iio_write_channel_info() does. */
static int parse_val(const char *src, int type, int *val, int *val2)
{
	char buf[32];
	long long ival;
	int ret;

	switch (type) {
	case NOOS_IIO_VAL_INT:
		ret = noos_value_parse_int(src, &ival);
		if (ret)
			return ret;
		if (ival < INT32_MIN || ival > INT32_MAX)
			return -EINVAL;

		*val = (int)ival;
		return 0;
	case NOOS_IIO_VAL_INT_PLUS_MICRO:
		return noos_value_parse_fixed(src, val, val2, 6);
	case NOOS_IIO_VAL_INT_PLUS_MICRO_DB:
		ret = strip_db(src, buf, sizeof(buf));
		if (ret)
			return ret;

		return noos_value_parse_fixed(buf, val, val2, 6);
	case NOOS_IIO_VAL_INT_PLUS_NANO:
		return noos_value_parse_fixed(src, val, val2, 9);
	case NOOS_IIO_VAL_INT_64:
		ret = noos_value_parse_int(src, &ival);
		if (ret)
			return ret;

		*val = (int)(uint32_t)(uint64_t)ival;
		*val2 = (int)(uint32_t)((uint64_t)ival >> 32);
		return 0;
	case NOOS_IIO_VAL_CHAR:
		if (!src[0])
			return -EINVAL;

		*val = src[0];
		return 0;
	default:
		return -EINVAL;
	}
}

/* Write one standard attribute: parse it, then a value entry or write_raw. */
static int write_info(const struct noos_iio_device_info *info, const struct noos_iio_chan *ch,
		int i, const char *src)
{
	const struct noos_iio_value *val = find_value(info, ch->type, i);
	int type = NOOS_IIO_VAL_INT_PLUS_MICRO;
	int v = 0, v2 = 0, ret;

	if (val) {
		/* A constant: refused before its type, which may not parse. */
		if (!val->set)
			return -EPERM;

		type = val->type;
	} else {
		if (!info->write_raw)
			return -EPERM;

		if (info->write_raw_get_fmt) {
			type = info->write_raw_get_fmt(info->dev, ch, i);
			if (type < 0)
				return type;
		}
	}

	ret = parse_val(src, type, &v, &v2);
	if (ret)
		return ret;

	if (val)
		return value_write(val, info->dev, v, v2);

	return info->write_raw(info->dev, ch, i, v, v2);
}

/* write_attr of a table device: route by attribute kind and name. */
static int table_write_attr(void *dev, const struct iio_device *iio_dev,
		const struct iio_attr *attr, const char *src, size_t len)
{
	const struct noos_iio_device_info *info = table_info(iio_dev);
	const char *name = iio_attr_get_name(attr);
	const struct noos_iio_ext_attr *ext;
	const struct noos_iio_chan *ch = NULL;
	struct dev_attr da;
	bool avail;
	int i, ret;

	if (!name)
		return -EINVAL;

	switch (attr->type) {
	case IIO_ATTR_TYPE_DEVICE:
		if (find_dev_attr(info, name, &da))
			return -ENOENT;
		if (!da.ext) {
			if (da.avail)
				return -EPERM;

			ret = write_info(info, da.ch, da.info, src);
			return ret < 0 ? ret : (int)len;
		}

		ext = da.ext;
		break;
	case IIO_ATTR_TYPE_CHANNEL:
		ch = attr->iio.chn ? noos_iio_chan_of(attr->iio.chn) : NULL;
		if (!ch)
			return -EINVAL;

		i = find_info(ch, name, &avail);
		if (i >= 0) {
			if (avail)
				return -EPERM;

			ret = write_info(info, ch, i, src);
			return ret < 0 ? ret : (int)len;
		}

		ext = find_ext(ch->ext, name);
		break;
	default:
		return -EINVAL;
	}

	if (!ext)
		return -ENOENT;
	if (!ext->write)
		return -EPERM;

	return ext->write(info->dev, ch, ext->priv, src, len);
}

struct scan_slot {
	unsigned int idx; /* in the device's channel table */
	size_t offset;
	size_t bytes;
	unsigned int bits;
	unsigned int shift;
	bool is_signed;
	bool is_be;
};

/*
 * Where each enabled channel goes in a sample, by the rule
 * iio_device_get_sample_size() follows: in channel order, each element
 * aligned to its own size, channels sharing a scan index sharing it.
 */
static int scan_layout(const struct noos_iio_device_info *info, const struct iio_device *iio_dev,
		const struct iio_channels_mask *mask, struct scan_slot *slots, uint32_t *enabled)
{
	unsigned int c, n = 0, nb = iio_device_get_channels_count(iio_dev);
	long prev_index = -1;
	size_t size = 0, prev_offset = 0;

	*enabled = 0;

	for (c = 0; c < nb; c++) {
		const struct iio_channel *chn = iio_device_get_channel(iio_dev, c);
		const struct iio_data_format *fmt;
		const struct noos_iio_chan *ch;
		long index = iio_channel_get_index(chn);
		size_t length, offset;

		if (index < 0)
			break;
		if (!iio_channel_is_enabled(chn, mask))
			continue;

		fmt = iio_channel_get_data_format(chn);
		ch = noos_iio_chan_of(chn);
		if (!ch || fmt->repeat > 1 || !fmt->length || fmt->length % 8 || fmt->length > 64)
			return -ENOTSUP;

		length = fmt->length / 8;

		if (n && index == prev_index) {
			offset = prev_offset;
		} else {
			if (size % length)
				size += 2 * length - (size % length);
			else
				size += length;
			offset = size - length;
		}

		slots[n] = (struct scan_slot){
			.idx = (unsigned int)(ch - info->channels),
			.offset = offset,
			.bytes = length,
			.bits = fmt->bits,
			.shift = fmt->shift,
			.is_signed = fmt->is_signed,
			.is_be = fmt->is_be,
		};
		*enabled |= UINT32_C(1) << slots[n].idx;

		prev_index = index;
		prev_offset = offset;
		n++;
	}

	return (int)n;
}

/*
 * A signed value is stored sign-extended over the whole element, so a raw
 * reader sees the same number a client decodes; an unsigned one is masked
 * to its bits. Either way it sits at its shift.
 */
static void put_value(uint8_t *dst, const struct scan_slot *slot, int64_t val)
{
	uint64_t u = (uint64_t)val;
	size_t b;

	if (!slot->is_signed && slot->bits < 64)
		u &= (UINT64_C(1) << slot->bits) - 1;
	u <<= slot->shift;

	for (b = 0; b < slot->bytes; b++) {
		unsigned int sh = slot->is_be ? (unsigned int)(slot->bytes - 1 - b) * 8
					      : (unsigned int)b * 8;

		dst[slot->offset + b] = (uint8_t)(u >> sh);
	}
}

/* One scan: paced by wait_scan, then one value per enabled channel. */
static int table_read_scan(const struct noos_iio_device_info *info, uint32_t enabled, int64_t *vals)
{
	int ret = 0;

	if (info->wait_scan)
		ret = info->wait_scan(info->dev);
	if (ret)
		return ret;

	return info->read_scan(info->dev, enabled, vals);
}

/* read_samples of a table device: one read_scan per scan, packed. */
static int table_read_samples(void *dev, const struct iio_device *iio_dev,
		const struct iio_channels_mask *mask, void *data, size_t bytes)
{
	const struct noos_iio_device_info *info = table_info(iio_dev);
	struct scan_slot slots[TABLE_MAX_CHANNELS];
	int64_t vals[TABLE_MAX_CHANNELS];
	uint8_t *out = data;
	ssize_t sample_size;
	size_t s, nb_samples;
	uint32_t enabled;
	int i, n, ret;

	if (!mask)
		return -EINVAL;

	sample_size = iio_device_get_sample_size(iio_dev, mask);
	if (sample_size <= 0)
		return sample_size ? (int)sample_size : -EINVAL;

	if (bytes % (size_t)sample_size)
		return -EINVAL;

	n = scan_layout(info, iio_dev, mask, slots, &enabled);
	if (n <= 0)
		return n ? n : -EINVAL;

	nb_samples = bytes / (size_t)sample_size;
	memset(data, 0, bytes);

	for (s = 0; s < nb_samples; s++) {
		ret = table_read_scan(info, enabled, vals);
		if (ret < 0)
			return ret;

		for (i = 0; i < n; i++)
			put_value(out, &slots[i], vals[slots[i].idx]);

		out += sample_size;
	}

	return 0;
}

/* Each standard attribute in one mask, each "_available" one with its
 * own, and nothing past NOOS_IIO_INFO_COUNT. */
static bool chan_masks_valid(const struct noos_iio_chan *ch)
{
	const uint64_t all = NOOS_IIO_INFO_BIT(NOOS_IIO_INFO_COUNT) - 1;
	const uint64_t masks[][2] = {
		{ ch->info_mask_separate, ch->info_mask_separate_available },
		{ ch->info_mask_shared_by_type, ch->info_mask_shared_by_type_available },
		{ ch->info_mask_shared_by_dir, ch->info_mask_shared_by_dir_available },
		{ ch->info_mask_shared_by_all, ch->info_mask_shared_by_all_available },
	};
	uint64_t seen = 0;
	unsigned int m;

	for (m = 0; m < 4; m++) {
		if ((masks[m][0] & ~all) || (masks[m][1] & ~masks[m][0]) || (masks[m][0] & seen))
			return false;

		seen |= masks[m][0];
	}

	return true;
}

/* A scan element's storage: a sign, whole bytes up to 64 bits, the data
 * and its shift inside them. */
static bool chan_scan_type_valid(const struct noos_iio_chan *ch)
{
	const struct noos_iio_scan_type *st = &ch->scan_type;

	if (ch->scan_index < 0)
		return true;

	if (st->sign != NOOS_IIO_SCAN_FORMAT_SIGNED_INT &&
			st->sign != NOOS_IIO_SCAN_FORMAT_UNSIGNED_INT)
		return false;

	if (!st->storagebits || st->storagebits % 8 || st->storagebits > 64)
		return false;

	if (!st->realbits || st->realbits + st->shift > st->storagebits)
		return false;

	return (unsigned int)st->endianness <= NOOS_IIO_LE;
}

/* What a channel without an id of its own must be for Linux to name it. */
static bool chan_spec_valid(const struct noos_iio_chan *ch)
{
	if (ch->id)
		return true;

	if ((unsigned int)ch->type >= NOOS_IIO_CHAN_TYPE_COUNT)
		return false;

	if (ch->modified && (ch->differential || ch->channel2 <= NOOS_IIO_NO_MOD ||
					    ch->channel2 >= NOOS_IIO_MOD_COUNT))
		return false;

	return !ch->differential || ch->indexed;
}

/* The info bits of @ch in any of its masks, and those with a list. */
static uint64_t chan_all_mask(const struct noos_iio_chan *ch)
{
	return ch->info_mask_separate | ch->info_mask_shared_by_type | ch->info_mask_shared_by_dir |
	       ch->info_mask_shared_by_all;
}

/* All the _available bits a channel declares, across its masks. */
static uint64_t chan_all_avail(const struct noos_iio_chan *ch)
{
	return ch->info_mask_separate_available | ch->info_mask_shared_by_type_available |
	       ch->info_mask_shared_by_dir_available | ch->info_mask_shared_by_all_available;
}

/* The types a written value can be parsed as, to find its row. */
static bool value_type_writable(int type)
{
	switch (type) {
	case NOOS_IIO_VAL_INT:
	case NOOS_IIO_VAL_INT_PLUS_MICRO:
	case NOOS_IIO_VAL_INT_PLUS_NANO:
	case NOOS_IIO_VAL_INT_PLUS_MICRO_DB:
	case NOOS_IIO_VAL_INT_64:
		return true;
	default:
		return false;
	}
}

/* A value entry is well formed, the only one of its kind, and some
 * channel has the attribute it serves. */
static bool value_valid(const struct noos_iio_device_info *info, const struct noos_iio_value *val)
{
	unsigned int c;

	if ((unsigned int)val->info >= NOOS_IIO_INFO_COUNT ||
			(unsigned int)val->chan_type >= NOOS_IIO_CHAN_TYPE_COUNT ||
			(!val->vals && !val->avail) || (val->vals && !val->nb) ||
			val->type == NOOS_IIO_VAL_INT_MULTIPLE ||
			(val->set && !value_type_writable(val->type)))
		return false;

	if (find_value(info, val->chan_type, (int)val->info) != val)
		return false;

	for (c = 0; c < info->num_channels; c++)
		if (info->channels[c].type == val->chan_type &&
				(chan_all_mask(&info->channels[c]) & NOOS_IIO_INFO_BIT(val->info)))
			return true;

	return false;
}

/* The channels are well formed, and something serves each attribute. */
static bool table_valid(const struct noos_iio_device_info *info)
{
	bool raw = info->read_raw || info->read_raw_multi;
	unsigned int c, v;
	int i;

	if (info->num_channels && !info->channels)
		return false;
	if (info->nb_values && !info->values)
		return false;

	for (v = 0; v < info->nb_values; v++)
		if (!value_valid(info, &info->values[v]))
			return false;

	for (c = 0; c < info->num_channels; c++) {
		const struct noos_iio_chan *ch = &info->channels[c];
		uint64_t mask = chan_all_mask(ch), avail = chan_all_avail(ch);

		if (!chan_masks_valid(ch) || !chan_spec_valid(ch) || !chan_scan_type_valid(ch))
			return false;

		for (i = 0; i < NOOS_IIO_INFO_COUNT; i++) {
			if (!(mask & NOOS_IIO_INFO_BIT(i)) || find_value(info, ch->type, i))
				continue;

			if (!raw || ((avail & NOOS_IIO_INFO_BIT(i)) && !info->read_avail))
				return false;
		}
	}

	return true;
}

/* Check a table device and make its callbacks; see iio_device.h. */
int noos_iio_table_install(struct noos_iio_device_info *info)
{
	if (!info->num_channels && !info->attrs)
		return 0;

	if (!table_valid(info) || info->num_channels > TABLE_MAX_CHANNELS)
		return -EINVAL;

	if (!info->add_channels)
		info->add_channels = table_add_channels;
	if (!info->read_attr)
		info->read_attr = table_read_attr;
	if (!info->write_attr)
		info->write_attr = table_write_attr;
	if (!info->read_samples && info->read_scan)
		info->read_samples = table_read_samples;

	return 0;
}

/* --- enum attributes (struct noos_iio_enum, as Linux's IIO_ENUM) --- */

/* The struct noos_iio_enum an enum attribute was declared with. */
static const struct noos_iio_enum *enum_of(intptr_t priv)
{
	return (const struct noos_iio_enum *)priv;
}

/* True if the hardware has item @idx of the enum (its mask, a name). */
static bool enum_has(const struct noos_iio_enum *e, unsigned int idx)
{
	if (idx >= e->nb_items || !e->items[idx])
		return false;

	return !e->mask || (idx < 32 && (e->mask & (UINT32_C(1) << idx)));
}

/* read of an enum attribute: the name of the item in use. */
int noos_iio_enum_read(
		void *dev, const struct noos_iio_chan *ch, intptr_t priv, char *dst, size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	int idx;

	if (!e || !e->get)
		return -EINVAL;

	idx = e->get(dev, ch);
	if (idx < 0)
		return idx;

	if ((unsigned int)idx >= e->nb_items || !e->items[idx])
		return -EINVAL;

	return (int)noos_value_format_str(dst, len, e->items[idx]);
}

/* write of an enum attribute: switch to the named item. */
int noos_iio_enum_write(void *dev, const struct noos_iio_chan *ch, intptr_t priv, const char *src,
		size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	int idx, ret;

	if (!e)
		return -EINVAL;
	if (!e->set)
		return -EPERM;

	idx = noos_value_match_str(e->items, e->nb_items, src, len);
	if (idx < 0)
		return idx;

	if (!enum_has(e, (unsigned int)idx))
		return -ENOTSUP;

	ret = e->set(dev, ch, (unsigned int)idx);
	if (ret)
		return ret;

	return (int)len;
}

/* read of an enum's _available attribute: the names it takes. */
int noos_iio_enum_available(
		void *dev, const struct noos_iio_chan *ch, intptr_t priv, char *dst, size_t len)
{
	const struct noos_iio_enum *e = enum_of(priv);
	size_t pos = 0;
	unsigned int i;
	ssize_t ret;

	if (!e)
		return -EINVAL;
	if (!len)
		return -ERANGE;

	dst[0] = '\0';

	for (i = 0; i < e->nb_items; i++) {
		if (!enum_has(e, i))
			continue;

		if (pos) {
			ret = noos_value_format_str(dst + pos, len - pos, " ");
			if (ret < 0)
				return (int)ret;
			pos += (size_t)ret - 1;
		}

		ret = noos_value_format_str(dst + pos, len - pos, e->items[i]);
		if (ret < 0)
			return (int)ret;
		pos += (size_t)ret - 1;
	}

	return (int)pos + 1;
}
