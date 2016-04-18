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

struct DevEntry;

/* Corresponds to a thread reading from a device */
struct ThdEntry {
	SLIST_ENTRY(ThdEntry) parser_list_entry;
	SLIST_ENTRY(ThdEntry) dev_list_entry;
	pthread_cond_t cond;
	unsigned int nb, sample_size, samples_count;
	ssize_t err;

	struct parser_pdata *pdata;
	struct iio_device *dev;
	struct DevEntry *entry;

	uint32_t *mask;
	bool active, is_writer, new_client, wait_for_open;
};

/* Corresponds to an opened device */
struct DevEntry {
	unsigned int ref_count;

	struct iio_device *dev;
	struct iio_buffer *buf;
	unsigned int sample_size, nb_clients;
	bool update_mask;
	bool cyclic;
	bool closed;

	/* Linked list of ThdEntry structures corresponding
	 * to all the threads who opened the device */
	SLIST_HEAD(ThdHead, ThdEntry) thdlist_head;
	pthread_mutex_t thdlist_lock;

	pthread_t thd;

	uint32_t *mask;
	size_t nb_words;
};

struct sample_cb_info {
	struct parser_pdata *pdata;
	unsigned int nb_bytes, cpt;
	uint32_t *mask;
};

/* Protects iio_device_{set,get}_data() from concurrent access from multiple
 * clients */
static pthread_mutex_t devlist_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t write_all(struct parser_pdata *pdata,
		const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;

	while (len) {
		ssize_t ret = writefd(pdata, (void *) ptr, len);
		if (ret < 0)
			return -errno;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) src;
}

static ssize_t read_all(struct parser_pdata *pdata,
		void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;

	while (len) {
		ssize_t ret = readfd(pdata, (void *) ptr, len);
		if (ret < 0)
			return -errno;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) dst;
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
	struct sample_cb_info *info = d;
	if (chn->index < 0 || !TEST_BIT(info->mask, chn->index))
		return 0;
	if (info->nb_bytes < length)
		return 0;

	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char zero = 0;
		for (i = 0; i < goal; i++)
			writefd(info->pdata, &zero, 1);
		info->cpt += goal;
	}

	info->cpt += length;
	info->nb_bytes -= length;
	return write_all(info->pdata, src, length);
}

static ssize_t receive_sample(const struct iio_channel *chn,
		void *dst, size_t length, void *d)
{
	struct sample_cb_info *info = d;
	if (chn->index < 0 || !TEST_BIT(info->mask, chn->index))
		return 0;
	if (info->cpt == info->nb_bytes)
		return 0;

	/* Skip the padding if needed */
	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char foo;
		for (i = 0; i < goal; i++)
			readfd(info->pdata, &foo, 1);
		info->cpt += goal;
	}

	info->cpt += length;
	return read_all(info->pdata, dst, length);
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
		char buf[129], *ptr = buf;
		uint32_t *mask = demux ? thd->mask : dev->mask;
		ssize_t ret;

		/* Send the current mask */
		for (i = dev->nb_words; i > 0 && ptr < buf + sizeof(buf);
				i--, ptr += 8)
			sprintf(ptr, "%08x", mask[i - 1]);

		*ptr = '\n';
		ret = write_all(pdata, buf, ptr + 1 - buf);
		if (ret < 0)
			return ret;

		thd->new_client = false;
	}

	if (!demux) {
		/* Short path */
		return write_all(pdata, dev->buf->buffer, len);
	} else {
		struct sample_cb_info info = {
			.pdata = pdata,
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

		return read_all(pdata, dev->buf->buffer, len);
	} else {
		/* Long path: Mux the samples to the buffer */

		struct sample_cb_info info = {
			.pdata = pdata,
			.cpt = 0,
			.nb_bytes = thd->nb,
			.mask = thd->mask,
		};

		return iio_buffer_foreach_sample(dev->buf,
				receive_sample, &info);
	}
}

static void dev_entry_put(struct DevEntry *entry)
{
	bool free_entry = false;

	pthread_mutex_lock(&entry->thdlist_lock);
	entry->ref_count--;
	if (entry->ref_count == 0)
		free_entry = true;
	pthread_mutex_unlock(&entry->thdlist_lock);

	if (free_entry) {
		pthread_mutex_destroy(&entry->thdlist_lock);

		free(entry->mask);
		free(entry);
	}
}

static void signal_thread(struct ThdEntry *thd, ssize_t ret)
{
	thd->err = ret;
	thd->nb = 0;
	thd->active = false;
	pthread_cond_signal(&thd->cond);
}

