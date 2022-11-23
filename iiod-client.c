// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"
#include "iiod-responder.h"

#include <iio/iiod-client.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#if WITH_ZSTD
#include <zstd.h>
#endif

struct iiod_client {
	const struct iio_context_params *params;
	struct iiod_client_pdata *desc;
	const struct iiod_client_ops *ops;
	struct iio_mutex *lock;

	struct iiod_responder *responder;
};

struct iiod_client_io {
	struct iiod_client *client;
	bool cyclic;
	size_t samples_count;
	size_t buffer_size;

	const struct iio_device *dev;
};

static int iiod_client_enable_binary(struct iiod_client *client);

struct iiod_client_buffer_pdata {
	struct iiod_client *client;
	struct iiod_client_io *io;

	struct iio_channels_mask *mask;
	const struct iio_device *dev;
	uint16_t idx;

	/* TODO: atomic? */
	uint16_t next_block_idx;
};

struct iio_block_pdata {
	struct iiod_client_buffer_pdata *buffer;
	struct iiod_io *io;

	struct iio_mutex *lock;

	size_t size;
	uint64_t bytes_used;
	uint16_t idx;

	void *data;
	bool enqueued;
};

void iiod_client_mutex_lock(struct iiod_client *client)
{
	iio_mutex_lock(client->lock);
}

void iiod_client_mutex_unlock(struct iiod_client *client)
{
	iio_mutex_unlock(client->lock);
}

static ssize_t iiod_client_read_integer(struct iiod_client *client, int *val)
{
	bool accept_eol = false, has_read_line = !!client->ops->read_line;
	unsigned int i, nb, first = 0, timeout_ms = client->params->timeout_ms;
	unsigned int remaining = 0;
	int64_t start_time = 0, diff_ms;
	char buf[1024], *end;
	ssize_t ret;
	int value;

	if (has_read_line) {
		ret = client->ops->read_line(client->desc, buf,
					     sizeof(buf), timeout_ms);
		if (ret < 0)
			return ret;

		nb = (unsigned int) ret;
	} else {
		start_time = iiod_responder_read_counter_us();
		nb = sizeof(buf);
	}

	for (i = 0; i < nb; i++) {
		if (!has_read_line) {
			diff_ms = (iiod_responder_read_counter_us() - start_time) / 1000;

			if (timeout_ms) {
				if (diff_ms >= timeout_ms)
					return -ETIMEDOUT;

				remaining = (unsigned int)((int64_t)timeout_ms - diff_ms);
			}

			ret = client->ops->read(client->desc, &buf[i], 1, remaining);
			if (ret < 0)
				return ret;
		}

		if (buf[i] != '\n')
			accept_eol = true;
		else if (!accept_eol)
			first = i + 1;
		else
			break;
	};

	if (i == nb)
		return -EINVAL;

	buf[i] = '\0';

	errno = 0;
	value = (int) strtol(&buf[first], &end, 10);
	if (buf == end || errno == ERANGE)
		return -EINVAL;

	*val = value;
	return 0;
}

