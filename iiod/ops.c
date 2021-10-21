// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "ops.h"
#include "parser.h"
#include "thread-pool.h"
#include "../debug.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

int yyparse(yyscan_t scanner);

struct DevEntry;

/* Corresponds to a thread reading from a device */
struct ThdEntry {
	SLIST_ENTRY(ThdEntry) parser_list_entry;
	SLIST_ENTRY(ThdEntry) dev_list_entry;
	unsigned int nb, sample_size, samples_count;
	ssize_t err;

	int eventfd;

	struct parser_pdata *pdata;
	struct iio_device *dev;
	struct DevEntry *entry;

	uint32_t *mask;
	bool active, is_writer, new_client, wait_for_open;
};

static void thd_entry_event_signal(struct ThdEntry *thd)
{
	uint64_t e = 1;
	int ret;

	do {
		ret = write(thd->eventfd, &e, sizeof(e));
	} while (ret == -1 && errno == EINTR);
}

static int thd_entry_event_wait(struct ThdEntry *thd, pthread_mutex_t *mutex,
	int fd_in)
{
	struct pollfd pfd[3];
	uint64_t e;
	int ret;

	pthread_mutex_unlock(mutex);

	pfd[0].fd = thd->eventfd;
	pfd[0].events = POLLIN;
	pfd[1].fd = fd_in;
	pfd[1].events = POLLRDHUP;
	pfd[2].fd = thread_pool_get_poll_fd(thd->pdata->pool);
	pfd[2].events = POLLIN;

	do {
		poll_nointr(pfd, 3);

		if ((pfd[1].revents & POLLRDHUP) || (pfd[2].revents & POLLIN)) {
			pthread_mutex_lock(mutex);
			return -EPIPE;
		}

		do {
			ret = read(thd->eventfd, &e, sizeof(e));
		} while (ret == -1 && errno == EINTR);
	} while (ret == -1 && errno == EAGAIN);

	pthread_mutex_lock(mutex);

	return 0;
}

/* Corresponds to an opened device */
struct DevEntry {
	unsigned int ref_count;

	struct iio_device *dev;
	struct iio_buffer *buf;
	unsigned int sample_size, nb_clients;
	unsigned int samples_count;
	bool update_mask;
	bool cyclic;
	bool closed;
	bool cancelled;

	/* Linked list of ThdEntry structures corresponding
	 * to all the threads who opened the device */
	SLIST_HEAD(ThdHead, ThdEntry) thdlist_head;
	pthread_mutex_t thdlist_lock;

	pthread_cond_t rw_ready_cond;

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

static unsigned int get_channel_number(const struct iio_channel *chn)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_channel *other;
	unsigned int i = 0;

	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		other = iio_device_get_channel(dev, i);
		if (other == chn)
			break;
	}

	return i;
}

static inline const char *dev_label_or_name_or_id(const struct iio_device *dev)
{
	const char *name;

	name = iio_device_get_label(dev);
	if (name)
		return name;

	name = iio_device_get_name(dev);
	if (name)
		return name;

	return iio_device_get_id(dev);
}

