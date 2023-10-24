// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-private.h"
#include "local.h"
#include "sort.h"
#include "deps/libini/ini.h"

#include <dirent.h>
#include <errno.h>
#include <iio/iio-debug.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define NB_BLOCKS 4

#define IIO_BUFFER_GET_FD_IOCTL		_IOWR('i', 0x91, int)

/* Forward declarations */
static ssize_t local_write_dev_attr(const struct iio_device *dev,
				    unsigned int buf_id, const char *attr,
				    const char *src, size_t len,
				    enum iio_attr_type type);
static struct iio_context *
local_create_context(const struct iio_context_params *params, const char *args);
static int local_context_scan(const struct iio_context_params *params,
			      struct iio_scan *ctx, const char *args);

struct iio_device_pdata {
	int fd;
};

struct iio_channel_pdata {
	char *enable_fn;
	struct iio_attr_list protected;
};

static const char * const device_attrs_denylist[] = {
	"dev",
	"uevent",
};

static const char * const buffer_attrs_reserved[] = {
	"length",
	"enable",
	"watermark",
};

int ioctl_nointr(int fd, unsigned long request, void *data)
{
	int ret;

	do {
		ret = ioctl(fd, request, data);
	} while (ret == -1 && errno == EINTR);

	if (ret == -1)
		ret = -errno;

	return ret;
}

static void local_free_channel_pdata(struct iio_channel *chn)
{
	if (chn->pdata) {
		free(chn->pdata->enable_fn);
		free(chn->pdata);
	}
}

static void local_free_pdata(struct iio_device *device)
{
	unsigned int i;

	for (i = 0; i < device->nb_channels; i++)
		local_free_channel_pdata(device->channels[i]);

	free(device->pdata);
}

static void local_shutdown(struct iio_context *ctx)
{
	/* Free the backend data stored in every device structure */
	unsigned int i;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);

		if (dev->pdata && dev->pdata->fd >= 0)
			close(dev->pdata->fd);
		local_free_pdata(dev);
	}
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
	struct iio_channel_pdata *pdata = chn->pdata;
	size_t prefix_len = 0;
	const char *attr0;
	const char *ptr;
	unsigned int i;

	if (chn->attrlist.num + pdata->protected.num < 2)
		return 0;

	if (chn->attrlist.num)
		attr0 = ptr = chn->attrlist.attrs[0].name;
	else
		attr0 = ptr = pdata->protected.attrs[0].name;

	while (true) {
		bool can_fix = true;
		size_t len;

		ptr = strchr(ptr, '_');
		if (!ptr)
			break;

		len = ptr - attr0 + 1;
		for (i = 1; can_fix && i < chn->attrlist.num; i++)
			can_fix = !strncmp(attr0, chn->attrlist.attrs[i].name, len);

		for (i = !chn->attrlist.num;
				can_fix && i < pdata->protected.num; i++) {
			can_fix = !strncmp(attr0,
					pdata->protected.attrs[i].name, len);
		}

		if (!can_fix)
			break;

		prefix_len = len;
		ptr = ptr + 1;
	}

	if (prefix_len) {
		char *name;

		name = malloc(prefix_len);
		if (!name)
			return -ENOMEM;
		iio_strlcpy(name, attr0, prefix_len);
		chn->name = name;
		chn_dbg(chn, "Setting name of channel %s to %s\n", chn->id, name);

		/* Shrink the attribute name */
		for (i = 0; i < chn->attrlist.num; i++)
			strcut((char *) chn->attrlist.attrs[i].name, prefix_len);
		for (i = 0; i < pdata->protected.num; i++)
			strcut((char *) pdata->protected.attrs[i].name, prefix_len);
	}

	return 0;
}

/*
 * Used to generate the timeout parameter for operations like poll. Returns the
 * number of ms until it is timeout_rel ms after the time specified in start. If
 * timeout_rel is 0 returns -1 to indicate no timeout.
 *
 * The timeout that is specified for IIO operations is the maximum time a buffer
 * push() or refill() operation should take before returning. poll() is used to
 * wait for either data activity or for the timeout to elapse. poll() might get
 * interrupted in which case it is called again or the read()/write() operation
 * might not complete the full buffer size in one call in which case we go back
 * to poll() again as well. Passing the same timeout as before would increase
 * the total timeout and if repeated interruptions occur (e.g. by a timer
 * signal) the operation might never time out or with significant delay. Hence
 * before each poll() invocation the timeout is recalculated relative to the
 * start of refill() or push() operation.
 */
static int get_rel_timeout_ms(struct timespec *start, unsigned int timeout_rel)
{
	struct timespec now;
	int diff_ms;

	if (timeout_rel == 0) /* No timeout */
		return -1;

	clock_gettime(CLOCK_MONOTONIC, &now);

	diff_ms = (now.tv_sec - start->tv_sec) * 1000;
	diff_ms += (now.tv_nsec - start->tv_nsec) / 1000000;

	if (diff_ms >= (int) timeout_rel) /* Expired */
		return 0;
	if (diff_ms > 0) /* Should never be false, but lets be safe */
		timeout_rel -= diff_ms;
	if (timeout_rel > INT_MAX)
		return INT_MAX;

	return (int) timeout_rel;
}

int buffer_check_ready(struct iio_buffer_pdata *pdata, int fd,
		       short events, struct timespec *start)
{
	struct pollfd pollfd[2] = {
		{
			.fd = fd,
			.events = events,
		}, {
			.fd = pdata->cancel_fd,
			.events = POLLIN,
		}
	};
	unsigned int rw_timeout_ms = pdata->dev->ctx->params.timeout_ms;
	int timeout_rel, ret;

	do {
		if (start)
			timeout_rel = get_rel_timeout_ms(start, rw_timeout_ms);
		else
			timeout_rel = 0; /* non-blocking */

		ret = poll(pollfd, 2, timeout_rel);
	} while (ret == -1 && errno == EINTR);

	if ((pollfd[1].revents & POLLIN))
		return -EBADF;

	if (ret < 0)
		return -errno;
	if (!ret)
		return start ? -ETIMEDOUT : -EBUSY;
	if (pollfd[0].revents & POLLNVAL)
		return -EBADF;
	if (!(pollfd[0].revents & events))
		return -EIO;
	return 0;
}