static void * rw_thd(void *d)
{
	struct DevEntry *entry = d;
	struct ThdEntry *thd, *next_thd;
	struct iio_device *dev = entry->dev;
	unsigned int nb_words = entry->nb_words;
	ssize_t ret = 0;
	bool had_readers = false;

	DEBUG("R/W thread started for device %s\n",
			dev->name ? dev->name : dev->id);

	while (true) {
		bool has_readers = false, has_writers = false,
		     mask_updated = false;
		unsigned int sample_size;

		/* NOTE: this while loop must exit with thdlist_lock locked. */
		pthread_mutex_lock(&entry->thdlist_lock);

		if (SLIST_EMPTY(&entry->thdlist_head))
			break;

		if (entry->update_mask) {
			unsigned int i;
			unsigned int samples_count = 0;

			memset(entry->mask, 0, nb_words * sizeof(*entry->mask));
			SLIST_FOREACH(thd, &entry->thdlist_head, dev_list_entry) {
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
			SLIST_FOREACH(thd, &entry->thdlist_head, dev_list_entry) {
				if (thd->wait_for_open) {
					thd->wait_for_open = false;
					signal_thread(thd, 0);
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

		SLIST_FOREACH(thd, &entry->thdlist_head, dev_list_entry) {
			thd->active = !thd->err && thd->nb >= sample_size;
			if (mask_updated && thd->active)
				signal_thread(thd, thd->nb);

			if (thd->is_writer)
				has_writers |= thd->active;
			else
				has_readers |= thd->active;
		}

		pthread_mutex_unlock(&entry->thdlist_lock);

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

			pthread_mutex_lock(&entry->thdlist_lock);

			if (ret < 0) {
				ERROR("Reading from device failed: %i\n",
						(int) ret);
				break;
			}

			had_readers = false;
			nb_bytes = ret;

			/* We don't use SLIST_FOREACH here. As soon as a thread is
			 * signaled, its "thd" structure might be freed;
			 * SLIST_FOREACH would then cause a segmentation fault, as it
			 * reads "thd" to get the address of the next element. */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, dev_list_entry);

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
			ssize_t nb_bytes = 0;

			pthread_mutex_lock(&entry->thdlist_lock);

			/* Reset the size of the buffer to its maximum size */
			entry->buf->data_length = entry->buf->length;

			/* Same comment as above */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, dev_list_entry);

				if (!thd->active || !thd->is_writer)
					continue;

				ret = receive_data(entry, thd);
				if (ret > 0) {
					thd->nb -= ret;
					if (ret > nb_bytes)
						nb_bytes = ret;
				}

				if (ret < 0)
					signal_thread(thd, ret);
			}

			ret = iio_buffer_push_partial(entry->buf,
					nb_bytes / sample_size);

			if (ret < 0) {
				ERROR("Writing to device failed: %i\n",
						(int) ret);
				break;
			}

			/* Signal threads which completed their RW command */
			for (thd = SLIST_FIRST(&entry->thdlist_head);
					thd; thd = next_thd) {
				next_thd = SLIST_NEXT(thd, dev_list_entry);
				if (thd->active && thd->is_writer &&
						thd->nb < sample_size)
					signal_thread(thd, thd->nb);
			}

			pthread_mutex_unlock(&entry->thdlist_lock);
		}
	}

	/* Signal all remaining threads */
	for (thd = SLIST_FIRST(&entry->thdlist_head); thd; thd = next_thd) {
		next_thd = SLIST_NEXT(thd, dev_list_entry);
		SLIST_REMOVE(&entry->thdlist_head, thd, ThdEntry, dev_list_entry);
		thd->wait_for_open = false;
		signal_thread(thd, ret);
	}
	if (entry->buf)
		iio_buffer_destroy(entry->buf);
	entry->closed = true;
	pthread_mutex_unlock(&entry->thdlist_lock);

	pthread_mutex_lock(&devlist_lock);
	/* It is possible that a new thread has already started, make sure to
	 * not overwrite it. */
	if (iio_device_get_data(dev) == entry)
		iio_device_set_data(dev, NULL);
	pthread_mutex_unlock(&devlist_lock);

	DEBUG("Stopping R/W thread for device %s\n",
			dev->name ? dev->name : dev->id);

	dev_entry_put(entry);

	return NULL;
}

static struct ThdEntry *parser_lookup_thd_entry(struct parser_pdata *pdata,
	struct iio_device *dev)
{
	struct ThdEntry *t;

	SLIST_FOREACH(t, &pdata->thdlist_head, parser_list_entry) {
		if (t->dev == dev)
			return t;
	}

	return NULL;
}