#if WITH_AIO
static ssize_t async_io(struct parser_pdata *pdata, void *buf, size_t len,
	bool do_read)
{
	ssize_t ret;
	struct pollfd pfd[2];
	unsigned int num_pfds;
	struct iocb iocb;
	struct iocb *ios[1];
	struct io_event e[1];

	ios[0] = &iocb;

	if (do_read)
		io_prep_pread(&iocb, pdata->fd_in, buf, len, 0);
	else
		io_prep_pwrite(&iocb, pdata->fd_out, buf, len, 0);

	io_set_eventfd(&iocb, pdata->aio_eventfd);

	pthread_mutex_lock(&pdata->aio_mutex);

	ret = io_submit(pdata->aio_ctx, 1, ios);
	if (ret != 1) {
		pthread_mutex_unlock(&pdata->aio_mutex);
		IIO_ERROR("Failed to submit IO operation: %zd\n", ret);
		return -EIO;
	}

	pfd[0].fd = pdata->aio_eventfd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;
	num_pfds = 2;

	do {
		poll_nointr(pfd, num_pfds);

		if (pfd[0].revents & POLLIN) {
			uint64_t event;
			ret = read(pdata->aio_eventfd, &event, sizeof(event));
			if (ret != sizeof(event)) {
				IIO_ERROR("Failed to read from eventfd: %d\n", -errno);
				ret = -EIO;
				break;
			}

			ret = io_getevents(pdata->aio_ctx, 0, 1, e, NULL);
			if (ret != 1) {
				IIO_ERROR("Failed to read IO events: %zd\n", ret);
				ret = -EIO;
				break;
			} else {
				ret = (long)e[0].res;
			}
		} else if ((num_pfds > 1 && pfd[1].revents & POLLIN)) {
			/* Got a STOP event to abort this whole session */
			ret = io_cancel(pdata->aio_ctx, &iocb, e);
			if (ret != -EINPROGRESS && ret != -EINVAL) {
				IIO_ERROR("Failed to cancel IO transfer: %zd\n", ret);
				ret = -EIO;
				break;
			}
			/* It should not be long now until we get the cancellation event */
			num_pfds = 1;
		}
	} while (!(pfd[0].revents & POLLIN));

	pthread_mutex_unlock(&pdata->aio_mutex);

	/* Got STOP event, treat it as EOF */
	if (num_pfds == 1)
		return 0;

	return ret;
}

#define MAX_AIO_REQ_SIZE (1024 * 1024)

static ssize_t readfd_aio(struct parser_pdata *pdata, void *dest, size_t len)
{
	if (len > MAX_AIO_REQ_SIZE)
		len = MAX_AIO_REQ_SIZE;
	return async_io(pdata, dest, len, true);
}

static ssize_t writefd_aio(struct parser_pdata *pdata, const void *dest,
		size_t len)
{
	if (len > MAX_AIO_REQ_SIZE)
		len = MAX_AIO_REQ_SIZE;
	return async_io(pdata, (void *)dest, len, false);
}
#endif /* WITH_AIO */

static ssize_t readfd_io(struct parser_pdata *pdata, void *dest, size_t len)
{
	ssize_t ret;
	struct pollfd pfd[2];

	pfd[0].fd = pdata->fd_in;
	pfd[0].events = POLLIN | POLLRDHUP;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	do {
		poll_nointr(pfd, 2);

		/* Got STOP event, or client closed the socket: treat it as EOF */
		if (pfd[1].revents & POLLIN || pfd[0].revents & POLLRDHUP)
			return 0;
		if (pfd[0].revents & POLLERR)
			return -EIO;
		if (!(pfd[0].revents & POLLIN))
			continue;

		do {
			if (pdata->fd_in_is_socket)
				ret = recv(pdata->fd_in, dest, len, MSG_NOSIGNAL);
			else
				ret = read(pdata->fd_in, dest, len);
		} while (ret == -1 && errno == EINTR);

		if (ret != -1 || errno != EAGAIN)
			break;
	} while (true);

	if (ret == -1)
		return -errno;

	return ret;
}

static ssize_t writefd_io(struct parser_pdata *pdata, const void *src, size_t len)
{
	ssize_t ret;
	struct pollfd pfd[2];

	pfd[0].fd = pdata->fd_out;
	pfd[0].events = POLLOUT;
	pfd[0].revents = 0;
	pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	do {
		poll_nointr(pfd, 2);

		/* Got STOP event, or client closed the socket: treat it as EOF */
		if (pfd[1].revents & POLLIN || pfd[0].revents & POLLHUP)
			return 0;
		if (pfd[0].revents & POLLERR)
			return -EIO;
		if (!(pfd[0].revents & POLLOUT))
			continue;

		do {
			if (pdata->fd_out_is_socket)
				ret = send(pdata->fd_out, src, len, MSG_NOSIGNAL);
			else
				ret = write(pdata->fd_out, src, len);
		} while (ret == -1 && errno == EINTR);

		if (ret != -1 || errno != EAGAIN)
			break;
	} while (true);

	if (ret == -1)
		return -errno;

	return ret;
}

ssize_t write_all(struct parser_pdata *pdata, const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;

	while (len) {
		ssize_t ret = pdata->writefd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
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
		ssize_t ret = pdata->readfd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
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
		snprintf(buf, sizeof(buf), "%li\n", value);
		output(pdata, buf);
	}
}

