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

#include "debug.h"
#include "iio-private.h"

#include <dirent.h>
#include <errno.h>
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

#define DEFAULT_TIMEOUT_MS 1000

#define NB_BLOCKS 4

#define BLOCK_ALLOC_IOCTL   _IOWR('i', 0xa0, struct block_alloc_req)
#define BLOCK_FREE_IOCTL      _IO('i', 0xa1)
#define BLOCK_QUERY_IOCTL   _IOWR('i', 0xa2, struct block)
#define BLOCK_ENQUEUE_IOCTL _IOWR('i', 0xa3, struct block)
#define BLOCK_DEQUEUE_IOCTL _IOWR('i', 0xa4, struct block)

#define BLOCK_FLAG_CYCLIC BIT(1)

/* Forward declarations */
static ssize_t local_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug);
static ssize_t local_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len);
static ssize_t local_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, bool is_debug);
static ssize_t local_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len);

struct block_alloc_req {
	uint32_t type,
		 size,
		 count,
		 id;
};

struct block {
	uint32_t id,
		 size,
		 bytes_used,
		 type,
		 flags,
		 offset;
	uint64_t timestamp;
};

struct iio_context_pdata {
	unsigned int rw_timeout_ms;
};

struct iio_device_pdata {
	int fd;
	bool blocking;
	unsigned int samples_count, nb_blocks;

	struct block *blocks;
	void **addrs;
	int last_dequeued;
	bool is_high_speed, cyclic, cyclic_buffer_enqueued, buffer_enabled;

	int cancel_fd;
};

static const char * const device_attrs_blacklist[] = {
	"dev",
	"uevent",
};

