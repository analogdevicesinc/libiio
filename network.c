// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "debug.h"
#include "dns_sd.h"
#include "iio-config.h"
#include "iio-private.h"
#include "iio-lock.h"
#include "iiod-client.h"
#include "network.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <netioapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
/*
 * Old OSes (CentOS 7, Ubuntu 16.04) require this for the
 * IN6_IS_ADDR_LINKLOCAL() macro to work.
 */
#ifndef _GNU_SOURCE
#define __USE_MISC
#include <netinet/in.h>
#undef __USE_MISC
#else
#include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define close(s) closesocket(s)
#define NETWORK_ERR_TIMEOUT WSAETIMEDOUT
#else
#define NETWORK_ERR_TIMEOUT ETIMEDOUT
#endif

#define DEFAULT_TIMEOUT_MS 5000

struct iio_context_pdata {
	struct iiod_client_pdata io_ctx;
	struct addrinfo *addrinfo;
	struct iiod_client *iiod_client;
	bool msg_trunc_supported;
};

struct iio_device_pdata {
	struct iiod_client_pdata io_ctx;
#ifdef WITH_NETWORK_GET_BUFFER
	int memfd;
	void *mmap_addr;
	size_t mmap_len;
#endif
	bool wait_for_err_code, is_cyclic, is_tx;
	struct iio_mutex *lock;
};

static ssize_t network_recv(struct iiod_client_pdata *io_ctx,
		void *data, size_t len, int flags)
{
	ssize_t ret;
	int err;

	while (1) {
		ret = wait_cancellable(io_ctx, true);
		if (ret < 0)
			return ret;

		ret = recv(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (network_should_retry(err)) {
			if (io_ctx->cancellable)
				continue;
			else
				return -EPIPE;
		} else if (!network_is_interrupted(err)) {
			return (ssize_t) err;
		}
	}
	return ret;
}

static ssize_t network_send(struct iiod_client_pdata *io_ctx,
		const void *data, size_t len, int flags)
{
	ssize_t ret;
	int err;

	while (1) {
		ret = wait_cancellable(io_ctx, false);
		if (ret < 0)
			return ret;

		ret = send(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (network_should_retry(err)) {
			if (io_ctx->cancellable)
				continue;
			else
				return -EPIPE;
		} else if (!network_is_interrupted(err)) {
			return (ssize_t) err;
		}
	}

	return ret;
}

static ssize_t write_all(struct iiod_client_pdata *io_ctx,
		const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;
	while (len) {
		ssize_t ret = network_send(io_ctx, (const void *) ptr, len, 0);
		if (ret < 0)
			return ret;
		ptr += ret;
		len -= ret;
	}
	return (ssize_t)(ptr - (uintptr_t) src);
}

static ssize_t write_command(struct iiod_client_pdata *io_ctx,
		const char *cmd)
{
	ssize_t ret;

	IIO_DEBUG("Writing command: %s\n", cmd);
	ret = write_all(io_ctx, cmd, strlen(cmd));
	if (ret < 0) {
		char buf[1024];
		iio_strerror(-(int) ret, buf, sizeof(buf));
		IIO_ERROR("Unable to send command: %s\n", buf);
	}
	return ret;
}

static void network_cancel(const struct iio_device *dev)
{
	struct iio_device_pdata *ppdata = dev->pdata;

	do_cancel(&ppdata->io_ctx);

	ppdata->io_ctx.cancelled = true;
}

/* The purpose of this function is to provide a version of connect()
 * that does not ignore timeouts... */
static int do_connect(int fd, const struct addrinfo *addrinfo,
	unsigned int timeout)
{
	int ret, error;
	socklen_t len;
#ifdef _WIN32
	struct timeval tv;
	struct timeval *ptv;
	fd_set set;
#else
	struct pollfd pfd;
#endif

	ret = set_blocking_mode(fd, false);
	if (ret < 0)
		return ret;

	ret = connect(fd, addrinfo->ai_addr, (int) addrinfo->ai_addrlen);
	if (ret < 0) {
		ret = network_get_error();
		if (!network_connect_in_progress(ret))
			return ret;
	}

#ifdef _WIN32
#ifdef _MSC_BUILD
	/* This is so stupid, but studio emits a signed/unsigned mismatch
	 * on their own FD_ZERO macro, so turn the warning off/on
	 */
#pragma warning(disable : 4389)
#endif
	FD_ZERO(&set);
	FD_SET(fd, &set);
#ifdef _MSC_BUILD
#pragma warning(default: 4389)
#endif

	if (timeout != 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		ptv = &tv;
	} else {
		ptv = NULL;
	}

	ret = select(fd + 1, NULL, &set, &set, ptv);
#else
	pfd.fd = fd;
	pfd.events = POLLOUT | POLLERR;
	pfd.revents = 0;

	do {
		ret = poll(&pfd, 1, timeout);
	} while (ret == -1 && errno == EINTR);
#endif

	if (ret < 0)
		return network_get_error();

	if (ret == 0)
		return -NETWORK_ERR_TIMEOUT;

	/* Verify that we don't have an error */
	len = sizeof(error);
	ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &len);
	if(ret < 0)
		return network_get_error();

	if (error)
		return -error;

	ret = set_blocking_mode(fd, true);
	if (ret < 0)
		return ret;

	return 0;
}

int create_socket(const struct addrinfo *addrinfo)
{
	const unsigned int timeout = DEFAULT_TIMEOUT_MS;
	int ret, fd, yes = 1;

	fd = do_create_socket(addrinfo);
	if (fd < 0)
		return fd;

	ret = do_connect(fd, addrinfo, timeout);
	if (ret < 0) {
		close(fd);
		return ret;
	}

	set_socket_timeout(fd, timeout);
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				(const char *) &yes, sizeof(yes)) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	return fd;
}