static ssize_t send_sample(const struct iio_channel *chn,
		void *src, size_t length, void *d)
{
	unsigned int number = get_channel_number(chn);
	struct sample_cb_info *info = d;

	if (iio_channel_get_index(chn) < 0 || !TEST_BIT(info->mask, number))
		return 0;
	if (info->nb_bytes < length)
		return 0;

	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char zero = 0;
		ssize_t ret;

		for (i = 0; i < goal; i++) {
			ret = info->pdata->writefd(info->pdata, &zero, 1);
			if (ret < 0)
				return ret;
		}
		info->cpt += goal;
	}

	info->cpt += length;
	info->nb_bytes -= length;
	return write_all(info->pdata, src, length);
}

static ssize_t receive_sample(const struct iio_channel *chn,
		void *dst, size_t length, void *d)
{
	unsigned int number = get_channel_number(chn);
	struct sample_cb_info *info = d;

	if (iio_channel_get_index(chn) < 0 || !TEST_BIT(info->mask, number))
		return 0;
	if (info->cpt == info->nb_bytes)
		return 0;

	/* Skip the padding if needed */
	if (info->cpt % length) {
		unsigned int i, goal = length - info->cpt % length;
		char foo;
		ssize_t ret;

		for (i = 0; i < goal; i++) {
			ret = info->pdata->readfd(info->pdata, &foo, 1);
			if (ret < 0)
				return ret;
		}
		info->cpt += goal;
	}

	info->cpt += length;
	return read_all(info->pdata, dst, length);
}

