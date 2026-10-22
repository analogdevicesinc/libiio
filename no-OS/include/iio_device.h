/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_DEVICE_H_
#define NOOS_INCLUDE_IIO_DEVICE_H_

#include <iio/iio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of IIO devices that can be registered */
#define NOOS_IIO_MAX_DEVICES 16

/*
 * Callback types a device gives the backend; @dev is the device's own
 * handle (noos_iio_device_info.dev).
 */
typedef int (*noos_iio_add_channels_t)(void *dev, struct iio_device *iio_device);

typedef int (*noos_iio_read_attr_t)(void *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, char *dst, size_t len);

typedef int (*noos_iio_write_attr_t)(void *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, const char *src, size_t len);

/*
 * Sample transfer: @mask says
 * which scan elements the client enabled, and @data holds whole samples laid
 * out as iio_device_get_sample_size() describes -- the enabled channels only,
 * in channel order, each at its own format.
 */
typedef int (*noos_iio_read_samples_t)(void *dev, const struct iio_device *iio_device,
		const struct iio_channels_mask *mask, void *data, size_t bytes);
typedef int (*noos_iio_write_samples_t)(void *dev, const struct iio_device *iio_device,
		const struct iio_channels_mask *mask, const void *data, size_t bytes);

/* Buffer enabled or disabled by the client. */
typedef int (*noos_iio_enable_buffer_t)(void *dev, bool enable);

/* ------------------------------------------------------------------------
 * Table-driven devices
 *
 * Instead of add_channels/read_attr/write_attr/read_samples, a device can
 * describe its channels in tables and let the backend create them, name and
 * format their attributes and lay out the samples, the way a Linux IIO
 * driver describes its iio_chan_spec:
 *
 * - one table of channels and one set of handlers, as Linux's iio_dev and
 *   iio_info; iio_channels.h gives the standard
 *   channels of their types, so all the parts of a type look alike;
 * - each channel says what it is (type, number, modifier), which gives its
 *   id, and lists the standard attributes it has as masks of
 *   NOOS_IIO_INFO_* bits, one per sharing (its own, its type's, its
 *   direction's, the device's); the device reads and writes them as numbers
 *   (read_raw/write_raw), and the backend names them, formats and parses
 *   the strings;
 * - the ones in the *_available masks also get an "_available" attribute,
 *   read from read_avail;
 * - any other attribute is a noos_iio_ext_attr, read and written as text;
 * - the standard attributes that are data (a fixed scale, a sampling
 *   frequency picked from a table) are struct noos_iio_value entries the
 *   backend serves itself; read_raw/write_raw do the rest;
 * - buffered samples come one scan at a time from read_scan, as one value
 *   per enabled channel; the backend packs them with each
 *   channel's scan_type (or its current format, for formats that change:
 *   get_scan_format);
 * - wait_scan, if set, is called before each scan, to pace it.
 * ------------------------------------------------------------------------ */

/* The standard attributes, as Linux's enum iio_chan_info_enum and in its
 * order, so a mask written for a Linux driver means the same here. */
