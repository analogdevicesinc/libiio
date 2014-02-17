#include "debug.h"
#include "iio-private.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysfs/libsysfs.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

struct local_pdata {
	char *path;
};

static const char * const device_attrs_blacklist[] = {
	"dev",
	"uevent",
};

static const char * const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y] = "sqrt(x^2+y^2)",
	[IIO_MOD_SUM_SQUARED_X_Y_Z] = "x^2+y^2+z^2",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
	[IIO_MOD_LIGHT_CLEAR] = "clear",
	[IIO_MOD_LIGHT_RED] = "red",
	[IIO_MOD_LIGHT_GREEN] = "green",
	[IIO_MOD_LIGHT_BLUE] = "blue",
};

static void free_channel(struct iio_channel *chn)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++)
		free((char *) chn->attrs[i]);
	if (chn->nb_attrs)
		free(chn->attrs);
	if (chn->name)
		free((char *) chn->name);
	if (chn->id)
		free((char *) chn->id);
	free(chn);
}

static void free_device(struct iio_device *dev)
{
	unsigned int i;
	for (i = 0; i < dev->nb_attrs; i++)
		free((char *) dev->attrs[i]);
	if (dev->nb_attrs)
		free(dev->attrs);
	for (i = 0; i < dev->nb_channels; i++)
		free_channel(dev->channels[i]);
	if (dev->nb_channels)
		free(dev->channels);
	if (dev->name)
		free((char *) dev->name);
	if (dev->id)
		free((char *) dev->id);
	free(dev);
}

static void local_shutdown(struct iio_context *ctx)
{
	unsigned int i;

	if (ctx->backend_data) {
		struct local_pdata *pdata = ctx->backend_data;
		free(pdata->path);
		free(pdata);
	}

	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
}

/** Shrinks the first nb characters of a string 
 * e.g. strcut("foobar", 4) replaces the content with "ar". */
static void strcut(char *str, int nb)
{
	char *ptr = str + nb;
	while (*ptr)
		*str++ = *ptr++;
	*str = 0;
}

static int set_channel_name(struct iio_channel *chn)
{
	if (chn->nb_attrs < 2)
		return 0;

	while (true) {
		bool can_fix = true;
		unsigned int i, len;
		char *name;
		const char *attr0 = chn->attrs[0];
		const char *ptr = strchr(attr0, '_');
		if (!ptr)
			break;

		len = ptr - attr0;
		for (i = 1; can_fix && i < chn->nb_attrs; i++)
			can_fix = !strncmp(attr0, chn->attrs[i], len);

		if (!can_fix)
			break;

		if (chn->name) {
			name = malloc(strlen(chn->name) + len + 2);
			if (!name)
				return -ENOMEM;
			sprintf(name, "%s_%.*s", chn->name, len, attr0);
			DEBUG("Fixing name of channel %s from %s to %s\n",
					chn->id, chn->name, name);
			free((char *) chn->name);
		} else {
			name = malloc(len + 2);
			if (!name)
				return -ENOMEM;
			sprintf(name, "%.*s", len, attr0);
			DEBUG("Setting name of channel %s to %s\n",
					chn->id, name);
		}
		chn->name = name;

		/* Shrink the attribute name */
		for (i = 0; i < chn->nb_attrs; i++)
			strcut((char *) chn->attrs[i], len + 1);
	}

	if (chn->name) {
		unsigned int i;
		for (i = 0; i <	ARRAY_SIZE(modifier_names); i++) {
			unsigned int len;

			if (!modifier_names[i])
				continue;

			len = strlen(modifier_names[i]);
			if (!strncmp(chn->name, modifier_names[i], len)) {
				if (chn->name[len]) {
					/* Shrink the modifier from the extended name */
					strcut((char *) chn->name, len + 1);
				} else {
					free((char *) chn->name);
					chn->name = NULL;
				}
				chn->modifier = i;
				DEBUG("Detected modifier for channel %s: %s\n",
						chn->id, modifier_names[i]);
				break;
			}
		}
	}
	return 0;
}

static ssize_t local_read_attr(const struct iio_device *dev,
		const char *path, char *dst, size_t len)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;

	sprintf(buf, "/sys/bus/iio/devices/%s/%s", dev->id, path);
	f = fopen(buf, "r");
	if (!f)
		return -errno;

	ret = fread(dst, 1, len, f);
	if (ret > 0)
		dst[ret - 1] = '\0';
	fclose(f);
	return ret;
}

static ssize_t local_write_attr(const struct iio_device *dev,
		const char *path, const char *src)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;
	size_t len = strlen(src) + 1;

	sprintf(buf, "/sys/bus/iio/devices/%s/%s", dev->id, path);
	f = fopen(buf, "w");
	if (!f)
		return -errno;

	ret = fwrite(src, 1, len, f);
	fclose(f);
	return ret;
}

