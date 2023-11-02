// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "debug.h"
#include "ops.h"
#include "thread-pool.h"

#include "../iiod-responder.h"

#include <fcntl.h>
#include <iio/iio-lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

/* Forward declaration */
static struct iio_buffer * get_iio_buffer(struct parser_pdata *pdata,
					  const struct iiod_command *cmd,
					  struct buffer_entry **entry_ptr);

static SLIST_HEAD(BufferList, buffer_entry) bufferlist;

/* Protect bufferlist from parallel access */
static pthread_mutex_t buflist_lock = PTHREAD_MUTEX_INITIALIZER;

static SLIST_HEAD(EventStreamList, evstream_entry) evlist;

/* Protect evlist from parallel access */
static pthread_mutex_t evlist_lock = PTHREAD_MUTEX_INITIALIZER;

static void free_block_entry(struct block_entry *entry)
{
	iiod_io_cancel(entry->io);
	iiod_io_unref(entry->io);
	iio_block_destroy(entry->block);
	free(entry);
}

static void free_buffer_entry(struct buffer_entry *entry)
{
	struct block_entry *block_entry, *block_next;

	iio_task_destroy(entry->enqueue_task);
	iio_task_destroy(entry->dequeue_task);

	pthread_mutex_lock(&entry->lock);

	for (block_entry = SLIST_FIRST(&entry->blocklist);
	     block_entry; block_entry = block_next) {
		block_next = SLIST_NEXT(block_entry, entry);
		free_block_entry(block_entry);
	}

	pthread_mutex_unlock(&entry->lock);

	iio_buffer_destroy(entry->buf);
	pthread_mutex_destroy(&entry->lock);
	free(entry->words);
	free(entry);
}

static void handle_print(struct parser_pdata *pdata,
			 const struct iiod_command *cmd,
			 struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	struct iiod_buf buf;

	if (pdata->xml_zstd) {
		buf.ptr = (void *) pdata->xml_zstd;
		buf.size = pdata->xml_zstd_len;

		iiod_io_send_response(io, pdata->xml_zstd_len, &buf, 1);
	} else {
		iiod_io_send_response_code(io, -EINVAL);
	}
}

static void handle_timeout(struct parser_pdata *pdata,
			   const struct iiod_command *cmd,
			   struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	struct iio_context *ctx = pdata->ctx;
	int ret;

	ret = iio_context_set_timeout(pdata->ctx, cmd->code);
	iiod_io_send_response_code(io, ret);
}

static const struct iio_attr *
get_attr(struct parser_pdata *pdata, const struct iiod_command *cmd)
{
	const struct iio_buffer *buf;
	const struct iio_device *dev;
	const struct iio_channel *chn;
	uint16_t arg1 = (uint32_t) cmd->code >> 16,
		 arg2 = cmd->code & 0xffff;

	dev = iio_context_get_device(pdata->ctx, cmd->dev);
	if (!dev)
		return NULL;

	switch (cmd->op) {
	case IIOD_OP_READ_ATTR:
	case IIOD_OP_WRITE_ATTR:
		return iio_device_get_attr(dev, arg1);
	case IIOD_OP_READ_DBG_ATTR:
	case IIOD_OP_WRITE_DBG_ATTR:
		return iio_device_get_debug_attr(dev, arg1);
	case IIOD_OP_READ_BUF_ATTR:
	case IIOD_OP_WRITE_BUF_ATTR:
		buf = get_iio_buffer(pdata, cmd, NULL);
		if (iio_err(buf))
			break;

		return iio_buffer_get_attr(buf, arg1);
	case IIOD_OP_READ_CHN_ATTR:
	case IIOD_OP_WRITE_CHN_ATTR:
		chn = iio_device_get_channel(dev, arg2);
		if (!chn)
			break;

		return iio_channel_get_attr(chn, arg1);
	default:
		break;
	}

	return NULL;
}