enum noos_iio_info {
	NOOS_IIO_INFO_RAW,
	NOOS_IIO_INFO_PROCESSED, /* "input" */
	NOOS_IIO_INFO_SCALE,
	NOOS_IIO_INFO_OFFSET,
	NOOS_IIO_INFO_CALIBSCALE,
	NOOS_IIO_INFO_CALIBBIAS,
	NOOS_IIO_INFO_PEAK, /* "peak_raw" */
	NOOS_IIO_INFO_PEAK_SCALE,
	NOOS_IIO_INFO_QUADRATURE_CORRECTION_RAW,
	NOOS_IIO_INFO_AVERAGE_RAW, /* "mean_raw" */
	NOOS_IIO_INFO_LOW_PASS_FILTER_3DB_FREQUENCY,
	NOOS_IIO_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY,
	NOOS_IIO_INFO_SAMP_FREQ, /* "sampling_frequency" */
	NOOS_IIO_INFO_FREQUENCY,
	NOOS_IIO_INFO_PHASE,
	NOOS_IIO_INFO_HARDWAREGAIN,
	NOOS_IIO_INFO_HYSTERESIS,
	NOOS_IIO_INFO_HYSTERESIS_RELATIVE,
	NOOS_IIO_INFO_INT_TIME, /* "integration_time" */
	NOOS_IIO_INFO_ENABLE,	/* "en" */
	NOOS_IIO_INFO_CALIBHEIGHT,
	NOOS_IIO_INFO_CALIBWEIGHT,
	NOOS_IIO_INFO_DEBOUNCE_COUNT,
	NOOS_IIO_INFO_DEBOUNCE_TIME,
	NOOS_IIO_INFO_CALIBEMISSIVITY,
	NOOS_IIO_INFO_OVERSAMPLING_RATIO,
	NOOS_IIO_INFO_THERMOCOUPLE_TYPE,
	NOOS_IIO_INFO_CALIBAMBIENT,
	NOOS_IIO_INFO_ZEROPOINT,
	NOOS_IIO_INFO_TROUGH, /* "trough_raw" */
	NOOS_IIO_INFO_CONVDELAY,
	NOOS_IIO_INFO_POWERFACTOR,
	NOOS_IIO_INFO_COUNT,
};

#define NOOS_IIO_INFO_BIT(info) (UINT64_C(1) << (info))

/*
 * What read_raw returns and write_raw takes, as Linux's IIO_VAL_* (same
 * numbers):
 * - INT: val;
 * - INT_PLUS_MICRO, INT_PLUS_NANO: val + val2 / 10^6 or / 10^9, negative
 *   when either part is; INT_PLUS_MICRO_DB is the micro one in dB;
 * - INT_MULTIPLE: several ints, from read_raw_multi only;
 * - INT_64: val is the low 32 bits, val2 the high ones;
 * - FRACTIONAL: val / val2; FRACTIONAL_LOG2: val / 2^val2 (read only, as
 *   on Linux);
 * - CHAR: val is a character.
 * A write is parsed as write_raw_get_fmt says: INT, a micro, nano or dB
 * one, INT_64 or CHAR.
 */
enum noos_iio_val {
	NOOS_IIO_VAL_INT = 1,
	NOOS_IIO_VAL_INT_PLUS_MICRO,
	NOOS_IIO_VAL_INT_PLUS_NANO,
	NOOS_IIO_VAL_INT_PLUS_MICRO_DB,
	NOOS_IIO_VAL_INT_MULTIPLE,
	NOOS_IIO_VAL_INT_64,
	NOOS_IIO_VAL_FRACTIONAL = 10,
	NOOS_IIO_VAL_FRACTIONAL_LOG2,
	NOOS_IIO_VAL_CHAR,
};

/* The most values read_raw_multi gives, as Linux's INDIO_MAX_RAW_ELEMENTS. */
#define NOOS_IIO_MAX_RAW_ELEMENTS 4

/* What read_avail returns: vals is a list, or [min, step, max]. Values are
 * ints for NOOS_IIO_VAL_INT and _CHAR, {val, val2} pairs for the other
 * types, and length counts ints. */
enum noos_iio_avail {
	NOOS_IIO_AVAIL_LIST = 1,
	NOOS_IIO_AVAIL_RANGE,
};