static int ioctl_nointr(int fd, unsigned long request, void *data)
{
	int ret;

	do {
		ret = ioctl(fd, request, data);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

static void local_free_pdata(struct iio_device *device)
{
	if (device && device->pdata) {
		free(device->pdata->blocks);
		free(device->pdata->addrs);
		free(device->pdata);
	}
}

static void local_shutdown(struct iio_context *ctx)
{
	/* Free the backend data stored in every device structure */
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++)
		local_free_pdata(ctx->devices[i]);

	free(ctx->pdata);
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
	size_t prefix_len = 0;
	const char *attr0;
	const char *ptr;
	unsigned int i;

	if (chn->nb_attrs < 2)
		return 0;

	attr0 = ptr = chn->attrs[0].name;

	while (true) {
		bool can_fix = true;
		size_t len;

		ptr = strchr(ptr, '_');
		if (!ptr)
			break;

		len = ptr - attr0 + 1;
		for (i = 1; can_fix && i < chn->nb_attrs; i++)
			can_fix = !strncmp(attr0, chn->attrs[i].name, len);

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
		strncpy(name, attr0, prefix_len - 1);
		name[prefix_len - 1] = '\0';
		DEBUG("Setting name of channel %s to %s\n", chn->id, name);
		chn->name = name;

		/* Shrink the attribute name */
		for (i = 0; i < chn->nb_attrs; i++)
			strcut(chn->attrs[i].name, prefix_len);
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

	if (diff_ms >= timeout_rel) /* Expired */
		return 0;
	if (diff_ms > 0) /* Should never be false, but lets be safe */
		timeout_rel -= diff_ms;
	if (timeout_rel > INT_MAX)
		return INT_MAX;

	return (int) timeout_rel;
}

static int device_check_ready(const struct iio_device *dev, short events,
	struct timespec *start)
{
	struct pollfd pollfd[2] = {
		{
			.fd = dev->pdata->fd,
			.events = events,
		}, {
			.fd = dev->pdata->cancel_fd,
			.events = POLLIN,
		}
	};
	unsigned int rw_timeout_ms = dev->ctx->pdata->rw_timeout_ms;
	int timeout_rel;
	int ret;

	if (!dev->pdata->blocking)
		return 0;

	do {
		timeout_rel = get_rel_timeout_ms(start, rw_timeout_ms);
		ret = poll(pollfd, 2, timeout_rel);
	} while (ret == -1 && errno == EINTR);

	if ((pollfd[1].revents & POLLIN))
		return -EBADF;

	if (ret < 0)
		return -errno;
	if (!ret)
		return -ETIMEDOUT;
	if (pollfd[0].revents & POLLNVAL)
		return -EBADF;
	if (!(pollfd[0].revents & events))
		return -EIO;
	return 0;
}

static ssize_t local_read(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	uintptr_t ptr = (uintptr_t) dst;
	struct timespec start;
	ssize_t readsize;
	ssize_t ret;

	if (pdata->fd == -1)
		return -EBADF;
	if (words != dev->words)
		return -EINVAL;

	memcpy(mask, dev->mask, words);

	if (len == 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (len > 0) {
		ret = device_check_ready(dev, POLLIN, &start);
		if (ret < 0)
			break;

		do {
			ret = read(pdata->fd, (void *) ptr, len);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1) {
			if (pdata->blocking && errno == EAGAIN)
				continue;
			ret = -EIO;
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

static ssize_t local_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	struct iio_device_pdata *pdata = dev->pdata;
	uintptr_t ptr = (uintptr_t) src;
	struct timespec start;
	ssize_t writtensize;
	ssize_t ret;

	if (pdata->fd == -1)
		return -EBADF;

	if (len == 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (len > 0) {
		ret = device_check_ready(dev, POLLOUT, &start);
		if (ret < 0)
			break;

		do {
			ret = write(pdata->fd, (void *) ptr, len);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1) {
			if (pdata->blocking && errno == EAGAIN)
				continue;

			ret = -EIO;
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

static ssize_t local_enable_buffer(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret = 0;

	if (!pdata->buffer_enabled) {
		ret = local_write_dev_attr(dev,
				"buffer/enable", "1", 2, false);
		if (ret >= 0)
			pdata->buffer_enabled = true;
	}

	return 0;
}

static int local_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	struct iio_device_pdata *pdata = dev->pdata;

	if (pdata->fd != -1)
		return -EBUSY;

	pdata->nb_blocks = nb_blocks;

	return 0;
}

static ssize_t local_get_buffer(const struct iio_device *dev,
		void **addr_ptr, size_t bytes_used,
		uint32_t *mask, size_t words)
{
	struct block block;
	struct iio_device_pdata *pdata = dev->pdata;
	struct timespec start;
	char err_str[1024];
	int f = pdata->fd;
	ssize_t ret;

	if (!pdata->is_high_speed)
		return -ENOSYS;
	if (f == -1)
		return -EBADF;
	if (!addr_ptr)
		return -EINVAL;

	if (pdata->last_dequeued >= 0) {
		struct block *last_block = &pdata->blocks[pdata->last_dequeued];

		if (pdata->cyclic) {
			if (pdata->cyclic_buffer_enqueued)
				return -EBUSY;
			pdata->blocks[0].flags |= BLOCK_FLAG_CYCLIC;
			pdata->cyclic_buffer_enqueued = true;
		}

		last_block->bytes_used = bytes_used;
		ret = (ssize_t) ioctl_nointr(f,
				BLOCK_ENQUEUE_IOCTL, last_block);
		if (ret) {
			ret = (ssize_t) -errno;
			iio_strerror(errno, err_str, sizeof(err_str));
			ERROR("Unable to enqueue block: %s\n", err_str);
			return ret;
		}

		if (pdata->cyclic) {
			*addr_ptr = pdata->addrs[pdata->last_dequeued];
			return (ssize_t) last_block->bytes_used;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &start);

	do {
		ret = (ssize_t) device_check_ready(dev, POLLIN | POLLOUT, &start);
		if (ret < 0)
			return ret;

		memset(&block, 0, sizeof(block));
		ret = (ssize_t) ioctl_nointr(f, BLOCK_DEQUEUE_IOCTL, &block);
	} while (pdata->blocking && ret == -1 && errno == EAGAIN);

	if (ret) {
		ret = (ssize_t) -errno;
		if ((!pdata->blocking && ret != -EAGAIN) ||
				(pdata->blocking && ret != -ETIMEDOUT)) {
			iio_strerror(errno, err_str, sizeof(err_str));
			ERROR("Unable to dequeue block: %s\n", err_str);
		}
		return ret;
	}

	/* Requested buffer size is too big! */
	if (pdata->last_dequeued < 0 && bytes_used != block.size)
		return -EFBIG;

	pdata->last_dequeued = block.id;
	*addr_ptr = pdata->addrs[block.id];
	return (ssize_t) block.bytes_used;
}

static ssize_t local_read_all_dev_attrs(const struct iio_device *dev,
		char *dst, size_t len, bool is_debug)
{
	unsigned int i, nb = is_debug ? dev->nb_debug_attrs : dev->nb_attrs;
	char **attrs = is_debug ? dev->debug_attrs : dev->attrs;
	char *ptr = dst;

	for (i = 0; len >= 4 && i < nb; i++) {
		/* Recursive! */
		ssize_t ret = local_read_dev_attr(dev, attrs[i],
				ptr + 4, len - 4, is_debug);
		*(uint32_t *) ptr = iio_htobe32(ret);

		/* Align the length to 4 bytes */
		if (ret > 0 && ret & 3)
			ret = ((ret >> 2) + 1) << 2;
		ptr += 4 + (ret < 0 ? 0 : ret);
		len -= 4 + (ret < 0 ? 0 : ret);
	}

	return ptr - dst;
}

static ssize_t local_read_all_chn_attrs(const struct iio_channel *chn,
		char *dst, size_t len)
{
	unsigned int i;
	char *ptr = dst;

	for (i = 0; len >= 4 && i < chn->nb_attrs; i++) {
		/* Recursive! */
		ssize_t ret = local_read_chn_attr(chn,
				chn->attrs[i].name, ptr + 4, len - 4);
		*(uint32_t *) ptr = iio_htobe32(ret);

		/* Align the length to 4 bytes */
		if (ret > 0 && ret & 3)
			ret = ((ret >> 2) + 1) << 2;
		ptr += 4 + (ret < 0 ? 0 : ret);
		len -= 4 + (ret < 0 ? 0 : ret);
	}

	return ptr - dst;
}

static int local_buffer_analyze(unsigned int nb, const char *src, size_t len)
{
	while (nb--) {
		int32_t val;

		if (len < 4)
			return -EINVAL;

		val = (int32_t) iio_be32toh(*(uint32_t *) src);
		src += 4;
		len -= 4;

		if (val > 0) {
			if ((uint32_t) val > len)
				return -EINVAL;

			/* Align the length to 4 bytes */
			if (val & 3)
				val = ((val >> 2) + 1) << 2;
			len -= val;
			src += val;
		}
	}

	/* We should have analyzed the whole buffer by now */
	return !len ? 0 : -EINVAL;
}

static ssize_t local_write_all_dev_attrs(const struct iio_device *dev,
		const char *src, size_t len, bool is_debug)
{
	unsigned int i, nb = is_debug ? dev->nb_debug_attrs : dev->nb_attrs;
	char **attrs = is_debug ? dev->debug_attrs : dev->attrs;
	const char *ptr = src;

	/* First step: Verify that the buffer is in the correct format */
	if (local_buffer_analyze(nb, src, len))
		return -EINVAL;

	/* Second step: write the attributes */
	for (i = 0; i < nb; i++) {
		int32_t val = (int32_t) iio_be32toh(*(uint32_t *) ptr);
		ptr += 4;

		if (val > 0) {
			local_write_dev_attr(dev, attrs[i], ptr, val, is_debug);

			/* Align the length to 4 bytes */
			if (val & 3)
				val = ((val >> 2) + 1) << 2;
			ptr += val;
		}
	}

	return ptr - src;
}

static ssize_t local_write_all_chn_attrs(const struct iio_channel *chn,
		const char *src, size_t len)
{
	unsigned int i, nb = chn->nb_attrs;
	const char *ptr = src;

	/* First step: Verify that the buffer is in the correct format */
	if (local_buffer_analyze(nb, src, len))
		return -EINVAL;

	/* Second step: write the attributes */
	for (i = 0; i < nb; i++) {
		int32_t val = (int32_t) iio_be32toh(*(uint32_t *) ptr);
		ptr += 4;

		if (val > 0) {
			local_write_chn_attr(chn, chn->attrs[i].name, ptr, val);

			/* Align the length to 4 bytes */
			if (val & 3)
				val = ((val >> 2) + 1) << 2;
			ptr += val;
		}
	}

	return ptr - src;
}

static ssize_t local_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;

	if (!attr)
		return local_read_all_dev_attrs(dev, dst, len, is_debug);

	if (is_debug)
		snprintf(buf, sizeof(buf), "/sys/kernel/debug/iio/%s/%s",
				dev->id, attr);
	else
		snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s",
				dev->id, attr);
	f = fopen(buf, "re");
	if (!f)
		return -errno;

	ret = fread(dst, 1, len, f);
	if (ret > 0)
		dst[ret - 1] = '\0';
	fflush(f);
	if (ferror(f))
		ret = -errno;
	fclose(f);
	return ret ? ret : -EIO;
}

static ssize_t local_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, bool is_debug)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;

	if (!attr)
		return local_write_all_dev_attrs(dev, src, len, is_debug);

	if (is_debug)
		snprintf(buf, sizeof(buf), "/sys/kernel/debug/iio/%s/%s",
				dev->id, attr);
	else
		snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s",
				dev->id, attr);
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
	for (i = 0; i < chn->nb_attrs; i++)
		if (!strcmp(attr, chn->attrs[i].name))
			return chn->attrs[i].filename;
	return attr;
}

static ssize_t local_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	if (!attr)
		return local_read_all_chn_attrs(chn, dst, len);

	attr = get_filename(chn, attr);
	return local_read_dev_attr(chn->dev, attr, dst, len, false);
}

static ssize_t local_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	if (!attr)
		return local_write_all_chn_attrs(chn, src, len);

	attr = get_filename(chn, attr);
	return local_write_dev_attr(chn->dev, attr, src, len, false);
}

static int channel_write_state(const struct iio_channel *chn)
{
	char *en = iio_channel_is_enabled(chn) ? "1" : "0";
	ssize_t ret = local_write_chn_attr(chn, "en", en, 2);
	if (ret < 0)
		return (int) ret;
	else
		return 0;
}

static int enable_high_speed(const struct iio_device *dev)
{
	struct block_alloc_req req;
	struct iio_device_pdata *pdata = dev->pdata;
	unsigned int i;
	int ret, fd = pdata->fd;

	if (pdata->cyclic) {
		pdata->nb_blocks = 1;
		DEBUG("Enabling cyclic mode\n");
	} else {
		DEBUG("Cyclic mode not enabled\n");
	}

	pdata->blocks = calloc(pdata->nb_blocks, sizeof(*pdata->blocks));
	if (!pdata->blocks) {
		pdata->nb_blocks = 0;
		return -ENOMEM;
	}

	pdata->addrs = calloc(pdata->nb_blocks, sizeof(*pdata->addrs));
	if (!pdata->addrs) {
		free(pdata->blocks);
		pdata->blocks = NULL;
		return -ENOMEM;
	}

	req.id = 0;
	req.type = 0;
	req.size = pdata->samples_count *
		iio_device_get_sample_size_mask(dev, dev->mask, dev->words);
	req.count = pdata->nb_blocks;

	ret = ioctl_nointr(fd, BLOCK_ALLOC_IOCTL, &req);
	if (ret < 0) {
		ret = -errno;
		goto err_freemem;
	}

	/* We might get less blocks than what we asked for */
	pdata->nb_blocks = req.count;

	/* mmap all the blocks */
	for (i = 0; i < pdata->nb_blocks; i++) {
		pdata->blocks[i].id = i;
		ret = ioctl_nointr(fd, BLOCK_QUERY_IOCTL, &pdata->blocks[i]);
		if (ret) {
			ret = -errno;
			goto err_munmap;
		}

		ret = ioctl_nointr(fd, BLOCK_ENQUEUE_IOCTL, &pdata->blocks[i]);
		if (ret) {
			ret = -errno;
			goto err_munmap;
		}

		pdata->addrs[i] = mmap(0, pdata->blocks[i].size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, pdata->blocks[i].offset);
		if (pdata->addrs[i] == MAP_FAILED) {
			ret = -errno;
			goto err_munmap;
		}
	}

	pdata->last_dequeued = -1;
	return 0;

err_munmap:
	for (; i > 0; i--)
		munmap(pdata->addrs[i - 1], pdata->blocks[i - 1].size);
	ioctl_nointr(fd, BLOCK_FREE_IOCTL, 0);
err_freemem:
	free(pdata->addrs);
	pdata->addrs = NULL;
	free(pdata->blocks);
	pdata->blocks = NULL;
	return ret;
}

static int local_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	unsigned int i;
	int ret;
	char buf[1024];
	struct iio_device_pdata *pdata = dev->pdata;

	if (pdata->fd != -1)
		return -EBUSY;

	ret = local_write_dev_attr(dev, "buffer/enable", "0", 2, false);
	if (ret < 0)
		return ret;

	snprintf(buf, sizeof(buf), "%lu", (unsigned long) samples_count);
	ret = local_write_dev_attr(dev, "buffer/length",
			buf, strlen(buf) + 1, false);
	if (ret < 0)
		return ret;

	pdata->cancel_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (pdata->cancel_fd == -1)
		return -errno;

	snprintf(buf, sizeof(buf), "/dev/%s", dev->id);
	pdata->fd = open(buf, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (pdata->fd == -1) {
		ret = -errno;
		goto err_close_cancel_fd;
	}

	/* Disable channels */
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (chn->index >= 0 && !iio_channel_is_enabled(chn)) {
			ret = channel_write_state(chn);
			if (ret < 0)
				goto err_close;
		}
	}
	/* Enable channels */
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (chn->index >= 0 && iio_channel_is_enabled(chn)) {
			ret = channel_write_state(chn);
			if (ret < 0)
				goto err_close;
		}
	}

	pdata->cyclic = cyclic;
	pdata->cyclic_buffer_enqueued = false;
	pdata->buffer_enabled = false;
	pdata->samples_count = samples_count;
	pdata->is_high_speed = !enable_high_speed(dev);

	if (!pdata->is_high_speed) {
		unsigned long size = samples_count * pdata->nb_blocks;
		WARNING("High-speed mode not enabled\n");

		/* Cyclic mode is only supported in high-speed mode */
		if (cyclic)
			return -EPERM;

		/* Increase the size of the kernel buffer, when using the
		 * low-speed interface. This avoids losing samples when
		 * refilling the iio_buffer. */
		snprintf(buf, sizeof(buf), "%lu", size);
		ret = local_write_dev_attr(dev, "buffer/length",
				buf, strlen(buf) + 1, false);
		if (ret < 0)
			goto err_close;
	}

	ret = local_enable_buffer(dev);
	if (ret < 0)
		goto err_close;

	return 0;
err_close:
	close(pdata->fd);
	pdata->fd = -1;
err_close_cancel_fd:
	close(pdata->cancel_fd);
	pdata->cancel_fd = -1;
	return ret;
}

