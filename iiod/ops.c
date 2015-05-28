/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
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
 * */

#include "ops.h"
#include "parser.h"
#include "../debug.h"
#include "../iio-private.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>

int yyparse(yyscan_t scanner);

/* Corresponds to a thread reading from a device */
struct ThdEntry {
	SLIST_ENTRY(ThdEntry) next;
	pthread_cond_t cond;
	pthread_mutex_t cond_lock;
	unsigned int nb, sample_size, samples_count;
	ssize_t err;
	struct parser_pdata *pdata;

	uint32_t *mask;
	bool active, is_writer, new_client, wait_for_open;
};

/* Corresponds to an opened device */
struct DevEntry {
	SLIST_ENTRY(DevEntry) next;

	struct iio_device *dev;
	struct iio_buffer *buf;
	unsigned int sample_size, nb_clients;
	bool update_mask;
	bool cyclic;

	/* Linked list of ThdEntry structures corresponding
	 * to all the threads who opened the device */
	SLIST_HEAD(ThdHead, ThdEntry) thdlist_head;
	pthread_mutex_t thdlist_lock;

	pthread_t thd;
	pthread_attr_t attr;

	pthread_cond_t last_cond;
	pthread_mutex_t last_lock;

	uint32_t *mask;
	size_t nb_words;
};

struct send_sample_cb_info {
	FILE *out;
	unsigned int nb_bytes, cpt;
	uint32_t *mask;
};

struct receive_sample_cb_info {
	FILE *in;
	unsigned int nb_bytes, cpt;
	uint32_t *mask;
};

/* This is a linked list of DevEntry structures corresponding to
 * all the devices which have threads trying to read them */
static SLIST_HEAD(DevHead, DevEntry) devlist_head =
	    SLIST_HEAD_INITIALIZER(DevHead);
static pthread_mutex_t devlist_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t write_all(const void *src, size_t len, FILE *out)
{
	const void *ptr = src;
	while (len) {
		ssize_t ret = writefd(fileno(out), ptr, len);
		if (ret < 0)
			return -errno;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}
	return ptr - src;
}

static ssize_t read_all(void *dst, size_t len, FILE *in)
{
	void *ptr = dst;
	while (len) {
		ssize_t ret = readfd(fileno(in), ptr, len);
		if (ret < 0)
			return -errno;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}
	return ptr - dst;
}

static void print_value(struct parser_pdata *pdata, long value)
{
	if (pdata->verbose && value < 0) {
		char buf[1024];
		iio_strerror(-value, buf, sizeof(buf));
		output(pdata, "ERROR: ");
		output(pdata, buf);
		output(pdata, "\n");
	} else {
		char buf[128];
		sprintf(buf, "%li\n", value);
		output(pdata, buf);
	}
}

static ssize_t send_sample(const struct iio_channel *chn,
		void *src, size_t length, void *d)
{
	struct send_sample_cb_info *info = d;
	if (chn->index < 0 || !TEST_BIT(info->mask, chn->index))
		return 0;
	if (info->nb_bytes < length)
		return 0;

	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char zero = 0;
		for (i = 0; i < goal; i++)
			writefd(fileno(info->out), &zero, 1);
		info->cpt += goal;
	}

	info->cpt += length;
	info->nb_bytes -= length;
	return write_all(src, length, info->out);
}

static ssize_t receive_sample(const struct iio_channel *chn,
		void *dst, size_t length, void *d)
{
	struct receive_sample_cb_info *info = d;
	if (chn->index < 0 || !TEST_BIT(info->mask, chn->index))
		return 0;
	if (info->cpt == info->nb_bytes)
		return 0;

	/* Skip the padding if needed */
	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char foo;
		for (i = 0; i < goal; i++)
			readfd(fileno(info->in), &foo, 1);
		info->cpt += goal;
	}

	info->cpt += length;
	return read_all(dst, length, info->in);
}