static int
local_set_buffer_size(const struct iio_buffer_pdata *pdata, size_t nb_samples)
{
	char buf[32];
	ssize_t ret;

	iio_snprintf(buf, sizeof(buf), "%zu", nb_samples);

	ret = local_write_dev_attr(pdata->dev, pdata->idx, "length",
				   buf, strlen(buf) + 1, IIO_ATTR_TYPE_BUFFER);
	if (ret < 0)
		return (int) ret;

	return 0;
}

static int
local_set_watermark(const struct iio_buffer_pdata *pdata, size_t nb_samples)
{
	char buf[32];
	ssize_t ret;

	iio_snprintf(buf, sizeof(buf), "%zu", nb_samples);

	ret = local_write_dev_attr(pdata->dev, pdata->idx, "watermark",
				   buf, strlen(buf) + 1, IIO_ATTR_TYPE_BUFFER);
	if (ret < 0 && ret != -ENOENT && ret != -EACCES)
		return (int) ret;

	return 0;
}

static int local_do_enable_buffer(struct iio_buffer_pdata *pdata, bool enable)
{
	int ret;

	ret = (int) local_write_dev_attr(pdata->dev, pdata->idx, "enable",
					 enable ? "1" : "0",
					 2, IIO_ATTR_TYPE_BUFFER);
	if (ret < 0)
		return ret;

	return 0;
}

static int local_enable_buffer(struct iio_buffer_pdata *pdata,
			       size_t nb_samples, bool enable)
{
	int ret;

	if ((pdata->dmabuf_supported | pdata->mmap_supported) != !nb_samples)
		return -EINVAL;

	if (nb_samples) {
		ret = local_set_buffer_size(pdata, nb_samples);
		if (ret)
			return ret;

		ret = local_set_watermark(pdata, nb_samples);
		if (ret)
			return ret;
	}

	return local_do_enable_buffer(pdata, true);
}

static ssize_t local_readbuf(struct iio_buffer_pdata *buffer,
			     void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;
	struct timespec start;
	int fd = buffer->fd;
	ssize_t readsize;
	ssize_t ret;

	if (fd == -1)
		return -EBADF;

	if (len == 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (len > 0) {
		ret = buffer_check_ready(buffer, fd, POLLIN, &start);
		if (ret < 0)
			break;

		do {
			ret = read(fd, (void *) ptr, len);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1) {
			if (errno == EAGAIN)
				continue;
			ret = -errno;
			break;
		} else if (ret == 0) {
			ret = -EIO;
			break;
		}

		ptr += ret;
		len -= ret;
	}

	readsize = (ssize_t)(ptr - (uintptr_t) dst);
	if ((ret > 0 || ret == -EAGAIN) && (readsize > 0))
		return readsize;
	else
		return ret;
}

static ssize_t
local_writebuf(struct iio_buffer_pdata *buffer, const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;
	struct timespec start;
	int fd = buffer->fd;
	ssize_t writtensize;
	ssize_t ret;

	if (fd == -1)
		return -EBADF;

	if (len == 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (len > 0) {
		ret = buffer_check_ready(buffer, fd, POLLOUT, &start);
		if (ret < 0)
			break;

		do {
			ret = write(fd, (void *) ptr, len);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1) {
			if (errno == EAGAIN)
				continue;

			ret = -errno;
			break;
		} else if (ret == 0) {
			ret = -EIO;
			break;
		}

		ptr += ret;
		len -= ret;
	}

	writtensize = (ssize_t)(ptr - (uintptr_t) src);
	if ((ret > 0 || ret == -EAGAIN) && (writtensize > 0))
		return writtensize;
	else
		return ret;
}

static ssize_t local_read_dev_attr(const struct iio_device *dev,
				   unsigned int buf_id, const char *attr,
				   char *dst, size_t len,
				   enum iio_attr_type type)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;

	if (buf_id > 0)
		return -ENOSYS;

	switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
		case IIO_ATTR_TYPE_CHANNEL:
			if (WITH_HWMON && iio_device_is_hwmon(dev)) {
				iio_snprintf(buf, sizeof(buf), "/sys/class/hwmon/%s/%s",
							dev->id, attr);
			} else {
				iio_snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s",
							dev->id, attr);
			}
			break;
		case IIO_ATTR_TYPE_DEBUG:
			iio_snprintf(buf, sizeof(buf), "/sys/kernel/debug/iio/%s/%s",
					dev->id, attr);
			break;
		case IIO_ATTR_TYPE_BUFFER:
			if (buf_id > 0) {
				iio_snprintf(buf, sizeof(buf),
					     "/sys/bus/iio/devices/%s/buffer%u/%s",
					     dev->id, buf_id, attr);
			} else {
				iio_snprintf(buf, sizeof(buf),
					     "/sys/bus/iio/devices/%s/buffer/%s",
					     dev->id, attr);
			}
			break;
		default:
			return -EINVAL;
	}

	f = fopen(buf, "re");
	if (!f)
		return -errno;

	ret = fread(dst, 1, len, f);

	/* if we didn't read the entire file, fail */
	if (!feof(f))
		ret = -EFBIG;

	if (ret > 0)
		dst[ret - 1] = '\0';
	else
		dst[0] = '\0';

	fflush(f);
	if (ferror(f))
		ret = -errno;
	fclose(f);
	return ret;
}