static char * network_get_description(struct addrinfo *res)
{
	char *description;
	unsigned int len;

#ifdef HAVE_IPV6
	len = INET6_ADDRSTRLEN + IF_NAMESIZE + 2;
#else
	len = INET_ADDRSTRLEN + 1;
#endif

	description = malloc(len);
	if (!description) {
		errno = ENOMEM;
		return NULL;
	}

	description[0] = '\0';

#ifdef HAVE_IPV6
	if (res->ai_family == AF_INET6) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *) res->ai_addr;
		char *ptr;
		const char *ptr2;
		ptr2 = inet_ntop(AF_INET6, &in->sin6_addr,
				description, INET6_ADDRSTRLEN);
		if (!ptr2) {
			char buf[256];
			iio_strerror(errno, buf, sizeof(buf));
			IIO_ERROR("Unable to look up IPv6 address: %s\n", buf);
			free(description);
			return NULL;
		}

		if (IN6_IS_ADDR_LINKLOCAL(&in->sin6_addr)) {
			ptr = if_indextoname(in->sin6_scope_id, description +
					strnlen(description, len) + 1);
			if (!ptr) {
#ifdef _WIN32
				if (errno == 0) {
					/* Windows uses numerical interface identifiers */
					ptr = description + strnlen(description, len) + 1;
					iio_snprintf(ptr, IF_NAMESIZE, "%u", (unsigned int)in->sin6_scope_id);
				} else
#endif
				{
					IIO_ERROR("Unable to lookup interface of IPv6 address\n");
					free(description);
					return NULL;
				}
			}

			*(ptr - 1) = '%';
		}
	}
#endif
	if (res->ai_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *) res->ai_addr;
#if (!_WIN32 || _WIN32_WINNT >= 0x600)
		inet_ntop(AF_INET, &in->sin_addr, description, INET_ADDRSTRLEN);
#else
		char *tmp = inet_ntoa(in->sin_addr);
		iio_strlcpy(description, tmp, len);
#endif
	}

	return description;
}

static int network_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);
	struct iio_device_pdata *ppdata = dev->pdata;
	int ret = -EBUSY;

	iio_mutex_lock(ppdata->lock);
	if (ppdata->io_ctx.fd >= 0)
		goto out_mutex_unlock;

	ret = create_socket(pdata->addrinfo);
	if (ret < 0) {
		IIO_ERROR("Create socket: %d\n", ret);
		goto out_mutex_unlock;
	}

	ppdata->io_ctx.fd = ret;
	ppdata->io_ctx.cancelled = false;
	ppdata->io_ctx.cancellable = false;
	ppdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;

	ret = iiod_client_open_unlocked(pdata->iiod_client,
			&ppdata->io_ctx, dev, samples_count, cyclic);
	if (ret < 0) {
		IIO_ERROR("Open unlocked: %d\n", ret);
		goto err_close_socket;
	}

	ret = setup_cancel(&ppdata->io_ctx);
	if (ret < 0)
		goto err_close_socket;

	ret = set_blocking_mode(ppdata->io_ctx.fd, false);
	if (ret)
		goto err_cleanup_cancel;

	set_socket_timeout(ppdata->io_ctx.fd, pdata->io_ctx.timeout_ms);

	ppdata->io_ctx.timeout_ms = pdata->io_ctx.timeout_ms;
	ppdata->io_ctx.cancellable = true;
	ppdata->is_tx = iio_device_is_tx(dev);
	ppdata->is_cyclic = cyclic;
	ppdata->wait_for_err_code = false;