static void handle_read_attr(struct parser_pdata *pdata,
			     const struct iiod_command *cmd,
			     struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	ssize_t ret = -EINVAL;
	char buf[0x10000];
	const struct iio_attr *attr;
	struct iiod_buf iiod_buf;

	attr = get_attr(pdata, cmd);
	if (attr)
		ret = iio_attr_read_raw(attr, buf, sizeof(buf));

	if (ret < 0) {
		iiod_io_send_response_code(io, ret);
	} else {
		iiod_buf.ptr = buf;
		iiod_buf.size = ret;

		/* TODO: async? */
		iiod_io_send_response(io, ret, &iiod_buf, 1);
	}
}

static void handle_write_attr(struct parser_pdata *pdata,
			      const struct iiod_command *cmd,
			      struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	const struct iio_attr *attr;
	size_t count;
	ssize_t ret = -EINVAL;
	uint64_t len;
	struct iiod_buf buf;

	attr = get_attr(pdata, cmd);
	if (!attr)
		goto out_send_response;

	buf.ptr = &len;
	buf.size = sizeof(len);

	ret = iiod_command_data_read(cmd_data, &buf);
	if (ret < 0)
		goto out_send_response;

	buf.ptr = malloc(len);
	if (!buf.ptr) {
		ret = -ENOMEM;
		goto out_send_response;
	}

	buf.size = (size_t) len;

	ret = iiod_command_data_read(cmd_data, &buf);
	if (ret < 0)
		goto out_free_buf;

	ret = iio_attr_write_raw(attr, buf.ptr, (size_t) len);

out_free_buf:
	free(buf.ptr);
out_send_response:
	iiod_io_send_response_code(io, ret);
}

static void handle_gettrig(struct parser_pdata *pdata,
			   const struct iiod_command *cmd,
			   struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	const struct iio_context *ctx = pdata->ctx;
	const struct iio_device *dev, *trigger;
	unsigned int i;
	int ret = -EINVAL;

	dev = iio_context_get_device(ctx, cmd->dev);
	if (!dev)
		goto out_send_response;

	trigger = iio_device_get_trigger(dev);
	ret = iio_err(trigger);
	if (ret)
		goto out_send_response;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++)
		if (trigger == iio_context_get_device(ctx, i))
			break;

	ret = i < iio_context_get_devices_count(ctx) ? i : -ENODEV;

out_send_response:
	iiod_io_send_response_code(io, ret);
}

static void handle_settrig(struct parser_pdata *pdata,
			   const struct iiod_command *cmd,
			   struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	const struct iio_context *ctx = pdata->ctx;
	const struct iio_device *dev, *trigger;
	int ret = -EINVAL;

	dev = iio_context_get_device(ctx, cmd->dev);
	if (!dev)
		goto out_send_response;

	if (cmd->code == -1) {
		trigger = NULL;
	} else {
		trigger = iio_context_get_device(ctx, cmd->code);
		if (!trigger)
			goto out_send_response;
	}

	ret = iio_device_set_trigger(dev, trigger);

out_send_response:
	iiod_io_send_response_code(io, ret);
}

static bool iio_buffer_is_tx(const struct iio_buffer *buf)
{
	const struct iio_device *dev = iio_buffer_get_device(buf);
	const struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		ch = iio_device_get_channel(dev, i);

		if (iio_channel_is_output(ch) &&
		    iio_channel_is_scan_element(ch))
			return true;
	}

	return false;
}

static int buffer_enqueue_block(void *priv, void *d)
{
	struct buffer_entry *buffer = priv;
	struct block_entry *entry = d;
	intptr_t ret;

	ret = iio_block_enqueue(entry->block, (size_t) entry->bytes_used,
				entry->cyclic);
	if (ret)
		goto out_send_response;

	if (entry->cyclic)
		goto out_send_response;

	ret = iio_task_enqueue_autoclear(buffer->dequeue_task, entry);
	if (ret)
		goto out_send_response;

	return 0;

out_send_response:
	iiod_io_send_response_code(entry->io, ret);
	return 0;
}