static ssize_t local_write_dev_attr(const struct iio_device *dev,
				    unsigned int buf_id, const char *attr,
				    const char *src, size_t len,
				    enum iio_attr_type type)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;

	if (buf_id > 0)
		return -ENOSYS;

	switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
		case IIO_ATTR_TYPE_CHANNEL:
			if (WITH_HWMON && iio_device_is_hwmon(dev)) {
				iio_snprintf(buf, sizeof(buf), "/sys/class/hwmon/%s/%s",
					dev->id, attr);
			} else {
				iio_snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s",
					dev->id, attr);
			}
			break;
		case IIO_ATTR_TYPE_DEBUG:
			iio_snprintf(buf, sizeof(buf), "/sys/kernel/debug/iio/%s/%s",
					dev->id, attr);
			break;
		case IIO_ATTR_TYPE_BUFFER:
			if (buf_id > 0) {
				iio_snprintf(buf, sizeof(buf),
					     "/sys/bus/iio/devices/%s/buffer%u/%s",
					     dev->id, buf_id, attr);
			} else {
				iio_snprintf(buf, sizeof(buf),
					     "/sys/bus/iio/devices/%s/buffer/%s",
					     dev->id, attr);
			}
			break;
		default:
			return -EINVAL;
	}

	f = fopen(buf, "we");
	if (!f)
		return -errno;

	ret = fwrite(src, 1, len, f);
	fflush(f);
	if (ferror(f))
		ret = -errno;
	fclose(f);
	return ret ? ret : -EIO;
}

static const char * get_filename(const struct iio_channel *chn,
		const char *attr)
{
	unsigned int i;
	for (i = 0; i < chn->attrlist.num; i++)
		if (!strcmp(attr, chn->attrlist.attrs[i].name))
			return chn->attrlist.attrs[i].filename;
	return attr;
}

static int channel_write_state(const struct iio_channel *chn,
			       unsigned int idx, bool en)
{
	enum iio_attr_type type = idx ? IIO_ATTR_TYPE_BUFFER : IIO_ATTR_TYPE_DEVICE;
	ssize_t ret;

	if (!chn->pdata->enable_fn) {
		chn_err(chn, "Libiio bug: No \"en\" attribute parsed\n");
		return -EINVAL;
	}

	ret = local_write_dev_attr(chn->dev, idx, chn->pdata->enable_fn,
				   en ? "1" : "0", 2, type);
	if (ret < 0)
		return (int) ret;
	else
		return 0;
}

static int channel_read_state(const struct iio_channel *chn, unsigned int idx)
{
	enum iio_attr_type type = idx ? IIO_ATTR_TYPE_BUFFER : IIO_ATTR_TYPE_DEVICE;
	char buf[8];
	int err;

	err = local_read_dev_attr(chn->dev, idx, chn->pdata->enable_fn,
				  buf, sizeof(buf), type);
	if (err < 0)
		return err;

	return buf[0] == '1';
}

static const struct iio_device * local_get_trigger(const struct iio_device *dev)
{
	const struct iio_device *cur;
	char buf[1024];
	unsigned int i;
	ssize_t nb;

	nb = local_read_dev_attr(dev, 0, "trigger/current_trigger",
				 buf, sizeof(buf), IIO_ATTR_TYPE_DEVICE);
	if (nb < 0)
		return iio_ptr(nb);

	if (buf[0] == '\0')
		return iio_ptr(-ENODEV);

	nb = iio_context_get_devices_count(dev->ctx);
	for (i = 0; i < (size_t) nb; i++) {
		cur = iio_context_get_device(dev->ctx, i);

		if (cur->name && !strcmp(cur->name, buf))
			return cur;
	}

	return iio_ptr(-ENXIO);
}

static int local_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	ssize_t nb;
	const char *value = trigger ? trigger->name : "";

	nb = local_write_dev_attr(dev, 0, "trigger/current_trigger", value,
				  strlen(value) + 1, IIO_ATTR_TYPE_DEVICE);
	if (nb < 0)
		return (int) nb;
	else
		return 0;
}

static bool is_channel(const struct iio_device *dev, const char *attr, bool strict)
{
	char *ptr = NULL;

	if (WITH_HWMON && iio_device_is_hwmon(dev))
		return iio_channel_is_hwmon(attr);
	if (!strncmp(attr, "in_timestamp_", sizeof("in_timestamp_") - 1))
		return true;
	if (!strncmp(attr, "in_", 3))
		ptr = strchr(attr + 3, '_');
	else if (!strncmp(attr, "out_", 4))
		ptr = strchr(attr + 4, '_');
	if (!ptr)
		return false;
	if (!strict)
		return true;
	if (*(ptr - 1) >= '0' && *(ptr - 1) <= '9')
		return true;

	if (find_channel_modifier(ptr + 1, NULL) != IIO_NO_MOD)
		return true;
	return false;
}

static char * get_channel_id(struct iio_device *dev, const char *attr)
{
	char *res, *ptr = strchr(attr, '_');
	size_t len;

	if (!WITH_HWMON || !iio_device_is_hwmon(dev)) {
		attr = ptr + 1;
		ptr = strchr(attr, '_');
		if (find_channel_modifier(ptr + 1, &len) != IIO_NO_MOD)
			ptr += len + 1;
	} else if (!ptr) {
		/*
		 * Attribute is 'pwmX' without underscore: the attribute name
		 * is our channel ID.
		 */
		return iio_strdup(attr);
	}

	res = malloc(ptr - attr + 1);
	if (!res)
		return NULL;

	memcpy(res, attr, ptr - attr);
	res[ptr - attr] = 0;
	return res;
}

static const char *
get_short_attr_name(struct iio_channel *chn, const char *attr)
{
	char *ptr = strchr(attr, '_');
	size_t len;

	if (WITH_HWMON && iio_device_is_hwmon(chn->dev)) {
		/*
		 * PWM hwmon devices can have an attribute named directly after
		 * the channel's ID; in that particular case we don't need to
		 * strip the prefix.
		 */
		return ptr ? ptr + 1 : attr;
	}

	ptr = strchr(ptr + 1, '_') + 1;
	if (find_channel_modifier(ptr, &len) != IIO_NO_MOD)
		ptr += len + 1;

	if (chn->name) {
		len = strlen(chn->name);
		if (strncmp(chn->name, ptr, len) == 0 && ptr[len] == '_')
			ptr += len + 1;
	}

	return ptr;
}

