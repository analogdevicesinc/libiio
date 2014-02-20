#ifndef __IIO_PRIVATE_H__
#define __IIO_PRIVATE_H__

/* Include public interface */
#pragma GCC visibility push(default)
#include "iio.h"
#pragma GCC visibility pop

#include <stdbool.h>

enum iio_context_type {
	IIO_LOCAL_CONTEXT,
	IIO_DUMMY_CONTEXT,
	IIO_NETWORK_CONTEXT,
};

enum iio_modifier {
	IIO_NO_MOD,
	IIO_MOD_X,
	IIO_MOD_Y,
	IIO_MOD_Z,
	IIO_MOD_LIGHT_BOTH,
	IIO_MOD_LIGHT_IR,
	IIO_MOD_ROOT_SUM_SQUARED_X_Y,
	IIO_MOD_SUM_SQUARED_X_Y_Z,
	IIO_MOD_LIGHT_CLEAR,
	IIO_MOD_LIGHT_RED,
	IIO_MOD_LIGHT_GREEN,
	IIO_MOD_LIGHT_BLUE,
};

struct iio_backend_ops {
	ssize_t (*read)(struct iio_device *dev, void *dst, size_t len);
	ssize_t (*write)(struct iio_device *dev, const void *src, size_t len);

	int (*channel_enable)(struct iio_channel *channel);
	int (*channel_disable)(struct iio_channel *channel);

	unsigned int (*get_devices_count)(const struct iio_context *ctx);
	struct iio_device * (*get_device)(const struct iio_context *ctx,
			unsigned int id);

	ssize_t (*read_device_attr)(const struct iio_device *dev,
			const char *attr, char *dst, size_t len);
	ssize_t (*write_device_attr)(const struct iio_device *dev,
			const char *attr, const char *src);
	ssize_t (*read_channel_attr)(const struct iio_channel *chn,
			const char *attr, char *dst, size_t len);
	ssize_t (*write_channel_attr)(const struct iio_channel *chn,
			const char *attr, const char *src);

	void (*shutdown)(struct iio_context *ctx);
};

struct iio_context {
	const struct iio_backend_ops *ops;
	void *backend_data;
	const char *name;

	struct iio_device **devices;
	unsigned int nb_devices;
};

struct iio_data_format {
	unsigned int length;
	unsigned int bits;
	unsigned int shift;
	bool with_scale;
	float scale;
};

struct iio_channel {
	bool is_output;
	enum iio_modifier modifier;
	struct iio_data_format data_format;
	struct iio_device *dev;
	char *name, *id;
	unsigned int index;
	bool enabled;

	char **attrs;
	unsigned int nb_attrs;
};

struct iio_device {
	const struct iio_context *ctx;
	char *name, *id;

	char **attrs;
	unsigned int nb_attrs;

	struct iio_channel **channels;
	unsigned int nb_channels;
};

void iio_msg(enum iio_debug_level level, const char *msg);
void free_channel(struct iio_channel *chn);
void free_device(struct iio_device *dev);

#endif /* __IIO_PRIVATE_H__ */