/* Channel types, as Linux's enum iio_chan_type and in its order. */
enum noos_iio_chan_type {
	NOOS_IIO_VOLTAGE,
	NOOS_IIO_CURRENT,
	NOOS_IIO_POWER,
	NOOS_IIO_ACCEL,
	NOOS_IIO_ANGL_VEL,
	NOOS_IIO_MAGN,
	NOOS_IIO_LIGHT,
	NOOS_IIO_INTENSITY,
	NOOS_IIO_PROXIMITY,
	NOOS_IIO_TEMP,
	NOOS_IIO_INCLI,
	NOOS_IIO_ROT,
	NOOS_IIO_ANGL,
	NOOS_IIO_TIMESTAMP,
	NOOS_IIO_CAPACITANCE,
	NOOS_IIO_ALTVOLTAGE,
	NOOS_IIO_CCT,
	NOOS_IIO_PRESSURE,
	NOOS_IIO_HUMIDITYRELATIVE,
	NOOS_IIO_ACTIVITY,
	NOOS_IIO_STEPS,
	NOOS_IIO_ENERGY,
	NOOS_IIO_DISTANCE,
	NOOS_IIO_VELOCITY,
	NOOS_IIO_CONCENTRATION,
	NOOS_IIO_RESISTANCE,
	NOOS_IIO_PH,
	NOOS_IIO_UVINDEX,
	NOOS_IIO_ELECTRICALCONDUCTIVITY,
	NOOS_IIO_COUNT,
	NOOS_IIO_INDEX,
	NOOS_IIO_GRAVITY,
	NOOS_IIO_POSITIONRELATIVE,
	NOOS_IIO_PHASE,
	NOOS_IIO_MASSCONCENTRATION,
	NOOS_IIO_DELTA_ANGL,
	NOOS_IIO_DELTA_VELOCITY,
	NOOS_IIO_COLORTEMP,
	NOOS_IIO_CHROMATICITY,
	NOOS_IIO_ATTENTION,
	NOOS_IIO_ALTCURRENT,
	NOOS_IIO_COVERAGE,
	NOOS_IIO_VOLUMEFLOW,
	NOOS_IIO_CHAN_TYPE_COUNT,
};

/* Channel modifiers, as Linux's enum iio_modifier and in its order. */
enum noos_iio_modifier {
	NOOS_IIO_NO_MOD,
	NOOS_IIO_MOD_X,
	NOOS_IIO_MOD_Y,
	NOOS_IIO_MOD_Z,
	NOOS_IIO_MOD_X_AND_Y,
	NOOS_IIO_MOD_X_AND_Z,
	NOOS_IIO_MOD_Y_AND_Z,
	NOOS_IIO_MOD_X_AND_Y_AND_Z,
	NOOS_IIO_MOD_X_OR_Y,
	NOOS_IIO_MOD_X_OR_Z,
	NOOS_IIO_MOD_Y_OR_Z,
	NOOS_IIO_MOD_X_OR_Y_OR_Z,
	NOOS_IIO_MOD_LIGHT_BOTH,
	NOOS_IIO_MOD_LIGHT_IR,
	NOOS_IIO_MOD_ROOT_SUM_SQUARED_X_Y,
	NOOS_IIO_MOD_SUM_SQUARED_X_Y_Z,
	NOOS_IIO_MOD_LIGHT_CLEAR,
	NOOS_IIO_MOD_LIGHT_RED,
	NOOS_IIO_MOD_LIGHT_GREEN,
	NOOS_IIO_MOD_LIGHT_BLUE,
	NOOS_IIO_MOD_QUATERNION,
	NOOS_IIO_MOD_TEMP_AMBIENT,
	NOOS_IIO_MOD_TEMP_OBJECT,
	NOOS_IIO_MOD_NORTH_MAGN,
	NOOS_IIO_MOD_NORTH_TRUE,
	NOOS_IIO_MOD_NORTH_MAGN_TILT_COMP,
	NOOS_IIO_MOD_NORTH_TRUE_TILT_COMP,
	NOOS_IIO_MOD_RUNNING,
	NOOS_IIO_MOD_JOGGING,
	NOOS_IIO_MOD_WALKING,
	NOOS_IIO_MOD_STILL,
	NOOS_IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z,
	NOOS_IIO_MOD_I,
	NOOS_IIO_MOD_Q,
	NOOS_IIO_MOD_CO2,
	NOOS_IIO_MOD_VOC,
	NOOS_IIO_MOD_LIGHT_UV,
	NOOS_IIO_MOD_LIGHT_DUV,
	NOOS_IIO_MOD_PM1,
	NOOS_IIO_MOD_PM2P5,
	NOOS_IIO_MOD_PM4,
	NOOS_IIO_MOD_PM10,
	NOOS_IIO_MOD_ETHANOL,
	NOOS_IIO_MOD_H2,
	NOOS_IIO_MOD_O2,
	NOOS_IIO_MOD_LINEAR_X,
	NOOS_IIO_MOD_LINEAR_Y,
	NOOS_IIO_MOD_LINEAR_Z,
	NOOS_IIO_MOD_PITCH,
	NOOS_IIO_MOD_YAW,
	NOOS_IIO_MOD_ROLL,
	NOOS_IIO_MOD_LIGHT_UVA,
	NOOS_IIO_MOD_LIGHT_UVB,
	NOOS_IIO_MOD_RMS,
	NOOS_IIO_MOD_ACTIVE,
	NOOS_IIO_MOD_REACTIVE,
	NOOS_IIO_MOD_APPARENT,
	NOOS_IIO_MOD_QUATERNION_AXIS,
	NOOS_IIO_MOD_COUNT,
};