static bool is_channel(const char *attr)
{
	unsigned int i;
	char *ptr = NULL;
	if (!strncmp(attr, "in_", 3))
		ptr = strchr(attr + 3, '_');
	else if (!strncmp(attr, "out_", 4))
		ptr = strchr(attr + 4, '_');
	if (!ptr)
		return false;
	if (*(ptr - 1) >= '0' && *(ptr - 1) <= '9')
		return true;
	for (i = 0; i < ARRAY_SIZE(modifier_names); i++)
		if (modifier_names[i] && !strncmp(ptr + 1, modifier_names[i],
					strlen(modifier_names[i])))
			return true;
	return false;
}

static char * get_channel_id(const char *attr)
{
	char *res, *ptr;
	unsigned int i;

	attr = strchr(attr, '_') + 1;
	ptr = strchr(attr, '_');
	for (i = 0; i < ARRAY_SIZE(modifier_names); i++) {
		if (modifier_names[i] && !strncmp(ptr + 1, modifier_names[i],
					strlen(modifier_names[i]))) {
			ptr = strchr(ptr + 1, '_');
			break;
		}
	}

	res = malloc(ptr - attr + 1);
	if (!res)
		return NULL;

	memcpy(res, attr, ptr - attr);
	res[ptr - attr] = 0;
	return res;
}

static char * get_short_attr_name(const char *attr)
{
	char *ptr = strchr(attr, '_') + 1;
	ptr = strchr(ptr, '_') + 1;
	return strdup(ptr);
}

static int read_device_name(struct iio_device *dev)
{
	char buf[1024];
	ssize_t ret = iio_device_attr_read(dev, "name", buf, 1024);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -EIO;

	dev->name = strdup(buf);
	if (!dev->name)
		return -ENOMEM;
	else
		return 0;
}

static int add_attr_to_device(struct iio_device *dev, const char *attr)
{
	const char **attrs;
	char *name;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(device_attrs_blacklist); i++)
		if (!strcmp(device_attrs_blacklist[i], attr))
			return 0;

	if (!strcmp(attr, "name"))
		return read_device_name(dev);

	name = strdup(attr);
	if (!name)
		return -ENOMEM;

	attrs = realloc(dev->attrs, (1 + dev->nb_attrs) * sizeof(char *));
	if (!attrs) {
		free(name);
		return -ENOMEM;
	}

	attrs[dev->nb_attrs++] = name;
	dev->attrs = attrs;
	DEBUG("Added attr \'%s\' to device \'%s\'\n", attr, dev->id);
	return 0;
}

static int add_attr_to_channel(struct iio_channel *chn, const char *attr)
{
	const char **attrs;
	char *name = get_short_attr_name(attr);
	if (!name)
		return -ENOMEM;

	attrs = realloc(chn->attrs, (1 + chn->nb_attrs) * sizeof(char *));
	if (!attrs) {
		free(name);
		return -ENOMEM;
	}

	attrs[chn->nb_attrs++] = name;
	chn->attrs = attrs;
	DEBUG("Added attr \'%s\' to channel \'%s\'\n", name, chn->id);
	return 0;
}

static int add_channel_to_device(struct iio_device *dev,
		struct iio_channel *chn)
{
	struct iio_channel **channels = realloc(dev->channels,
			(dev->nb_channels + 1) * sizeof(struct iio_channel *));
	if (!channels)
		return -ENOMEM;

	channels[dev->nb_channels++] = chn;
	dev->channels = channels;
	DEBUG("Added channel \'%s\' to device \'%s\'\n", chn->id, dev->id);
	return 0;
}

static int add_device_to_context(struct iio_context *ctx,
		struct iio_device *dev)
{
	struct iio_device **devices = realloc(ctx->devices,
			(ctx->nb_devices + 1) * sizeof(struct iio_device *));
	if (!devices)
		return -ENOMEM;

	devices[ctx->nb_devices++] = dev;
	ctx->devices = devices;
	DEBUG("Added device \'%s\' to context \'%s\'\n", dev->id, ctx->name);
	return 0;
}

static bool is_global_attr(struct iio_channel *chn, const char *attr)
{
	unsigned int i, len;
	char *ptr;

	if (!strncmp(attr, "in_", 3)) {
		if (chn->type != IIO_CHANNEL_INPUT)
			return false;
		else
			attr += 3;
	} else if (!strncmp(attr, "out_", 4)) {
		if (chn->type != IIO_CHANNEL_OUTPUT)
			return false;
		else
			attr += 4;
	} else
		return false;

	ptr = strchr(attr, '_');
	if (!ptr)
		return false;

	len = ptr - attr;

	if (strncmp(chn->id, attr, len))
		return false;

	DEBUG("Found match: %s and %s\n", chn->id, attr);
	if (chn->id[len] >= '0' && chn->id[len] <= '9')
		return true;
	else if (chn->id[len] != '_')
		return false;

	for (i = 0; i < ARRAY_SIZE(modifier_names); i++)
		if (modifier_names[i] &&
				!strncmp(chn->id + len + 1, modifier_names[i],
					strlen(modifier_names[i])))
			return true;

	return false;
}