static int read_device_name(struct iio_device *dev)
{
	char buf[1024];
	ssize_t ret;

	ret = local_read_dev_attr(dev, 0, "name", buf, sizeof(buf),
				  IIO_ATTR_TYPE_DEVICE);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -EIO;

	dev->name = iio_strdup(buf);
	if (!dev->name)
		return -ENOMEM;
	else
		return 0;
}

static int read_device_label(struct iio_device *dev)
{
	char buf[1024];
	ssize_t ret;

	ret = local_read_dev_attr(dev, 0, "label", buf, sizeof(buf),
				  IIO_ATTR_TYPE_DEVICE);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -EIO;

	dev->label = iio_strdup(buf);
	if (!dev->label)
		return -ENOMEM;
	else
		return 0;
}

static int add_attr_to_device(struct iio_device *dev, const char *attr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(device_attrs_denylist); i++)
		if (!strcmp(device_attrs_denylist[i], attr))
			return 0;

	if (!strcmp(attr, "name"))
		return read_device_name(dev);
	if (!strcmp(attr, "label"))
		return read_device_label(dev);

	return iio_device_add_attr(dev, attr, IIO_ATTR_TYPE_DEVICE);
}

static int handle_protected_scan_element_attr(struct iio_channel *chn,
			const char *name, const char *path)
{
	struct iio_device *dev = chn->dev;
	char buf[1024];
	int ret;

	if (!strcmp(name, "index")) {
		ret = local_read_dev_attr(dev, 0, path, buf, sizeof(buf),
					  IIO_ATTR_TYPE_DEVICE);
		if (ret > 0) {
			char *end;
			long long value;

			errno = 0;
			value = strtoll(buf, &end, 0);
			if (end == buf || value < 0 || errno == ERANGE)
				return -EINVAL;

			chn->index = (long) value;
		}
	} else if (!strcmp(name, "type")) {
		ret = local_read_dev_attr(dev, 0, path, buf, sizeof(buf),
					  IIO_ATTR_TYPE_DEVICE);
		if (ret > 0) {
			char endian, sign;

			if (strchr(buf, 'X')) {
				iio_sscanf(buf, "%ce:%c%u/%uX%u>>%u",
#ifdef _MSC_BUILD
					&endian, sizeof(endian),
					&sign, sizeof(sign),
#else
					&endian, &sign,
#endif
					&chn->format.bits, &chn->format.length,
					&chn->format.repeat, &chn->format.shift);
			} else {
				chn->format.repeat = 1;
				iio_sscanf(buf, "%ce:%c%u/%u>>%u",
#ifdef _MSC_BUILD
					&endian, sizeof(endian),
					&sign, sizeof(sign),
#else
					&endian, &sign,
#endif
					&chn->format.bits, &chn->format.length,
					&chn->format.shift);
			}
			chn->format.is_signed = (sign == 's' || sign == 'S');
			chn->format.is_fully_defined =
					(sign == 'S' || sign == 'U'||
					chn->format.bits == chn->format.length);
			chn->format.is_be = endian == 'b';
		}

	} else if (!strcmp(name, "en")) {
		if (chn->pdata->enable_fn) {
			chn_err(chn, "Libiio bug: \"en\" attribute already "
				"parsed for channel %s!\n", chn->id);
			return -EINVAL;
		}

		chn->pdata->enable_fn = iio_strdup(path);
		if (!chn->pdata->enable_fn)
			return -ENOMEM;

	} else {
		return -EINVAL;
	}

	return 0;
}

static int handle_scan_elements(struct iio_channel *chn)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	unsigned int i;

	for (i = 0; i < pdata->protected.num; i++) {
		int ret = handle_protected_scan_element_attr(chn,
				pdata->protected.attrs[i].name,
				pdata->protected.attrs[i].filename);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void free_protected_attrs(struct iio_channel *chn)
{
	struct iio_channel_pdata *pdata = chn->pdata;

	iio_free_attrs(&pdata->protected);
}

static int add_attr_to_channel(struct iio_channel *chn,
			       const char *name, const char *path,
			       bool is_scan_element)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	union iio_pointer p = { .chn = chn, };
	struct iio_attr_list *attrs;
	int ret;

	name = get_short_attr_name(chn, name);

	attrs = is_scan_element ? &pdata->protected : &chn->attrlist;
	ret = iio_add_attr(p, attrs, name, path, IIO_ATTR_TYPE_CHANNEL);
	if (ret)
		return ret;

	chn_dbg(chn, "Added%s attr \'%s\' to channel \'%s\'\n",
		is_scan_element ? " protected" : "", name, chn->id);
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

	dev_dbg(dev, "Added %s channel \'%s\' to device \'%s\'\n",
		chn->is_output ? "output" : "input", chn->id, dev->id);

	return 0;
}

static struct iio_channel *create_channel(struct iio_device *dev,
		char *id, const char *attr, const char *path,
		bool is_scan_element)
{
	struct iio_channel *chn;
	int err = -ENOMEM;

	chn = zalloc(sizeof(*chn));
	if (!chn)
		return iio_ptr(-ENOMEM);

	chn->pdata = zalloc(sizeof(*chn->pdata));
	if (!chn->pdata)
		goto err_free_chn;

	if (!WITH_HWMON || !iio_device_is_hwmon(dev)) {
		if (!strncmp(attr, "out_", 4)) {
			chn->is_output = true;
		} else if (strncmp(attr, "in_", 3)) {
			err = -EINVAL;
			goto err_free_chn_pdata;
		}
	}

	chn->dev = dev;
	chn->id = id;
	chn->is_scan_element = is_scan_element;
	chn->index = -ENOENT;

	err = add_attr_to_channel(chn, attr, path, is_scan_element);
	if (err)
		goto err_free_chn_pdata;

	return chn;

err_free_chn_pdata:
	free(chn->pdata->enable_fn);
	free(chn->pdata);
err_free_chn:
	free(chn);
	return iio_ptr(err);
}