/* Byte order of a scan element, as Linux's enum iio_endian and in its order:
 * NOOS_IIO_CPU is the board's own. */
enum noos_iio_endian {
	NOOS_IIO_CPU,
	NOOS_IIO_BE,
	NOOS_IIO_LE,
};

/* A scan element's sign, as Linux's IIO_SCAN_FORMAT_*_INT. */
#define NOOS_IIO_SCAN_FORMAT_SIGNED_INT 's'
#define NOOS_IIO_SCAN_FORMAT_UNSIGNED_INT 'u'

/**
 * struct noos_iio_scan_type - how a scan element is stored in a sample, as
 * Linux's struct iio_scan_type, field for field, so a Linux driver's
 * scan_type carries over as it is
 * @sign:        's' signed (two's complement) or 'u' unsigned
 * @realbits:    bits of data
 * @storagebits: bits it takes in the sample: 8, 16, 32 or 64
 * @shift:       right shift that brings the data to bit 0
 * @repeat:      values per element (0 or 1: one; more is not carried yet)
 * @endianness:  NOOS_IIO_BE, NOOS_IIO_LE or NOOS_IIO_CPU
 *
 * Clients see it as libiio's format, "le:s20/32>>0"; the scale they convert
 * with is the channel's scale attribute.
 */
struct noos_iio_scan_type {
	char sign;
	uint8_t realbits;
	uint8_t storagebits;
	uint8_t shift;
	uint8_t repeat;
	enum noos_iio_endian endianness;
};

struct noos_iio_chan;

/* An attribute outside NOOS_IIO_INFO_*, as text, on the device's @dev. @ch
 * is NULL for a device attribute, and @priv is the entry's own, so one
 * handler can serve several attributes (iio_enum.h). Returns as
 * read_attr/write_attr do. */
struct noos_iio_ext_attr {
	const char *name;
	int (*read)(void *dev, const struct noos_iio_chan *ch, intptr_t priv, char *dst,
			size_t len);
	int (*write)(void *dev, const struct noos_iio_chan *ch, intptr_t priv, const char *src,
			size_t len);
	intptr_t priv;
};