static int buffer_dequeue_block(void *priv, void *d)
{
	struct buffer_entry *buffer = priv;
	struct block_entry *entry = d;
	struct iiod_buf data;
	unsigned int nb_data = 0;
	intptr_t ret;

	ret = iio_block_dequeue(entry->block, false);
	if (ret < 0)
		goto out_send_response;

	if (!iio_buffer_is_tx(buffer->buf)) {
		data.ptr = iio_block_start(entry->block);
		data.size = iio_block_end(entry->block) - data.ptr;
		nb_data++;

		ret = data.size;
	}

out_send_response:
	iiod_io_send_response(entry->io, ret, &data, nb_data);
	return 0;
}

static void handle_create_buffer(struct parser_pdata *pdata,
				 const struct iiod_command *cmd,
				 struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	const struct iio_context *ctx = pdata->ctx;
	struct iio_channels_mask *mask;
	const struct iio_device *dev;
	struct iio_channel *chn;
	struct buffer_entry *entry;
	struct iio_buffer *buf;
	struct iiod_buf data;
	unsigned int i, nb_channels;
	size_t nb_words;
	int ret = -EINVAL;

	dev = iio_context_get_device(ctx, cmd->dev);
	if (!dev)
		goto err_send_response;

	entry = zalloc(sizeof(*entry));
	if (!entry) {
		ret = -ENOMEM;
		goto err_send_response;
	}

	nb_channels = iio_device_get_channels_count(dev);
	nb_words = (nb_channels + 31) / 32;

	entry->words = malloc(nb_words * 4);
	if (!entry->words) {
		ret = -ENOMEM;
		goto err_free_entry;
	}

	/* TODO: endianness */
	data.ptr = entry->words;
	data.size = nb_words * 4;

	ret = iiod_command_data_read(cmd_data, &data);
	if (ret < 0)
		goto err_free_words;

	/* Create a temporary mask object */
	mask = iio_create_channels_mask(nb_channels);
	if (!mask) {
		ret = -ENOMEM;
		goto err_free_words;
	}

	/* Fill it according to the "words" bitmask */
	for (i = 0; i < nb_channels; i++) {
		chn = iio_device_get_channel(dev, i);

		if (TEST_BIT(entry->words, i))
			iio_channel_enable(chn, mask);
		else
			iio_channel_disable(chn, mask);
	}

	entry->enqueue_task = iio_task_create(buffer_enqueue_block, entry,
					      "buffer-enqueue-thd");
	ret = iio_err(entry->enqueue_task);
	if (ret)
		goto err_free_mask;

	entry->dequeue_task = iio_task_create(buffer_dequeue_block, entry,
					      "buffer-dequeue-thd");
	ret = iio_err(entry->dequeue_task);
	if (ret)
		goto err_destroy_enqueue_task;

	entry->dev = dev;
	entry->idx = (uint16_t) cmd->code;

	buf = iio_device_create_buffer(dev, entry->idx, mask);
	ret = iio_err(buf);
	if (ret)
		goto err_destroy_dequeue_task;

	/* Rewrite the "words" bitmask according to the mask object,
	 * which may have been modified when creating the buffer. */
	for (i = 0; i < nb_channels; i++) {
		chn = iio_device_get_channel(dev, i);

		if (iio_channel_is_enabled(chn, mask))
			entry->words[BIT_WORD(i)] |= BIT_MASK(i);
		else
			entry->words[BIT_WORD(i)] &= ~BIT_MASK(i);
	}

	/* Success, destroy the temporary mask object */
	iio_channels_mask_destroy(mask);

	entry->buf = buf;
	entry->pdata = pdata;
	pthread_mutex_init(&entry->lock, NULL);

	pthread_mutex_lock(&buflist_lock);
	SLIST_INSERT_HEAD(&bufferlist, entry, entry);
	pthread_mutex_unlock(&buflist_lock);

	IIO_DEBUG("Buffer %u created.\n", entry->idx);

	/* Send the success code + updated mask back */
	iiod_io_send_response(io, data.size, &data, 1);
	return;

err_destroy_dequeue_task:
	iio_task_destroy(entry->dequeue_task);
err_destroy_enqueue_task:
	iio_task_destroy(entry->enqueue_task);
err_free_mask:
	iio_channels_mask_destroy(mask);
err_free_words:
	free(entry->words);
err_free_entry:
	free(entry);
err_send_response:
	iiod_io_send_response_code(io, ret);
}

