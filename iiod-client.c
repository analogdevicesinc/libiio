/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#include "debug.h"
#include "iiod-client.h"
#include "iio-lock.h"
#include "iio-private.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

struct iiod_client {
	struct iio_context_pdata *pdata;
	const struct iiod_client_ops *ops;
	struct iio_mutex *lock;
};

static ssize_t iiod_client_read_integer(struct iiod_client *client,
		void *desc, int *val)
{
	unsigned int i;
	char buf[1024], *ptr = NULL, *end;
	ssize_t ret;
	int value;

	do {
		ret = client->ops->read_line(client->pdata,
				desc, buf, sizeof(buf));
		if (ret < 0)
			return ret;

		for (i = 0; i < (unsigned int) ret; i++) {
			if (buf[i] != '\n') {
				if (!ptr)
					ptr = &buf[i];
			} else if (!!ptr) {
				break;
			}
		}
	} while (!ptr);

	buf[i] = '\0';

	value = (int) strtol(ptr, &end, 10);
	if (ptr == end)
		return -EINVAL;

	*val = value;
	return 0;
}

static int iiod_client_exec_command(struct iiod_client *client,
		void *desc, const char *cmd)
{
	int resp;
	ssize_t ret;

	ret = client->ops->write(client->pdata, desc, cmd, strlen(cmd));
	if (ret < 0)
		return (int) ret;

	ret = iiod_client_read_integer(client, desc, &resp);
	return ret < 0 ? (int) ret : resp;
}

static ssize_t iiod_client_write_all(struct iiod_client *client,
		void *desc, const void *src, size_t len)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iiod_client_ops *ops = client->ops;
	uintptr_t ptr = (uintptr_t) src;

	while (len) {
		ssize_t ret = ops->write(pdata, desc, (const void *) ptr, len);

		if (ret < 0) {
			if (ret == -EINTR)
				continue;
			else
				return ret;
		}

		if (ret == 0)
			return -EPIPE;

		ptr += ret;
		len -= ret;
	}

	return (ssize_t) (ptr - (uintptr_t) src);
}

static ssize_t iiod_client_read_all(struct iiod_client *client,
		void *desc, void *dst, size_t len)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iiod_client_ops *ops = client->ops;
	uintptr_t ptr = (uintptr_t) dst;

	while (len) {
		ssize_t ret = ops->read(pdata, desc, (void *) ptr, len);

		if (ret < 0) {
			if (ret == -EINTR)
				continue;
			else
				return ret;
		}

		if (ret == 0)
			return -EPIPE;

		ptr += ret;
		len -= ret;
	}

	return (ssize_t) (ptr - (uintptr_t) dst);
}

struct iiod_client * iiod_client_new(struct iio_context_pdata *pdata,
		struct iio_mutex *lock, const struct iiod_client_ops *ops)
{
	struct iiod_client *client;

	client = malloc(sizeof(*client));
	if (!client) {
		errno = ENOMEM;
		return NULL;
	}

	client->lock = lock;
	client->pdata = pdata;
	client->ops = ops;
	return client;
}

void iiod_client_destroy(struct iiod_client *client)
{
	free(client);
}

int iiod_client_get_version(struct iiod_client *client, void *desc,
		unsigned int *major, unsigned int *minor, char *git_tag)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iiod_client_ops *ops = client->ops;
	char buf[256], *ptr = buf, *end;
	long maj, min;
	int ret;

	iio_mutex_lock(client->lock);

	ret = ops->write(pdata, desc, "VERSION\r\n", sizeof("VERSION\r\n") - 1);
	if (ret < 0) {
		iio_mutex_unlock(client->lock);
		return ret;
	}

	ret = ops->read_line(pdata, desc, buf, sizeof(buf));
	iio_mutex_unlock(client->lock);

	if (ret < 0)
		return ret;

	maj = strtol(ptr, &end, 10);
	if (ptr == end)
		return -EIO;

	ptr = end + 1;
	min = strtol(ptr, &end, 10);
	if (ptr == end)
		return -EIO;

	ptr = end + 1;
	if (buf + ret < ptr + 8)
		return -EIO;

	/* Strip the \n */
	ptr[buf + ret - ptr - 1] = '\0';

	if (major)
		*major = (unsigned int) maj;
	if (minor)
		*minor = (unsigned int) min;
	if (git_tag)
		strncpy(git_tag, ptr, 8);
	return 0;
}

int iiod_client_get_trigger(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_device **trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	char buf[1024];
	unsigned int name_len;
	int ret;

	iio_snprintf(buf, sizeof(buf), "GETTRIG %s\r\n",
			iio_device_get_id(dev));

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);

	if (ret == 0)
		*trigger = NULL;
	if (ret <= 0)
		goto out_unlock;

	if ((unsigned int) ret > sizeof(buf) - 1) {
		ret = -EIO;
		goto out_unlock;
	}

	name_len = ret;

	ret = (int) iiod_client_read_all(client, desc, buf, name_len + 1);
	if (ret < 0)
		goto out_unlock;

	ret = -ENXIO;

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *cur = iio_context_get_device(ctx, i);

		if (iio_device_is_trigger(cur)) {
			const char *name = iio_device_get_name(cur);

			if (!name)
				continue;

			if (!strncmp(name, buf, name_len)) {
				*trigger = cur;
				ret = 0;
				goto out_unlock;
			}
		}
	}