#ifdef WITH_NETWORK_GET_BUFFER
	ppdata->mmap_len = samples_count * iio_device_get_sample_size(dev);
#endif

	iio_mutex_unlock(ppdata->lock);

	return 0;

err_cleanup_cancel:
	cleanup_cancel(&ppdata->io_ctx);
err_close_socket:
	close(ppdata->io_ctx.fd);
	ppdata->io_ctx.fd = -1;
out_mutex_unlock:
	iio_mutex_unlock(ppdata->lock);
	return ret;
}

static int network_close(const struct iio_device *dev)
{
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(dev->ctx);
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBADF;

	iio_mutex_lock(pdata->lock);

	if (pdata->io_ctx.fd >= 0) {
		if (!pdata->io_ctx.cancelled) {
			ret = iiod_client_close_unlocked(
					ctx_pdata->iiod_client,
					&pdata->io_ctx, dev);

			write_command(&pdata->io_ctx, "\r\nEXIT\r\n");
		} else {
			ret = 0;
		}

		cleanup_cancel(&pdata->io_ctx);
		close(pdata->io_ctx.fd);
		pdata->io_ctx.fd = -1;
	}

#ifdef WITH_NETWORK_GET_BUFFER
	if (pdata->memfd >= 0)
		close(pdata->memfd);
	pdata->memfd = -1;

	if (pdata->mmap_addr) {
		munmap(pdata->mmap_addr, pdata->mmap_len);
		pdata->mmap_addr = NULL;
	}
#endif

	iio_mutex_unlock(pdata->lock);
	return ret;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(dev->ctx);
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_read_unlocked(ctx_pdata->iiod_client,
			&pdata->io_ctx, dev, dst, len, mask, words);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t network_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(dev->ctx);
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_write_unlocked(ctx_pdata->iiod_client,
			&pdata->io_ctx, dev, src, len);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

#ifdef WITH_NETWORK_GET_BUFFER

static ssize_t read_all(struct iiod_client_pdata *io_ctx,
		void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;
	while (len) {
		ssize_t ret = network_recv(io_ctx, (void *) ptr, len, 0);
		if (ret < 0) {
			IIO_ERROR("NETWORK RECV: %zu\n", ret);
			return ret;
		}
		ptr += ret;
		len -= ret;
	}
	return (ssize_t)(ptr - (uintptr_t) dst);
}

static int read_integer(struct iiod_client_pdata *io_ctx, long *val)
{
	unsigned int i;
	char buf[1024], *ptr;
	ssize_t ret;
	bool found = false;

	for (i = 0; i < sizeof(buf) - 1; i++) {
		ret = read_all(io_ctx, buf + i, 1);
		if (ret < 0)
			return (int) ret;

		/* Skip the eventual first few carriage returns.
		 * Also stop when a dot is found (for parsing floats) */
		if (buf[i] != '\n' && buf[i] != '.')
			found = true;
		else if (found)
			break;
	}

	buf[i] = '\0';
	errno = 0;
	ret = (ssize_t) strtol(buf, &ptr, 10);
	if (ptr == buf || errno == ERANGE)
		return -EINVAL;
	*val = (long) ret;
	return 0;
}

static ssize_t network_read_mask(struct iiod_client_pdata *io_ctx,
		uint32_t *mask, size_t words)
{
	long read_len;
	ssize_t ret;

	ret = read_integer(io_ctx, &read_len);
	if (ret < 0) {
		IIO_ERROR("READ INTEGER: %zu\n", ret);
		return ret;
	}

	if (read_len > 0 && mask) {
		size_t i;
		char buf[9];

		buf[8] = '\0';
		IIO_DEBUG("Reading mask\n");

		for (i = words; i > 0; i--) {
			ret = read_all(io_ctx, buf, 8);
			if (ret < 0)
				return ret;

			iio_sscanf(buf, "%08x", &mask[i - 1]);
			IIO_DEBUG("mask[%lu] = 0x%x\n",
					(unsigned long)(i - 1), mask[i - 1]);
		}
	}

	if (read_len > 0) {
		char c;
		ssize_t nb = read_all(io_ctx, &c, 1);
		if (nb > 0 && c != '\n')
			read_len = -EIO;
	}

	return (ssize_t) read_len;
}

static ssize_t read_error_code(struct iiod_client_pdata *io_ctx)
{
	/*
	 * The server returns two integer codes.
	 * The first one is returned right after the WRITEBUF command is issued,
	 * and corresponds to the error code returned when the server attempted
	 * to open the device.
	 * If zero, a second error code is returned, that corresponds (if positive)
	 * to the number of bytes written.
	 *
	 * To speed up things, we delay error reporting. We just send out the
	 * data without reading the error code that the server gives us, because
	 * the answer will take too much time. If an error occurred, it will be
	 * reported by the next call to iio_buffer_push().
	 */

	unsigned int i;
	long resp = 0;

	for (i = 0; i < 2; i++) {
		ssize_t ret = read_integer(io_ctx, &resp);
		if (ret < 0)
			return ret;
		if (resp < 0)
			return (ssize_t) resp;
	}

	return (ssize_t) resp;
}

static ssize_t write_rwbuf_command(const struct iio_device *dev,
		const char *cmd)
{
	struct iio_device_pdata *pdata = dev->pdata;

	if (pdata->wait_for_err_code) {
		ssize_t ret = read_error_code(&pdata->io_ctx);

		pdata->wait_for_err_code = false;
		if (ret < 0)
			return ret;
	}

	return write_command(&pdata->io_ctx, cmd);
}

static ssize_t network_do_splice(struct iio_device_pdata *pdata, size_t len,
		bool read)
{
	int pipefd[2];
	int fd_in, fd_out;
	ssize_t ret, read_len = len, write_len = 0;

	ret = (ssize_t) pipe2(pipefd, O_CLOEXEC);
	if (ret < 0)
		return -errno;

	if (read) {
	    fd_in = pdata->io_ctx.fd;
	    fd_out = pdata->memfd;
	} else {
	    fd_in = pdata->memfd;
	    fd_out = pdata->io_ctx.fd;
	}

	do {
		ret = wait_cancellable(&pdata->io_ctx, read);
		if (ret < 0)
			goto err_close_pipe;

		if (read_len) {
			/*
			 * SPLICE_F_NONBLOCK is just here to avoid a deadlock when
			 * splicing from a socket. As the socket is not in
			 * non-blocking mode, it should never return -EAGAIN.
			 * TODO(pcercuei): Find why it locks...
			 * */
			ret = splice(fd_in, NULL, pipefd[1], NULL, read_len,
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
			if (!ret)
				ret = -EIO;
			if (ret < 0 && errno != EAGAIN) {
				ret = -errno;
				goto err_close_pipe;
			} else if (ret > 0) {
				write_len += ret;
				read_len -= ret;
			}
		}

		if (write_len) {
			ret = splice(pipefd[0], NULL, fd_out, NULL, write_len,
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
			if (!ret)
				ret = -EIO;
			if (ret < 0 && errno != EAGAIN) {
				ret = -errno;
				goto err_close_pipe;
			} else if (ret > 0) {
				write_len -= ret;
			}
		}

	} while (write_len || read_len);

err_close_pipe:
	close(pipefd[0]);
	close(pipefd[1]);
	return ret < 0 ? ret : (ssize_t)len;
}

static ssize_t network_get_buffer(const struct iio_device *dev,
		void **addr_ptr, size_t bytes_used,
		uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret, read = 0;
	int memfd;

	if (pdata->is_cyclic)
		return -ENOSYS;

	/* We check early that the temporary file can be created, so that we can
	 * return -ENOSYS in case it fails, which will indicate that the
	 * high-speed interface is not available.
	 *
	 * O_TMPFILE -> Linux 3.11.
	 * TODO: use memfd_create (Linux 3.17) */
	memfd = open(P_tmpdir, O_RDWR | O_TMPFILE | O_EXCL | O_CLOEXEC, S_IRWXU);
	if (memfd < 0)
		return -ENOSYS;

	if (!addr_ptr || words != (dev->nb_channels + 31) / 32) {
		close(memfd);
		return -EINVAL;
	}

	if (pdata->mmap_addr)
		munmap(pdata->mmap_addr, pdata->mmap_len);

	if (pdata->mmap_addr && pdata->is_tx) {
		char buf[1024];

		iio_snprintf(buf, sizeof(buf), "WRITEBUF %s %lu\r\n",
				dev->id, (unsigned long) bytes_used);

		iio_mutex_lock(pdata->lock);

		ret = write_rwbuf_command(dev, buf);
		if (ret < 0)
			goto err_close_memfd;

		ret = network_do_splice(pdata, bytes_used, false);
		if (ret < 0)
			goto err_close_memfd;

		pdata->wait_for_err_code = true;
		iio_mutex_unlock(pdata->lock);
	}

	if (pdata->memfd >= 0)
		close(pdata->memfd);

	pdata->memfd = memfd;

	ret = (ssize_t) ftruncate(pdata->memfd, pdata->mmap_len);
	if (ret < 0) {
		ret = -errno;
		IIO_ERROR("Unable to truncate temp file: %zi\n", -ret);
		return ret;
	}

	if (!pdata->is_tx) {
		char buf[1024];
		size_t len = pdata->mmap_len;

		iio_snprintf(buf, sizeof(buf), "READBUF %s %lu\r\n",
				dev->id, (unsigned long) len);

		iio_mutex_lock(pdata->lock);
		ret = write_rwbuf_command(dev, buf);
		if (ret < 0)
			goto err_unlock;

		do {
			ret = network_read_mask(&pdata->io_ctx, mask, words);
			if (!ret)
				break;
			if (ret < 0)
				goto err_unlock;

			mask = NULL; /* We read the mask only once */

			ret = network_do_splice(pdata, ret, true);
			if (ret < 0)
				goto err_unlock;

			read += ret;
			len -= ret;
		} while (len);

		iio_mutex_unlock(pdata->lock);
	}

	pdata->mmap_addr = mmap(NULL, pdata->mmap_len,
			PROT_READ | PROT_WRITE, MAP_SHARED, pdata->memfd, 0);
	if (pdata->mmap_addr == MAP_FAILED) {
		pdata->mmap_addr = NULL;
		ret = -errno;
		IIO_ERROR("Unable to mmap: %zi\n", -ret);
		return ret;
	}

	*addr_ptr = pdata->mmap_addr;
	return read ? read : (ssize_t) bytes_used;

err_close_memfd:
	close(memfd);
err_unlock:
	iio_mutex_unlock(pdata->lock);
	return ret;
}
#endif

static ssize_t network_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr, dst, len, type);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr, src, len, type);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(chn->dev->ctx);

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr, dst, len, false);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(chn->dev->ctx);

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr, src, len, false);
}

