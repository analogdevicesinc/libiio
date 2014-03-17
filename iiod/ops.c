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
#include "../iio.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>

/* No more than 8 samples per read (arbitrary) */
#define SAMPLES_PER_READ 8

int yyparse(yyscan_t scanner);

/* Corresponds to a thread reading from a device */
struct ThdEntry {
	SLIST_ENTRY(ThdEntry) next;
	pthread_cond_t cond;
	pthread_mutex_t cond_lock;
	unsigned int nb;
	ssize_t err;
	struct parser_pdata *pdata;

	uint32_t *mask;
	bool reader;
};

/* Corresponds to an opened device */
struct DevEntry {
	SLIST_ENTRY(DevEntry) next;

	struct iio_device *dev;
	unsigned int sample_size, nb_clients;
	bool update_mask;

	/* Linked list of ThdEntry structures corresponding
	 * to all the threads who opened the device */
	SLIST_HEAD(ThdHead, ThdEntry) thdlist_head;
	pthread_mutex_t thdlist_lock;

	pthread_t thd;
	pthread_attr_t attr;

	uint32_t *mask;
	size_t nb_words;
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
		ssize_t ret = fwrite(ptr, 1, len, out);
		if (ret == 0)
			return -EIO;
		ptr += ret;
		len -= ret;
	}
	return ptr - src;
}

static void print_value(struct parser_pdata *pdata, long value)
{
	if (pdata->verbose && value < 0) {
		char buf[1024];
		strerror_r(-value, buf, sizeof(buf));
		fprintf(pdata->out, "ERROR: %s\n", buf);
	} else {
		fprintf(pdata->out, "%li\n", value);
	}
}