static ssize_t rw_buffer(struct parser_pdata *pdata,
		struct iio_device *dev, unsigned int nb, bool is_write)
{
	struct DevEntry *entry;
	struct ThdEntry *thd;
	ssize_t ret;

	if (!dev)
		return -ENODEV;

	thd = parser_lookup_thd_entry(pdata, dev);
	if (!thd)
		return -EBADF;

	entry = thd->entry;

	if (nb < entry->sample_size)
		return 0;

	pthread_mutex_lock(&entry->thdlist_lock);
	if (entry->closed) {
		pthread_mutex_unlock(&entry->thdlist_lock);
		return -EBADF;
	}

	if (thd->nb) {
		pthread_mutex_unlock(&entry->thdlist_lock);
		return -EBUSY;
	}

	thd->new_client = true;
	thd->nb = nb;
	thd->err = 0;
	thd->is_writer = is_write;
	thd->active = true;

	DEBUG("Waiting for completion...\n");
	while (thd->active)
		pthread_cond_wait(&thd->cond, &entry->thdlist_lock);
	ret = thd->err;
	pthread_mutex_unlock(&entry->thdlist_lock);

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

static void free_thd_entry(struct ThdEntry *t)
{
	pthread_cond_destroy(&t->cond);
	free(t->mask);
	free(t);
}

static void remove_thd_entry(struct ThdEntry *t)
{
	struct DevEntry *entry = t->entry;

	pthread_mutex_lock(&entry->thdlist_lock);
	if (!entry->closed) {
		entry->update_mask = true;
		SLIST_REMOVE(&entry->thdlist_head, t, ThdEntry, dev_list_entry);
	}
	pthread_mutex_unlock(&entry->thdlist_lock);
	dev_entry_put(entry);

	free_thd_entry(t);
}

static int open_dev_helper(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic)
{
	pthread_attr_t attr;
	int ret = -ENOMEM;
	struct DevEntry *entry;
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

	thd = zalloc(sizeof(*thd));
	if (!thd)
		goto err_free_words;

	thd->wait_for_open = true;
	thd->mask = words;
	thd->nb = 0;
	thd->samples_count = samples_count;
	thd->sample_size = iio_device_get_sample_size_mask(dev, words, len);
	thd->pdata = pdata;
	thd->dev = dev;
	pthread_cond_init(&thd->cond, NULL);

	/* Atomically look up the thread and make sure that it is still active
	 * or allocate new one. */
	pthread_mutex_lock(&devlist_lock);
	entry = iio_device_get_data(dev);
	if (entry) {
		pthread_mutex_lock(&entry->thdlist_lock);
		if (!entry->closed) {
			pthread_mutex_unlock(&devlist_lock);

			if (cyclic || entry->cyclic) {
				pthread_mutex_unlock(&entry->thdlist_lock);
				/* Only one client allowed in cyclic mode */
				ret = -EBUSY;
				goto err_free_thd;
			}

			entry->ref_count++;

			SLIST_INSERT_HEAD(&entry->thdlist_head, thd, dev_list_entry);
			thd->entry = entry;
			entry->update_mask = true;
			DEBUG("Added thread to client list\n");

			/* Wait until the device is opened by the rw thread */
			while (thd->wait_for_open)
				pthread_cond_wait(&thd->cond, &entry->thdlist_lock);
			pthread_mutex_unlock(&entry->thdlist_lock);

			ret = (int) thd->err;
			if (ret < 0)
				remove_thd_entry(thd);
			else
				SLIST_INSERT_HEAD(&pdata->thdlist_head, thd, parser_list_entry);
			return ret;
		} else {
			pthread_mutex_unlock(&entry->thdlist_lock);
		}
	}

	entry = zalloc(sizeof(*entry));
	if (!entry) {
		pthread_mutex_unlock(&devlist_lock);
		goto err_free_thd;
	}

	entry->ref_count = 2; /* One for thread, one for the client */

	entry->mask = malloc(len * sizeof(*words));
	if (!entry->mask) {
		pthread_mutex_unlock(&devlist_lock);
		goto err_free_entry;
	}

	entry->cyclic = cyclic;
	entry->nb_words = len;
	entry->update_mask = true;
	entry->dev = dev;
	entry->buf = NULL;
	SLIST_INIT(&entry->thdlist_head);
	SLIST_INSERT_HEAD(&entry->thdlist_head, thd, dev_list_entry);
	thd->entry = entry;
	DEBUG("Added thread to client list\n");

	pthread_mutex_init(&entry->thdlist_lock, NULL);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	ret = pthread_create(&entry->thd, &attr, rw_thd, entry);
	pthread_attr_destroy(&attr);
	if (ret) {
		pthread_mutex_unlock(&devlist_lock);
		goto err_free_entry_mask;
	}

	DEBUG("Adding new device thread to device list\n");
	iio_device_set_data(dev, entry);
	pthread_mutex_unlock(&devlist_lock);

	pthread_mutex_lock(&entry->thdlist_lock);
	/* Wait until the device is opened by the rw thread */
	while (thd->wait_for_open)
		pthread_cond_wait(&thd->cond, &entry->thdlist_lock);
	pthread_mutex_unlock(&entry->thdlist_lock);

	ret = (int) thd->err;
	if (ret < 0)
		remove_thd_entry(thd);
	else
		SLIST_INSERT_HEAD(&pdata->thdlist_head, thd, parser_list_entry);
	return ret;

err_free_entry_mask:
	free(entry->mask);
err_free_entry:
	free(entry);
err_free_thd:
	pthread_cond_destroy(&thd->cond);
	free(thd);
err_free_words:
	free(words);
	return ret;
}

static int close_dev_helper(struct parser_pdata *pdata, struct iio_device *dev)
{
	struct ThdEntry *t;

	if (!dev)
		return -ENODEV;

	t = parser_lookup_thd_entry(pdata, dev);
	if (!t)
		return -ENXIO;

	SLIST_REMOVE(&pdata->thdlist_head, t, ThdEntry, parser_list_entry);
	remove_thd_entry(t);

	return 0;
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
		ret = iio_device_debug_attr_read(dev,
				attr, buf, sizeof(buf) - 1);
	else
		ret = iio_device_attr_read(dev, attr, buf, sizeof(buf) - 1);
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	buf[ret] = '\n';
	return write_all(pdata, buf, ret + 1);
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

	ret = read_all(pdata, buf, len);
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
		ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf) - 1);
	else if (pdata->dev)
		ret = -ENXIO;
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	buf[ret] = '\n';
	return write_all(pdata, buf, ret + 1);
}