static struct iio_buffer * get_iio_buffer(struct parser_pdata *pdata,
					  const struct iiod_command *cmd,
					  struct buffer_entry **entry_ptr)
{
	const struct iio_device *dev;
	struct buffer_entry *entry;
	struct iio_buffer *buf = NULL;

	dev = iio_context_get_device(pdata->ctx, cmd->dev);
	if (!dev)
		return iio_ptr(-EINVAL);

	pthread_mutex_lock(&buflist_lock);

	SLIST_FOREACH(entry, &bufferlist, entry) {
		if (entry->dev == dev && entry->idx == (cmd->code & 0xffff)) {
			buf = entry->buf;
			break;
		}
	}

	pthread_mutex_unlock(&buflist_lock);

	if (buf && entry_ptr)
		*entry_ptr = entry;

	return buf ?: iio_ptr(-EBADF);
}

static struct iio_block * get_iio_block(struct parser_pdata *pdata,
					struct buffer_entry *entry_buf,
					const struct iiod_command *cmd,
					struct block_entry **entry_ptr)
{
	struct block_entry *entry;
	struct iio_block *block = NULL;
	int err;

	pthread_mutex_lock(&entry_buf->lock);

	SLIST_FOREACH(entry, &entry_buf->blocklist, entry) {
		if (entry->client_id == cmd->client_id) {
			block = entry->block;
			break;
		}
	}

	pthread_mutex_unlock(&entry_buf->lock);

	if (block && entry_ptr)
		*entry_ptr = entry;

	return block ?: iio_ptr(-EBADF);
}

static void handle_free_buffer(struct parser_pdata *pdata,
			       const struct iiod_command *cmd,
			       struct iiod_command_data *cmd_data)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	const struct iio_device *dev;
	struct block_entry *block_entry, *block_next;
	struct buffer_entry *entry, *buf_entry;
	struct iio_buffer *buf;
	int ret;

	dev = iio_context_get_device(pdata->ctx, cmd->dev);
	if (!dev)
		goto out_send_response;

	buf = get_iio_buffer(pdata, cmd, &buf_entry);
	ret = iio_err(buf);
	if (ret)
		goto out_send_response;

	ret = -EBADF;

	pthread_mutex_lock(&buflist_lock);

	SLIST_FOREACH(entry, &bufferlist, entry) {
		if (entry != buf_entry)
			continue;

		SLIST_REMOVE(&bufferlist, entry, buffer_entry, entry);
		free_buffer_entry(entry);
		ret = 0;
		break;
	}

	pthread_mutex_unlock(&buflist_lock);

	IIO_DEBUG("Buffer %u freed.\n", cmd->code);

out_send_response:
	iiod_io_send_response_code(io, ret);
}

static void handle_set_enabled_buffer(struct parser_pdata *pdata,
				      const struct iiod_command *cmd,
				      struct iiod_command_data *cmd_data,
				      bool enabled)
{
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	struct buffer_entry *entry;
	struct iio_buffer *buf;
	int ret;

	buf = get_iio_buffer(pdata, cmd, &entry);
	ret = iio_err(buf);
	if (ret)
		goto out_send_response;

	if (enabled) {
		iio_task_start(entry->enqueue_task);
		iio_task_start(entry->dequeue_task);
		ret = iio_buffer_enable(buf);
	} else {
		ret = iio_buffer_disable(buf);
		iio_task_stop(entry->dequeue_task);
		iio_task_stop(entry->enqueue_task);
	}

out_send_response:
	iiod_io_send_response_code(io, ret);
}