static void * read_thd(void *d)
{
	struct DevEntry *entry = d;
	struct ThdEntry *thd;
	unsigned int nb_words = entry->nb_words;
	ssize_t ret = 0;

	DEBUG("Read thread started\n");

	while (true) {
		char *buf;
		struct ThdEntry *next_thd;
		bool has_readers = false;
		unsigned int nb_bytes, sample_size;

		pthread_mutex_lock(&devlist_lock);

		/* We do this check here, because this while loop must exit
		 * with devlist_lock locked and thdlist_lock unlocked. */
		if (ret < 0)
			break;

		pthread_mutex_lock(&entry->thdlist_lock);
		if (SLIST_EMPTY(&entry->thdlist_head)) {
			pthread_mutex_unlock(&entry->thdlist_lock);
			break;
		}

		if (entry->update_mask) {
			memset(entry->mask, 0, nb_words);
			SLIST_FOREACH(thd, &entry->thdlist_head, next) {
				unsigned int i;
				for (i = 0; i < nb_words; i++)
					entry->mask[i] |= thd->mask[i];
			}

			iio_device_close(entry->dev);
			ret = iio_device_open_mask(entry->dev,
					entry->mask, nb_words);
			if (ret < 0) {
				pthread_mutex_unlock(&entry->thdlist_lock);
				break;
			}

			DEBUG("IIO device %s reopened with new mask\n",
					iio_device_get_id(entry->dev));
			entry->update_mask = false;

			entry->sample_size = iio_device_get_sample_size(
					entry->dev, entry->mask, nb_words);
		}

		sample_size = entry->sample_size;
		nb_bytes = sample_size * SAMPLES_PER_READ;

		SLIST_FOREACH(thd, &entry->thdlist_head, next) {
			thd->reader = thd->nb > 0;
			has_readers |= thd->reader;
			if (thd->reader && thd->nb < nb_bytes)
				nb_bytes =
					(thd->nb / sample_size) * sample_size;
		}

		pthread_mutex_unlock(&entry->thdlist_lock);

		if (!has_readers) {
			struct timespec ts = {
				.tv_sec = 0,
				.tv_nsec = 1000000, /* 1 ms */
			};

			pthread_mutex_unlock(&devlist_lock);
			nanosleep(&ts, NULL);
			continue;
		}

		buf = malloc(nb_bytes);
		if (!buf) {
			ret = -ENOMEM;
			break;
		}

		pthread_mutex_unlock(&devlist_lock);

		DEBUG("Reading %u bytes from device\n", nb_bytes);
		ret = iio_device_read_raw(entry->dev, buf, nb_bytes);
		pthread_mutex_lock(&entry->thdlist_lock);

		/* We don't use SLIST_FOREACH here. As soon as a thread is
		 * signaled, its "thd" structure might be freed;
		 * SLIST_FOREACH would then cause a segmentation fault, as it
		 * reads "thd" to get the address of the next element. */
		for (thd = SLIST_FIRST(&entry->thdlist_head);
				thd; thd = next_thd) {
			unsigned int i;
			ssize_t ret2;
			FILE *out = thd->pdata->out;

			next_thd = SLIST_NEXT(thd, next);

			if (!thd->reader)
				continue;

			print_value(thd->pdata, ret);
			DEBUG("Integer written: %li\n", (long) ret);
			if (ret < 0)
				continue;

			/* Send the current mask */
			for (i = nb_words; i > 0; i--)
				fprintf(out, "%08x", entry->mask[i - 1]);
			fputc('\n', out);

			/* Send the raw data */
			ret2 = write_all(buf, ret, out);
			if (ret2 > 0)
				thd->nb -= ret2;
			if (ret2 < 0)
				thd->err = ret2;
			else if (thd->nb == 0)
				thd->err = 0;

			if (ret2 < 0 || thd->nb == 0) {
				/* Ensure that the client thread
				 * is already waiting */
				pthread_mutex_lock(&thd->cond_lock);
				pthread_mutex_unlock(&thd->cond_lock);
				pthread_cond_signal(&thd->cond);
			}
		}

		pthread_mutex_unlock(&entry->thdlist_lock);
		free(buf);
	}

	/* Signal all remaining threads */
	pthread_mutex_lock(&entry->thdlist_lock);
	SLIST_FOREACH(thd, &entry->thdlist_head, next) {
		SLIST_REMOVE(&entry->thdlist_head, thd,
				ThdEntry, next);
		if (ret < 0)
			thd->err = ret;

		/* Ensure that the client thread
		 * is already waiting */
		pthread_mutex_lock(&thd->cond_lock);
		pthread_mutex_unlock(&thd->cond_lock);
		pthread_cond_signal(&thd->cond);
	}
	pthread_mutex_unlock(&entry->thdlist_lock);

	DEBUG("Removing device %s from list\n",
			iio_device_get_id(entry->dev));
	SLIST_REMOVE(&devlist_head, entry, DevEntry, next);

	iio_device_close(entry->dev);
	pthread_mutex_unlock(&devlist_lock);

	pthread_mutex_destroy(&entry->thdlist_lock);
	pthread_attr_destroy(&entry->attr);
	free(entry->mask);
	free(entry);

	DEBUG("Thread terminated\n");
	return NULL;
}

static ssize_t read_buffer(struct parser_pdata *pdata,
		struct iio_device *dev, unsigned int nb)
{
	struct DevEntry *e, *entry = NULL;
	struct ThdEntry *t, *thd = NULL;
	ssize_t ret;

	if (!pdata->opened)
		return -EBADF;

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

	thd->nb = nb;
	thd->err = 0;

	pthread_mutex_unlock(&entry->thdlist_lock);
	pthread_mutex_unlock(&devlist_lock);

	DEBUG("Waiting for completion...\n");
	pthread_cond_wait(&thd->cond, &thd->cond_lock);

	fflush(thd->pdata->out);

	ret = thd->err;

	DEBUG("Exiting read_buffer with code %li\n", (long) ret);
	if (ret < 0)
		return ret;
	else
		return nb;
}

static struct iio_device * get_device(struct iio_context *ctx, const char *id)
{
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		if (!strcmp(id, iio_device_get_id(dev))
				|| (name && !strcmp(id, name)))
			return dev;
	}

	return NULL;
}