out_unlock:
	iio_mutex_unlock(client->lock);
	return ret;
}

int iiod_client_set_trigger(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_device *trigger)
{
	char buf[1024];
	int ret;

	if (trigger) {
		iio_snprintf(buf, sizeof(buf), "SETTRIG %s %s\r\n",
				iio_device_get_id(dev),
				iio_device_get_id(trigger));
	} else {
		iio_snprintf(buf, sizeof(buf), "SETTRIG %s\r\n",
				iio_device_get_id(dev));
	}

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}

int iiod_client_set_kernel_buffers_count(struct iiod_client *client, void *desc,
		const struct iio_device *dev, unsigned int nb_blocks)
{
	int ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "SET %s BUFFERS_COUNT %u\r\n",
			iio_device_get_id(dev), nb_blocks);

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}

int iiod_client_set_timeout(struct iiod_client *client,
		void *desc, unsigned int timeout)
{
	int ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "TIMEOUT %u\r\n", timeout);

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}

static int iiod_client_discard(struct iiod_client *client, void *desc,
		char *buf, size_t buf_len, size_t to_discard)
{
	do {
		size_t read_len;
		ssize_t ret;

		if (to_discard > buf_len)
			read_len = buf_len;
		else
			read_len = to_discard;

		ret = iiod_client_read_all(client, desc, buf, read_len);
		if (ret < 0)
			return ret;

		to_discard -= (size_t) ret;
	} while (to_discard);

	return 0;
}

ssize_t iiod_client_read_attr(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_channel *chn,
		const char *attr, char *dest, size_t len, bool is_debug)
{
	const char *id = iio_device_get_id(dev);
	char buf[1024];
	ssize_t ret;

	if (attr) {
		if (chn) {
			if (!iio_channel_find_attr(chn, attr))
				return -ENOENT;
		} else if (is_debug) {
			if (!iio_device_find_debug_attr(dev, attr))
				return -ENOENT;
		} else {
			if (!iio_device_find_attr(dev, attr))
				return -ENOENT;
		}
	}

	if (chn) {
		iio_snprintf(buf, sizeof(buf), "READ %s %s %s %s\r\n", id,
				iio_channel_is_output(chn) ? "OUTPUT" : "INPUT",
				iio_channel_get_id(chn), attr ? attr : "");
	} else if (is_debug) {
		iio_snprintf(buf, sizeof(buf), "READ %s DEBUG %s\r\n",
				id, attr ? attr : "");
	} else {
		iio_snprintf(buf, sizeof(buf), "READ %s %s\r\n",
				id, attr ? attr : "");
	}

	iio_mutex_lock(client->lock);

	ret = (ssize_t) iiod_client_exec_command(client, desc, buf);
	if (ret < 0)
		goto out_unlock;

	if ((size_t) ret + 1 > len) {
		iiod_client_discard(client, desc, dest, len, ret + 1);
		ret = -EIO;
		goto out_unlock;
	}

	/* +1: Also read the trailing \n */
	ret = iiod_client_read_all(client, desc, dest, ret + 1);

	if (ret > 0) {
		/* Discard the trailing \n */
		ret--;

		/* Replace it with a \0 just in case */
		dest[ret] = '\0';
	}

out_unlock:
	iio_mutex_unlock(client->lock);
	return ret;
}

ssize_t iiod_client_write_attr(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_channel *chn,
		const char *attr, const char *src, size_t len, bool is_debug)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iiod_client_ops *ops = client->ops;
	const char *id = iio_device_get_id(dev);
	char buf[1024];
	ssize_t ret;
	int resp;

	if (attr) {
		if (chn) {
			if (!iio_channel_find_attr(chn, attr))
				return -ENOENT;
		} else if (is_debug) {
			if (!iio_device_find_debug_attr(dev, attr))
				return -ENOENT;
		} else {
			if (!iio_device_find_attr(dev, attr))
				return -ENOENT;
		}
	}

	if (chn) {
		iio_snprintf(buf, sizeof(buf), "WRITE %s %s %s %s %lu\r\n", id,
				iio_channel_is_output(chn) ? "OUTPUT" : "INPUT",
				iio_channel_get_id(chn), attr ? attr : "",
				(unsigned long) len);
	} else if (is_debug) {
		iio_snprintf(buf, sizeof(buf), "WRITE %s DEBUG %s %lu\r\n",
				id, attr ? attr : "", (unsigned long) len);
	} else {
		iio_snprintf(buf, sizeof(buf), "WRITE %s %s %lu\r\n",
				id, attr ? attr : "", (unsigned long) len);
	}

	iio_mutex_lock(client->lock);
	ret = ops->write(pdata, desc, buf, strlen(buf));
	if (ret < 0)
		goto out_unlock;

	ret = iiod_client_write_all(client, desc, src, len);
	if (ret < 0)
		goto out_unlock;

	ret = iiod_client_read_integer(client, desc, &resp);
	if (ret < 0)
		goto out_unlock;

	ret = (ssize_t) resp;