static ssize_t send_data(struct DevEntry *dev, struct ThdEntry *thd, size_t len)
{
	struct parser_pdata *pdata = thd->pdata;
	bool demux = server_demux && dev->sample_size != thd->sample_size;

	if (demux)
		len = (len / dev->sample_size) * thd->sample_size;
	if (len > thd->nb)
		len = thd->nb;

	print_value(pdata, len);

	if (thd->new_client) {
		unsigned int i;
		char buf[128];

		/* Send the current mask */
		if (demux)
			for (i = dev->nb_words; i > 0; i--) {
				sprintf(buf, "%08x", thd->mask[i - 1]);
				output(pdata, buf);
			}
		else
			for (i = dev->nb_words; i > 0; i--) {
				sprintf(buf, "%08x", dev->mask[i - 1]);
				output(pdata, buf);
			}
		output(pdata, "\n");
		thd->new_client = false;
	}

	if (!demux) {
		/* Short path */
		return write_all(dev->buf->buffer, len, pdata->out);
	} else {
		struct send_sample_cb_info info = {
			.out = pdata->out,
			.cpt = 0,
			.nb_bytes = len,
			.mask = thd->mask,
		};

		return iio_buffer_foreach_sample(dev->buf, send_sample, &info);
	}
}

static ssize_t receive_data(struct DevEntry *dev, struct ThdEntry *thd)
{
	struct parser_pdata *pdata = thd->pdata;

	/* Inform that no error occured, and that we'll start reading data */
	if (thd->new_client) {
		print_value(thd->pdata, 0);
		thd->new_client = false;
	}

	if (dev->sample_size == thd->sample_size) {
		/* Short path: Receive directly in the buffer */

		size_t len = dev->buf->length;
		if (thd->nb < len)
			len = thd->nb;

		return read_all(dev->buf->buffer, len, pdata->in);
	} else {
		/* Long path: Mux the samples to the buffer */

		struct receive_sample_cb_info info = {
			.in = thd->pdata->in,
			.cpt = 0,
			.nb_bytes = thd->nb,
			.mask = thd->mask,
		};

		return iio_buffer_foreach_sample(dev->buf,
				receive_sample, &info);
	}
}

static void signal_thread(struct ThdEntry *thd, ssize_t ret)
{

	thd->err = ret;
	thd->nb = 0;
	thd->active = false;

	/* Ensure that the client thread is already waiting */
	pthread_mutex_lock(&thd->cond_lock);
	pthread_cond_signal(&thd->cond);
	pthread_mutex_unlock(&thd->cond_lock);
}