static struct iio_channel * get_channel(const struct iio_device *dev,
		const char *id)
{
	unsigned i, nb_channels = iio_device_get_channels_count(dev);
	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		const char *name = iio_channel_get_name(chn);
		if (!strcmp(id, iio_channel_get_id(chn))
				|| (name && !strcmp(id, name)))
			return chn;
	}

	return NULL;
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

static int open_dev_helper(struct parser_pdata *pdata,
		struct iio_device *dev, const char *id, const char *mask)
{
	int ret = -ENOMEM;
	struct DevEntry *e, *entry = NULL;
	struct ThdEntry *thd;
	size_t len = strlen(mask);
	uint32_t *words;
	unsigned int nb_channels = iio_device_get_channels_count(dev);

	if (pdata->opened)
		return -EBUSY;
	else if (len != ((nb_channels + 31) / 32) * 8)
		return -EINVAL;

	words = get_mask(mask, &len);
	if (!words)
		return -ENOMEM;

	if (nb_channels % 32)
		words[nb_channels / 32] &= (1 << (nb_channels % 32)) - 1;

	pthread_mutex_lock(&devlist_lock);
	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			entry = e;
			break;
		}
	}

	thd = malloc(sizeof(*thd));
	if (!thd)
		goto err_free_words;

	thd->mask = words;
	thd->nb = 0;
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

		pdata->opened = true;
		pthread_mutex_unlock(&devlist_lock);
		return 0;
	}

	entry = malloc(sizeof(*entry));
	if (!entry)
		goto err_free_thd;

	entry->mask = malloc(len * sizeof(*words));
	if (!entry->mask)
		goto err_free_entry;

	entry->sample_size = iio_device_get_sample_size(dev, words, len);
	if (!entry->sample_size)
		goto err_free_entry_mask;

	memcpy(entry->mask, words, len * sizeof(*words));

	ret = iio_device_open_mask(dev, words, len);
	if (ret)
		goto err_free_entry_mask;

	entry->nb_words = len;
	entry->update_mask = false;
	entry->dev = dev;
	SLIST_INIT(&entry->thdlist_head);
	SLIST_INSERT_HEAD(&entry->thdlist_head, thd, next);
	DEBUG("Added thread to client list\n");

	pthread_mutex_init(&entry->thdlist_lock, NULL);
	pthread_attr_init(&entry->attr);
	pthread_attr_setdetachstate(&entry->attr,
			PTHREAD_CREATE_DETACHED);

	ret = pthread_create(&entry->thd, &entry->attr,
			read_thd, entry);
	if (ret)
		goto err_iio_close;

	pdata->opened = true;

	DEBUG("Adding new device thread to device list\n");
	SLIST_INSERT_HEAD(&devlist_head, entry, next);
	pthread_mutex_unlock(&devlist_lock);

	return 0;

err_iio_close:
	iio_device_close(dev);
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

	if (!pdata->opened)
		return -EBADF;

	pthread_mutex_lock(&devlist_lock);
	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			struct ThdEntry *t;
			pthread_mutex_lock(&e->thdlist_lock);
			SLIST_FOREACH(t, &e->thdlist_head, next) {
				if (t->pdata == pdata) {
					e->update_mask = true;
					SLIST_REMOVE(&e->thdlist_head,
							t, ThdEntry, next);
					free(t->mask);
					free(t);
					pthread_mutex_unlock(&e->thdlist_lock);
					pthread_mutex_unlock(&devlist_lock);
					pdata->opened = false;
					return 0;
				}
			}

			pthread_mutex_unlock(&e->thdlist_lock);
			pthread_mutex_unlock(&devlist_lock);
			return -ENXIO;
		}
	}

	pthread_mutex_unlock(&devlist_lock);
	return -ENODEV;
}

int open_dev(struct parser_pdata *pdata, const char *id, const char *mask)
{
	int ret = -ENODEV;
	struct iio_device *dev = get_device(pdata->ctx, id);
	if (dev)
		ret = open_dev_helper(pdata, dev, id, mask);
	print_value(pdata, ret);
	return ret;
}