static int add_channel(struct iio_device *dev, const char *name,
	const char *path, bool dir_is_scan_elements)
{
	struct iio_channel *chn;
	char *channel_id;
	unsigned int i;
	int ret;

	channel_id = get_channel_id(dev, name);
	if (!channel_id)
		return -ENOMEM;

	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (!strcmp(chn->id, channel_id)
				&& chn->is_output == (name[0] == 'o')) {
			free(channel_id);
			ret = add_attr_to_channel(chn, name, path,
					dir_is_scan_elements);
			chn->is_scan_element |= dir_is_scan_elements && !ret;
			return ret;
		}
	}

	chn = create_channel(dev, channel_id, name, path, dir_is_scan_elements);
	ret = iio_err(chn);
	if (ret) {
		free(channel_id);
		return ret;
	}

	iio_channel_init_finalize(chn);

	ret = add_channel_to_device(dev, chn);
	if (ret) {
		local_free_channel_pdata(chn);
		free_channel(chn);
	}
	return ret;
}

/*
 * Possible return values:
 * 0 = Attribute should not be moved to the channel
 * 1 = Attribute should be moved to the channel and it is a shared attribute
 * 2 = Attribute should be moved to the channel and it is a private attribute
 */
static unsigned int is_global_attr(struct iio_channel *chn, const char *attr)
{
	unsigned int len;
	char *ptr, *dashptr;

	if (!chn->is_output && !strncmp(attr, "in_", 3))
		attr += 3;
	else if (chn->is_output && !strncmp(attr, "out_", 4))
		attr += 4;
	else
		return 0;

	ptr = strchr(attr, '_');
	if (!ptr)
		return 0;

	len = ptr - attr;

	// Check for matching global differential attr, like "voltage-voltage"
	dashptr = strchr(attr, '-');
	if (dashptr && dashptr > attr && dashptr < ptr) {
		unsigned int len1 = dashptr - attr;
		unsigned int len2 = ptr - dashptr - 1;
		const char*  iddashptr = strchr(chn->id, '-');
		if (iddashptr && strlen(iddashptr + 1) > len2 &&
			(unsigned int)(iddashptr - chn->id) > len1 &&
			chn->id[len1] >= '0' && chn->id[len1] <= '9' &&
			!strncmp(chn->id, attr, len1) &&
			iddashptr[len2 + 1] >= '0' && iddashptr[len2 + 1] <= '9' &&
			!strncmp(iddashptr + 1, dashptr + 1, len2))
			return 1;
	}

	if (strncmp(chn->id, attr, len))
		return 0;

	chn_dbg(chn, "Found match: %s and %s\n", chn->id, attr);
	if (chn->id[len] >= '0' && chn->id[len] <= '9') {
		if (chn->name) {
			size_t name_len = strlen(chn->name);
			if (strncmp(chn->name, attr + len + 1, name_len) == 0 &&
				attr[len + 1 + name_len] == '_')
				return 2;
		}
		return 1;
	} else if (chn->id[len] != '_') {
		return 0;
	}

	if (find_channel_modifier(chn->id + len + 1, NULL) != IIO_NO_MOD)
		return 1;

	return 0;
}