static ssize_t send_data(struct DevEntry *dev, struct ThdEntry *thd, size_t len)
{
	struct parser_pdata *pdata = thd->pdata;
	bool demux = server_demux && dev->sample_size != thd->sample_size;
	void *start;

	if (demux)
		len = (len / dev->sample_size) * thd->sample_size;
	if (len > thd->nb)
		len = thd->nb;

	print_value(pdata, len);

	if (thd->new_client) {
		unsigned int i;
		char buf[129], *ptr = buf;
		uint32_t *mask = demux ? thd->mask : dev->mask;
		ssize_t ret, length;

		length = sizeof(buf);
		/* Send the current mask */
		for (i = dev->nb_words; i > 0 && ptr < buf + sizeof(buf);
				i--, ptr += 8) {
			snprintf(ptr, length, "%08x", mask[i - 1]);
			length -= 8;
		}

		*ptr = '\n';
		length--;

		if (length < 0) {
			IIO_ERROR("send_data: string length error\n");
			return -ENOSPC;
		}

		ret = write_all(pdata, buf, ptr + 1 - buf);
		if (ret < 0)
			return ret;

		thd->new_client = false;
	}

	if (!demux) {
		/* Short path */
		start = iio_buffer_start(dev->buf);
		return write_all(pdata, start, len);
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

	/* Inform that no error occurred, and that we'll start reading data */
	if (thd->new_client) {
		print_value(thd->pdata, 0);
		thd->new_client = false;
	}

	if (dev->sample_size == thd->sample_size) {
		/* Short path: Receive directly in the buffer */

		size_t len = dev->sample_size * dev->samples_count;
		if (thd->nb < len)
			len = thd->nb;

		return read_all(pdata, iio_buffer_start(dev->buf), len);
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
		pthread_cond_destroy(&entry->rw_ready_cond);

		free(entry->mask);
		free(entry);
	}
}

static void signal_thread(struct ThdEntry *thd, ssize_t ret)
{
	thd->err = ret;
	thd->nb = 0;
	thd->active = false;
	thd_entry_event_signal(thd);
}

static void rw_thd(struct thread_pool *pool, void *d)
{
	struct DevEntry *entry = d;
	struct ThdEntry *thd, *next_thd;
	struct iio_device *dev = entry->dev;
	unsigned int nb_words = entry->nb_words;
	ssize_t ret = 0;

	IIO_DEBUG("R/W thread started for device %s\n",
		  dev_label_or_name_or_id(dev));

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

			for (i = 0; i < iio_device_get_channels_count(dev); i++) {
				struct iio_channel *chn = iio_device_get_channel(dev, i);
				unsigned int number = get_channel_number(chn);
				long index = iio_channel_get_index(chn);

				if (index < 0)
					continue;

				if (TEST_BIT(entry->mask, number))
					iio_channel_enable(chn);
				else
					iio_channel_disable(chn);
			}

			entry->buf = iio_device_create_buffer(dev,
					samples_count, entry->cyclic);
			if (!entry->buf) {
				ret = -errno;
				IIO_ERROR("Unable to create buffer\n");
				break;
			}
			entry->cancelled = false;

			/* Signal the threads that we opened the device */
			SLIST_FOREACH(thd, &entry->thdlist_head, dev_list_entry) {
				if (thd->wait_for_open) {
					thd->wait_for_open = false;
					signal_thread(thd, 0);
				}
			}

			IIO_DEBUG("IIO device %s reopened with new mask:\n",
				  dev_label_or_name_or_id(dev));
			for (i = 0; i < nb_words; i++)
				IIO_DEBUG("Mask[%i] = 0x%08x\n", i, entry->mask[i]);
			entry->update_mask = false;

			entry->sample_size = iio_device_get_sample_size(dev);
			entry->samples_count = samples_count;
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

		if (!has_readers && !has_writers) {
			pthread_cond_wait(&entry->rw_ready_cond,
					&entry->thdlist_lock);
		}

		pthread_mutex_unlock(&entry->thdlist_lock);

		if (!has_readers && !has_writers)
			continue;

		if (has_readers) {
			ssize_t nb_bytes;

			ret = iio_buffer_refill(entry->buf);

			pthread_mutex_lock(&entry->thdlist_lock);

			/*
			 * When the last client disconnects the buffer is
			 * cancelled and iio_buffer_refill() returns an error. A
			 * new client might have connected before we got here
			 * though, in that case the rw thread has to stay active
			 * and a new buffer is created. If the list is still empty the loop
			 * will exit normally.
			 */
			if (entry->cancelled) {
				pthread_mutex_unlock(&entry->thdlist_lock);
				continue;
			}

			if (ret < 0) {
				IIO_ERROR("Reading from device failed: %i\n",
						(int) ret);
				break;
			}

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

				ret = send_data(entry, thd, nb_bytes);
				if (ret > 0)
					thd->nb -= ret;

				if (ret < 0 || thd->nb < sample_size)
					signal_thread(thd, (ret < 0) ?
							ret : (ssize_t) thd->nb);
			}

			pthread_mutex_unlock(&entry->thdlist_lock);
		}

		if (has_writers) {
			ssize_t nb_bytes = 0;

			pthread_mutex_lock(&entry->thdlist_lock);

			/* Reset the size of the buffer to its maximum size.
			 *
			 * XXX(pcercuei): There is no way to perform this with
			 * the public libiio API. However, it probably does not
			 * matter; we only need to reset the size of the buffer
			 * if the buffer was used for receiving samples, and
			 * to date there is no IIO device that supports both
			 * receiving and sending samples.
			 *
			 * entry->buf->data_length = entry->buf->length;
			 */

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
			if (entry->cancelled) {
				pthread_mutex_unlock(&entry->thdlist_lock);
				continue;
			}
			if (ret < 0) {
				IIO_ERROR("Writing to device failed: %i\n",
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
	if (entry->buf) {
		iio_buffer_destroy(entry->buf);
		entry->buf = NULL;
	}
	entry->closed = true;
	pthread_mutex_unlock(&entry->thdlist_lock);

	pthread_mutex_lock(&devlist_lock);
	/* It is possible that a new thread has already started, make sure to
	 * not overwrite it. */
	if (iio_device_get_data(dev) == entry)
		iio_device_set_data(dev, NULL);
	pthread_mutex_unlock(&devlist_lock);

	IIO_DEBUG("Stopping R/W thread for device %s\n",
		  dev_label_or_name_or_id(dev));

	dev_entry_put(entry);
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

	pthread_cond_signal(&entry->rw_ready_cond);

	IIO_DEBUG("Waiting for completion...\n");
	while (thd->active) {
		ret = thd_entry_event_wait(thd, &entry->thdlist_lock, pdata->fd_in);
		if (ret)
			break;
	}
	if (ret == 0)
		ret = thd->err;
	pthread_mutex_unlock(&entry->thdlist_lock);

	if (ret > 0 && ret < (ssize_t) nb)
		print_value(thd->pdata, 0);

	IIO_DEBUG("Exiting rw_buffer with code %li\n", (long) ret);
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
		snprintf(buf, sizeof(buf), "%.*s", 8, mask);
		sscanf(buf, "%08x", --ptr);
		mask += 8;
		IIO_DEBUG("Mask[%lu]: 0x%08x\n",
				(unsigned long) (words - ptr) / 4, *ptr);
	}

	*len = nb;
	return words;
}

static void free_thd_entry(struct ThdEntry *t)
{
	close(t->eventfd);
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
		if (SLIST_EMPTY(&entry->thdlist_head) && entry->buf) {
			entry->cancelled = true;
			iio_buffer_cancel(entry->buf); /* Wakeup the rw thread */
		}

		pthread_cond_signal(&entry->rw_ready_cond);
	}
	pthread_mutex_unlock(&entry->thdlist_lock);
	dev_entry_put(entry);

	free_thd_entry(t);
}

static ssize_t get_dev_sample_size_mask(const struct iio_device *dev,
					const uint32_t *mask, size_t words)
{
	unsigned int i, len, number,
		     nb_channels = iio_device_get_channels_count(dev);
	const struct iio_channel *prev = NULL;
	const struct iio_channel *chn;
	const struct iio_data_format *fmt;
	long index;
	ssize_t size = 0;

	if (words != (nb_channels + 31) / 32)
		return -EINVAL;

	for (i = 0; i < nb_channels; i++) {
		chn = iio_device_get_channel(dev, i);
		number = get_channel_number(chn);
		fmt = iio_channel_get_data_format(chn);
		index = iio_channel_get_index(chn);
		len = fmt->length / 8 * fmt->repeat;

		if (index < 0)
			break;
		if (!TEST_BIT(mask, number))
			continue;

		if (prev && index == iio_channel_get_index(prev)) {
			prev = chn;
			continue;
		}

		if (size % len)
			size += 2 * len - (size % len);
		else
			size += len;

		prev = chn;
	}

	return size;
}

static int open_dev_helper(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic)
{
	int ret = -ENOMEM;
	struct DevEntry *entry;
	struct ThdEntry *thd;
	size_t len = strlen(mask);
	uint32_t *words;
	unsigned int nb_channels;
	unsigned int cyclic_retry = 500;

	if (!dev)
		return -ENODEV;

	nb_channels = iio_device_get_channels_count(dev);
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
	thd->sample_size = get_dev_sample_size_mask(dev, words, len);
	thd->pdata = pdata;
	thd->dev = dev;
	thd->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

retry:
	/* Atomically look up the thread and make sure that it is still active
	 * or allocate new one. */
	pthread_mutex_lock(&devlist_lock);
	entry = iio_device_get_data(dev);
	if (entry) {
		if (cyclic || entry->cyclic) {
			/* Only one client allowed in cyclic mode */
			pthread_mutex_unlock(&devlist_lock);

			/* There is an inherent race condition if a client
			 * creates a new cyclic buffer shortly after destroying
			 * a previous. E.g. like
			 *
			 *     iio_buffer_destroy(buf);
			 *     buf = iio_device_create_buffer(dev, n, true);
			 *
			 * In this case the two buffers each use their own
			 * communication channel which are unordered to each
			 * other. E.g. the socket open might arrive before the
			 * socket close on the host side, even though they were
			 * sent in the opposite order on the client side. This
			 * race condition can cause an error being reported back
			 * to the client, even though the code on the client
			 * side was well formed and would work fine e.g. using
			 * the local backend.
			 *
			 * To avoid this issue go to sleep for up to 50ms in
			 * intervals of 100us. This should be enough time for
			 * the issue to resolve itself. If there actually is
			 * contention on the buffer an error will eventually be
			 * returned in which case the additional delay cause by
			 * the retires should not matter too much.
			 *
			 * This is not pretty but it works.
			 */
			if (cyclic_retry) {
				struct timespec wait;
				wait.tv_sec = 0;
				wait.tv_nsec = (100 * 1000);
				cyclic_retry--;
				nanosleep(&wait, &wait);
				goto retry;
			}

			ret = -EBUSY;
			goto err_free_thd;
		}

		pthread_mutex_lock(&entry->thdlist_lock);
		if (!entry->closed) {
			pthread_mutex_unlock(&devlist_lock);

			entry->ref_count++;

			SLIST_INSERT_HEAD(&entry->thdlist_head, thd, dev_list_entry);
			thd->entry = entry;
			entry->update_mask = true;
			IIO_DEBUG("Added thread to client list\n");

			pthread_cond_signal(&entry->rw_ready_cond);

			/* Wait until the device is opened by the rw thread */
			while (thd->wait_for_open) {
				ret = thd_entry_event_wait(thd, &entry->thdlist_lock, pdata->fd_in);
				if (ret)
					break;
			}
			pthread_mutex_unlock(&entry->thdlist_lock);

			if (ret == 0)
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
	IIO_DEBUG("Added thread to client list\n");

	pthread_mutex_init(&entry->thdlist_lock, NULL);
	pthread_cond_init(&entry->rw_ready_cond, NULL);

	ret = thread_pool_add_thread(main_thread_pool, rw_thd, entry, "rw_thd");
	if (ret) {
		pthread_mutex_unlock(&devlist_lock);
		goto err_free_entry_mask;
	}

	IIO_DEBUG("Adding new device thread to device list\n");
	iio_device_set_data(dev, entry);
	pthread_mutex_unlock(&devlist_lock);

	pthread_mutex_lock(&entry->thdlist_lock);
	/* Wait until the device is opened by the rw thread */
	while (thd->wait_for_open) {
		ret = thd_entry_event_wait(thd, &entry->thdlist_lock, pdata->fd_in);
		if (ret)
			break;
	}
	pthread_mutex_unlock(&entry->thdlist_lock);

	if (ret == 0)
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
	close(thd->eventfd);
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
		const char *attr, enum iio_attr_type type)
{
	/* We use a very large buffer here, as if attr is NULL all the
	 * attributes will be read, which may represents a few kilobytes worth
	 * of data. */
	char buf[0x10000];
	ssize_t ret = -EINVAL;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			ret = iio_device_attr_read(dev, attr, buf, sizeof(buf) - 1);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			ret = iio_device_debug_attr_read(dev,
				attr, buf, sizeof(buf) - 1);
			break;
		case IIO_ATTR_TYPE_BUFFER:
			ret = iio_device_buffer_attr_read(dev,
							attr, buf, sizeof(buf) - 1);
			break;
		default:
			ret = -EINVAL;
			break;
	}
	print_value(pdata, ret);
	if (ret < 0)
		return ret;

	buf[ret] = '\n';
	return write_all(pdata, buf, ret + 1);
}

ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, size_t len, enum iio_attr_type type)
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

	switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			ret = iio_device_attr_write_raw(dev, attr, buf, len);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			ret = iio_device_debug_attr_write_raw(dev, attr, buf, len);
			break;
		case IIO_ATTR_TYPE_BUFFER:
			ret = iio_device_buffer_attr_write_raw(dev, attr, buf, len);
			break;
		default:
			ret = -EINVAL;
			break;
	}

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
		const char *name = iio_device_get_name(trigger);
		char buf[256];

		ret = strlen(name);
		print_value(pdata, ret);

		snprintf(buf, sizeof(buf), "%s\n", name);
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
	unsigned int i, nb = (unsigned int) value;
	struct timespec wait;
	int ret = -EINVAL;

	if (!dev) {
		ret = -ENODEV;
		goto err_print_value;
	}

	if (nb >= 1) {
		/*
		 * Avoid the same race condition described in open_dev_helper().
		 * We must be sure that the buffer has not been enabled in order
		 * to set the number of kernel buffers.
		 */
		for (i = 0; i < 500; i++) {
			ret = iio_device_set_kernel_buffers_count(dev, nb);
			if (ret != -EBUSY)
				break;

			wait.tv_sec = 0;
			wait.tv_nsec = (100 * 1000);
			do {
				ret = nanosleep(&wait, &wait);
			} while (ret == -1 && errno == EINTR);
		}
	}
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len)
{
	size_t bytes_read = 0;
	ssize_t ret;
	bool found;

	if (pdata->is_usb)
	      return pdata->readfd(pdata, buf, len);

	if (pdata->fd_in_is_socket) {
		struct pollfd pfd[2];

		pfd[0].fd = pdata->fd_in;
		pfd[0].events = POLLIN | POLLRDHUP;
		pfd[0].revents = 0;
		pfd[1].fd = thread_pool_get_poll_fd(pdata->pool);
		pfd[1].events = POLLIN;
		pfd[1].revents = 0;

		do {
			size_t i, to_trunc;

			poll_nointr(pfd, 2);

			if (pfd[1].revents & POLLIN ||
					pfd[0].revents & POLLRDHUP)
				return 0;

			/* First read from the socket, without advancing the
			 * read offset */
			ret = recv(pdata->fd_in, buf, len,
					MSG_NOSIGNAL | MSG_PEEK);
			if (ret < 0)
				return -errno;

			/* Lookup for the trailing \n */
			for (i = 0; i < (size_t) ret && buf[i] != '\n'; i++);
			found = i < (size_t) ret;

			len -= ret;
			buf += ret;

			to_trunc = found ? i + 1 : (size_t) ret;

			/* Advance the read offset after the \n if found, or
			 * after the last character read otherwise */
			ret = recv(pdata->fd_in, NULL, to_trunc,
					MSG_NOSIGNAL | MSG_TRUNC);
			if (ret < 0)
				return -errno;

			bytes_read += to_trunc;
		} while (!found && len);
	} else {
		while (len) {
			ret = pdata->readfd(pdata, buf, 1);
			if (ret < 0)
			      return ret;

			bytes_read++;

			if (*buf == '\n')
			      break;

			len--;
			buf++;
		}

		found = !!len;
	}

	return found ? (ssize_t) bytes_read : -EIO;
}