int close_dev(struct parser_pdata *pdata, const char *id)
{
	int ret = -ENODEV;
	struct iio_device *dev = get_device(pdata->ctx, id);
	if (dev)
		ret = close_dev_helper(pdata, dev);
	print_value(pdata, ret);
	return ret;
}

ssize_t read_dev(struct parser_pdata *pdata, const char *id, unsigned int nb)
{
	struct iio_device *dev = get_device(pdata->ctx, id);
	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	return read_buffer(pdata, dev, nb);
}

ssize_t read_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr)
{
	FILE *out = pdata->out;
	struct iio_device *dev = get_device(pdata->ctx, id);
	char buf[1024];
	ssize_t ret = -ENODEV;

	if (dev)
		ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, out);
	fputc('\n', out);
	return ret;
}

ssize_t write_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr, const char *value)
{
	struct iio_device *dev = get_device(pdata->ctx, id);
	size_t ret = -ENODEV;

	if (dev)
		ret = iio_device_attr_write(dev, attr, value);
	print_value(pdata, ret);
	return ret;
}

ssize_t read_chn_attr(struct parser_pdata *pdata, const char *id,
		const char *channel, const char *attr)
{
	FILE *out = pdata->out;
	char buf[1024];
	ssize_t ret = -ENODEV;
	struct iio_channel *chn = NULL;
	struct iio_device *dev = get_device(pdata->ctx, id);

	if (dev)
		chn = get_channel(dev, channel);
	if (chn)
		ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, out);
	fputc('\n', out);
	return ret;
}

ssize_t write_chn_attr(struct parser_pdata *pdata, const char *id,
		const char *channel, const char *attr, const char *value)
{
	ssize_t ret = -ENODEV;
	struct iio_channel *chn = NULL;
	struct iio_device *dev = get_device(pdata->ctx, id);

	if (dev)
		chn = get_channel(dev, channel);
	if (chn)
		ret = iio_channel_attr_write(chn, attr, value);
	print_value(pdata, ret);
	return ret;
}

ssize_t set_trigger(struct parser_pdata *pdata,
		const char *id, const char *trigger)
{
	ssize_t ret = -ENODEV;
	struct iio_device *dev = get_device(pdata->ctx, id);

	if (dev) {
		if (!trigger) {
			ret = iio_device_set_trigger(dev, NULL);
		} else {
			struct iio_device *trig;
			trig = get_device(pdata->ctx, trigger);
			if (trig)
				ret = iio_device_set_trigger(dev, trig);
			else
				ret = -ENXIO;
		}
	}
	print_value(pdata, ret);
	return ret;
}

ssize_t get_trigger(struct parser_pdata *pdata, const char *id)
{
	ssize_t ret = -ENODEV;
	const struct iio_device *trigger, *dev = get_device(pdata->ctx, id);

	if (dev)
		ret = iio_device_get_trigger(dev, &trigger);
	if (!ret && trigger) {
		const char *name = iio_device_get_name(trigger);
		ret = strlen(name);
		print_value(pdata, ret);
		ret = write_all(name, ret, pdata->out);
		fputc('\n', pdata->out);
	} else {
		print_value(pdata, ret);
	}
	return ret;
}

void interpreter(struct iio_context *ctx, FILE *in, FILE *out, bool verbose)
{
	yyscan_t scanner;
	struct parser_pdata pdata;

	pdata.ctx = ctx;
	pdata.stop = false;
	pdata.in = in;
	pdata.out = out;
	pdata.verbose = verbose;
	pdata.opened = false;

	yylex_init_extra(&pdata, &scanner);
	yyset_out(out, scanner);
	yyset_in(in, scanner);

	do {
		if (verbose) {
			fprintf(out, "iio-daemon > ");
			fflush(out);
		}
		yyparse(scanner);
		if (pdata.stop)
			break;
	} while (!feof(in));

	yylex_destroy(scanner);
}