static int detect_global_attr(struct iio_device *dev, const char *attr,
	unsigned int level, bool *match)
{
	unsigned int i;

	*match = false;
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (is_global_attr(chn, attr) == level) {
			int ret;
			*match = true;
			ret = add_attr_to_channel(chn, attr, attr, false);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int detect_and_move_global_attrs(struct iio_device *dev)
{
	struct iio_attr_list *list = &dev->attrlist[IIO_ATTR_TYPE_DEVICE];
	struct iio_attr *attrs = list->attrs;
	unsigned int i, j;

	for (i = 0; i < list->num; i++) {
		bool match;
		int ret;

		ret = detect_global_attr(dev, attrs[i].name, 2, &match);
		if (ret)
			return ret;

		if (!match) {
			ret = detect_global_attr(dev, attrs[i].name, 1, &match);
			if (ret)
				return ret;
		}

		if (match)
			iio_free_attr_data(&attrs[i]);
	}

	/* Find channels without an index */
	for (i = 0; i < list->num; i++) {
		int ret;

		if (!attrs[i].name)
			continue;

		if (is_channel(dev, attrs[i].name, false)) {
			ret = add_channel(dev, attrs[i].name, attrs[i].name, false);
			if (ret)
				return ret;

			iio_free_attr_data(&attrs[i]);
		}
	}

	for (i = 0, j = 0; i < list->num; i++) {
		if (attrs[i].name)
			attrs[j++] = attrs[i];
	}

	list->num = j;
	if (!list->num) {
		free(list->attrs);
		list->attrs = NULL;
	}

	return 0;
}

static int add_buffer_attr(void *d, const char *path)
{
	struct iio_device *dev = (struct iio_device *) d;
	const char *name = strrchr(path, '/') + 1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(buffer_attrs_reserved); i++)
		if (!strcmp(buffer_attrs_reserved[i], name))
			return 0;

	return iio_device_add_attr(dev, name, IIO_ATTR_TYPE_BUFFER);
}

static int add_attr_or_channel_helper(struct iio_device *dev,
		const char *path, bool dir_is_scan_elements)
{
	char buf[1024];
	const char *name = strrchr(path, '/') + 1;

	if (dir_is_scan_elements) {
		iio_snprintf(buf, sizeof(buf), "scan_elements/%s", name);
		path = buf;
	} else {
		if (!is_channel(dev, name, true))
			return add_attr_to_device(dev, name);
		path = name;
	}

	return add_channel(dev, name, path, dir_is_scan_elements);
}

static int add_attr_or_channel(void *d, const char *path)
{
	return add_attr_or_channel_helper((struct iio_device *) d, path, false);
}

static int add_scan_element(void *d, const char *path)
{
	return add_attr_or_channel_helper((struct iio_device *) d, path, true);
}

static int foreach_in_dir(const struct iio_context *ctx,
			  void *d, const char *path, bool is_dir,
			  int (*callback)(void *, const char *))
{
	struct dirent *entry;
	DIR *dir;
	int ret = 0;

	dir = opendir(path);
	if (!dir)
		return -errno;

	while (true) {
		struct stat st;
		char buf[PATH_MAX];

		errno = 0;
		entry = readdir(dir);
		if (!entry) {
			if (!errno)
				break;

			ret = -errno;
			ctx_perror(ctx, ret, "Unable to open directory");
			goto out_close_dir;
		}

		iio_snprintf(buf, sizeof(buf), "%s/%s", path, entry->d_name);
		if (stat(buf, &st) < 0) {
			ret = -errno;
			ctx_perror(ctx, ret, "Unable to stat file");
			goto out_close_dir;
		}

		if (is_dir && S_ISDIR(st.st_mode) && entry->d_name[0] != '.')
			ret = callback(d, buf);
		else if (!is_dir && S_ISREG(st.st_mode))
			ret = callback(d, buf);
		else
			continue;

		if (ret < 0)
			goto out_close_dir;
	}

out_close_dir:
	closedir(dir);
	return ret;
}

static int add_scan_elements(struct iio_device *dev, const char *devpath)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct stat st;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%s/scan_elements", devpath);

	if (!stat(buf, &st) && S_ISDIR(st.st_mode)) {
		int ret = foreach_in_dir(ctx, dev, buf, false, add_scan_element);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int add_buffer_attributes(struct iio_device *dev, const char *devpath)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct stat st;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%s/buffer", devpath);

	if (!stat(buf, &st) && S_ISDIR(st.st_mode)) {
		int ret = foreach_in_dir(ctx, dev, buf, false, add_buffer_attr);
		if (ret < 0)
			return ret;

		iio_sort_attrs(&dev->attrlist[IIO_ATTR_TYPE_BUFFER]);
	}

	return 0;
}

static int create_device(void *d, const char *path)
{
	unsigned int i;
	int ret;
	struct iio_context *ctx = d;
	struct iio_device *dev = zalloc(sizeof(*dev));
	if (!dev)
		return -ENOMEM;

	dev->ctx = ctx;
	dev->id = iio_strdup(strrchr(path, '/') + 1);
	if (!dev->id) {
		free(dev);
		return -ENOMEM;
	}

	ret = foreach_in_dir(ctx, dev, path, false, add_attr_or_channel);
	if (ret < 0)
		goto err_free_device;

	ret = add_buffer_attributes(dev, path);
	if (ret < 0)
		goto err_free_device;

	ret = add_scan_elements(dev, path);
	if (ret < 0)
		goto err_free_scan_elements;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];

		ret = set_channel_name(chn);
		if (ret < 0)
			goto err_free_scan_elements;

		ret = handle_scan_elements(chn);
		free_protected_attrs(chn);
		if (ret < 0)
			goto err_free_scan_elements;
	}

	ret = detect_and_move_global_attrs(dev);
	if (ret < 0)
		goto err_free_device;

	/* sorting is done after global attrs are added */
	for (i = 0; i < dev->nb_channels; i++)
		iio_sort_attrs(&dev->channels[i]->attrlist);

	iio_sort_attrs(&dev->attrlist[IIO_ATTR_TYPE_DEVICE]);

	ret = iio_context_add_device(ctx, dev);
	if (!ret)
		return 0;

err_free_scan_elements:
	for (i = 0; i < dev->nb_channels; i++)
		free_protected_attrs(dev->channels[i]);
err_free_device:
	local_free_pdata(dev);
	free_device(dev);
	return ret;
}

static int add_debug_attr(void *d, const char *path)
{
	struct iio_device *dev = d;
	const char *attr = strrchr(path, '/') + 1;

	return iio_device_add_attr(dev, attr, IIO_ATTR_TYPE_DEBUG);
}

static int add_debug(void *d, const char *path)
{
	struct iio_context *ctx = d;
	const char *name = strrchr(path, '/') + 1;
	struct iio_device *dev = iio_context_find_device(ctx, name);

	if (!dev)
		return -ENODEV;

	return foreach_in_dir(ctx, dev, path, false, add_debug_attr);
}

static void local_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	const struct iio_device *dev = pdata->dev;
	uint64_t event = 1;
	int ret;

	ret = write(pdata->cancel_fd, &event, sizeof(event));
	if (ret == -1) {
		/* If this happens something went very seriously wrong */
		dev_perror(dev, -errno, "Unable to signal cancellation event");
	}
}

static char * local_get_description(const struct iio_context *ctx)
{
	char *description;
	unsigned int len;
	struct utsname uts;

	uname(&uts);
	len = strlen(uts.sysname) + strlen(uts.nodename) + strlen(uts.release)
		+ strlen(uts.version) + strlen(uts.machine);
	description = malloc(len + 5); /* 4 spaces + EOF */
	if (!description)
		return NULL;

	iio_snprintf(description, len + 5, "%s %s %s %s %s", uts.sysname,
			uts.nodename, uts.release, uts.version, uts.machine);

	return description;
}

static ssize_t
local_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	enum iio_attr_type type = attr->type;
	const char *filename = attr->filename;
	unsigned int buf_id = 0;

	if (type == IIO_ATTR_TYPE_BUFFER)
		buf_id = attr->iio.buf->idx;

	return local_read_dev_attr(dev, buf_id, filename, dst, len, type);
}

static ssize_t
local_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	enum iio_attr_type type = attr->type;
	const char *filename = attr->filename;
	unsigned int buf_id = 0;

	if (type == IIO_ATTR_TYPE_BUFFER)
		buf_id = attr->iio.buf->idx;

	return local_write_dev_attr(dev, buf_id, filename, src, len, type);
}