static int network_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);

	return iiod_client_get_trigger(pdata->iiod_client,
			&pdata->io_ctx, dev, trigger);
}

static int network_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);

	return iiod_client_set_trigger(pdata->iiod_client,
			&pdata->io_ctx, dev, trigger);
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	unsigned int i;

	iiod_client_mutex_lock(pdata->iiod_client);
	write_command(&pdata->io_ctx, "\r\nEXIT\r\n");
	close(pdata->io_ctx.fd);
	iiod_client_mutex_unlock(pdata->iiod_client);

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *dpdata = dev->pdata;

		if (dpdata) {
			network_close(dev);
			iio_mutex_destroy(dpdata->lock);
			free(dpdata);
		}
	}

	iiod_client_destroy(pdata->iiod_client);
	freeaddrinfo(pdata->addrinfo);
}

static int network_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_get_version(pdata->iiod_client,
			&pdata->io_ctx, major, minor, git_tag);
}

static unsigned int calculate_remote_timeout(unsigned int timeout)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout / 2;
}

static int network_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	int ret, fd = pdata->io_ctx.fd;

	ret = set_socket_timeout(fd, timeout);
	if (!ret) {
		unsigned int remote_timeout = calculate_remote_timeout(timeout);

		ret = iiod_client_set_timeout(pdata->iiod_client,
			&pdata->io_ctx, remote_timeout);
		if (!ret)
			pdata->io_ctx.timeout_ms = timeout;
	}
	if (ret < 0) {
		char buf[1024];
		iio_strerror(-ret, buf, sizeof(buf));
		IIO_WARNING("Unable to set R/W timeout: %s\n", buf);
	}
	return ret;
}