static void handle_enable_buffer(struct parser_pdata *pdata,
				 const struct iiod_command *cmd,
				 struct iiod_command_data *cmd_data)
{
	handle_set_enabled_buffer(pdata, cmd, cmd_data, true);
}

static void handle_disable_buffer(struct parser_pdata *pdata,
				  const struct iiod_command *cmd,
				  struct iiod_command_data *cmd_data)
{
	handle_set_enabled_buffer(pdata, cmd, cmd_data, false);
}

static void handle_create_block(struct parser_pdata *pdata,
				const struct iiod_command *cmd,
				struct iiod_command_data *cmd_data)
{
	struct buffer_entry *buf_entry;
	struct block_entry *entry;
	struct iio_block *block;
	struct iio_buffer *buf;
	struct iiod_buf data;
	uint64_t block_size;
	struct iiod_io *io;
	int ret;

	io = iiod_command_create_io(cmd, cmd_data);
	ret = iio_err(io);
	if (ret) {
		/* TODO: How to handle this error? */
		return;
	}

	data.ptr = &block_size;
	data.size = sizeof(block_size);

	ret = iiod_command_data_read(cmd_data, &data);
	if (ret < 0)
		goto out_send_response;

	buf = get_iio_buffer(pdata, cmd, &buf_entry);
	ret = iio_err(buf);
	if (ret)
		goto out_send_response;

	block = get_iio_block(pdata, buf_entry, cmd, NULL);
	ret = iio_err(block);
	if (ret == 0) {
		/* No error? This block already exists, so return
		 * -EINVAL. */
		ret = -EINVAL;
		goto out_send_response;
	}

	block = iio_buffer_create_block(buf, (size_t) block_size);
	ret = iio_err(block);
	if (ret)
		goto out_send_response;

	entry = zalloc(sizeof(*entry));
	if (!entry) {
		ret = -ENOMEM;
		iio_block_destroy(block);
		goto out_send_response;
	}

	entry->block = block;
	entry->io = io;
	entry->client_id = cmd->client_id;

	/* Keep a reference to the iiod_io until the block is freed. */
	iiod_io_ref(io);

	pthread_mutex_lock(&buf_entry->lock);
	SLIST_INSERT_HEAD(&buf_entry->blocklist, entry, entry);
	pthread_mutex_unlock(&buf_entry->lock);

out_send_response:
	iiod_io_send_response_code(io, ret);
	iiod_io_unref(io);
}

static void handle_free_block(struct parser_pdata *pdata,
			      const struct iiod_command *cmd,
			      struct iiod_command_data *cmd_data)
{
	struct buffer_entry *buf_entry;
	struct block_entry *entry;
	struct iio_buffer *buf;
	struct iio_block *block;
	struct iiod_io *io;
	int ret;

	buf = get_iio_buffer(pdata, cmd, &buf_entry);
	ret = iio_err(buf);
	if (ret)
		goto out_send_response;

	block = get_iio_block(pdata, buf_entry, cmd, NULL);
	ret = iio_err(block);
	if (ret)
		goto out_send_response;

	ret = -EBADF;

	pthread_mutex_lock(&buf_entry->lock);

	SLIST_FOREACH(entry, &buf_entry->blocklist, entry) {
		if (entry->block != block)
			continue;

		SLIST_REMOVE(&buf_entry->blocklist, entry, block_entry, entry);

		free_block_entry(entry);
		ret = 0;
		break;
	}

	pthread_mutex_unlock(&buf_entry->lock);