static void * rw_thd(void *d)
{
	struct DevEntry *entry = d;
	struct ThdEntry *thd, *next_thd;
	struct iio_device *dev = entry->dev;
	unsigned int nb_words = entry->nb_words;
	ssize_t ret = 0;
	bool had_readers = false;

	INFO("R/W thread started for device %s\n",
			dev->name ? dev->name : dev->id);

	while (true) {
		bool has_readers = false, has_writers = false,
		     mask_updated = false;
		unsigned int sample_size;

		/* NOTE: this while loop must exit with
		 * devlist_lock and thdlist_lock locked. */
		pthread_mutex_lock(&devlist_lock);
		pthread_mutex_lock(&entry->thdlist_lock);

		if (SLIST_EMPTY(&entry->thdlist_head))
			break;

		if (entry->update_mask) {
			unsigned int i;
			unsigned int samples_count = 0;

			memset(entry->mask, 0, nb_words * sizeof(*entry->mask));
			SLIST_FOREACH(thd, &entry->thdlist_head, next) {
				for (i = 0; i < nb_words; i++)
					entry->mask[i] |= thd->mask[i];

				if (thd->samples_count > samples_count)
					samples_count = thd->samples_count;
			}

			if (entry->buf)
				iio_buffer_destroy(entry->buf);

			for (i = 0; i < dev->nb_channels; i++) {
				struct iio_channel *chn = dev->channels[i];
				long index = chn->index;
				if (index >= 0 && TEST_BIT(entry->mask, index))
					iio_channel_enable(chn);
				else
					iio_channel_disable(chn);
			}

			entry->buf = iio_device_create_buffer(dev,
					samples_count, entry->cyclic);
			if (!entry->buf) {
				ret = -errno;
				ERROR("Unable to create buffer\n");
				break;
			}

			/* Signal the threads that we opened the device */
			SLIST_FOREACH(thd, &entry->thdlist_head, next) {
				if (thd->wait_for_open) {
					signal_thread(thd, 0);
					thd->wait_for_open = false;
				}
			}

			DEBUG("IIO device %s reopened with new mask:\n",
					dev->id);
			for (i = 0; i < nb_words; i++)
				DEBUG("Mask[%i] = 0x%08x\n", i, entry->mask[i]);
			entry->update_mask = false;

			entry->sample_size = iio_device_get_sample_size(dev);
			mask_updated = true;
		}

		sample_size = entry->sample_size;

		SLIST_FOREACH(thd, &entry->thdlist_head, next) {
			thd->active = !thd->err && thd->nb >= sample_size;
			if (mask_updated && thd->active)
				signal_thread(thd, thd->nb);

			if (thd->is_writer)
				has_writers |= thd->active;
			else
				has_readers |= thd->active;
		}

		pthread_mutex_unlock(&entry->thdlist_lock);
		pthread_mutex_unlock(&devlist_lock);

		if (!has_readers && !had_readers && !has_writers) {
			struct timespec ts = {
				.tv_sec = 0,
				.tv_nsec = 1000000, /* 1 ms */
			};

			nanosleep(&ts, NULL);
			continue;
		}

		/* had_readers: if no readers were found in this loop, but we
		 * had readers in the previous iteration, chances are that new
		 * clients will ask for data soon; so we refill the buffer now,
		 * to be sure that we don't lose samples. */
		if (has_readers || had_readers) {
			ssize_t nb_bytes;

			ret = iio_buffer_refill(entry->buf);
			if (ret < 0) {
				ERROR("Reading from device failed: %i\n",
						(int) ret);
				pthread_mutex_lock(&devlist_lock);
				break;
			}

			had_readers = false;
			nb_bytes = ret;
			pthread_mutex_lock(&entry->thdlist_lock);

			/* We don't use SLIST_FOREACH here. As soon as a thread is
			 * signaled, its "thd" structure might be freed;
			 * SLIST_FOREACH would then cause a segmentation fault, as it
			 * reads "thd" to get the address of the next element. */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, next);

				if (!thd->active || thd->is_writer)
					continue;

				had_readers = true;
				ret = send_data(entry, thd, nb_bytes);
				if (ret > 0)
					thd->nb -= ret;

				if (ret < 0 || thd->nb < sample_size)
					signal_thread(thd, (ret < 0) ?
							ret : thd->nb);
			}

			pthread_mutex_unlock(&entry->thdlist_lock);
		}

		if (has_writers) {
			pthread_mutex_lock(&entry->thdlist_lock);

			/* Reset the size of the buffer to its maximum size */
			entry->buf->data_length = entry->buf->length;

			/* Same comment as above */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, next);

				if (!thd->active || !thd->is_writer)
					continue;

				ret = receive_data(entry, thd);
				if (ret > 0)
					thd->nb -= ret;

				if (ret < 0)
					signal_thread(thd, ret);
			}

			ret = iio_buffer_push(entry->buf);
			if (ret < 0) {
				ERROR("Writing to device failed: %i\n",
						(int) ret);
				pthread_mutex_lock(&devlist_lock);
				break;
			}

			/* Signal threads which completed their RW command */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, next);
				if (thd->active && thd->is_writer &&
						thd->nb < sample_size)
					signal_thread(thd, thd->nb);
			}

			pthread_mutex_unlock(&entry->thdlist_lock);
		}
	}

	/* Signal all remaining threads */
	for (thd = SLIST_FIRST(&entry->thdlist_head); thd; thd = next_thd) {
		next_thd = SLIST_NEXT(thd, next);
		SLIST_REMOVE(&entry->thdlist_head, thd, ThdEntry, next);
		signal_thread(thd, ret);
	}
	pthread_mutex_unlock(&entry->thdlist_lock);

	INFO("Stopping R/W thread for device %s\n",
			dev->name ? dev->name : dev->id);
	SLIST_REMOVE(&devlist_head, entry, DevEntry, next);

	if (entry->buf)
		iio_buffer_destroy(entry->buf);
	pthread_mutex_unlock(&devlist_lock);

	if (ret >= 0) {
		/* Signal the last client that the device has been closed */
		pthread_mutex_lock(&entry->last_lock);
		pthread_cond_signal(&entry->last_cond);

		/* And wait for the last client to give us back the lock,
		 * so that we can destroy it... */
		pthread_cond_wait(&entry->last_cond, &entry->last_lock);
		pthread_mutex_unlock(&entry->last_lock);
	}

	pthread_mutex_destroy(&entry->last_lock);
	pthread_cond_destroy(&entry->last_cond);
	pthread_mutex_destroy(&entry->thdlist_lock);
	pthread_attr_destroy(&entry->attr);

	free(entry->mask);
	free(entry);
	return NULL;
}