static struct iio_buffer_pdata *
local_create_buffer(const struct iio_device *dev, unsigned int idx,
		    struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata;
	const struct iio_channel *chn;
	int err, cancel_fd, fd = idx;
	unsigned int i;

	if (dev->pdata->fd < 0)
		return iio_ptr(dev->pdata->fd);

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;

	if (WITH_LOCAL_MMAP_API) {
		pdata->pdata = local_alloc_mmap_buffer_impl();
		err = iio_err(pdata->pdata);
		if (err)
			goto err_free_pdata;
	}

	cancel_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (cancel_fd == -1) {
		err = -errno;
		goto err_free_mmap_pdata;
	}

	err = ioctl_nointr(dev->pdata->fd, IIO_BUFFER_GET_FD_IOCTL, &fd);
	if (err == 0) {
		pdata->multi_buffer = true;
	} else if (idx == 0) {
		fd = dup(dev->pdata->fd);
		if (fd == -1) {
			err = -errno;
			goto err_close_eventfd;
		}
	} else {
		goto err_close_eventfd;
	}

	pdata->cancel_fd = cancel_fd;
	pdata->fd = fd;
	pdata->idx = idx;

	/* Disable buffer */
	err = local_do_enable_buffer(pdata, false);
	if (err < 0)
		goto err_close;

	/* Disable all channels */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0) {
			err = channel_write_state(chn, idx, false);
			if (err < 0)
				goto err_close;
		}
	}

	/* Enable channels */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0 && iio_channel_is_enabled(chn, mask)) {
			err = channel_write_state(chn, idx, true);
			if (err < 0)
				goto err_close;
		}
	}

	/* Finally, update the channels mask by reading the hardware again,
	 * since some channels may be coupled together. */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0) {
			err = channel_read_state(chn, idx);
			if (err < 0)
				goto err_close;

			if (err > 0)
				iio_channel_enable(chn, mask);
		}
	}

	return pdata;

err_close:
	close(fd);
err_close_eventfd:
	close(cancel_fd);
err_free_mmap_pdata:
	free(pdata->pdata);
err_free_pdata:
	free(pdata);
	return iio_ptr(err);
}

static void local_free_buffer(struct iio_buffer_pdata *pdata)
{
	free(pdata->pdata);
	close(pdata->fd);
	close(pdata->cancel_fd);
	local_do_enable_buffer(pdata, false);

	free(pdata);
}

static struct iio_block_pdata *
local_create_block(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_block_pdata *block;
	int ret;

	if (WITH_LOCAL_DMABUF_API) {
		block = local_create_dmabuf(pdata, size, data);
		ret = iio_err(block);

		if (ret != -ENOSYS)
			return block;
	}

	if (WITH_LOCAL_MMAP_API) {
		block = local_create_mmap_block(pdata, size, data);
		ret = iio_err(block);

		if (ret != -ENOSYS)
			return block;
	}

	return iio_ptr(-ENOSYS);
}

static void local_free_block(struct iio_block_pdata *pdata)
{
	if (WITH_LOCAL_DMABUF_API && pdata->buf->dmabuf_supported)
		local_free_dmabuf(pdata);

	if (WITH_LOCAL_MMAP_API && pdata->buf->mmap_supported)
		local_free_mmap_block(pdata);
}

static int local_enqueue_block(struct iio_block_pdata *pdata,
			       size_t bytes_used, bool cyclic)
{
	if (WITH_LOCAL_DMABUF_API && pdata->buf->dmabuf_supported)
		return local_enqueue_dmabuf(pdata, bytes_used, cyclic);

	if (WITH_LOCAL_MMAP_API && pdata->buf->mmap_supported)
		return local_enqueue_mmap_block(pdata, bytes_used, cyclic);

	return -ENOSYS;
}

int local_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	if (WITH_LOCAL_DMABUF_API && pdata->buf->dmabuf_supported)
		return local_dequeue_dmabuf(pdata, nonblock);

	if (WITH_LOCAL_MMAP_API && pdata->buf->mmap_supported)
		return local_dequeue_mmap_block(pdata, nonblock);

	return -ENOSYS;
}

static const struct iio_backend_ops local_ops = {
	.scan = local_context_scan,
	.create = local_create_context,
	.read_attr = local_read_attr,
	.write_attr = local_write_attr,
	.get_trigger = local_get_trigger,
	.set_trigger = local_set_trigger,
	.shutdown = local_shutdown,

	.create_block = local_create_block,
	.free_block = local_free_block,
	.enqueue_block = local_enqueue_block,
	.dequeue_block = local_dequeue_block,

	.create_buffer = local_create_buffer,
	.free_buffer = local_free_buffer,
	.enable_buffer = local_enable_buffer,
	.cancel_buffer = local_cancel_buffer,

	.readbuf = local_readbuf,
	.writebuf = local_writebuf,
};

const struct iio_backend iio_local_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "local",
	.uri_prefix = "local:",
	.ops = &local_ops,
	.default_timeout_ms = 1000,
};

static void init_data_scale(struct iio_channel *chn)
{
	char *end, buf[1024];
	const char *attr;
	ssize_t ret;
	float value;

	chn->format.with_scale = false;
	attr = get_filename(chn, "scale");

	ret = local_read_dev_attr(chn->dev, 0, attr,
				  buf, sizeof(buf), IIO_ATTR_TYPE_DEVICE);
	if (ret < 0)
		return;

	errno = 0;
	value = strtof(buf, &end);
	if (end == buf || errno == ERANGE)
		return;

	chn->format.with_scale = true;
	chn->format.scale = value;
}

static void init_scan_elements(struct iio_context *ctx)
{
	unsigned int i, j;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);

		for (j = 0; j < dev->nb_channels; j++)
			init_data_scale(dev->channels[j]);
	}
}

static int local_open_buffer(const struct iio_device *dev)
{
	char buf[1024];
	int fd;

	iio_snprintf(buf, sizeof(buf), "/dev/%s", dev->id);
	fd = open(buf, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd == -1)
		return -errno;

	return fd;
}

static int init_devices(struct iio_context *ctx)
{
	struct iio_device *dev;
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++) {
		dev = ctx->devices[i];

		dev->pdata = malloc(sizeof(*dev->pdata));
		if (!dev->pdata)
			return -ENOMEM;

		dev->pdata->fd = local_open_buffer(dev);
	}

	return 0;
}