/**
 * struct noos_iio_chan - one channel, as Linux's struct iio_chan_spec
 *
 * What it is, which gives its id the way Linux names it: "voltage0",
 * "voltage0-voltage1", "accel_x", "accel0_x", "temp":
 * @type:         NOOS_IIO_VOLTAGE, NOOS_IIO_ACCEL ...
 * @channel:      its number, with @indexed
 * @channel2:     with @modified, its NOOS_IIO_MOD_*; with @differential,
 *                the number of the other input
 * @indexed:      the id carries @channel
 * @modified:     the id carries the modifier (not with @differential)
 * @differential: @channel minus @channel2 (needs @indexed)
 * @id:           an id of its own instead (may be NULL)
 *
 * @scan_index:   -1: not a scan element
 * @output:       an output channel ("out_"), else an input one ("in_")
 * @scan_type:    a scan element's storage, as Linux's
 *
 * Its standard attributes, as NOOS_IIO_INFO_BIT()s, each in one mask, with
 * the same placement as on Linux, so a client sees them where it would
 * there:
 * @info_mask_separate:       its own ("scale" of accel_x)
 * @info_mask_shared_by_type: one value for all the channels of its type and
 *                direction, still an attribute of each ("scale" of each
 *                accel channel, in_accel_scale on Linux)
 * @info_mask_shared_by_dir:  one value for all the channels of its
 *                direction: a device attribute "in_<name>" or "out_<name>"
 * @info_mask_shared_by_all:  one value for the device: a device attribute
 *                "<name>"
 * Each *_available mask holds those of its mask with an "_available"
 * attribute. A device attribute that several channels have is read and
 * written through the first of them, as Linux does.
 *
 * @ext:          more attributes of the channel, ending at a NULL name
 * @priv:         the driver's, e.g. an axis
 */
struct noos_iio_chan {
	enum noos_iio_chan_type type;
	int channel;
	int channel2;
	bool indexed;
	bool modified;
	bool differential;
	const char *id;

	long scan_index;
	bool output;
	struct noos_iio_scan_type scan_type;

	uint64_t info_mask_separate;
	uint64_t info_mask_separate_available;
	uint64_t info_mask_shared_by_type;
	uint64_t info_mask_shared_by_type_available;
	uint64_t info_mask_shared_by_dir;
	uint64_t info_mask_shared_by_dir_available;
	uint64_t info_mask_shared_by_all;
	uint64_t info_mask_shared_by_all_available;

	const struct noos_iio_ext_attr *ext;
	intptr_t priv;
};

typedef int (*noos_iio_read_raw_t)(void *dev, const struct noos_iio_chan *ch,
		enum noos_iio_info info, int *val, int *val2);
/* Several values at once (NOOS_IIO_VAL_INT_MULTIPLE ...): at most @max_len
 * of them in @vals, their count in @val_len; returns their NOOS_IIO_VAL_*. */
typedef int (*noos_iio_read_raw_multi_t)(void *dev, const struct noos_iio_chan *ch, int max_len,
		int *vals, int *val_len, enum noos_iio_info info);
typedef int (*noos_iio_write_raw_t)(void *dev, const struct noos_iio_chan *ch,
		enum noos_iio_info info, int val, int val2);
typedef int (*noos_iio_write_raw_get_fmt_t)(
		void *dev, const struct noos_iio_chan *ch, enum noos_iio_info info);
typedef int (*noos_iio_read_avail_t)(void *dev, const struct noos_iio_chan *ch,
		enum noos_iio_info info, const int **vals, int *type, int *length);
/* One scan: vals[i] for each channels[i] whose bit is set in @enabled. */
typedef int (*noos_iio_read_scan_t)(void *dev, uint32_t enabled, int64_t *vals);
typedef int (*noos_iio_wait_scan_t)(void *dev);

/* NOOS_IIO_BIT(SCALE): the bit of NOOS_IIO_INFO_SCALE, for channel masks. */
#define NOOS_IIO_BIT(info) NOOS_IIO_INFO_BIT(NOOS_IIO_INFO_##info)

/* The scan_type of a channel that is not a scan element. */
#define NOOS_IIO_NO_SCAN_TYPE { 0 }

