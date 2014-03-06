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

int yyparse(yyscan_t scanner);

/* Corresponds to a thread reading from a device */
struct ThdEntry {
	SLIST_ENTRY(ThdEntry) next;
	pthread_cond_t cond;
	pthread_mutex_t cond_lock;
	unsigned int nb;
	ssize_t err;
	FILE *fd;
	bool verbose;
};

/* Corresponds to an opened device */
struct DevEntry {
	SLIST_ENTRY(DevEntry) next;

	struct iio_device *dev;
	unsigned int sample_size;

	/* Linked list of ThdEntry structures corresponding
	 * to all the threads trying to read data */
	SLIST_HEAD(ThdHead, ThdEntry) thdlist_head;
	pthread_mutex_t thdlist_lock;

	pthread_t thd;
	pthread_attr_t attr;
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

static void * read_thd(void *d)
{
	struct DevEntry *entry = d;
	struct ThdEntry *thd;
	unsigned int sample_size = entry->sample_size;
	unsigned int timeout_ms = TIMEOUT_MS;
	ssize_t ret = 0;

	/* No more than 1024 bytes per read (arbitrary) */
	unsigned int max_size = 1024 / sample_size;

	DEBUG("Read thread started\n");

	while (true) {
		char *buf;
		unsigned long len, nb_samples = max_size;
		struct ThdEntry *next_thd;

		pthread_mutex_lock(&devlist_lock);

		/* We do this check here, because this while loop must exit
		 * with devlist_lock locked and thdlist_lock unlocked. */
		if (ret < 0)
			break;

		pthread_mutex_lock(&entry->thdlist_lock);
		if (SLIST_EMPTY(&entry->thdlist_head)) {
			if (!--timeout_ms) {
				pthread_mutex_unlock(&entry->thdlist_lock);
				break;
			} else {
				struct timespec ts = {
					.tv_sec = 0,
					.tv_nsec = 1000000, /* 1 ms */
				};

				pthread_mutex_unlock(&entry->thdlist_lock);
				pthread_mutex_unlock(&devlist_lock);
				nanosleep(&ts, NULL);
				continue;
			}
		} else {
			/* Reset the timeout */
			timeout_ms = TIMEOUT_MS;
		}

		SLIST_FOREACH(thd, &entry->thdlist_head, next) {
			if (thd->nb < nb_samples)
				nb_samples = thd->nb;
		}

		pthread_mutex_unlock(&entry->thdlist_lock);

		len = nb_samples * sample_size;
		buf = malloc(len);
		if (!buf) {
			ret = -ENOMEM;
			break;
		}

		pthread_mutex_unlock(&devlist_lock);

		DEBUG("Reading %lu bytes from device\n", len);
		ret = iio_device_read_raw(entry->dev, buf, len);

		pthread_mutex_lock(&entry->thdlist_lock);
		nb_samples = ret / sample_size;

		/* We don't use SLIST_FOREACH here. As soon as a thread is
		 * signaled, its "thd" structure might be freed;
		 * SLIST_FOREACH would then cause a segmentation fault, as it
		 * reads "thd" to get the address of the next element. */
		for (thd = SLIST_FIRST(&entry->thdlist_head);
				thd; thd = next_thd) {
			size_t ret2;

			next_thd = SLIST_NEXT(thd, next);

			if (!thd->verbose) {
				fprintf(thd->fd, "%li\n", (long) ret);
			} else if (ret < 0) {
				char err_buf[1024];
				strerror_r(ret, err_buf, sizeof(err_buf));
				fprintf(thd->fd, "ERROR reading device: %s\n",
						err_buf);
			}
			DEBUG("Integer written: %li\n", (long) ret);
			if (ret < 0)
				continue;

			/* (nb_samples > thd->nb) may happen when
			 * the thread just connected. In this case we'll feed
			 * it with data on the next iteration. */
			if (nb_samples > thd->nb)
				continue;

			ret2 = write_all(buf, ret, thd->fd);
			if (ret2 > 0)
				thd->nb -= ret2 / sample_size;
			if (ret2 < 0)
				thd->err = ret2;
			else if (thd->nb == 0)
				thd->err = 0;

			if (ret2 < 0 || thd->nb == 0) {
				SLIST_REMOVE(&entry->thdlist_head, thd,
						ThdEntry, next);

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
	free(entry);

	DEBUG("Thread terminated\n");
	return NULL;
}

static ssize_t read_buffer(struct parser_pdata *pdata, struct iio_device *dev,
		unsigned int nb, unsigned int sample_size)
{
	struct DevEntry *e, *entry = NULL;
	struct ThdEntry *thd;
	ssize_t ret;

	pthread_mutex_lock(&devlist_lock);

	SLIST_FOREACH(e, &devlist_head, next) {
		if (e->dev == dev) {
			entry = e;
			break;
		}
	}

	/* Ensure that two threads reading the same device
	 * use the same sample size */
	if (entry && (entry->sample_size != sample_size)) {
		pthread_mutex_unlock(&devlist_lock);
		return -EINVAL;
	}

	thd = malloc(sizeof(*thd));
	if (!thd) {
		pthread_mutex_unlock(&devlist_lock);
		return -ENOMEM;
	}

	/* !entry: no DevEntry in the linked list corresponding to the
	 *         device, so we add one */
	if (!entry) {
		int ret;

		entry = malloc(sizeof(*entry));
		if (!entry) {
			free(thd);
			pthread_mutex_unlock(&devlist_lock);
			return -ENOMEM;
		}

		ret = iio_device_open(dev);
		if (ret) {
			free(entry);
			free(thd);
			pthread_mutex_unlock(&devlist_lock);
			return ret;
		}

		entry->dev = dev;
		entry->sample_size = sample_size;
		SLIST_INIT(&entry->thdlist_head);
		pthread_mutex_init(&entry->thdlist_lock, NULL);
		pthread_attr_init(&entry->attr);
		pthread_attr_setdetachstate(&entry->attr,
				PTHREAD_CREATE_DETACHED);

		ret = pthread_create(&entry->thd, &entry->attr,
				read_thd, entry);
		if (ret) {
			iio_device_close(dev);
			free(entry);
			free(thd);
			pthread_mutex_unlock(&devlist_lock);
			return ret;
		}

		DEBUG("Adding new device thread to device list\n");
		SLIST_INSERT_HEAD(&devlist_head, entry, next);
	}

	thd->nb = nb;
	thd->fd = pdata->out;
	thd->verbose = pdata->verbose;
	pthread_cond_init(&thd->cond, NULL);
	pthread_mutex_init(&thd->cond_lock, NULL);
	pthread_mutex_lock(&thd->cond_lock);

	DEBUG("Added thread to client list\n");
	pthread_mutex_lock(&entry->thdlist_lock);
	SLIST_INSERT_HEAD(&entry->thdlist_head, thd, next);
	pthread_mutex_unlock(&entry->thdlist_lock);
	pthread_mutex_unlock(&devlist_lock);

	DEBUG("Waiting for completion...\n");
	pthread_cond_wait(&thd->cond, &thd->cond_lock);

	fflush(thd->fd);

	ret = thd->err;
	free(thd);

	DEBUG("Exiting read_buffer\n");

	if (ret < 0)
		return ret;
	else
		return nb * sample_size;
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

ssize_t read_dev(struct parser_pdata *pdata, const char *id,
		unsigned int nb, unsigned int sample_size)
{
	struct iio_device *dev = get_device(pdata->ctx, id);
	if (!dev) {
		if (pdata->verbose) {
			char buf[1024];
			strerror_r(ENODEV, buf, sizeof(buf));
			fprintf(pdata->out, "ERROR: %s\n", buf);
		} else {
			fprintf(pdata->out, "%i\n", -ENODEV);
		}
		return -ENODEV;
	}

	return read_buffer(pdata, dev, nb, sample_size);
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

	if (pdata->verbose && ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		fprintf(out, "ERROR: %s\n", buf);
	} else {
		fprintf(out, "%li\n", (long) ret);
	}
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, out);
	fputc('\n', out);
	return ret;
}

ssize_t write_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr, const char *value)
{
	FILE *out = pdata->out;
	struct iio_device *dev = get_device(pdata->ctx, id);
	size_t ret = -ENODEV;

	if (dev)
		ret = iio_device_attr_write(dev, attr, value);

	if (pdata->verbose && ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		fprintf(out, "ERROR: %s\n", buf);
	} else {
		fprintf(out, "%li\n", (long) ret);
	}
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

	if (pdata->verbose && ret < 0) {
		strerror_r(-ret, buf, sizeof(buf));
		fprintf(out, "ERROR: %s\n", buf);
	} else {
		fprintf(out, "%li\n", (long) ret);
	}

	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, out);
	fputc('\n', out);
	return ret;
}

ssize_t write_chn_attr(struct parser_pdata *pdata, const char *id,
		const char *channel, const char *attr, const char *value)
{
	FILE *out = pdata->out;
	ssize_t ret = -ENODEV;
	struct iio_channel *chn = NULL;
	struct iio_device *dev = get_device(pdata->ctx, id);

	if (dev)
		chn = get_channel(dev, channel);
	if (chn)
		ret = iio_channel_attr_write(chn, attr, value);

	if (pdata->verbose && ret < 0) {
		char buf[1024];
		strerror_r(-ret, buf, sizeof(buf));
		fprintf(out, "ERROR: %s\n", buf);
	} else {
		fprintf(out, "%li\n", (long) ret);
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