static int local_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret;

	if (pdata->fd == -1)
		return -EBADF;

	if (pdata->is_high_speed) {
		unsigned int i;
		for (i = 0; i < pdata->nb_blocks; i++)
			munmap(pdata->addrs[i], pdata->blocks[i].size);
		ioctl_nointr(pdata->fd, BLOCK_FREE_IOCTL, 0);
		free(pdata->addrs);
		pdata->addrs = NULL;
		free(pdata->blocks);
		pdata->blocks = NULL;
	}

	ret = close(pdata->fd);
	if (ret)
		return ret;

	close(pdata->cancel_fd);

	pdata->fd = -1;
	pdata->cancel_fd = -1;

	ret = local_write_dev_attr(dev, "buffer/enable", "0", 2, false);
	return (ret < 0) ? ret : 0;
}

static int local_get_fd(const struct iio_device *dev)
{
	if (dev->pdata->fd == -1)
		return -EBADF;
	else
		return dev->pdata->fd;
}

static int local_set_blocking_mode(const struct iio_device *dev, bool blocking)
{
	if (dev->pdata->fd == -1)
		return -EBADF;

	if (dev->pdata->cyclic)
		return -EPERM;

	dev->pdata->blocking = blocking;

	return 0;
}

static int local_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	char buf[1024];
	unsigned int i;
	ssize_t nb = local_read_dev_attr(dev, "trigger/current_trigger",
			buf, sizeof(buf), false);
	if (nb < 0) {
		*trigger = NULL;
		return (int) nb;
	}

	if (buf[0] == '\0') {
		*trigger = NULL;
		return 0;
	}

	nb = dev->ctx->nb_devices;
	for (i = 0; i < (size_t) nb; i++) {
		const struct iio_device *cur = dev->ctx->devices[i];
		if (cur->name && !strcmp(cur->name, buf)) {
			*trigger = cur;
			return 0;
		}
	}
	return -ENXIO;
}