/**
 * struct noos_iio_value - a standard attribute of the channels of one type
 * that is data: a constant, or a value picked from a table
 * @chan_type: the channels it serves (NOOS_IIO_ACCEL ...)
 * @info:      the attribute (NOOS_IIO_INFO_*)
 * @type:      NOOS_IIO_VAL_* of the values; one that can be written (@set)
 *             is INT, INT_PLUS_MICRO, INT_PLUS_NANO, INT_PLUS_MICRO_DB or
 *             INT_64, the types a write is parsed as
 * @vals:      the values, flat (ints, or {val, val2} pairs), @nb of them
 * @avail:     instead of @vals, for a table that changes (filter corners
 *             that follow the sampling frequency): the values now, and
 *             their count
 * @get:       the index in use (may be NULL: 0)
 * @set:       switch to index @idx (may be NULL: read-only)
 *
 * The backend serves such an attribute before the device's read_raw and
 * write_raw see it: a read gives the row in use, a write is parsed as @type
 * and must be a row of the table (else -EINVAL), and its "_available" list
 * is the table. The callbacks get the device's dev. Written with the
 * macros below, one line each.
 */
struct noos_iio_value {
	enum noos_iio_chan_type chan_type;
	enum noos_iio_info info;
	int type;
	const int *vals;
	unsigned int nb;
	const int *(*avail)(void *dev, unsigned int *nb);
	int (*get)(void *dev);
	int (*set)(void *dev, unsigned int idx);
};

/* Ints per value of a NOOS_IIO_VAL_* type in a table. */
#define NOOS_IIO_VAL_STRIDE(type) \
	((type) == NOOS_IIO_VAL_INT || (type) == NOOS_IIO_VAL_CHAR ? 1 : 2)

/*
 * NOOS_IIO_CONST - a read-only attribute with one fixed value
 * @_chan, @_info, @_type: without their NOOS_IIO_, NOOS_IIO_INFO_ and
 *                         NOOS_IIO_VAL_ prefixes
 * @...: the value: one int for INT and CHAR, else val, val2
 *
 * Served on every channel of @_chan with @_info; writes give -EPERM.
 * NOOS_IIO_CONST(ACCEL, SCALE, INT_PLUS_NANO, 0, 38245) gives
 * in_accel_scale = 0.000038245.
 */
#define NOOS_IIO_CONST(_chan, _info, _type, ...) \
	{ \
		.chan_type = NOOS_IIO_##_chan, \
		.info = NOOS_IIO_INFO_##_info, \
		.type = NOOS_IIO_VAL_##_type, \
		.vals = (const int[]){ __VA_ARGS__ }, \
		.nb = 1, \
	}

/*
 * NOOS_IIO_LIST - an attribute picked from a fixed table
 * @_chan, @_info, @_type: as NOOS_IIO_CONST
 * @_table: an array (not a pointer, its size is the row count) of ints,
 *          or {val, val2} pairs for two-int types
 * @_get:   the row in use, or a negative error (may be NULL: row 0)
 * @_set:   switch to row idx (may be NULL: writes give -EPERM)
 *
 * A write must equal a row (else -EINVAL) and reaches @_set as its index;
 * "_available" lists the table.
 */