static int network_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(dev->ctx);

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
			 &pdata->io_ctx, dev, nb_blocks);
}

static struct iio_context * network_clone(const struct iio_context *ctx)
{
	const char *addr = iio_context_get_attr_value(ctx, "ip,ip-addr");

	return iio_create_network_context(addr);
}

static const struct iio_backend_ops network_ops = {
	.clone = network_clone,
	.open = network_open,
	.close = network_close,
	.read = network_read,
	.write = network_write,
#ifdef WITH_NETWORK_GET_BUFFER
	.get_buffer = network_get_buffer,
#endif
	.read_device_attr = network_read_dev_attr,
	.write_device_attr = network_write_dev_attr,
	.read_channel_attr = network_read_chn_attr,
	.write_channel_attr = network_write_chn_attr,
	.get_trigger = network_get_trigger,
	.set_trigger = network_set_trigger,
	.shutdown = network_shutdown,
	.get_version = network_get_version,
	.set_timeout = network_set_timeout,
	.set_kernel_buffers_count = network_set_kernel_buffers_count,

	.cancel = network_cancel,
};

static ssize_t network_write_data(struct iio_context_pdata *pdata,
				  struct iiod_client_pdata *io_data,
				  const char *src, size_t len)
{
	struct iiod_client_pdata *io_ctx = io_data;

	return network_send(io_ctx, src, len, 0);
}