static int detect_and_move_global_attrs(struct iio_device *dev)
{
	unsigned int i;
	const char **ptr = dev->attrs;

	for (i = 0; i < dev->nb_attrs; i++) {
		unsigned int j;
		bool global = false;
		const char *attr = dev->attrs[i];

		for (j = 0; j < dev->nb_channels; j++) {
			struct iio_channel *chn = dev->channels[j];
			if (is_global_attr(chn, attr)) {
				int ret;
				global = true;
				ret = add_attr_to_channel(chn, attr);
				if (ret)
					return ret;
			}
		}

		if (global) {
			free((char *) dev->attrs[i]);
			dev->attrs[i] = NULL;
		}
	}

	for (i = 0; i < dev->nb_attrs; i++) {
		if (dev->attrs[i])
			*ptr++ = dev->attrs[i];
	}

	dev->nb_attrs = ptr - dev->attrs;
	return 0;
}

static struct iio_channel *create_channel(struct iio_device *dev,
		char *id, const char *attr)
{
	struct iio_channel *chn = calloc(1, sizeof(*chn));
	if (!chn)
		return NULL;

	if (!strncmp(attr, "in_", 3))
		chn->type = IIO_CHANNEL_INPUT;
	else if (!strncmp(attr, "out_", 4))
		chn->type = IIO_CHANNEL_OUTPUT;
	else
		chn->type = IIO_CHANNEL_UNKNOWN;

	chn->dev = dev;
	chn->id = id;
	chn->modifier = IIO_NO_MOD;

	if (add_attr_to_channel(chn, attr)) {
		free(chn);
		return NULL;
	}
	return chn;
}

static struct iio_device *create_device(struct iio_context *ctx,
		struct sysfs_device *device)
{
	struct iio_device *dev;
	struct dlist *attrlist;
	struct sysfs_attribute *attr;
	unsigned int i;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->ctx = ctx;
	dev->id = strdup(device->name);
	if (!dev->id) {
		free(dev);
		return NULL;
	}

	attrlist = sysfs_get_device_attributes(device);
	dlist_for_each_data(attrlist, attr, struct sysfs_attribute) {
		if (is_channel(attr->name)) {
			unsigned int i;
			bool new_channel = true;
			struct iio_channel *chn;
			char *channel_id = get_channel_id(attr->name);
			if (!channel_id) {
				free_device(dev);
				return NULL;
			}

			for (i = 0; i < dev->nb_channels; i++) {
				chn = dev->channels[i];
				if (strcmp(chn->id, channel_id))
					continue;

				free(channel_id);
				new_channel = false;
				if (add_attr_to_channel(chn, attr->name)) {
					free_device(dev);
					return NULL;
				}
				break;
			}

			if (!new_channel)
				continue;

			chn = create_channel(dev, channel_id, attr->name);
			if (!chn) {
				free(channel_id);
				free_device(dev);
				return NULL;
			}
			if (add_channel_to_device(dev, chn)) {
				free_channel(chn);
				free_device(dev);
				return NULL;
			}
		} else if (add_attr_to_device(dev, attr->name)) {
			free_device(dev);
			return NULL;
		}
	}

	for (i = 0; i < dev->nb_channels; i++)
		set_channel_name(dev->channels[i]);

	if (detect_and_move_global_attrs(dev)) {
		free_device(dev);
		return NULL;
	}

	return dev;
}

static struct iio_backend_ops local_ops = {
	.read_attr = local_read_attr,
	.write_attr = local_write_attr,
	.shutdown = local_shutdown,
};

struct iio_context * iio_create_local_context(void)
{
	struct sysfs_bus *iio;
	struct sysfs_device *device;
	struct dlist *devlist;
	char *path;

	struct iio_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		ERROR("Unable to allocate memory\n");
		return NULL;
	}

	ctx->backend_data = malloc(sizeof(struct local_pdata));
	if (!ctx->backend_data) {
		ERROR("Unable to allocate memory\n");
		goto err_free_ctx;
	}

	ctx->ops = &local_ops;
	ctx->name = "local";

	iio = sysfs_open_bus("iio");
	if (!iio) {
		ERROR("Unable to open IIO bus\n");
		goto err_shutdown_ctx;
	}

	path = strdup(iio->path);
	if (!path) {
		ERROR("Unable to allocate memory\n");
		goto err_close_sysfs_bus;
	}

	((struct local_pdata *) ctx->backend_data)->path = path;

	devlist = sysfs_get_bus_devices(iio);
	dlist_for_each_data(devlist, device, struct sysfs_device) {
		struct iio_device *dev = create_device(ctx, device);
		if (!dev) {
			ERROR("Unable to create IIO device structure\n");
			goto err_close_sysfs_bus;
		}

		if (add_device_to_context(ctx, dev)) {
			ERROR("Unable to allocate memory\n");
			free(dev);
			goto err_close_sysfs_bus;
		}
	}

	sysfs_close_bus(iio);

	return ctx;

err_close_sysfs_bus:
	sysfs_close_bus(iio);
err_shutdown_ctx:
	local_shutdown(ctx);
err_free_ctx:
	free(ctx);
	return NULL;
}