static ssize_t rw_buffer(struct parser_pdata *pdata,
		struct iio_device *dev, unsigned int nb, bool is_write)
{
	struct DevEntry *e, *entry = NULL;
	struct ThdEntry *t, *thd = NULL;
	ssize_t ret;

	if (!dev)
		return -ENODEV;

	pthread_mutex_lock(&devlist_lock);

	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			entry = e;
			break;
		}
	}

	if (!entry) {
		pthread_mutex_unlock(&devlist_lock);
		return -ENXIO;
	}

	if (nb < entry->sample_size) {
		pthread_mutex_unlock(&devlist_lock);
		return 0;
	}

	pthread_mutex_lock(&entry->thdlist_lock);
	SLIST_FOREACH(t, &entry->thdlist_head, next) {
		if (t->pdata == pdata) {
			thd = t;
			break;
		}
	}

	if (!thd) {
		pthread_mutex_unlock(&entry->thdlist_lock);
		pthread_mutex_unlock(&devlist_lock);
		return -ENXIO;
	}

	if (thd->nb) {
		pthread_mutex_unlock(&entry->thdlist_lock);
		pthread_mutex_unlock(&devlist_lock);
		return -EBUSY;
	}

	thd->new_client = true;
	thd->nb = nb;
	thd->err = 0;
	thd->is_writer = is_write;
	thd->active = true;

	pthread_mutex_unlock(&entry->thdlist_lock);
	pthread_mutex_unlock(&devlist_lock);

	DEBUG("Waiting for completion...\n");
	pthread_cond_wait(&thd->cond, &thd->cond_lock);
	ret = thd->err;
	pthread_mutex_unlock(&thd->cond_lock);

	fflush(thd->pdata->out);

	if (ret > 0 && ret < nb)
		print_value(thd->pdata, 0);

	DEBUG("Exiting rw_buffer with code %li\n", (long) ret);
	if (ret < 0)
		return ret;
	else
		return nb - ret;
}

static uint32_t *get_mask(const char *mask, size_t *len)
{
	size_t nb = (*len + 7) / 8;
	uint32_t *ptr, *words = calloc(nb, sizeof(*words));
	if (!words)
		return NULL;

	ptr = words + nb;
	while (*mask) {
		char buf[9];
		sprintf(buf, "%.*s", 8, mask);
		sscanf(buf, "%08x", --ptr);
		mask += 8;
		DEBUG("Mask[%lu]: 0x%08x\n",
				(unsigned long) (words - ptr) / 4, *ptr);
	}

	*len = nb;
	return words;
}