void interpreter(struct iio_context *ctx, int fd_in, int fd_out, bool verbose,
		 bool is_socket, bool is_usb, bool use_aio,
		 struct thread_pool *pool, const void *xml_zstd,
		 size_t xml_zstd_len)
{
	yyscan_t scanner;
	struct parser_pdata pdata;
	unsigned int i;
	int ret;

	pdata.ctx = ctx;
	pdata.stop = false;
	pdata.fd_in = fd_in;
	pdata.fd_out = fd_out;
	pdata.verbose = verbose;
	pdata.pool = pool;

	pdata.xml_zstd = xml_zstd;
	pdata.xml_zstd_len = xml_zstd_len;

	pdata.fd_in_is_socket = is_socket;
	pdata.fd_out_is_socket = is_socket;
	pdata.is_usb = is_usb;

	SLIST_INIT(&pdata.thdlist_head);

	if (use_aio) {
		/* Note: if WITH_AIO is not defined, use_aio is always false.
		 * We ensure that in iiod.c. */
#if WITH_AIO
		char err_str[1024];

		pdata.aio_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (pdata.aio_eventfd < 0) {
			iio_strerror(errno, err_str, sizeof(err_str));
			IIO_ERROR("Failed to create AIO eventfd: %s\n", err_str);
			return;
		}

		pdata.aio_ctx = 0;
		ret = io_setup(1, &pdata.aio_ctx);
		if (ret < 0) {
			iio_strerror(-ret, err_str, sizeof(err_str));
			IIO_ERROR("Failed to create AIO context: %s\n", err_str);
			close(pdata.aio_eventfd);
			return;
		}
		pthread_mutex_init(&pdata.aio_mutex, NULL);
		pdata.readfd = readfd_aio;
		pdata.writefd = writefd_aio;
#endif
	} else {
		pdata.readfd = readfd_io;
		pdata.writefd = writefd_io;
	}

	yylex_init_extra(&pdata, &scanner);

	do {
		if (verbose)
			output(&pdata, "iio-daemon > ");
		ret = yyparse(scanner);
	} while (!pdata.stop && ret >= 0);

	yylex_destroy(scanner);

	/* Close all opened devices */
	for (i = 0; i < iio_context_get_devices_count(ctx); i++)
		close_dev_helper(&pdata, iio_context_get_device(ctx, i));

#if WITH_AIO
	if (use_aio) {
		io_destroy(pdata.aio_ctx);
		close(pdata.aio_eventfd);
	}
#endif
}