static ssize_t network_read_data(struct iio_context_pdata *pdata,
				 struct iiod_client_pdata *io_data,
				 char *dst, size_t len)
{
	struct iiod_client_pdata *io_ctx = io_data;

	return network_recv(io_ctx, dst, len, 0);
}

static ssize_t network_read_line(struct iio_context_pdata *pdata,
				 struct iiod_client_pdata *io_data,
				 char *dst, size_t len)
{
	bool found = false;
	size_t i;
#ifdef __linux__
	struct iiod_client_pdata *io_ctx = io_data;
	ssize_t ret;
	size_t bytes_read = 0;

	do {
		size_t to_trunc;

		ret = network_recv(io_ctx, dst, len, MSG_PEEK);
		if (ret < 0)
			return ret;

		/* Lookup for the trailing \n */
		for (i = 0; i < (size_t) ret && dst[i] != '\n'; i++);
		found = i < (size_t) ret;

		len -= ret;
		dst += ret;

		if (found)
			to_trunc = i + 1;
		else
			to_trunc = (size_t) ret;

		/* Advance the read offset to the byte following the \n if
		 * found, or after the last character read otherwise */
		if (pdata->msg_trunc_supported)
			ret = network_recv(io_ctx, NULL, to_trunc, MSG_TRUNC);
		else
			ret = network_recv(io_ctx, dst - ret, to_trunc, 0);
		if (ret < 0) {
			IIO_ERROR("NETWORK RECV: %zu\n", ret);
			return ret;
		}

		bytes_read += to_trunc;
	} while (!found && len);

	if (!found) {
		IIO_ERROR("EIO: %zu\n", ret);
		return -EIO;
	} else
		return bytes_read;
#else
	for (i = 0; i < len - 1; i++) {
		ssize_t ret = network_read_data(pdata, io_data, dst + i, 1);

		if (ret < 0)
			return ret;

		if (dst[i] != '\n')
			found = true;
		else if (found)
			break;
	}

	if (!found || i == len - 1)
		return -EIO;

	return (ssize_t) i + 1;
#endif
}

static const struct iiod_client_ops network_iiod_client_ops = {
	.write = network_write_data,
	.read = network_read_data,
	.read_line = network_read_line,
};

#ifdef __linux__
/*
 * As of build 16299, Windows Subsystem for Linux presents a Linux API but
 * without support for MSG_TRUNC. Since WSL allows running native Linux
 * applications this is not something that can be detected at compile time. If
 * we want to support WSL we have to have a runtime workaround.
 */
static bool msg_trunc_supported(struct iiod_client_pdata *io_ctx)
{
	int ret;

	ret = network_recv(io_ctx, NULL, 0, MSG_TRUNC | MSG_DONTWAIT);

	return ret != -EFAULT && ret != -EINVAL;
}
#else
static bool msg_trunc_supported(struct iiod_client_pdata *io_ctx)
{
	return false;
}
#endif

struct iio_context * network_create_context(const char *hostname)
{
	struct addrinfo hints, *res;
	struct iio_context *ctx;
	struct iiod_client *iiod_client;
	struct iio_context_pdata *pdata;
	size_t uri_len;
	unsigned int i;
	int fd, ret;
	char *description, *uri, *end, *port = NULL;
	char port_str[6];
	uint16_t port_num = IIOD_PORT;
	char host_buf[FQDN_LEN + sizeof(":65535") + 1];
	char *host = hostname ? host_buf : NULL;