static int local_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	ssize_t nb;
	const char *value = trigger ? trigger->name : "";
	nb = local_write_dev_attr(dev, "trigger/current_trigger",
			value, strlen(value) + 1, false);
	if (nb < 0)
		return (int) nb;
	else
		return 0;
}

static bool is_channel(const char *attr, bool strict)
{
	char *ptr = NULL;
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

static char * get_channel_id(const char *attr)
{
	char *res, *ptr;
	size_t len;

	attr = strchr(attr, '_') + 1;
	ptr = strchr(attr, '_');
	if (find_channel_modifier(ptr + 1, &len) != IIO_NO_MOD)
		ptr += len + 1;

	res = malloc(ptr - attr + 1);
	if (!res)
		return NULL;

	memcpy(res, attr, ptr - attr);
	res[ptr - attr] = 0;
	return res;
}

static char * get_short_attr_name(struct iio_channel *chn, const char *attr)
{
	char *ptr = strchr(attr, '_') + 1;
	size_t len;

	ptr = strchr(ptr, '_') + 1;
	if (find_channel_modifier(ptr, &len) != IIO_NO_MOD)
		ptr += len + 1;

	if (chn->name) {
		size_t len = strlen(chn->name);
		if  (strncmp(chn->name, ptr, len) == 0 && ptr[len] == '_')
			ptr += len + 1;
	}

	return strdup(ptr);
}

static int read_device_name(struct iio_device *dev)
{
	char buf[1024];
	ssize_t ret = iio_device_attr_read(dev, "name", buf, sizeof(buf));
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
	char **attrs, *name;
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

static int add_attr_to_channel(struct iio_channel *chn,
		const char *attr, const char *path)
{
	struct iio_channel_attr *attrs;
	char *fn, *name = get_short_attr_name(chn, attr);
	if (!name)
		return -ENOMEM;

	fn = strdup(path);
	if (!fn)
		goto err_free_name;

	attrs = realloc(chn->attrs, (1 + chn->nb_attrs) *
			sizeof(struct iio_channel_attr));
	if (!attrs)
		goto err_free_fn;

	attrs[chn->nb_attrs].filename = fn;
	attrs[chn->nb_attrs++].name = name;
	chn->attrs = attrs;
	DEBUG("Added attr \'%s\' to channel \'%s\'\n", name, chn->id);
	return 0;

err_free_fn:
	free(fn);
err_free_name:
	free(name);
	return -ENOMEM;
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

static struct iio_channel *create_channel(struct iio_device *dev,
		char *id, const char *attr, const char *path)
{
	struct iio_channel *chn = zalloc(sizeof(*chn));
	if (!chn)
		return NULL;

	if (!strncmp(attr, "out_", 4))
		chn->is_output = true;
	else if (strncmp(attr, "in_", 3))
		goto err_free_chn;

	chn->dev = dev;
	chn->id = id;

	if (!add_attr_to_channel(chn, attr, path))
		return chn;

err_free_chn:
	free(chn);
	return NULL;
}

static int add_channel(struct iio_device *dev, const char *name,
	const char *path, bool dir_is_scan_elements)
{
	struct iio_channel *chn;
	char *channel_id;
	unsigned int i;
	int ret;

	channel_id = get_channel_id(name);
	if (!channel_id)
		return -ENOMEM;

	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (!strcmp(chn->id, channel_id)
				&& chn->is_output == (name[0] == 'o')) {
			free(channel_id);
			ret = add_attr_to_channel(chn, name, path);
			chn->is_scan_element = dir_is_scan_elements && !ret;
			return ret;
		}
	}

	chn = create_channel(dev, channel_id, name, path);
	if (!chn) {
		free(channel_id);
		return -ENXIO;
	}

	iio_channel_init_finalize(chn);

	ret = add_channel_to_device(dev, chn);
	if (ret)
		free_channel(chn);
	else
		chn->is_scan_element = dir_is_scan_elements;
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
	char *ptr;

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

	if (strncmp(chn->id, attr, len))
		return 0;

	DEBUG("Found match: %s and %s\n", chn->id, attr);
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
			ret = add_attr_to_channel(chn, attr, attr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int detect_and_move_global_attrs(struct iio_device *dev)
{
	unsigned int i;
	char **ptr = dev->attrs;

	for (i = 0; i < dev->nb_attrs; i++) {
		const char *attr = dev->attrs[i];
		bool match;
		int ret;

		ret = detect_global_attr(dev, attr, 2, &match);
		if (ret)
			return ret;

		if (!match) {
			ret = detect_global_attr(dev, attr, 1, &match);
			if (ret)
				return ret;
		}

		if (match) {
			free(dev->attrs[i]);
			dev->attrs[i] = NULL;
		}
	}

	/* Find channels without an index */
	for (i = 0; i < dev->nb_attrs; i++) {
		const char *attr = dev->attrs[i];
		int ret;

		if (!dev->attrs[i])
			continue;

		if (is_channel(attr, false)) {
			ret = add_channel(dev, attr, attr, false);
			if (ret)
				return ret;

			free(dev->attrs[i]);
			dev->attrs[i] = NULL;
		}
	}

	for (i = 0; i < dev->nb_attrs; i++) {
		if (dev->attrs[i])
			*ptr++ = dev->attrs[i];
	}

	dev->nb_attrs = ptr - dev->attrs;
	if (!dev->nb_attrs) {
		free(dev->attrs);
		dev->attrs = NULL;
	}

	return 0;
}

static int add_attr_or_channel_helper(struct iio_device *dev,
		const char *path, bool dir_is_scan_elements)
{
	char buf[1024];
	const char *name = strrchr(path, '/') + 1;

	if (dir_is_scan_elements) {
		snprintf(buf, sizeof(buf), "scan_elements/%s", name);
		path = buf;
	} else {
		if (!is_channel(name, true))
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

static int foreach_in_dir(void *d, const char *path, bool is_dir,
		int (*callback)(void *, const char *))
{
	long name_max;
	struct dirent *entry, *result;
	DIR *dir = opendir(path);
	if (!dir)
		return -errno;

	name_max = pathconf(path, _PC_NAME_MAX);
	if (name_max == -1)
		name_max = 255;
	entry = malloc(offsetof(struct dirent, d_name) + name_max + 1);
	if (!entry) {
		closedir(dir);
		return -ENOMEM;
	}

	while (true) {
		struct stat st;
		char buf[1024];
		int ret = readdir_r(dir, entry, &result);
		if (ret) {
			iio_strerror(ret, buf, sizeof(buf));
			ERROR("Unable to open directory %s: %s\n", path, buf);
			free(entry);
			closedir(dir);
			return -ret;
		}
		if (!result)
			break;

		snprintf(buf, sizeof(buf), "%s/%s", path, entry->d_name);
		if (stat(buf, &st) < 0) {
			ret = -errno;
			iio_strerror(errno, buf, sizeof(buf));
			ERROR("Unable to stat file: %s\n", buf);
			free(entry);
			closedir(dir);
			return ret;
		}

		if (is_dir && S_ISDIR(st.st_mode) && entry->d_name[0] != '.')
			ret = callback(d, buf);
		else if (!is_dir && S_ISREG(st.st_mode))
			ret = callback(d, buf);
		else
			continue;

		if (ret < 0) {
			free(entry);
			closedir(dir);
			return ret;
		}
	}

	free(entry);
	closedir(dir);
	return 0;
}

static int add_scan_elements(struct iio_device *dev, const char *devpath)
{
	struct stat st;
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s/scan_elements", devpath);

	if (!stat(buf, &st) && S_ISDIR(st.st_mode)) {
		int ret = foreach_in_dir(dev, buf, false, add_scan_element);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int create_device(void *d, const char *path)
{
	uint32_t *mask = NULL;
	unsigned int i;
	int ret;
	struct iio_context *ctx = d;
	struct iio_device *dev = zalloc(sizeof(*dev));
	if (!dev)
		return -ENOMEM;

	dev->pdata = zalloc(sizeof(*dev->pdata));
	if (!dev->pdata) {
		free(dev);
		return -ENOMEM;
	}

	dev->pdata->fd = -1;
	dev->pdata->blocking = true;
	dev->pdata->nb_blocks = NB_BLOCKS;

	dev->ctx = ctx;
	dev->id = strdup(strrchr(path, '/') + 1);
	if (!dev->id) {
		local_free_pdata(dev);
		free(dev);
		return -ENOMEM;
	}

	ret = foreach_in_dir(dev, path, false, add_attr_or_channel);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	ret = add_scan_elements(dev, path);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	for (i = 0; i < dev->nb_channels; i++)
		set_channel_name(dev->channels[i]);

	ret = detect_and_move_global_attrs(dev);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	dev->words = (dev->nb_channels + 31) / 32;
	if (dev->words) {
		mask = calloc(dev->words, sizeof(*mask));
		if (!mask) {
			free_device(dev);
			return ret;
		}
	}

	dev->mask = mask;

	ret = add_device_to_context(ctx, dev);
	if (ret < 0)
		free_device(dev);
	return ret;
}

static int add_debug_attr(void *d, const char *path)
{
	struct iio_device *dev = d;
	const char *attr = strrchr(path, '/') + 1;
	char **attrs, *name = strdup(attr);
	if (!name)
		return -ENOMEM;

	attrs = realloc(dev->debug_attrs,
			(1 + dev->nb_debug_attrs) * sizeof(char *));
	if (!attrs) {
		free(name);
		return -ENOMEM;
	}

	attrs[dev->nb_debug_attrs++] = name;
	dev->debug_attrs = attrs;
	DEBUG("Added debug attr \'%s\' to device \'%s\'\n", name, dev->id);
	return 0;
}

static int add_debug(void *d, const char *path)
{
	struct iio_context *ctx = d;
	const char *name = strrchr(path, '/') + 1;
	struct iio_device *dev = iio_context_find_device(ctx, name);
	if (!dev)
		return -ENODEV;
	else
		return foreach_in_dir(dev, path, false, add_debug_attr);
}

static int local_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	ctx->pdata->rw_timeout_ms = timeout;
	return 0;
}

static void local_cancel(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	uint64_t event = 1;
	int ret;

	ret = write(pdata->cancel_fd, &event, sizeof(event));
	if (ret == -1) {
		/* If this happens something went very seriously wrong */
		char err_str[1024];
		iio_strerror(errno, err_str, sizeof(err_str));
		ERROR("Unable to signal cancellation event: %s\n", err_str);
	}
}

static struct iio_context * local_clone(
		const struct iio_context *ctx __attribute__((unused)))
{
	return local_create_context();
}

static const struct iio_backend_ops local_ops = {
	.clone = local_clone,
	.open = local_open,
	.close = local_close,
	.get_fd = local_get_fd,
	.set_blocking_mode = local_set_blocking_mode,
	.read = local_read,
	.write = local_write,
	.set_kernel_buffers_count = local_set_kernel_buffers_count,
	.get_buffer = local_get_buffer,
	.read_device_attr = local_read_dev_attr,
	.write_device_attr = local_write_dev_attr,
	.read_channel_attr = local_read_chn_attr,
	.write_channel_attr = local_write_chn_attr,
	.get_trigger = local_get_trigger,
	.set_trigger = local_set_trigger,
	.shutdown = local_shutdown,
	.set_timeout = local_set_timeout,
	.cancel = local_cancel,
};

static void init_index(struct iio_channel *chn)
{
	char buf[1024];
	long id = -ENOENT;

	if (chn->is_scan_element) {
		id = (long) local_read_chn_attr(chn, "index", buf, sizeof(buf));
		if (id > 0)
			id = atol(buf);
	}
	chn->index = id;
}

static void init_data_format(struct iio_channel *chn)
{
	char buf[1024];
	ssize_t ret;

	if (chn->is_scan_element) {
		ret = local_read_chn_attr(chn, "type", buf, sizeof(buf));
		if (ret < 0) {
			chn->format.length = 0;
		} else {
			char endian, sign;

			sscanf(buf, "%ce:%c%u/%u>>%u", &endian, &sign,
					&chn->format.bits, &chn->format.length,
					&chn->format.shift);
			chn->format.is_signed = (sign == 's' || sign == 'S');
			chn->format.is_fully_defined =
					(sign == 'S' || sign == 'U'||
					chn->format.bits == chn->format.length);
			chn->format.is_be = endian == 'b';
		}
	}

	ret = iio_channel_attr_read(chn, "scale", buf, sizeof(buf));
	if (ret < 0) {
		chn->format.with_scale = false;
	} else {
		chn->format.with_scale = true;
		chn->format.scale = atof(buf);
	}
}

static void init_scan_elements(struct iio_context *ctx)
{
	unsigned int i, j;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		for (j = 0; j < dev->nb_channels; j++) {
			struct iio_channel *chn = dev->channels[j];
			init_index(chn);
			init_data_format(chn);
		}
	}
}

struct iio_context * local_create_context(void)
{
	int ret = -ENOMEM;
	unsigned int len;
	struct utsname uts;
	struct iio_context *ctx = zalloc(sizeof(*ctx));
	if (!ctx)
		goto err_set_errno;

	ctx->ops = &local_ops;
	ctx->name = "local";

	ctx->pdata = zalloc(sizeof(*ctx->pdata));
	if (!ctx->pdata) {
		free(ctx);
		goto err_set_errno;
	}

	local_set_timeout(ctx, DEFAULT_TIMEOUT_MS);

	uname(&uts);
	len = strlen(uts.sysname) + strlen(uts.nodename) + strlen(uts.release)
		+ strlen(uts.version) + strlen(uts.machine);
	ctx->description = malloc(len + 5); /* 4 spaces + EOF */
	if (!ctx->description) {
		free(ctx->pdata);
		free(ctx);
		goto err_set_errno;
	}

	snprintf(ctx->description, len + 5, "%s %s %s %s %s", uts.sysname,
			uts.nodename, uts.release, uts.version, uts.machine);

	ret = foreach_in_dir(ctx, "/sys/bus/iio/devices", true, create_device);
	if (ret < 0)
		goto err_context_destroy;

	foreach_in_dir(ctx, "/sys/kernel/debug/iio", true, add_debug);

	init_scan_elements(ctx);
	ret = iio_context_init(ctx);
	if (ret < 0)
		goto err_context_destroy;

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
err_set_errno:
	errno = -ret;
	return NULL;
}

static int check_device(void *d, const char *path)
{
	*(bool *)d = true;
	return 0;
}

int local_context_scan(struct iio_scan_result *scan_result)
{
	struct iio_context_info **info;
	bool exists = false;
	int ret;

	ret = foreach_in_dir(&exists, "/sys/bus/iio/devices",
			true, check_device);
	if (ret < 0 || !exists)
		return 0;

	info = iio_scan_result_add(scan_result, 1);
	if (!info)
		return -ENOMEM;

	info[0]->description = "Local devices";
	info[0]->uri = "local:";

	return 0;
}