static int open_dev_helper(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic)
{
	int ret = -ENOMEM;
	struct DevEntry *e, *entry = NULL;
	struct ThdEntry *thd;
	size_t len = strlen(mask);
	uint32_t *words;
	unsigned int nb_channels;

	if (!dev)
		return -ENODEV;

	nb_channels = dev->nb_channels;
	if (len != ((nb_channels + 31) / 32) * 8)
		return -EINVAL;

	words = get_mask(mask, &len);
	if (!words)
		return -ENOMEM;

	pthread_mutex_lock(&devlist_lock);
	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			entry = e;
			break;
		}
	}

	if (entry && (cyclic || entry->cyclic)) {
		/* Only one client allowed in cyclic mode */
		ret = -EBUSY;
		goto err_free_words;
	}

	thd = malloc(sizeof(*thd));
	if (!thd)
		goto err_free_words;

	thd->wait_for_open = true;
	thd->mask = words;
	thd->nb = 0;
	thd->samples_count = samples_count;
	thd->sample_size = iio_device_get_sample_size_mask(dev, words, len);
	thd->pdata = pdata;
	pthread_cond_init(&thd->cond, NULL);
	pthread_mutex_init(&thd->cond_lock, NULL);
	pthread_mutex_lock(&thd->cond_lock);

	if (entry) {
		pthread_mutex_lock(&entry->thdlist_lock);
		SLIST_INSERT_HEAD(&entry->thdlist_head, thd, next);
		entry->update_mask = true;
		pthread_mutex_unlock(&entry->thdlist_lock);
		DEBUG("Added thread to client list\n");

		pthread_mutex_unlock(&devlist_lock);

		/* Wait until the device is opened by the rw thread */
		pthread_cond_wait(&thd->cond, &thd->cond_lock);
		pthread_mutex_unlock(&thd->cond_lock);
		return (int) thd->err;
	}

	entry = malloc(sizeof(*entry));
	if (!entry)
		goto err_free_thd;

	entry->mask = malloc(len * sizeof(*words));
	if (!entry->mask)
		goto err_free_entry;

	entry->cyclic = cyclic;
	entry->nb_words = len;
	entry->update_mask = true;
	entry->dev = dev;
	entry->buf = NULL;
	SLIST_INIT(&entry->thdlist_head);
	SLIST_INSERT_HEAD(&entry->thdlist_head, thd, next);
	DEBUG("Added thread to client list\n");

	pthread_cond_init(&entry->last_cond, NULL);
	pthread_mutex_init(&entry->last_lock, NULL);
	pthread_mutex_init(&entry->thdlist_lock, NULL);
	pthread_attr_init(&entry->attr);
	pthread_attr_setdetachstate(&entry->attr,
			PTHREAD_CREATE_DETACHED);

	ret = pthread_create(&entry->thd, &entry->attr, rw_thd, entry);
	if (ret)
		goto err_free_entry_mask;

	DEBUG("Adding new device thread to device list\n");
	SLIST_INSERT_HEAD(&devlist_head, entry, next);
	pthread_mutex_unlock(&devlist_lock);

	/* Wait until the device is opened by the rw thread */
	pthread_cond_wait(&thd->cond, &thd->cond_lock);
	pthread_mutex_unlock(&thd->cond_lock);
	return (int) thd->err;

err_free_entry_mask:
	free(entry->mask);
err_free_entry:
	free(entry);
err_free_thd:
	free(thd);
err_free_words:
	free(words);
	pthread_mutex_unlock(&devlist_lock);
	return ret;
}

static int close_dev_helper(struct parser_pdata *pdata, struct iio_device *dev)
{
	struct DevEntry *e;

	if (!dev)
		return -ENODEV;

	pthread_mutex_lock(&devlist_lock);
	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			struct ThdEntry *t;
			pthread_mutex_lock(&e->thdlist_lock);
			SLIST_FOREACH(t, &e->thdlist_head, next) {
				if (t->pdata == pdata) {
					bool last;
					pthread_mutex_t *last_lock =
						&e->last_lock;
					pthread_cond_t *last_cond =
						&e->last_cond;

					e->update_mask = true;
					SLIST_REMOVE(&e->thdlist_head,
							t, ThdEntry, next);
					last = SLIST_EMPTY(&e->thdlist_head);
					free(t->mask);
					free(t);

					if (last)
						pthread_mutex_lock(last_lock);
					pthread_mutex_unlock(&e->thdlist_lock);
					pthread_mutex_unlock(&devlist_lock);

					/* If we are the last client for this
					 * device, we wait until the R/W thread
					 * closes it. Otherwise, the CLOSE
					 * command is returned too soon, which
					 * might cause problems if the client
					 * sends a command which requires the
					 * device to be closed. */
					if (last) {
						pthread_cond_wait(last_cond,
								last_lock);
						pthread_cond_signal(last_cond);
						pthread_mutex_unlock(last_lock);
					}
					return 0;
				}
			}

			pthread_mutex_unlock(&e->thdlist_lock);
			pthread_mutex_unlock(&devlist_lock);
			return -ENXIO;
		}
	}

	pthread_mutex_unlock(&devlist_lock);
	return -EBADF;
}

int open_dev(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic)
{
	int ret = open_dev_helper(pdata, dev, samples_count, mask, cyclic);
	print_value(pdata, ret);
	return ret;
}