ssize_t write_chn_attr(struct parser_pdata *pdata,
		struct iio_channel *chn, const char *attr, size_t len)
{
	ssize_t ret = -ENOMEM;
	char *buf = malloc(len);
	if (!buf)
		goto err_print_value;

	ret = read_all(pdata, buf, len);
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
		char buf[256];

		ret = strlen(trigger->name);
		print_value(pdata, ret);

		snprintf(buf, sizeof(buf), "%s\n", trigger->name);
		ret = write_all(pdata, buf, ret + 1);
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

int set_buffers_count(struct parser_pdata *pdata,
		struct iio_device *dev, long value)
{
	int ret = -EINVAL;

	if (value >= 1)
		ret = iio_device_set_kernel_buffers_count(
				dev, (unsigned int) value);

	print_value(pdata, ret);
	return ret;
}

ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len)
{
	ssize_t ret;

	if (pdata->fd_in_is_socket) {
		/* First read from the socket, without advancing the
		 * read offset */
		ret = recv(pdata->fd_in, buf, len, MSG_NOSIGNAL | MSG_PEEK);
		if (ret > 0) {
			size_t i;

			/* Lookup for the trailing \n */
			for (i = 0; i < (size_t) ret && buf[i] != '\n'; i++);

			/* No \n found? Just garbage data */
			if (i == (size_t) ret) {
				errno = EIO;
				return -1;
			}

			/* Advance the read offset to the byte following
			 * the \n */
			ret = recv(pdata->fd_in, buf, i + 1,
					MSG_NOSIGNAL | MSG_TRUNC);
		}
	} else {
		ret = read(pdata->fd_in, buf, len);
	}

	return ret;
}

void interpreter(struct iio_context *ctx, int fd_in, int fd_out, bool verbose,
	bool is_socket)
{
	yyscan_t scanner;
	struct parser_pdata pdata;
	unsigned int i;

	pdata.ctx = ctx;
	pdata.stop = false;
	pdata.fd_in = fd_in;
	pdata.fd_out = fd_out;
	pdata.verbose = verbose;

	pdata.fd_in_is_socket = is_socket;
	pdata.fd_out_is_socket = is_socket;

	SLIST_INIT(&pdata.thdlist_head);

	yylex_init_extra(&pdata, &scanner);

	do {
		if (verbose)
			output(&pdata, "iio-daemon > ");
		yyparse(scanner);
	} while (!pdata.stop);

	yylex_destroy(scanner);

	/* Close all opened devices */
	for (i = 0; i < ctx->nb_devices; i++)
		close_dev_helper(&pdata, ctx->devices[i]);
}