out_unlock:
	iio_mutex_unlock(client->lock);
	return ret;
}

struct iio_context * iiod_client_create_context(
		struct iiod_client *client, void *desc)
{
	struct iio_context *ctx = NULL;
	size_t xml_len;
	char *xml;
	int ret;

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, "PRINT\r\n");
	if (ret < 0)
		goto out_unlock;

	xml_len = (size_t) ret;
	xml = malloc(xml_len + 1);
	if (!xml) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* +1: Also read the trailing \n */
	ret = (int) iiod_client_read_all(client, desc, xml, xml_len + 1);
	if (ret < 0)
		goto out_free_xml;

	ctx = iio_create_xml_context_mem(xml, xml_len);
	if (!ctx)
		ret = -errno;

out_free_xml:
	free(xml);
out_unlock:
	iio_mutex_unlock(client->lock);
	if (!ctx)
		errno = -ret;
	return ctx;
}

int iiod_client_open_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, size_t samples_count, bool cyclic)
{
	char buf[1024], *ptr;
	size_t i;

	iio_snprintf(buf, sizeof(buf), "OPEN %s %lu ",
			iio_device_get_id(dev), (unsigned long) samples_count);
	ptr = buf + strlen(buf);

	for (i = dev->words; i > 0; i--, ptr += 8) {
		iio_snprintf(ptr, (ptr - buf) + i * 8, "%08" PRIx32,
				dev->mask[i - 1]);
	}

	strcpy(ptr, cyclic ? " CYCLIC\r\n" : "\r\n");

	return iiod_client_exec_command(client, desc, buf);
}

int iiod_client_close_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev)
{
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "CLOSE %s\r\n", iio_device_get_id(dev));
	return iiod_client_exec_command(client, desc, buf);
}

static int iiod_client_read_mask(struct iiod_client *client,
		void *desc, uint32_t *mask, size_t words)
{
	size_t i;
	ssize_t ret;
	char *buf, *ptr;

	buf = malloc(words * 8 + 1);
	if (!buf)
		return -ENOMEM;

	ret = iiod_client_read_all(client, desc, buf, words * 8 + 1);
	if (ret < 0)
		goto out_buf_free;
	else
		ret = 0;

	buf[words*8] = '\0';

	DEBUG("Reading mask\n");

	for (i = words, ptr = buf; i > 0; i--) {
		sscanf(ptr, "%08" PRIx32, &mask[i - 1]);
		DEBUG("mask[%lu] = 0x%08" PRIx32 "\n",
				(unsigned long)(i - 1), mask[i - 1]);

		ptr = (char *) ((uintptr_t) ptr + 8);
	}

out_buf_free:
	free(buf);
	return (int) ret;
}

ssize_t iiod_client_read_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	unsigned int nb_channels = iio_device_get_channels_count(dev);
	uintptr_t ptr = (uintptr_t) dst;
	char buf[1024];
	ssize_t ret, read = 0;

	if (!len || words != (nb_channels + 31) / 32)
		return -EINVAL;

	iio_snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
			iio_device_get_id(dev), (unsigned long) len);

	ret = iiod_client_write_all(client, desc, buf, strlen(buf));
	if (ret < 0)
		return ret;

	do {
		int to_read;

		ret = iiod_client_read_integer(client, desc, &to_read);
		if (ret < 0)
			return ret;
		if (to_read < 0)
			return (ssize_t) to_read;
		if (!to_read)
			break;

		if (mask) {
			ret = iiod_client_read_mask(client, desc, mask, words);
			if (ret < 0)
				return ret;

			mask = NULL; /* We read the mask only once */
		}

		ret = iiod_client_read_all(client, desc, (char *) ptr, to_read);
		if (ret < 0)
			return ret;

		ptr += ret;
		read += ret;
		len -= ret;
	} while (len);

	return read;
}

ssize_t iiod_client_write_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const void *src, size_t len)
{
	ssize_t ret;
	char buf[1024];
	int val;

	iio_snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
			dev->id, (unsigned long) len);

	ret = iiod_client_write_all(client, desc, buf, strlen(buf));
	if (ret < 0)
		return ret;

	ret = iiod_client_read_integer(client, desc, &val);
	if (ret < 0)
		return ret;
	if (val < 0)
		return (ssize_t) val;

	ret = iiod_client_write_all(client, desc, src, len);
	if (ret < 0)
		return ret;

	ret = iiod_client_read_integer(client, desc, &val);
	if (ret < 0)
		return ret;
	if (val < 0)
		return (ssize_t) val;

	return (ssize_t) len;
}