static ssize_t iiod_client_write_all(struct iiod_client *client,
				     const void *src, size_t len)
{
	const struct iiod_client_ops *ops = client->ops;
	struct iiod_client_pdata *desc = client->desc;
	uintptr_t ptr = (uintptr_t) src;
	unsigned int remaining = 0, timeout_ms = client->params->timeout_ms;
	uint64_t start_time, diff_ms;
	ssize_t ret;

	start_time = iiod_responder_read_counter_us();

	if (iiod_client_uses_binary_interface(client))
		timeout_ms = 0;

	while (len) {
		diff_ms = (iiod_responder_read_counter_us() - start_time) / 1000;

		if (timeout_ms) {
			if (diff_ms >= timeout_ms)
				return -ETIMEDOUT;

			remaining = (unsigned int)((int64_t)timeout_ms - diff_ms);
		}

		ret = ops->write(desc, (const void *) ptr, len, remaining);
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

static int iiod_client_exec_command(struct iiod_client *client, const char *cmd)
{
	int resp;
	ssize_t ret;

	ret = iiod_client_write_all(client, cmd, strlen(cmd));
	if (ret < 0)
		return (int) ret;

	ret = iiod_client_read_integer(client, &resp);
	return ret < 0 ? (int) ret : resp;
}

static ssize_t iiod_client_read_all(struct iiod_client *client,
				    void *dst, size_t len)
{
	const struct iiod_client_ops *ops = client->ops;
	uintptr_t ptr = (uintptr_t) dst;
	unsigned int remaining = 0, timeout_ms = client->params->timeout_ms;
	uint64_t start_time, diff_ms;
	ssize_t ret;

	start_time = iiod_responder_read_counter_us();

	if (iiod_client_uses_binary_interface(client))
		timeout_ms = 0;

	while (len) {
		diff_ms = (iiod_responder_read_counter_us() - start_time) / 1000;

		if (timeout_ms) {
			if (diff_ms >= timeout_ms)
				return -ETIMEDOUT;

			remaining = (unsigned int)((int64_t)timeout_ms - diff_ms);
		}

		ret = ops->read(client->desc, (void *) ptr, len, remaining);
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

static void iiod_client_cancel(struct iiod_client *client)
{
	if (client->ops->cancel)
		client->ops->cancel(client->desc);
}

struct iiod_client * iiod_client_new(const struct iio_context_params *params,
				     struct iiod_client_pdata *desc,
				     const struct iiod_client_ops *ops)
{
	struct iiod_client *client;
	int err;

	client = malloc(sizeof(*client));
	if (!client)
		return iio_ptr(-ENOMEM);

	client->lock = iio_mutex_create();
	err = iio_err(client->lock);
	if (err)
		goto err_free_client;

	client->params = params;
	client->ops = ops;
	client->desc = desc;
	client->responder = NULL;

	err = iiod_client_enable_binary(client);
	if (err)
		goto err_free_lock;

	err = iiod_client_set_timeout(client, params->timeout_ms);
	if (err)
		goto err_free_responder;

	return client;

err_free_responder:
	if (client->responder) {
		iiod_client_cancel(client);
		iiod_responder_destroy(client->responder);
	}
err_free_lock:
	iio_mutex_destroy(client->lock);
err_free_client:
	free(client);
	return iio_ptr(err);
}

void iiod_client_destroy(struct iiod_client *client)
{
	if (client->responder) {
		iiod_client_cancel(client);
		iiod_responder_destroy(client->responder);
	}

	iio_mutex_destroy(client->lock);
	free(client);
}

static int iio_device_get_index(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	unsigned int idx;

	for (idx = 0; idx < iio_context_get_devices_count(ctx); idx++)
		if (dev == iio_context_get_device(ctx, idx))
			return idx;

	return -ENODEV; /* Cannot happen */
}

static int iiod_client_get_trigger_new(struct iiod_client *client,
				       const struct iio_device *dev,
				       const struct iio_device **trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iiod_io *io = iiod_responder_get_default_io(client->responder);
	struct iiod_command cmd;
	int ret;

	cmd.op = IIOD_OP_GETTRIG;
	cmd.dev = (uint8_t) iio_device_get_index(dev);

	ret = iiod_io_exec_simple_command(io, &cmd);
	if (ret == -ENODEV)
		*trigger = NULL;
	else if (ret >= 0)
		*trigger = iio_context_get_device(ctx, ret);
	else
		return ret;

	return 0;
}

int iiod_client_get_trigger(struct iiod_client *client,
			    const struct iio_device *dev,
			    const struct iio_device **trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	char buf[1024];
	unsigned int name_len;
	int ret;

	if (iiod_client_uses_binary_interface(client))
		return iiod_client_get_trigger_new(client, dev, trigger);

	iio_snprintf(buf, sizeof(buf), "GETTRIG %s\r\n",
			iio_device_get_id(dev));

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, buf);

	if (ret == 0)
		*trigger = NULL;
	if (ret <= 0)
		goto out_unlock;

	if ((unsigned int) ret > sizeof(buf) - 1) {
		ret = -EIO;
		goto out_unlock;
	}

	name_len = ret;

	ret = (int) iiod_client_read_all(client, buf, name_len + 1);
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

static int iiod_client_set_trigger_new(struct iiod_client *client,
				       const struct iio_device *dev,
				       const struct iio_device *trigger)
{
	struct iiod_io *io = iiod_responder_get_default_io(client->responder);
	struct iiod_command cmd;

	cmd.op = IIOD_OP_SETTRIG;
	cmd.dev = (uint8_t) iio_device_get_index(dev);
	if (!trigger)
		cmd.code = -1;
	else
		cmd.code = iio_device_get_index(trigger);

	return iiod_io_exec_simple_command(io, &cmd);
}

int iiod_client_set_trigger(struct iiod_client *client,
			    const struct iio_device *dev,
			    const struct iio_device *trigger)
{
	char buf[1024];
	int ret;

	if (iiod_client_uses_binary_interface(client))
		return iiod_client_set_trigger_new(client, dev, trigger);

	if (trigger) {
		iio_snprintf(buf, sizeof(buf), "SETTRIG %s %s\r\n",
				iio_device_get_id(dev),
				iio_device_get_id(trigger));
	} else {
		iio_snprintf(buf, sizeof(buf), "SETTRIG %s\r\n",
				iio_device_get_id(dev));
	}

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}

int iiod_client_set_kernel_buffers_count(struct iiod_client *client,
					 const struct iio_device *dev,
					 unsigned int nb_blocks)
{
	int ret;
	char buf[1024];

	if (iiod_client_uses_binary_interface(client))
		return -ENOSYS;

	iio_snprintf(buf, sizeof(buf), "SET %s BUFFERS_COUNT %u\r\n",
			iio_device_get_id(dev), nb_blocks);

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}

static unsigned int calculate_remote_timeout(unsigned int timeout_ms)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout_ms / 2;
}

int iiod_client_set_timeout(struct iiod_client *client, unsigned int timeout)
{
	unsigned int remote_timeout = calculate_remote_timeout(timeout);
	struct iiod_io *io;
	int ret;

	if (iiod_client_uses_binary_interface(client)) {
		struct iiod_command cmd;

		iiod_responder_set_timeout(client->responder, timeout);

		cmd.op = IIOD_OP_TIMEOUT;
		cmd.code = remote_timeout;

		io = iiod_responder_get_default_io(client->responder);
		ret = iiod_io_exec_simple_command(io, &cmd);
	} else {
		char buf[1024];

		iio_mutex_lock(client->lock);
		iio_snprintf(buf, sizeof(buf), "TIMEOUT %u\r\n", remote_timeout);
		ret = iiod_client_exec_command(client, buf);
		iio_mutex_unlock(client->lock);

		if (ret == -EINVAL) {
			/* The TIMEOUT command is not implemented in tinyiiod
			 * based programs; so ignore if we get -EINVAL here. */
			prm_dbg(client->params, "Unable to set remote timeout\n");
			ret = 0;
		}
	}

	return ret;
}

static int iiod_client_discard(struct iiod_client *client,
			       char *buf, size_t buf_len, size_t to_discard)
{
	do {
		size_t read_len;
		ssize_t ret;

		if (to_discard > buf_len)
			read_len = buf_len;
		else
			read_len = to_discard;

		ret = iiod_client_read_all(client, buf, read_len);
		if (ret < 0)
			return (int) ret;

		to_discard -= (size_t) ret;
	} while (to_discard);

	return 0;
}

static ssize_t iiod_client_read_attr_new(struct iiod_client *client,
					 const struct iio_device *dev,
					 const struct iio_channel *chn,
					 const char *attr, char *dest,
					 size_t len, enum iio_attr_type type,
					 unsigned int buf_id)
{
	struct iiod_io *io = iiod_responder_get_default_io(client->responder);
	struct iiod_command cmd = { 0 };
	unsigned int i;
	uint16_t arg1, arg2 = 0;
	struct iiod_buf iiod_buf;

	if (chn) {
		cmd.op = IIOD_OP_READ_CHN_ATTR;

		for (i = 0; i < iio_device_get_channels_count(dev); i++)
			if (iio_device_get_channel(dev, i) == chn)
				break;

		arg2 = (uint16_t) i;

		for (i = 0; i < iio_channel_get_attrs_count(chn); i++)
			if (!strcmp(iio_channel_get_attr(chn, i), attr))
				break;

		if (i == iio_channel_get_attrs_count(chn))
			return -ENOENT;

		arg1 = (uint16_t) i;
	} else {
		switch (type) {
		default:
			cmd.op = IIOD_OP_READ_ATTR;

			for (i = 0; i < iio_device_get_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_attr(dev, i), attr))
					break;

			if (i == iio_device_get_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			break;
		case IIO_ATTR_TYPE_DEBUG:
			cmd.op = IIOD_OP_READ_DBG_ATTR;

			for (i = 0; i < iio_device_get_debug_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_debug_attr(dev, i), attr))
					break;

			if (i == iio_device_get_debug_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			break;
		case IIO_ATTR_TYPE_BUFFER:
			cmd.op = IIOD_OP_READ_BUF_ATTR;

			for (i = 0; i < iio_device_get_buffer_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_buffer_attr(dev, i), attr))
					break;

			if (i == iio_device_get_buffer_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			arg2 = (uint16_t) buf_id;
			break;
		}
	}

	cmd.dev = (uint8_t) iio_device_get_index(dev);
	cmd.code = (arg1 << 16) | arg2;

	iiod_buf.ptr = dest;
	iiod_buf.size = len;

	return iiod_io_exec_command(io, &cmd, NULL, &iiod_buf);
}

ssize_t iiod_client_read_attr(struct iiod_client *client,
			      const struct iio_device *dev,
			      const struct iio_channel *chn,
			      const char *attr, char *dest,
			      size_t len, enum iio_attr_type type,
			      unsigned int buf_id)
{
	const char *id = iio_device_get_id(dev);
	char buf[1024];
	ssize_t ret;

	if (iiod_client_uses_binary_interface(client)) {
		return iiod_client_read_attr_new(client, dev, chn,
						 attr, dest, len, type, buf_id);
	}

	if (buf_id > 0)
		return -ENOSYS;

	if (attr) {
		if (chn) {
			if (!iio_channel_find_attr(chn, attr))
				return -ENOENT;
		} else {
			switch (type) {
				case IIO_ATTR_TYPE_DEVICE:
					if (!iio_device_find_attr(dev, attr))
						return -ENOENT;
					break;
				case IIO_ATTR_TYPE_DEBUG:
					if (!iio_device_find_debug_attr(dev, attr))
						return -ENOENT;
					break;
				case IIO_ATTR_TYPE_BUFFER:
					if (!iio_device_find_buffer_attr(dev, attr))
						return -ENOENT;
					break;
				default:
					return -EINVAL;
			}
		}
	}

	if (chn) {
		iio_snprintf(buf, sizeof(buf), "READ %s %s %s %s\r\n", id,
				iio_channel_is_output(chn) ? "OUTPUT" : "INPUT",
				iio_channel_get_id(chn), attr ? attr : "");
	} else {
		switch (type) {
			case IIO_ATTR_TYPE_DEVICE:
				iio_snprintf(buf, sizeof(buf), "READ %s %s\r\n",
						id, attr ? attr : "");
				break;
			case IIO_ATTR_TYPE_DEBUG:
				iio_snprintf(buf, sizeof(buf), "READ %s DEBUG %s\r\n",
						id, attr ? attr : "");
				break;
			case IIO_ATTR_TYPE_BUFFER:
				iio_snprintf(buf, sizeof(buf), "READ %s BUFFER %s\r\n",
						id, attr ? attr : "");
				break;
		}
	}

	iio_mutex_lock(client->lock);

	ret = (ssize_t) iiod_client_exec_command(client, buf);
	if (ret < 0)
		goto out_unlock;

	if ((size_t) ret + 1 > len) {
		iiod_client_discard(client, dest, len, ret + 1);
		ret = -EIO;
		goto out_unlock;
	}

	/* +1: Also read the trailing \n */
	ret = iiod_client_read_all(client, dest, ret + 1);

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

static ssize_t iiod_client_write_attr_new(struct iiod_client *client,
					  const struct iio_device *dev,
					  const struct iio_channel *chn,
					  const char *attr, const char *src,
					  size_t len, enum iio_attr_type type,
					  unsigned int buf_id)
{
	struct iiod_io *io = iiod_responder_get_default_io(client->responder);
	struct iiod_command cmd = { 0 };
	uint16_t arg1, arg2 = 0;
	struct iiod_buf iiod_buf[2];
	uint64_t length = (uint64_t) len;
	unsigned int i;
	int ret;

	if (chn) {
		cmd.op = IIOD_OP_WRITE_CHN_ATTR;

		for (i = 0; i < iio_device_get_channels_count(dev); i++)
			if (iio_device_get_channel(dev, i) == chn)
				break;

		arg2 = (uint16_t) i;

		for (i = 0; i < iio_channel_get_attrs_count(chn); i++)
			if (!strcmp(iio_channel_get_attr(chn, i), attr))
				break;

		if (i == iio_channel_get_attrs_count(chn))
			return -ENOENT;

		arg1 = (uint16_t) i;
	} else {
		switch (type) {
		default:
			cmd.op = IIOD_OP_WRITE_ATTR;

			for (i = 0; i < iio_device_get_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_attr(dev, i), attr))
					break;

			if (i == iio_device_get_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			break;
		case IIO_ATTR_TYPE_DEBUG:
			cmd.op = IIOD_OP_WRITE_DBG_ATTR;

			for (i = 0; i < iio_device_get_debug_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_debug_attr(dev, i), attr))
					break;

			if (i == iio_device_get_debug_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			break;
		case IIO_ATTR_TYPE_BUFFER:
			cmd.op = IIOD_OP_WRITE_BUF_ATTR;

			for (i = 0; i < iio_device_get_buffer_attrs_count(dev); i++)
				if (!strcmp(iio_device_get_buffer_attr(dev, i), attr))
					break;

			if (i == iio_device_get_buffer_attrs_count(dev))
				return -ENOENT;

			arg1 = (uint16_t) i;
			arg2 = (uint16_t) buf_id;
			break;
		}
	}

	cmd.dev = (uint8_t) iio_device_get_index(dev);
	cmd.code = (arg1 << 16) | arg2;

	iiod_buf[0].ptr = &length;
	iiod_buf[0].size = sizeof(length);
	iiod_buf[1].ptr = (void *) src;
	iiod_buf[1].size = len;

	iiod_io_get_response_async(io, NULL, 0);

	ret = iiod_io_send_command(io, &cmd, iiod_buf, ARRAY_SIZE(iiod_buf));
	if (ret < 0) {
		iiod_io_cancel(io);
		return ret;
	}

	return (ssize_t) iiod_io_wait_for_response(io);
}

ssize_t iiod_client_write_attr(struct iiod_client *client,
			       const struct iio_device *dev,
			       const struct iio_channel *chn,
			       const char *attr, const char *src,
			       size_t len, enum iio_attr_type type,
			       unsigned int buf_id)
{
	const char *id = iio_device_get_id(dev);
	char buf[1024];
	ssize_t ret;
	int resp;

	if (iiod_client_uses_binary_interface(client)) {
		return iiod_client_write_attr_new(client, dev, chn,
						  attr, src, len, type, buf_id);
	}

	if (buf_id > 0)
		return -ENOSYS;

	if (attr) {
		if (chn) {
			if (!iio_channel_find_attr(chn, attr))
				return -ENOENT;
		} else {
			switch (type) {
				case IIO_ATTR_TYPE_DEVICE:
					if (!iio_device_find_attr(dev, attr))
						return -ENOENT;
					break;
				case IIO_ATTR_TYPE_DEBUG:
					if (!iio_device_find_debug_attr(dev, attr))
						return -ENOENT;
					break;
				case IIO_ATTR_TYPE_BUFFER:
					if (!iio_device_find_buffer_attr(dev, attr))
						return -ENOENT;
					break;
				default:
					return -EINVAL;
			}
		}
	}

	if (chn) {
		iio_snprintf(buf, sizeof(buf), "WRITE %s %s %s %s %lu\r\n", id,
				iio_channel_is_output(chn) ? "OUTPUT" : "INPUT",
				iio_channel_get_id(chn), attr ? attr : "",
				(unsigned long) len);
	} else {
		switch (type) {
			case IIO_ATTR_TYPE_DEVICE:
				iio_snprintf(buf, sizeof(buf), "WRITE %s %s %lu\r\n",
						id, attr ? attr : "", (unsigned long) len);
				break;
			case IIO_ATTR_TYPE_DEBUG:
				iio_snprintf(buf, sizeof(buf), "WRITE %s DEBUG %s %lu\r\n",
						id, attr ? attr : "", (unsigned long) len);
				break;
			case IIO_ATTR_TYPE_BUFFER:
				iio_snprintf(buf, sizeof(buf), "WRITE %s BUFFER %s %lu\r\n",
						id, attr ? attr : "", (unsigned long) len);
				break;
		}
	}

	iio_mutex_lock(client->lock);
	ret = iiod_client_write_all(client, buf, strlen(buf));
	if (ret < 0)
		goto out_unlock;

	ret = iiod_client_write_all(client, src, len);
	if (ret < 0)
		goto out_unlock;

	ret = iiod_client_read_integer(client, &resp);
	if (ret < 0)
		goto out_unlock;

	ret = (ssize_t) resp;

out_unlock:
	iio_mutex_unlock(client->lock);
	return ret;
}

static int iiod_client_cmd(const struct iiod_command *cmd,
			   struct iiod_command_data *data, void *d)
{
	/* We don't support receiving commands. */

	return -EINVAL;
}

static ssize_t
iiod_client_read_cb(void *d, const struct iiod_buf *buf, size_t nb)
{
	struct iiod_client *client = d;
	ssize_t ret, count = 0;
	unsigned int i;

	for (i = 0; i < nb; i++) {
		ret = iiod_client_read_all(client, buf[i].ptr, buf[i].size);
		if (ret <= 0)
			return ret;

		count += ret;
	}

	return count;
}

static ssize_t
iiod_client_write_cb(void *d, const struct iiod_buf *buf, size_t nb)
{
	struct iiod_client *client = d;
	ssize_t ret, count = 0;
	unsigned int i;

	for (i = 0; i < nb; i++) {
		ret = iiod_client_write_all(client, buf[i].ptr, buf[i].size);
		if (ret <= 0)
			return ret;

		count += ret;
	}

	return count;
}

static ssize_t iiod_client_discard_cb(void *d, size_t bytes)
{
	struct iiod_client *client = d;
	char buf[0x1000];
	int ret;

	ret = iiod_client_discard(client, buf, sizeof(buf), bytes);
	if (ret < 0)
		return ret;

	return bytes;
}

static const struct iiod_responder_ops iiod_client_ops = {
	.cmd		= iiod_client_cmd,
	.read		= iiod_client_read_cb,
	.write		= iiod_client_write_cb,
	.discard	= iiod_client_discard_cb,
};

static int iiod_client_enable_binary(struct iiod_client *client)
{
	int ret;

	ret = iiod_client_exec_command(client, "BINARY\r\n");

	/* If the BINARY command fail, don't create the responder */
	if (ret != 0)
		return 0;

	client->responder = iiod_responder_create(&iiod_client_ops, client);
	if (!client->responder) {
		prm_err(client->params, "Unable to create responder\n");
		return -ENOMEM;
	}

	return 0;
}

#if WITH_ZSTD
static int iiod_client_send_print(struct iiod_client *client,
				  void *buf, size_t buf_len)
{
	struct iiod_io *io = iiod_responder_get_default_io(client->responder);
	struct iiod_command cmd = { .op = IIOD_OP_PRINT };
	struct iiod_buf iiod_buf = { .ptr = buf, .size = buf_len };

	return iiod_io_exec_command(io, &cmd, NULL, &iiod_buf);
}

static struct iio_context *
iiod_client_create_context_private_new(struct iiod_client *client,
				       const struct iio_backend *backend,
				       const char *description,
				       const char **ctx_attrs,
				       const char **ctx_values,
				       unsigned int nb_ctx_attrs)
{
	size_t xml_len = 0x10000, uri_len = sizeof("xml:") - 1;
	struct iio_context *ctx = NULL;
	unsigned long long len;
	char *xml_zstd, *xml;
	int ret;

	xml = malloc(xml_len);
	if (!xml)
		return iio_ptr(-ENOMEM);

	ret = iiod_client_send_print(client, xml, xml_len);
	if (ret < 0) {
		prm_perror(client->params, -ret,
			   "Unable to send PRINT command");
		goto out_free_xml;
	}

	xml_len = ret;


	prm_dbg(client->params, "Received ZSTD-compressed XML string.\n");

	len = ZSTD_getFrameContentSize(xml, xml_len);
	if (len == ZSTD_CONTENTSIZE_UNKNOWN ||
	    len == ZSTD_CONTENTSIZE_ERROR) {
		ret = -EIO;
		goto out_free_xml;
	}

	xml_zstd = malloc(uri_len + len + 1);
	if (!xml_zstd) {
		ret = -ENOMEM;
		goto out_free_xml;
	}

	xml_len = ZSTD_decompress(xml_zstd + uri_len, len, xml, xml_len);
	if (ZSTD_isError(xml_len)) {
		prm_err(client->params, "Unable to decompress ZSTD data: %s\n",
			ZSTD_getErrorName(xml_len));
		ret = -EIO;
		free(xml_zstd);
		goto out_free_xml;
	}

	memcpy(xml_zstd, "xml:", uri_len);
	xml_zstd[uri_len + xml_len] = '\0';

	/* Free compressed data, make "xml" point to uncompressed data */
	free(xml);
	xml = xml_zstd;

	prm_dbg(client->params, "Creating context\n");

	ctx = iio_create_context_from_xml(client->params, xml,
					  backend, description,
					  ctx_attrs, ctx_values, nb_ctx_attrs);
	ret = iio_err(ctx);
	if (ret) {
		ctx = NULL;
	} else {
		/* If the context creation succeeded, update our "params"
		 * pointer to point to the context's params, as we know their
		 * lifetime is bigger than ours. */
		client->params = &ctx->params;
	}

	if (ctx)
		prm_dbg(client->params, "Context created.\n");

out_free_xml:
	free(xml);
	return ctx ? ctx : iio_ptr(ret);
}
#endif

static struct iio_context *
iiod_client_create_context_private(struct iiod_client *client,
				   const struct iio_backend *backend,
				   const char *description,
				   const char **ctx_attrs,
				   const char **ctx_values,
				   unsigned int nb_ctx_attrs,
				   bool zstd)
{
	const char *cmd = zstd ? "ZPRINT\r\n" : "PRINT\r\n";
	struct iio_context *ctx = NULL;
	unsigned int extra_char = !iiod_client_uses_binary_interface(client);
	size_t xml_len, uri_len = sizeof("xml:") - 1;
	char *xml;
	int ret;

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, cmd);
	if (ret == -EINVAL && zstd) {
		/* If the ZPRINT command does not exist, try again
		 * with the regular PRINT command. */
		iio_mutex_unlock(client->lock);

		return iiod_client_create_context_private(client,
							  backend, description,
							  ctx_attrs, ctx_values,
							  nb_ctx_attrs, false);
	}
	if (ret < 0)
		goto out_unlock;

	xml_len = (size_t) ret;
	xml = malloc(xml_len + uri_len + 1);
	if (!xml) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	memcpy(xml, "xml:", uri_len);

	ret = (int) iiod_client_read_all(client, xml + uri_len,
					 xml_len + extra_char);
	if (ret < 0)
		goto out_free_xml;

	/* Replace the \n with a \0 */
	xml[xml_len + uri_len] = '\0';

#if WITH_ZSTD
	if (zstd) {
		unsigned long long len;
		char *xml_zstd;

		prm_dbg(client->params, "Received ZSTD-compressed XML string.\n");

		len = ZSTD_getFrameContentSize(xml + uri_len, xml_len);
		if (len == ZSTD_CONTENTSIZE_UNKNOWN ||
		    len == ZSTD_CONTENTSIZE_ERROR) {
			ret = -EIO;
			goto out_free_xml;
		}

		/* +1: Leave space for the terminating \0 */
		xml_zstd = malloc(len + uri_len + 1);
		if (!xml_zstd) {
			ret = -ENOMEM;
			goto out_free_xml;
		}

		memcpy(xml_zstd, "xml:", uri_len);

		xml_len = ZSTD_decompress(xml_zstd + uri_len, len, xml + uri_len, xml_len);
		if (ZSTD_isError(xml_len)) {
			prm_err(client->params, "Unable to decompress ZSTD data: %s\n",
				ZSTD_getErrorName(xml_len));
			ret = -EIO;
			free(xml_zstd);
			goto out_free_xml;
		}

		/* Free compressed data, make "xml" point to uncompressed data */
		free(xml);
		xml = xml_zstd;

		xml_zstd[xml_len + uri_len] = '\0';
	}
#endif

	ctx = iio_create_context_from_xml(client->params, xml,
					  backend, description,
					  ctx_attrs, ctx_values, nb_ctx_attrs);
	ret = iio_err(ctx);
	if (ret) {
		ctx = NULL;
	} else {
		/* If the context creation succeeded, update our "params"
		 * pointer to point to the context's params, as we know their
		 * lifetime is bigger than ours. */
		client->params = &ctx->params;
	}

out_free_xml:
	free(xml);
out_unlock:
	iio_mutex_unlock(client->lock);
	return ctx ? ctx : iio_ptr(ret);
}

struct iio_context * iiod_client_create_context(struct iiod_client *client,
						const struct iio_backend *backend,
						const char *description,
						const char **ctx_attrs,
						const char **ctx_values,
						unsigned int nb_ctx_attrs)
{
	if (!WITH_ZSTD || !iiod_client_uses_binary_interface(client))
		return iiod_client_create_context_private(client, backend,
							  description,
							  ctx_attrs, ctx_values,
							  nb_ctx_attrs,
							  WITH_ZSTD);


#if WITH_ZSTD
	return iiod_client_create_context_private_new(client, backend,
						      description, ctx_attrs,
						      ctx_values, nb_ctx_attrs);
#endif
}

static struct iiod_client_io *
iiod_client_create_io_context(struct iiod_client *client,
			      const struct iio_device *dev,
			      size_t samples_count, bool cyclic)
{
	struct iiod_client_io *io;

	io = zalloc(sizeof(*io));
	if (!io)
		return NULL;

	io->client = client;
	io->dev = dev;
	io->samples_count = samples_count;
	io->cyclic = cyclic;

	return io;
}

static void iiod_client_io_destroy(struct iiod_client_io *io)
{
	free(io);
}

static struct iiod_client_io *
iiod_client_open_with_mask(struct iiod_client *client,
			   const struct iio_device *dev,
			   const struct iio_channels_mask *mask,
			   size_t samples_count, bool cyclic)
{
	struct iiod_client_io *io;
	char buf[1024], *ptr;
	size_t i;
	ssize_t len;
	int ret;

	if (iiod_client_uses_binary_interface(client)) {
		/* Only supported on the legacy interface */
		return iio_ptr(-ENOSYS);
	}

	io = iiod_client_create_io_context(client, dev, samples_count, cyclic);
	if (!io)
		return iio_ptr(-ENOMEM);

	len = sizeof(buf);
	len -= iio_snprintf(buf, len, "OPEN %s %lu ",
			iio_device_get_id(dev), (unsigned long) samples_count);
	ptr = buf + strlen(buf);

	for (i = mask->words; i > 0; i--, ptr += 8) {
		len -= iio_snprintf(ptr, len, "%08" PRIx32,
				mask->mask[i - 1]);
	}

	len -= iio_strlcpy(ptr, cyclic ? " CYCLIC\r\n" : "\r\n", len);

	if (len < 0) {
		prm_err(client->params, "strlength problem in iiod_client_open_unlocked\n");
		ret = -ENOMEM;
		goto err_destroy_io;
	}

	ret = iiod_client_exec_command(client, buf);
	if (ret < 0)
		goto err_destroy_io;

	return io;

err_destroy_io:
	iiod_client_io_destroy(io);

	return iio_ptr(ret);
}

static int iiod_client_close_unlocked(struct iiod_client_io *io)
{
	char buf[1024];
	int ret;

	if (iiod_client_uses_binary_interface(io->client)) {
		/* Only supported on the legacy interface */
		return -ENOSYS;
	}

	iio_snprintf(buf, sizeof(buf), "CLOSE %s\r\n",
		     iio_device_get_id(io->dev));
	ret = iiod_client_exec_command(io->client, buf);

	iiod_client_io_destroy(io);

	return ret;
}

static int iiod_client_read_mask(struct iiod_client *client,
				 struct iio_channels_mask *mask)
{
	size_t i;
	ssize_t ret;
	char *buf, *ptr;

	buf = malloc(mask->words * 8 + 1);
	if (!buf)
		return -ENOMEM;

	ret = iiod_client_read_all(client, buf, mask->words * 8 + 1);
	if (ret < 0) {
		prm_err(NULL, "READ ALL: %zd\n", ret);
		goto out_buf_free;
	} else
		ret = 0;

	buf[mask->words * 8] = '\0';

	prm_dbg(client->params, "Reading mask\n");

	for (i = mask->words, ptr = buf; i > 0; i--) {
		iio_sscanf(ptr, "%08" PRIx32, &mask->mask[i - 1]);
		prm_dbg(client->params, "mask[%lu] = 0x%08" PRIx32 "\n",
				(unsigned long)(i - 1), mask->mask[i - 1]);

		ptr = (char *) ((uintptr_t) ptr + 8);
	}

out_buf_free:
	free(buf);
	return (int) ret;
}

static ssize_t iiod_client_read_unlocked(struct iiod_client *client,
					 const struct iio_device *dev,
					 void *dst, size_t len,
					 struct iio_channels_mask *mask)
{
	uintptr_t ptr = (uintptr_t) dst;
	char buf[1024];
	ssize_t ret, read = 0;

	if (iiod_client_uses_binary_interface(client))
		return -ENOSYS;

	if (!len || (mask && mask->words != (dev->nb_channels + 31) / 32))
		return -EINVAL;

	iio_snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
			iio_device_get_id(dev), (unsigned long) len);

	ret = iiod_client_write_all(client, buf, strlen(buf));
	if (ret < 0) {
		prm_err(client->params, "WRITE ALL: %zd\n", ret);
		return ret;
	}

	do {
		int to_read;

		ret = iiod_client_read_integer(client, &to_read);
		if (ret < 0) {
			prm_err(client->params, "READ INTEGER: %zd\n", ret);
			return ret;
		}
		if (to_read < 0)
			return (ssize_t) to_read;
		if (!to_read)
			break;

		if (mask) {
			ret = iiod_client_read_mask(client, mask);
			if (ret < 0) {
				prm_err(client->params, "READ ALL: %zd\n", ret);
				return ret;
			}

			mask = NULL; /* We read the mask only once */
		}

		ret = iiod_client_read_all(client, (char *) ptr, to_read);
		if (ret < 0)
			return ret;

		ptr += ret;
		read += ret;
		len -= ret;
	} while (len);

	return read;
}

static ssize_t
iiod_client_read(struct iiod_client *client, const struct iio_device *dev,
		 void *dst, size_t len, struct iio_channels_mask *mask)
{
	ssize_t ret;

	iiod_client_mutex_lock(client);
	ret = iiod_client_read_unlocked(client, dev, dst, len, mask);
	iiod_client_mutex_unlock(client);

	return ret;
}

ssize_t iiod_client_readbuf(struct iiod_client_buffer_pdata *pdata,
			    void *dst, size_t len)
{
	return iiod_client_read(pdata->client, pdata->dev,
				dst, len, pdata->mask);
}

static ssize_t iiod_client_write_unlocked(struct iiod_client *client,
					  const struct iio_device *dev,
					  const void *src, size_t len)
{
	ssize_t ret;
	char buf[1024];
	int val;

	if (iiod_client_uses_binary_interface(client))
		return -ENOSYS;

	iio_snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
			dev->id, (unsigned long) len);

	ret = iiod_client_write_all(client, buf, strlen(buf));
	if (ret < 0)
		return ret;

	ret = iiod_client_read_integer(client, &val);
	if (ret < 0)
		return ret;
	if (val < 0)
		return (ssize_t) val;

	ret = iiod_client_write_all(client, src, len);
	if (ret < 0)
		return ret;

	ret = iiod_client_read_integer(client, &val);
	if (ret < 0)
		return ret;
	if (val < 0)
		return (ssize_t) val;

	return (ssize_t) len;
}

static ssize_t
iiod_client_write(struct iiod_client *client, const struct iio_device *dev,
		  const void *src, size_t len)
{
	ssize_t ret;

	iiod_client_mutex_lock(client);
	ret = iiod_client_write_unlocked(client, dev, src, len);
	iiod_client_mutex_unlock(client);

	return ret;
}

ssize_t iiod_client_writebuf(struct iiod_client_buffer_pdata *pdata,
			     const void *src, size_t len)
{
	return iiod_client_write(pdata->client, pdata->dev, src, len);
}

struct iiod_client_buffer_pdata *
iiod_client_create_buffer(struct iiod_client *client,
			  const struct iio_device *dev, unsigned int idx,
			  struct iio_channels_mask *mask)
{
	struct iiod_io *io;
	struct iiod_client_buffer_pdata *pdata;
	struct iiod_command cmd;
	struct iiod_buf buf;
	int err;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->idx = (uint16_t) idx;
	pdata->client = client;
	pdata->mask = mask;

	if (iiod_client_uses_binary_interface(client)) {
		io = iiod_responder_get_default_io(client->responder);

		cmd.op = IIOD_OP_CREATE_BUFFER;
		cmd.dev = (uint8_t) iio_device_get_index(dev);
		cmd.code = pdata->idx;

		/* TODO: endianness */
		buf.ptr = mask->mask;
		buf.size = mask->words * 4;

		err = iiod_io_exec_command(io, &cmd, &buf, &buf);
		if (err < 0)
			goto err_free_pdata;
	}

	return pdata;

err_free_pdata:
	free(pdata);
	return iio_ptr(err);
}

void iiod_client_free_buffer(struct iiod_client_buffer_pdata *pdata)
{
	struct iiod_client *client = pdata->client;
	struct iiod_io *io;
	struct iiod_command cmd;

	if (iiod_client_uses_binary_interface(client)) {
		io = iiod_responder_get_default_io(client->responder);

		cmd.op = IIOD_OP_FREE_BUFFER;
		cmd.dev = (uint8_t) iio_device_get_index(pdata->dev);
		cmd.code = pdata->idx;

		iiod_io_exec_simple_command(io, &cmd);
	}

	free(pdata);
}

int iiod_client_enable_buffer(struct iiod_client_buffer_pdata *pdata,
			      size_t nb_samples, bool enable)
{
	struct iiod_client *client = pdata->client;
	struct iiod_client_io *client_io;
	struct iiod_io *io;
	struct iiod_command cmd;
	int err;

	if (!iiod_client_uses_binary_interface(client)) {
		if (!nb_samples)
			return -ENOSYS;

		if (enable && pdata->io)
			return -EBUSY;

		if (!enable && !pdata->io)
			return -EBADF;

		if (enable) {
			client_io = iiod_client_open_with_mask(client, pdata->dev,
							       pdata->mask,
							       nb_samples, false);
			err = iio_err(client_io);
			pdata->io = err ? NULL : client_io;
		} else {
			err = iiod_client_close_unlocked(pdata->io);
			pdata->io = NULL;
		}

		return err;
	}

	cmd.op = enable ? IIOD_OP_ENABLE_BUFFER : IIOD_OP_DISABLE_BUFFER;
	cmd.dev = (uint8_t) iio_device_get_index(pdata->dev);
	cmd.code = pdata->idx;

	io = iiod_responder_get_default_io(client->responder);

	return iiod_io_exec_simple_command(io, &cmd);
}

struct iio_block_pdata *
iiod_client_create_block(struct iiod_client_buffer_pdata *pdata,
			 size_t size, void **data)
{
	struct iiod_client *client = pdata->client;
	struct iio_block_pdata *block;
	struct iiod_command cmd;
	struct iiod_buf buf;
	int ret = -ENOMEM;
	uint64_t block_size = size;

	if (!iiod_client_uses_binary_interface(client))
		return iio_ptr(-ENOSYS);

	buf.ptr = &block_size;
	buf.size = sizeof(block_size);

	block = zalloc(sizeof(*block));
	if (!block)
		return iio_ptr(-ENOMEM);

	block->lock = iio_mutex_create();
	ret = iio_err(block->lock);
	if (ret)
		goto err_free_block;

	block->data = malloc(size);
	if (!block->data)
		goto err_free_mutex;

	block->idx = pdata->next_block_idx++;

	block->io = iiod_responder_create_io(client->responder, block->idx + 1);
	ret = iio_err(block->io);
	if (ret)
		goto err_free_data;

	cmd.op = IIOD_OP_CREATE_BLOCK;
	cmd.dev = (uint8_t) iio_device_get_index(pdata->dev);
	cmd.code = pdata->idx | (block->idx << 16);

	ret = iiod_io_exec_command(block->io, &cmd, &buf, NULL);
	if (ret < 0)
		goto err_free_io;

	*data = block->data;

	block->buffer = pdata;
	block->size = size;

	return block;

err_free_io:
	iiod_io_unref(block->io);
err_free_data:
	free(block->data);
err_free_mutex:
	iio_mutex_destroy(block->lock);
err_free_block:
	free(block);
	return iio_ptr(ret);
}

void iiod_client_free_block(struct iio_block_pdata *block)
{
	struct iiod_client *client = block->buffer->client;
	struct iiod_io *io;
	struct iiod_client_buffer_pdata *pdata = block->buffer;
	struct iiod_command cmd;

	if (!iiod_client_uses_binary_interface(client))
		return;

	cmd.op = IIOD_OP_FREE_BLOCK;
	cmd.dev = (uint8_t) iio_device_get_index(pdata->dev);
	cmd.code = pdata->idx | (block->idx << 16);

	/* Cancel any I/O going on. This means we must send the block free
	 * command through the main I/O as the block's I/O stream is
	 * disrupted. */
	iiod_io_cancel(block->io);
	iiod_io_unref(block->io);

	io = iiod_responder_get_default_io(client->responder);
	iiod_io_exec_simple_command(io, &cmd);

	free(block->data);
	iio_mutex_destroy(block->lock);
	free(block);
}

int iiod_client_enqueue_block(struct iio_block_pdata *block,
			      size_t bytes_used, bool cyclic)
{
	struct iiod_client_buffer_pdata *pdata = block->buffer;
	struct iiod_command cmd;
	struct iiod_buf buf[2];
	bool is_rx = !iio_device_is_tx(pdata->dev);
	unsigned int nb_buf = 1;
	int ret = 0;

	cmd.op = cyclic ? IIOD_OP_ENQUEUE_BLOCK_CYCLIC : IIOD_OP_TRANSFER_BLOCK;
	cmd.dev = (uint8_t) iio_device_get_index(pdata->dev);
	cmd.code = pdata->idx | (block->idx << 16);

	block->bytes_used = bytes_used;
	buf[0].ptr = &block->bytes_used;
	buf[0].size = 8;
	buf[1].ptr = block->data;

	if (is_rx) {
		buf[1].size = block->size;
	} else if (bytes_used) {
		buf[1].size = bytes_used;
		nb_buf++;
	}

	iio_mutex_lock(block->lock);

	if (block->enqueued) {
		ret = -EPERM;
		goto out_unlock;
	}

	iiod_io_get_response_async(block->io, &buf[1], is_rx);

	ret = iiod_io_send_command_async(block->io, &cmd, buf, nb_buf);
	if (ret < 0) {
		iiod_io_cancel_response(block->io);
		goto out_unlock;
	}

	block->enqueued = true;

out_unlock:
	iio_mutex_unlock(block->lock);

	return ret;
}

int iiod_client_dequeue_block(struct iio_block_pdata *block, bool nonblock)
{
	int ret = 0;

	iio_mutex_lock(block->lock);

	if (!block->enqueued) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (nonblock && !iiod_io_command_is_done(block->io)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = iiod_io_wait_for_command_done(block->io);
	if (ret)
		goto out_unlock;

	if (nonblock && !iiod_io_has_response(block->io)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	/* Retrieve return code of enqueue */
	ret = (int) iiod_io_wait_for_response(block->io);

	block->enqueued = false;

out_unlock:
	iio_mutex_unlock(block->lock);

	return ret;
}

bool iiod_client_uses_binary_interface(const struct iiod_client *client)
{
	return !!client->responder;
}