static int populate_context_attrs(struct iio_context *ctx, const char *file)
{
	struct INI *ini;
	int ret;

	ini = ini_open(file);
	if (!ini) {
		/* INI file not present -> not an error */
		if (errno == ENOENT)
			return 0;
		else
			return -errno;
	}

	while (true) {
		const char *section;
		size_t len;

		ret = ini_next_section(ini, &section, &len);
		if (ret <= 0)
			goto out_close_ini;

		if (!strncmp(section, "Context Attributes", len))
			break;
	}

	do {
		const char *key, *value;
		char *new_key, *new_val;
		size_t klen, vlen;

		ret = ini_read_pair(ini, &key, &klen, &value, &vlen);
		if (ret <= 0)
			break;

		/* Create a dup of the strings read from the INI, since they are
		 * not NULL-terminated. */
		new_key = strndup(key, klen);
		new_val = strndup(value, vlen);

		if (!new_key || !new_val)
			ret = -ENOMEM;
		else
			ret = iio_context_add_attr(ctx, new_key, new_val);

		free(new_key);
		free(new_val);
	} while (!ret);

out_close_ini:
	ini_close(ini);
	return ret;
}

static struct iio_context *
local_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	char *description;
	int ret = -ENOMEM;
	struct utsname uts;
	bool no_iio;

	description = local_get_description(NULL);
	if (!description)
		return iio_ptr(-ENOMEM);

	ctx = iio_context_create_from_backend(&iio_local_backend, description);
	free(description);
	ret = iio_err(ctx);
	if (ret)
		return iio_err_cast(ctx);

	ctx->params = *params;

	ret = foreach_in_dir(ctx, ctx, "/sys/bus/iio/devices",
			     true, create_device);
	no_iio = ret == -ENOENT;
	if (WITH_HWMON && no_iio)
	      ret = 0; /* Not an error, unless we also have no hwmon devices */
	if (ret < 0)
	      goto err_context_destroy;

	if (WITH_HWMON) {
		ret = foreach_in_dir(ctx, ctx, "/sys/class/hwmon",
				     true, create_device);
		if (ret == -ENOENT && !no_iio)
			ret = 0; /* IIO devices but no hwmon devices - not an error */
		if (ret < 0)
			goto err_context_destroy;
	}

	iio_sort_devices(ctx);

	foreach_in_dir(ctx, ctx, "/sys/kernel/debug/iio", true, add_debug);

	init_scan_elements(ctx);

	if (WITH_LOCAL_CONFIG) {
		ret = populate_context_attrs(ctx, "/etc/libiio.ini");
		if (ret < 0)
			prm_warn(params, "Unable to read INI file: %d\n", ret);
	}

	uname(&uts);
	ret = iio_context_add_attr(ctx, "local,kernel", uts.release);
	if (ret < 0)
		goto err_context_destroy;

	ret = iio_context_add_attr(ctx, "uri", "local:");
	if (ret < 0)
		goto err_context_destroy;

	ret = iio_context_init(ctx);
	if (ret < 0)
		goto err_context_destroy;

	ret = init_devices(ctx);
	if (ret < 0)
		goto err_context_destroy;

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	return iio_ptr(ret);
}

#define BUF_SIZE 128

static char * cat_file(const char *path)
{
	char buf[BUF_SIZE];
	ssize_t ret;

	FILE *f;

	f = fopen(path, "re");
	if (!f)
		return NULL;

	ret = fread(buf, 1, sizeof(buf)-1, f);
	fclose(f);
	if (ret > 0)
		buf[ret - 1] = '\0';
	else
		return NULL;

	return strndup(buf, sizeof(buf) - 1);
}

static int build_names(void *d, const char *path)
{
	char buf[BUF_SIZE], *dst;
	char *names = (char *)d;
	size_t len;

	if (!strstr(path, "iio:device") && !(WITH_HWMON && strstr(path, "class/hwmon")))
		return 0;

	iio_snprintf(buf, sizeof(buf), "%s/name", path);
	dst = cat_file(buf);
	if (dst) {
		len = strnlen(names, sizeof(buf));
		iio_snprintf(&names[len], BUF_SIZE - len - 1, "%s,", dst);
		free(dst);
	}
	return 0;
}

static int check_device(void *d, const char *path)
{
	*(bool *)d = true;
	return 0;
}

static int local_context_scan(const struct iio_context_params *params,
			      struct iio_scan *ctx, const char *args)
{
	char *machine, buf[2 * BUF_SIZE], names[BUF_SIZE];
	bool no_iio, exists = false;
	const char *desc;
	int ret;

	ret = foreach_in_dir(NULL, &exists, "/sys/bus/iio", true, check_device);
	no_iio = !exists;
	if (WITH_HWMON && no_iio)
		ret = 0; /* Not an error, unless we also have no hwmon devices */
	if (ret < 0)
		return 0;

	names[0] = '\0';
	if (exists) {
		ret = foreach_in_dir(NULL, &names, "/sys/bus/iio/devices",
				     true, build_names);
		if (ret < 0)
			return 0;
	}

	if (WITH_HWMON) {
		exists = false;
		ret = foreach_in_dir(NULL, &exists, "/sys/class/hwmon",
				     true, check_device);
		if (!exists && !no_iio)
			ret = 0; /* IIO devices but no hwmon devices - not an error */
		if (ret < 0)
			return 0;

		ret = foreach_in_dir(NULL, &names, "/sys/class/hwmon",
				     true, build_names);
		if (ret < 0)
			return 0;
	}

	machine = cat_file("/sys/firmware/devicetree/base/model");
	if (!machine)
		machine = cat_file("/sys/class/dmi/id/board_vendor");

	if (machine) {
		if (names[0]) {
			ret = strnlen(names, sizeof(names));
			names[ret - 1] = '\0';
			iio_snprintf(buf, sizeof(buf), "(%s on %s)", names, machine);
		} else
			iio_snprintf(buf, sizeof(buf), "(Local IIO devices on %s)", machine);
		free(machine);
		desc = buf;
	} else {
		desc = "(Local IIO devices)";
	}

	return iio_scan_add_result(ctx, desc, "local:");
}