	iio_strlcpy(host_buf, hostname, sizeof(host_buf));

#ifdef _WIN32
	WSADATA wsaData;

	ret = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (ret < 0) {
		IIO_ERROR("WSAStartup failed with error %i\n", ret);
		errno = -ret;
		return NULL;
	}
#endif
	/* ipv4 addresses are simply ip:port, e.g. 192.168.1.67:80
	 * Because IPv6 addresses contain colons, and URLs use colons to separate
	 * the host from the port number, RFC2732 (Format for Literal IPv6
	 * Addresses in URL's) specifies that an IPv6 address used as the
	 * host-part of a URL (our in our case the URI) should be enclosed
	 * in square brackets, e.g.
	 * [2001:db8:4006:812::200e] or [2001:db8:4006:812::200e]:8080
	 * A compressed form allows one string of 0s to be replaced by ‘::'
	 * e.g.: [FF01:0:0:0:0:0:0:1A2] can be represented by [FF01::1A2]
	 *
	 * RFC1123 (Requirements for Internet Hosts) requires valid hostnames to
	 * only contain the ASCII letters 'a' through 'z' (in a case-insensitive
	 * manner), the digits '0' through '9', and the hyphen ('-').
	 *
	 * We need to distinguish between:
	 *   - ipv4       192.168.1.67                       Default Port
	 *   - ipv4:port  192.168.1.67:50000                 Custom Port
	 *   - ipv6       2001:db8:4006:812::200e            Default Port
	 *   - [ip6]:port [2001:db8:4006:812::200e]:50000    Custom Port
	 *
	 *   We shouldn't test for ipv6 all the time, but it makes it easier.
	 */
	if (host) {
		char *first_colon = strchr(host, ':');
		char *last_colon = strrchr(host, ':');

		if (!first_colon) {
			/* IPv4 default address, no port, fall through */
		} else if (first_colon == last_colon) {
			/* Single colon, IPv4 non default port */
			*first_colon = '\0';
			port = first_colon + 1;
		} else if (*(last_colon - 1) == ']') {
			/* IPv6 address with port number starting at (last_colon + 1) */
			host++;
			*(last_colon - 1) = '\0';
			port = last_colon + 1;
		} else {
			/* IPv6 address with default port number */
		}
	}
	if (port) {
		unsigned long int tmp;

		errno = 0;
		tmp = strtoul(port, &end, 0);
		if (port == end || tmp > 0xFFFF || errno == ERANGE) {
			errno = ENOENT;
			return NULL;
		}

		port_num = (uint16_t)tmp;
	} else {
		port = port_str;
		iio_snprintf(port_str, sizeof(port_str), "%hu", port_num);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (HAVE_DNS_SD && (!host || !host[0])) {
		char addr_str[DNS_SD_ADDRESS_STR_MAX];

		ret = dnssd_discover_host(addr_str, sizeof(addr_str), &port_num);
		if (ret < 0) {
			char buf[1024];
			iio_strerror(-ret, buf, sizeof(buf));
			IIO_DEBUG("Unable to find host: %s\n", buf);
			errno = -ret;
			return NULL;
		}
		if (!strlen(addr_str)) {
			IIO_DEBUG("No DNS Service Discovery hosts on network\n");
			errno = ENOENT;
			return NULL;
		}

		ret = getaddrinfo(addr_str, port, &hints, &res);
	} else {
		ret = getaddrinfo(host, port, &hints, &res);
#ifdef _WIN32
		/* Yes, This is lame, but Windows is flakey with local addresses
		 * WSANO_DATA = Valid name, no data record of requested type.
		 * Normally when the host does not have the correct associated data
		 * being resolved for. Just ask again. Try a max of 2.5 seconds.
		 */
		for (i = 0; HAVE_DNS_SD && ret == WSANO_DATA && i < 10; i++) {
			Sleep(250);
			ret = getaddrinfo(host, port, &hints, &res);
		}
#endif
		/*
		 * It might be an avahi hostname which means that getaddrinfo() will only work if
		 * nss-mdns is installed on the host and /etc/nsswitch.conf is correctly configured
		 * which might be not the case for some minimalist distros. In this case,
		 * as a last resort, let's try to resolve the host with avahi...
		 */
		if (HAVE_DNS_SD && ret) {
			char addr_str[DNS_SD_ADDRESS_STR_MAX];

			IIO_DEBUG("'getaddrinfo()' failed: %s. Trying dnssd as a last resort...\n",
				  gai_strerror(ret));

			ret = dnssd_resolve_host(host, addr_str, sizeof(addr_str));
			if (ret) {
				char buf[256];

				iio_strerror(-ret, buf, sizeof(buf));
				IIO_DEBUG("Unable to find host: %s\n", buf);
				errno = -ret;
				return NULL;
			}

			ret = getaddrinfo(addr_str, port, &hints, &res);
		}
	}

	if (ret) {
		IIO_ERROR("Unable to find host: %s\n", gai_strerror(ret));
#ifndef _WIN32
		if (ret != EAI_SYSTEM)
			errno = -ret;
#endif
		return NULL;
	}

	fd = create_socket(res);
	if (fd < 0) {
		errno = -fd;
		goto err_free_addrinfo;
	}

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		errno = ENOMEM;
		goto err_close_socket;
	}