	IIO_DEBUG("Block %u freed.\n", cmd->code);

out_send_response:
	/* We may have freed the block's iiod_io, so create a new one to
	 * answer the request. */
	io = iiod_command_create_io(cmd, cmd_data);
	if (iio_err(io)) {
		/* TODO: How to handle the error? */
		return;
	}
	iiod_io_send_response_code(io, ret);
	iiod_io_unref(io);
}

static void handle_transfer_block(struct parser_pdata *pdata,
				  const struct iiod_command *cmd,
				  struct iiod_command_data *cmd_data)
{
	struct buffer_entry *entry;
	struct block_entry *block_entry;
	struct iio_block *block;
	struct iio_buffer *buf;
	struct iiod_buf readbuf;
	uint64_t bytes_used;
	int ret;

	buf = get_iio_buffer(pdata, cmd, &entry);
	ret = iio_err(buf);
	if (ret) {
		IIO_ERROR("handle_transfer_block: Could not find IIO buffer\n");
		return;
	}

	block = get_iio_block(pdata, entry, cmd, &block_entry);
	ret = iio_err(block);
	if (ret) {
		IIO_ERROR("handle_transfer_block: Could not find IIO block\n");
		return;
	}

	readbuf.ptr = &bytes_used;
	readbuf.size = 8;

	/* Read bytes_used */
	ret = iiod_command_data_read(cmd_data, &readbuf);
	if (ret < 0)
		goto out_send_response;

	if (bytes_used == 0) {
		IIO_ERROR("Cannot enqueue a block with size 0\n");
		ret =  -EINVAL;
		goto out_send_response;
	}

	/* Read the data into the block if we are dealing with a TX buffer */
	if (iio_buffer_is_tx(buf)) {
		readbuf.ptr = iio_block_start(block);
		readbuf.size = iio_block_end(block) - readbuf.ptr;

		ret = iiod_command_data_read(cmd_data, &readbuf);
		if (ret < 0)
			goto out_send_response;
	}

	block_entry->bytes_used = bytes_used;
	block_entry->cyclic = cmd->op == IIOD_OP_ENQUEUE_BLOCK_CYCLIC;

	ret = iio_task_enqueue_autoclear(entry->enqueue_task, block_entry);
	if (ret)
		goto out_send_response;

	/* The return code and/or data will be sent from the task handler. */
	return;

out_send_response:
	iiod_io_send_response_code(block_entry->io, ret);
}

static int evstream_read(void *priv, void *d)
{
	struct evstream_entry *entry = priv;
	struct iio_event event;
	struct iiod_buf buf = {
		.ptr = &event,
		.size = sizeof(event),
	};
	int ret;

	ret = iio_event_stream_read(entry->stream, &event, false);
	if (ret < 0)
	      return iiod_io_send_response_code(entry->io, ret);

	return iiod_io_send_response(entry->io, sizeof(event), &buf, 1);
}

static void handle_create_evstream(struct parser_pdata *pdata,
				   const struct iiod_command *cmd,
				   struct iiod_command_data *cmd_data)
{
	const struct iio_context *ctx = pdata->ctx;
	const struct iio_device *dev;
	struct evstream_entry *entry;
	struct iiod_io *io;
	int ret = -EINVAL;

	io = iiod_command_create_io(cmd, cmd_data);
	ret = iio_err(io);
	if (ret) {
		/* TODO: How to handle this error? */
		return;
	}

	dev = iio_context_get_device(ctx, cmd->dev);
	if (!dev)
		goto out_send_response;

	entry = zalloc(sizeof(*entry));
	if (!entry) {
		ret = -ENOMEM;
		goto out_send_response;
	}

	entry->io = io;
	entry->dev = dev;
	entry->client_id = cmd->client_id;
	entry->pdata = pdata;

	entry->stream = iio_device_create_event_stream(dev);
	ret = iio_err(entry->stream);
	if (ret) {
		free(entry);
		goto out_send_response;
	}

	entry->task = iio_task_create(evstream_read, entry,
				      "evstream-read-thd");
	ret = iio_err(entry->task);
	if (ret) {
		iio_event_stream_destroy(entry->stream);
		free(entry);
		goto out_send_response;
	}