#define NOOS_IIO_LIST(_chan, _info, _type, _table, _get, _set) \
	{ \
		.chan_type = NOOS_IIO_##_chan, \
		.info = NOOS_IIO_INFO_##_info, \
		.type = NOOS_IIO_VAL_##_type, \
		.vals = (_table), \
		.nb = sizeof(_table) / sizeof(int) / NOOS_IIO_VAL_STRIDE(NOOS_IIO_VAL_##_type), \
		.get = (_get), \
		.set = (_set), \
	}

/*
 * NOOS_IIO_LIST_FN - as NOOS_IIO_LIST, for a table that changes at run time
 * @_avail: returns the table now, laid out as @_table, and its row count
 *          in *nb; called on every access, so it must return driver-owned
 *          memory, never a local array
 *
 * For a table that follows the device's state, e.g. the ADXL355's
 * high-pass corners, which scale with its ODR.
 */
#define NOOS_IIO_LIST_FN(_chan, _info, _type, _avail, _get, _set) \
	{ \
		.chan_type = NOOS_IIO_##_chan, \
		.info = NOOS_IIO_INFO_##_info, \
		.type = NOOS_IIO_VAL_##_type, \
		.avail = (_avail), \
		.get = (_get), \
		.set = (_set), \
	}

/* The table entry a channel of a table-driven device was created from. */
const struct noos_iio_chan *noos_iio_chan_of(const struct iio_channel *chn);

typedef int (*noos_iio_reg_read_t)(void *dev, uint32_t reg, uint32_t *val);
typedef int (*noos_iio_reg_write_t)(void *dev, uint32_t reg, uint32_t val);

/**
 * struct noos_iio_device_info - describes one IIO device for the backend
 * @name:          human-readable device name
 * @dev:           pointer to the no-OS device instance (opaque)
 * @direction:     0 = input (RX), 1 = output (TX)
 * @add_channels:  populate channels on the iio_device (may be NULL)
 * @read_attr:     read an attribute value (may be NULL)
 * @write_attr:    write an attribute value (may be NULL)
 * @read_samples:  read sample data from hardware (RX, may be NULL)
 * @write_samples: write sample data to hardware (TX, may be NULL)
 * @enable_buffer: buffer enabled / disabled (may be NULL)
 * @reg_read:      read a device register (may be NULL)
 * @reg_write:     write a device register (may be NULL)
 *
 * A table-driven device describes itself as a Linux driver does (iio_dev
 * and iio_info) and sets these instead of add_channels, read_attr,
 * write_attr and read_samples (any of those it sets still wins); the
 * callbacks get @dev:
 * @channels, @num_channels: its channels, at most 32; the array must
 *                 outlive the device (iio_channels.h
 *                 have the standard ones of their types)
 * @attrs:         its device attributes outside NOOS_IIO_INFO_*, ending at
 *                 a NULL name (may be NULL); their callbacks get a NULL
 *                 channel
 * @values, @nb_values: the standard attributes that are data (struct
 *                 noos_iio_value), served before @read_raw (may be NULL)
 * @read_raw:      read a NOOS_IIO_INFO_* value, returns its NOOS_IIO_VAL_*
 * @read_raw_multi: instead of @read_raw, for values that are more than two
 *                 ints (may be NULL)
 * @write_raw:     write one (may be NULL: read-only)
 * @write_raw_get_fmt: the NOOS_IIO_VAL_* a write is parsed as (may be NULL:
 *                 NOOS_IIO_VAL_INT_PLUS_MICRO, which also takes integers)
 * @read_avail:    the "_available" values, returns a NOOS_IIO_AVAIL_*
 * @read_scan:     one scan of the enabled scan elements (may be NULL: none)
 * @wait_scan:     pace each scan, e.g. on a data-ready (may be NULL)
 *
 * noos_iio_register_device() refuses, with -EINVAL, a channel whose masks
 * overlap or name no NOOS_IIO_INFO_*, an id it cannot make, a scan element
 * whose scan_type has no sign, storage that is not 8 to 64 bits in whole
 * bytes, or data and shift wider than it, an attribute nothing serves, and
 * a value entry no channel has or that cannot be.
 *
 *
 * Fill it with a designated initializer so the fields left out are zero.
 */
struct noos_iio_device_info {
	const char *name;
	void *dev;
	int direction;
	noos_iio_add_channels_t add_channels;
	noos_iio_read_attr_t read_attr;
	noos_iio_write_attr_t write_attr;
	noos_iio_read_samples_t read_samples;
	noos_iio_write_samples_t write_samples;
	noos_iio_enable_buffer_t enable_buffer;
	noos_iio_reg_read_t reg_read;
	noos_iio_reg_write_t reg_write;

	/* Table-driven devices */
	const struct noos_iio_chan *channels;
	unsigned int num_channels;
	const struct noos_iio_ext_attr *attrs;
	const struct noos_iio_value *values;
	unsigned int nb_values;
	noos_iio_read_raw_t read_raw;
	noos_iio_read_raw_multi_t read_raw_multi;
	noos_iio_write_raw_t write_raw;
	noos_iio_write_raw_get_fmt_t write_raw_get_fmt;
	noos_iio_read_avail_t read_avail;
	noos_iio_read_scan_t read_scan;
	noos_iio_wait_scan_t wait_scan;
};

/**
 * struct noos_iio_enum - an attribute that takes one of a list of names,
 * as Linux's IIO_ENUM
 * @items:    the names, by index; a NULL entry is never shown nor accepted
 * @nb_items: number of entries in @items
 * @mask:     the indexes the hardware has, as bits (0: all of them); the
 *            others are refused with -ENOTSUP and not listed
 * @get:      the index in use on @ch, or a negative error code
 * @set:      switch @ch to index @idx (may be NULL: read-only); an index
 *            the hardware cannot take returns -ENOTSUP
 */
struct noos_iio_enum {
	const char *const *items;
	unsigned int nb_items;
	uint32_t mask;
	int (*get)(void *dev, const struct noos_iio_chan *ch);
	int (*set)(void *dev, const struct noos_iio_chan *ch, unsigned int idx);
};

int noos_iio_enum_read(
		void *dev, const struct noos_iio_chan *ch, intptr_t priv, char *dst, size_t len);
int noos_iio_enum_write(void *dev, const struct noos_iio_chan *ch, intptr_t priv, const char *src,
		size_t len);
int noos_iio_enum_available(
		void *dev, const struct noos_iio_chan *ch, intptr_t priv, char *dst, size_t len);

/* noos_iio_ext_attr entries for a struct noos_iio_enum: the attribute
 * itself, and its "_available" list of names. */
#define NOOS_IIO_ENUM(_name, _e) \
	{ \
		.name = (_name), \
		.read = noos_iio_enum_read, \
		.write = noos_iio_enum_write, \
		.priv = (intptr_t)(_e), \
	}

#define NOOS_IIO_ENUM_AVAILABLE(_name, _e) \
	{ \
		.name = _name "_available", \
		.read = noos_iio_enum_available, \
		.priv = (intptr_t)(_e), \
	}

/*
 * Make a table-driven device's callbacks (add_channels, read_attr,
 * write_attr, read_samples) from its tables, after checking them; a
 * device that is not table-driven is left as it is. Called by
 * noos_iio_register_device(). Returns 0 or -EINVAL.
 */
int noos_iio_table_install(struct noos_iio_device_info *info);

/*
 * Attribute values, for read_attr/write_attr: integer and fixed point only,
 * so no floating point is needed. The format functions write a
 * NUL-terminated string and return its length with the NUL (what a
 * read_attr returns), or -ERANGE if it does not fit; fixed point is
 * val + val2 / 10^digits (digits 1..9). The parse functions take a
 * NUL-terminated string, accept one trailing newline and return 0 or
 * -EINVAL.
 */
ssize_t noos_value_format_int(char *dst, size_t len, long long val);
ssize_t noos_value_format_str(char *dst, size_t len, const char *val);
ssize_t noos_value_format_fixed(char *dst, size_t len, int val, int val2, unsigned int digits);
int noos_value_parse_int(const char *src, long long *val);
int noos_value_parse_fixed(const char *src, int *val, int *val2, unsigned int digits);
/* The index of @src (@len bytes, trailing NULs/newlines ignored) in @table. */
int noos_value_match_str(const char *const *table, size_t count, const char *src, size_t len);

/**
 * noos_iio_register_device() - register an IIO device before context creation
 * @info: pointer to a device info structure (contents are copied)
 *
 * Returns 0 on success, -EINVAL if @info is NULL, -ENOMEM if the device
 * table is full.
 */
int noos_iio_register_device(const struct noos_iio_device_info *info);

struct noos_iiod_param;

/*
 * Open the project's console and register its device table. Each
 * transport's noos_iiod_run() calls it once, before building the context.
 */
void noos_iiod_register(const struct noos_iiod_param *param);

/* Accessed by the backend – not for direct application use */
extern struct noos_iio_device_info noos_iio_devices[];
extern unsigned int noos_iio_device_count;

#ifdef __cplusplus
}
#endif

#endif /* NOOS_INCLUDE_IIO_DEVICE_H_ */