	description = network_get_description(res);
	if (!description)
		goto err_free_pdata;

	iiod_client = iiod_client_new(pdata, &network_iiod_client_ops);
	if (!iiod_client)
		goto err_free_description;

	pdata->iiod_client = iiod_client;
	pdata->io_ctx.fd = fd;
	pdata->addrinfo = res;
	pdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;

	pdata->msg_trunc_supported = msg_trunc_supported(&pdata->io_ctx);
	if (pdata->msg_trunc_supported)
		IIO_DEBUG("MSG_TRUNC is supported\n");
	else
		IIO_DEBUG("MSG_TRUNC is NOT supported\n");

	IIO_DEBUG("Creating context...\n");
	ctx = iiod_client_create_context(pdata->iiod_client, &pdata->io_ctx);
	if (!ctx)
		goto err_destroy_iiod_client;

	/* Override the name and low-level functions of the XML context
	 * with those corresponding to the network context */
	ctx->name = "network";
	ctx->ops = &network_ops;
	ctx->pdata = pdata;

	uri_len = strlen(description);
	if (host && host[0])
		uri_len = strnlen(host, FQDN_LEN);
	uri_len += sizeof ("ip:");

	uri = malloc(uri_len);
	if (!uri) {
		ret = -ENOMEM;
		goto err_network_shutdown;
	}

	ret = iio_context_add_attr(ctx, "ip,ip-addr", description);
	if (ret < 0)
		goto err_free_uri;

	if (host && host[0])
		iio_snprintf(uri, uri_len, "ip:%s", host);
	else
		iio_snprintf(uri, uri_len, "ip:%s\n", description);

	ret = iio_context_add_attr(ctx, "uri", uri);
	if (ret < 0)
		goto err_free_uri;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);

		dev->pdata = zalloc(sizeof(*dev->pdata));
		if (!dev->pdata) {
			ret = -ENOMEM;
			goto err_free_uri;
		}

		dev->pdata->io_ctx.fd = -1;
		dev->pdata->io_ctx.timeout_ms = DEFAULT_TIMEOUT_MS;
#ifdef WITH_NETWORK_GET_BUFFER
		dev->pdata->memfd = -1;
#endif

		dev->pdata->lock = iio_mutex_create();
		if (!dev->pdata->lock) {
			ret = -ENOMEM;
			goto err_free_uri;
		}
	}

	if (ctx->description) {
		size_t desc_len = strlen(description);
		size_t new_size = desc_len + strlen(ctx->description) + 2;
		char *ptr, *new_description = realloc(description, new_size);
		if (!new_description) {
			ret = -ENOMEM;
			goto err_free_uri;
		}

		ptr = strrchr(new_description, '\0');
		iio_snprintf(ptr, new_size - desc_len, " %s", ctx->description);
		free(ctx->description);

		ctx->description = new_description;
	} else {
		ctx->description = description;
	}

	free(uri);
	iiod_client_set_timeout(pdata->iiod_client, &pdata->io_ctx,
			calculate_remote_timeout(DEFAULT_TIMEOUT_MS));
	return ctx;

err_free_uri:
	free(uri);
err_network_shutdown:
	free(description);
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_destroy_iiod_client:
	iiod_client_destroy(iiod_client);
err_free_description:
	free(description);
err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
err_free_addrinfo:
	freeaddrinfo(res);
	return NULL;
}