	iio_task_start(entry->task);

	/* Keep a reference to the iiod_io until the evstream is freed. */
	iiod_io_ref(io);

	pthread_mutex_lock(&evlist_lock);
	SLIST_INSERT_HEAD(&evlist, entry, entry);
	pthread_mutex_unlock(&evlist_lock);

out_send_response:
	iiod_io_send_response_code(io, ret);
	iiod_io_unref(io);
}

static struct evstream_entry * get_evstream(struct parser_pdata *pdata,
					    const struct iiod_command *cmd,
					    uint16_t idx, bool remove)
{
	const struct iio_context *ctx = pdata->ctx;
	const struct iio_device *dev;
	struct evstream_entry *entry;

	dev = iio_context_get_device(ctx, cmd->dev);
	if (!dev)
		return NULL;

	pthread_mutex_lock(&evlist_lock);
	SLIST_FOREACH(entry, &evlist, entry) {
		if (entry->client_id == idx
		    && entry->dev == dev
		    && entry->pdata == pdata) {
			break;
		}
	}

	if (entry && remove)
	      SLIST_REMOVE(&evlist, entry, evstream_entry, entry);

	pthread_mutex_unlock(&evlist_lock);

	return entry;
}

static void free_evstream(struct evstream_entry *entry)
{

	iio_event_stream_destroy(entry->stream);
	iiod_io_cancel(entry->io);

	iio_task_stop(entry->task);
	iio_task_destroy(entry->task);

	iiod_io_unref(entry->io);
	free(entry);
}

static void handle_free_evstream(struct parser_pdata *pdata,
				 const struct iiod_command *cmd,
				 struct iiod_command_data *cmd_data)
{
	struct evstream_entry *entry, *each;
	struct iiod_io *io = iiod_command_get_default_io(cmd_data);
	int ret = 0;

	entry = get_evstream(pdata, cmd, cmd->code, true);
	if (!entry) {
		ret = -EBADF;
		goto out_send_response;
	}

	free_evstream(entry);

out_send_response:
	iiod_io_send_response_code(io, ret);
}

static void handle_read_event(struct parser_pdata *pdata,
			      const struct iiod_command *cmd,
			      struct iiod_command_data *cmd_data)
{
	struct evstream_entry *entry;
	int ret;

	entry = get_evstream(pdata, cmd, cmd->client_id, false);
	if (!entry) {
		/* TODO: How to handle this error? */
		return;
	}

	if (cmd->code) {
		struct iio_event event;
		struct iiod_buf buf = {
			.ptr = &event,
			.size = sizeof(event),
		};

		/* Nonblock mode: run iio_event_stream_read() inline,
		 * and respond here. */
		ret = iio_event_stream_read(entry->stream, &event, true);
		if (ret < 0)
		      iiod_io_send_response_code(entry->io, ret);
		else
		      iiod_io_send_response(entry->io, sizeof(event), &buf, 1);
	} else {
		/* Blocking mode: defer the answer. */
		ret = iio_task_enqueue_autoclear(entry->task, entry);
		if (ret)
			iiod_io_send_response_code(entry->io, ret);
	}
}

typedef void (*iiod_opcode_fn)(struct parser_pdata *,
			       const struct iiod_command *,
			       struct iiod_command_data *cmd_data);