int close_dev(struct parser_pdata *pdata, struct iio_device *dev)
{
	int ret = close_dev_helper(pdata, dev);
	print_value(pdata, ret);
	return ret;
}

ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev,
		unsigned int nb, bool is_write)
{
	ssize_t ret = rw_buffer(pdata, dev, nb, is_write);
	if (ret <= 0 || is_write)
		print_value(pdata, ret);
	return ret;
}

ssize_t read_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, bool is_debug)
{
	/* We use a very large buffer here, as if attr is NULL all the
	 * attributes will be read, which may represents a few kilobytes worth
	 * of data. */
	char buf[0x10000];
	ssize_t ret;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	if (is_debug)
		ret = iio_device_debug_attr_read(dev, attr, buf, sizeof(buf));
	else
		ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, pdata->out);
	output(pdata, "\n");
	return ret;
}

ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, size_t len, bool is_debug)
{
	ssize_t ret = -ENOMEM;
	char *buf;

	if (!dev) {
		ret = -ENODEV;
		goto err_print_value;
	}

	buf = malloc(len);
	if (!buf)
		goto err_print_value;

	ret = read_all(buf, len, pdata->in);
	if (ret < 0)
		goto err_free_buffer;

	if (is_debug)
		ret = iio_device_debug_attr_write_raw(dev, attr, buf, len);
	else
		ret = iio_device_attr_write_raw(dev, attr, buf, len);

err_free_buffer:
	free(buf);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t read_chn_attr(struct parser_pdata *pdata,
		struct iio_channel *chn, const char *attr)
{
	char buf[1024];
	ssize_t ret = -ENODEV;

	if (chn)
		ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
	else if (pdata->dev)
		ret = -ENXIO;
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, pdata->out);
	output(pdata, "\n");
	return ret;
}

ssize_t write_chn_attr(struct parser_pdata *pdata,
		struct iio_channel *chn, const char *attr, size_t len)
{
	ssize_t ret = -ENOMEM;
	char *buf = malloc(len);
	if (!buf)
		goto err_print_value;

	ret = read_all(buf, len, pdata->in);
	if (ret < 0)
		goto err_free_buffer;

	if (chn)
		ret = iio_channel_attr_write_raw(chn, attr, buf, len);
	else if (pdata->dev)
		ret = -ENXIO;
	else
		ret = -ENODEV;
err_free_buffer:
	free(buf);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t set_trigger(struct parser_pdata *pdata,
		struct iio_device *dev, const char *trigger)
{
	struct iio_device *trig = NULL;
	ssize_t ret = -ENOENT;

	if (!dev) {
		ret = -ENODEV;
		goto err_print_value;
	}

	if (trigger) {
		trig = iio_context_find_device(pdata->ctx, trigger);
		if (!trig)
			goto err_print_value;
	}

	ret = iio_device_set_trigger(dev, trig);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t get_trigger(struct parser_pdata *pdata, struct iio_device *dev)
{
	const struct iio_device *trigger;
	ssize_t ret;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	ret = iio_device_get_trigger(dev, &trigger);
	if (!ret && trigger) {
		ret = strlen(trigger->name);
		print_value(pdata, ret);
		ret = write_all(trigger->name, ret, pdata->out);
		output(pdata, "\n");
	} else {
		print_value(pdata, ret);
	}
	return ret;
}

int set_timeout(struct parser_pdata *pdata, unsigned int timeout)
{
	int ret = iio_context_set_timeout(pdata->ctx, timeout);
	print_value(pdata, ret);
	return ret;
}

void interpreter(struct iio_context *ctx, FILE *in, FILE *out, bool verbose)
{
	yyscan_t scanner;
	struct parser_pdata pdata;
	unsigned int i;

	pdata.ctx = ctx;
	pdata.stop = false;
	pdata.in = in;
	pdata.out = out;
	pdata.verbose = verbose;

	yylex_init_extra(&pdata, &scanner);
	yyset_out(out, scanner);
	yyset_in(in, scanner);

	do {
		if (verbose) {
			output(&pdata, "iio-daemon > ");
			fflush(out);
		}
		yyparse(scanner);
	} while (!pdata.stop && !feof(in));

	yylex_destroy(scanner);

	/* Close all opened devices */
	for (i = 0; i < ctx->nb_devices; i++)
		close_dev_helper(&pdata, ctx->devices[i]);
}