static const iiod_opcode_fn iiod_op_functions[] = {
	[IIOD_OP_PRINT]			= handle_print,
	[IIOD_OP_TIMEOUT]		= handle_timeout,
	[IIOD_OP_READ_ATTR]		= handle_read_attr,
	[IIOD_OP_READ_DBG_ATTR]		= handle_read_attr,
	[IIOD_OP_READ_BUF_ATTR]		= handle_read_attr,
	[IIOD_OP_READ_CHN_ATTR]		= handle_read_attr,
	[IIOD_OP_WRITE_ATTR]		= handle_write_attr,
	[IIOD_OP_WRITE_DBG_ATTR]	= handle_write_attr,
	[IIOD_OP_WRITE_BUF_ATTR]	= handle_write_attr,
	[IIOD_OP_WRITE_CHN_ATTR]	= handle_write_attr,
	[IIOD_OP_GETTRIG]		= handle_gettrig,
	[IIOD_OP_SETTRIG]		= handle_settrig,

	[IIOD_OP_CREATE_BUFFER]		= handle_create_buffer,
	[IIOD_OP_FREE_BUFFER]		= handle_free_buffer,
	[IIOD_OP_ENABLE_BUFFER]		= handle_enable_buffer,
	[IIOD_OP_DISABLE_BUFFER]	= handle_disable_buffer,

	[IIOD_OP_CREATE_BLOCK]		= handle_create_block,
	[IIOD_OP_FREE_BLOCK]		= handle_free_block,
	[IIOD_OP_TRANSFER_BLOCK]	= handle_transfer_block,
	[IIOD_OP_ENQUEUE_BLOCK_CYCLIC]	= handle_transfer_block,

	[IIOD_OP_CREATE_EVSTREAM]	= handle_create_evstream,
	[IIOD_OP_FREE_EVSTREAM]		= handle_free_evstream,
	[IIOD_OP_READ_EVENT]		= handle_read_event,
};

static int iiod_cmd(const struct iiod_command *cmd,
		    struct iiod_command_data *data, void *d)
{
	struct parser_pdata *pdata = d;

	if (cmd->op >= IIOD_NB_OPCODES) {
		IIO_ERROR("Received invalid opcode 0x%x\n", cmd->op);
		return -EINVAL;
	}

	iiod_op_functions[cmd->op](pdata, cmd, data);

	return 0;
}

static ssize_t iiod_read(void *d, const struct iiod_buf *buf, size_t nb)
{
	return read_all(d, buf->ptr, buf->size);
}

static ssize_t iiod_write(void *d, const struct iiod_buf *buf, size_t nb)
{
	return write_all(d, buf->ptr, buf->size);
}

static const struct iiod_responder_ops iiod_responder_ops = {
	.cmd	= iiod_cmd,
	.read	= iiod_read,
	.write	= iiod_write,
};

static void iiod_responder_free_resources(struct parser_pdata *pdata)
{
	struct buffer_entry *buf_entry, *buf_next;
	struct evstream_entry *ev_entry, *ev_next;

	pthread_mutex_lock(&buflist_lock);

	for (buf_entry = SLIST_FIRST(&bufferlist);
	     buf_entry; buf_entry = buf_next) {
		buf_next = SLIST_NEXT(buf_entry, entry);

		/* Only free the buffers that this client created */
		if (buf_entry->pdata == pdata) {
			SLIST_REMOVE(&bufferlist, buf_entry, buffer_entry, entry);
			free_buffer_entry(buf_entry);
		}
	}

	pthread_mutex_unlock(&buflist_lock);

	pthread_mutex_lock(&evlist_lock);

	for (ev_entry = SLIST_FIRST(&evlist); ev_entry; ev_entry = ev_next) {
		ev_next = SLIST_NEXT(ev_entry, entry);

		/* Only free the event streams that this client created */
		if (ev_entry->pdata == pdata) {
			SLIST_REMOVE(&evlist, ev_entry, evstream_entry, entry);
			free_evstream(ev_entry);
		}
	}

	pthread_mutex_unlock(&evlist_lock);
}

int binary_parse(struct parser_pdata *pdata)
{
	struct iiod_responder *responder;

	responder = iiod_responder_create(&iiod_responder_ops, pdata);
	if (!responder)
		return -ENOMEM;

	/* TODO: poll main thread pool FD */

	iiod_responder_wait_done(responder);
	iiod_responder_free_resources(pdata);
	iiod_responder_destroy(responder);

	return 0;
}
