// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dns_sd.h"
#include "iio-config.h"
#include "network.h"

#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include <iio/iiod-client.h>

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
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define close(s) closesocket(s)
#endif

struct iio_context_pdata {
	struct iiod_client_pdata io_ctx;
	struct addrinfo *addrinfo;
	struct iiod_client *iiod_client;
};

struct iio_device_pdata {
	struct iiod_client_pdata io_ctx;
	struct iiod_client *iiod_client;
	struct iiod_client_io *client_io;
};

static struct iio_context *
network_create_context(const struct iio_context_params *params,
		       const char *host);

static ssize_t
network_write_data(struct iiod_client_pdata *io_ctx, const char *src, size_t len);
static ssize_t
network_read_data(struct iiod_client_pdata *io_ctx, char *dst, size_t len);

static const struct iiod_client_ops network_iiod_client_ops = {
	.write = network_write_data,
	.read = network_read_data,
};

static ssize_t network_recv(struct iiod_client_pdata *io_ctx,
			    void *data, size_t len, int flags)
{
	unsigned int timeout_ms = io_ctx->timeout_ms;
	bool cancellable = true;
	ssize_t ret;
	int err;

#ifdef __linux__
	cancellable &= !(flags & MSG_DONTWAIT);
#endif

	while (1) {
		if (cancellable) {
			ret = wait_cancellable(io_ctx, true, timeout_ms);
			if (ret < 0)
				return ret;
		}

		ret = recv(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (network_should_retry(err)) {
			if (cancellable)
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
	unsigned int timeout_ms = io_ctx->timeout_ms;
	ssize_t ret;
	int err;

	while (1) {
		ret = wait_cancellable(io_ctx, false, timeout_ms);
		if (ret < 0)
			return ret;

		ret = send(io_ctx->fd, data, (int) len, flags);
		if (ret == 0)
			return -EPIPE;
		else if (ret > 0)
			break;

		err = network_get_error();
		if (!network_should_retry(err) && !network_is_interrupted(err))
			return (ssize_t) err;
	}

	return ret;
}

static void network_cancel(const struct iio_device *dev)
{
	struct iio_device_pdata *ppdata = iio_device_get_pdata(dev);

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

	ret = set_blocking_mode(fd, false);
	if (ret < 0)
		return ret;

	ret = connect(fd, addrinfo->ai_addr, (int) addrinfo->ai_addrlen);
	if (ret < 0) {
		ret = network_get_error();
		if (!network_connect_in_progress(ret))
			return ret;
	}

	ret = do_select(fd, timeout);
	if (ret < 0)
		return ret;

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

int create_socket(const struct addrinfo *addrinfo, unsigned int timeout)
{
	int ret, fd, yes = 1;

	fd = do_create_socket(addrinfo);
	if (fd < 0)
		return fd;

	ret = do_connect(fd, addrinfo, timeout);
	if (ret < 0) {
		close(fd);
		return ret;
	}

	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				(const char *) &yes, sizeof(yes)) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	return fd;
}

static char * network_get_description(struct addrinfo *res,
				      const struct iio_context_params *params)
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
			prm_perror(params, -errno, "Unable to look up IPv6 address");
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
					iio_snprintf(ptr, IF_NAMESIZE, "%u", in->sin6_scope_id);
				} else
#endif
				{
					prm_err(params,
						"Unable to lookup interface of IPv6 address\n");
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

static struct iiod_client *
network_setup_iiod_client(const struct iio_device *dev,
			  struct iiod_client_pdata *io_ctx)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iiod_client *client;
	unsigned int timeout_ms;
	int ret;

	/*
	 * Use the timeout that was set when creating the context.
	 * See commit 9eff490 for more info.
	 */
	timeout_ms = pdata->io_ctx.params->timeout_ms;

	ret = create_socket(pdata->addrinfo, timeout_ms);
	if (ret < 0) {
		dev_perror(dev, ret, "Unable to create socket");
		return iio_ptr(ret);
	}

	io_ctx->fd = ret;
	io_ctx->cancelled = false;
	io_ctx->timeout_ms = timeout_ms;

	ret = setup_cancel(io_ctx);
	if (ret < 0) {
		goto err_close_socket;
	}

	client = iiod_client_new(pdata->io_ctx.params, io_ctx,
				 &network_iiod_client_ops);
	ret = iio_err(client);
	if (ret < 0) {
		dev_perror(dev, ret, "Unable to create IIOD client");
		goto err_cleanup_cancel;
	}

	ret = set_blocking_mode(io_ctx->fd, false);
	if (ret < 0) {
		dev_perror(dev, ret, "Unable to set blocking mode");
		goto err_free_iiod_client;
	}

	io_ctx->timeout_ms = pdata->io_ctx.timeout_ms;

	return client;

err_free_iiod_client:
	iiod_client_destroy(client);
err_cleanup_cancel:
	cleanup_cancel(io_ctx);
err_close_socket:
	close(io_ctx->fd);
	return iio_ptr(ret);
}

static void network_free_iiod_client(struct iiod_client *client,
				     struct iiod_client_pdata *io_ctx)
{
	iiod_client_destroy(client);
	cleanup_cancel(io_ctx);
	close(io_ctx->fd);
	io_ctx->fd = -1;
}

static int network_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_device_pdata *ppdata = iio_device_get_pdata(dev);
	struct iiod_client_io *io;
	struct iiod_client *client;
	int ret;

	client = network_setup_iiod_client(dev, &ppdata->io_ctx);
	ret = iio_err(client);
	if (ret)
		return ret;

	io = iiod_client_open_unlocked(client, dev, samples_count, cyclic);
	ret = iio_err(io);
	if (ret) {
		dev_perror(dev, ret, "Unable to open device");
		goto err_free_iiod_client;
	}

	ppdata->iiod_client = client;
	ppdata->client_io = io;

	return 0;

err_free_iiod_client:
	network_free_iiod_client(client, &ppdata->io_ctx);
	return ret;
}

static int network_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = iio_device_get_pdata(dev);
	struct iiod_client *client = pdata->iiod_client;
	int ret = -EBADF;

	if (!client)
		return -EBADF;

	iiod_client_mutex_lock(client);

	if (pdata->io_ctx.fd >= 0) {
		if (!pdata->io_ctx.cancelled)
			ret = iiod_client_close_unlocked(pdata->client_io);
		else
			ret = 0;

		cleanup_cancel(&pdata->io_ctx);
		close(pdata->io_ctx.fd);
		pdata->io_ctx.fd = -1;
	}

	iiod_client_mutex_unlock(client);

	iiod_client_destroy(client);

	return ret;
}

static ssize_t network_read(const struct iio_device *dev, void *dst, size_t len,
			    struct iio_channels_mask *mask)
{
	struct iio_device_pdata *pdata = iio_device_get_pdata(dev);

	return iiod_client_read(pdata->iiod_client, dev, dst, len, mask);
}

static ssize_t network_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	struct iio_device_pdata *pdata = iio_device_get_pdata(dev);

	return iiod_client_write(pdata->iiod_client, dev, src, len);
}

static ssize_t network_read_dev_attr(const struct iio_device *dev,
				     unsigned int buf_id, const char *attr,
				     char *dst, size_t len,
				     enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_read_attr(pdata->iiod_client, dev, NULL,
				     attr, dst, len, type, buf_id);
}

static ssize_t network_write_dev_attr(const struct iio_device *dev,
				      unsigned int buf_id, const char *attr,
				      const char *src, size_t len,
				      enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_write_attr(pdata->iiod_client, dev, NULL,
				      attr, src, len, type, buf_id);
}

static ssize_t network_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_read_attr(pdata->iiod_client, dev, chn,
				     attr, dst, len, false, 0);
}

static ssize_t network_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_write_attr(pdata->iiod_client, dev, chn,
				      attr, src, len, false, 0);
}

static int network_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_get_trigger(pdata->iiod_client, dev, trigger);
}

static int network_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_trigger(pdata->iiod_client, dev, trigger);
}

static void network_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	unsigned int i;

	do_cancel(&pdata->io_ctx);
	pdata->io_ctx.cancelled = true;

	close(pdata->io_ctx.fd);
	cleanup_cancel(&pdata->io_ctx);

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *dpdata = iio_device_get_pdata(dev);

		if (dpdata) {
			network_close(dev);
			free(dpdata);
		}
	}

	network_free_iiod_client(pdata->iiod_client, &pdata->io_ctx);
	freeaddrinfo(pdata->addrinfo);
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
	unsigned int remote_timeout = calculate_remote_timeout(timeout);
	int ret;

	ret = iiod_client_set_timeout(pdata->iiod_client, remote_timeout);
	if (ret < 0) {
		char buf[1024];
		iio_strerror(-ret, buf, sizeof(buf));
		ctx_warn(ctx, "Unable to set R/W timeout: %s\n", buf);
		return ret;
	}

	pdata->io_ctx.timeout_ms = timeout;

	return 0;
}

static int network_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
						    dev, nb_blocks);
}

static struct iio_context * network_clone(const struct iio_context *ctx)
{
	const struct iio_context_params *params = iio_context_get_params(ctx);
	const char *addr = iio_context_get_attr_value(ctx, "ip,ip-addr");

	return network_create_context(params, addr);
}

static const struct iio_backend_ops network_ops = {
	.scan = IF_ENABLED(HAVE_DNS_SD, dnssd_context_scan),
	.create = network_create_context,
	.clone = network_clone,
	.open = network_open,
	.close = network_close,
	.read = network_read,
	.write = network_write,
	.read_device_attr = network_read_dev_attr,
	.write_device_attr = network_write_dev_attr,
	.read_channel_attr = network_read_chn_attr,
	.write_channel_attr = network_write_chn_attr,
	.get_trigger = network_get_trigger,
	.set_trigger = network_set_trigger,
	.shutdown = network_shutdown,
	.set_timeout = network_set_timeout,
	.set_kernel_buffers_count = network_set_kernel_buffers_count,

	.cancel = network_cancel,
};

__api_export_if(WITH_NETWORK_BACKEND_DYNAMIC)
const struct iio_backend iio_ip_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "network",
	.uri_prefix = "ip:",
	.ops = &network_ops,
	.default_timeout_ms = 5000,
};

static ssize_t network_write_data(struct iiod_client_pdata *io_ctx,
				  const char *src, size_t len)
{
	return network_send(io_ctx, src, len, 0);
}

static ssize_t network_read_data(struct iiod_client_pdata *io_ctx,
				 char *dst, size_t len)
{
	return network_recv(io_ctx, dst, len, 0);
}

static struct iio_context * network_create_context(const struct iio_context_params *params,
						   const char *host)
{
	struct addrinfo hints, *res;
	struct iio_context *ctx;
	struct iiod_client *iiod_client;
	struct iio_context_pdata *pdata;
	unsigned int i;
	int fd, ret;
	char *description, *end, *port = NULL;
	const char *ctx_attrs[] = { "ip,ip-addr", "uri" };
	const char *ctx_values[2];
	char uri[FQDN_LEN + 3];
	char port_str[6];
	uint16_t port_num = IIOD_PORT;

#ifdef _WIN32
	WSADATA wsaData;

	ret = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (ret < 0) {
		prm_perror(params, ret, "WSAStartup failed");
		return iio_ptr(ret);
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
		tmp = strtoul(port, &end, 0);
		if (port == end || tmp > 0xFFFF) {
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

		ret = dnssd_discover_host(params, addr_str,
					  sizeof(addr_str), &port_num);
		if (ret < 0) {
			char buf[1024];
			iio_strerror(-ret, buf, sizeof(buf));
			prm_dbg(params, "Unable to find host: %s\n", buf);
			return iio_ptr(ret);
		}
		if (!strlen(addr_str)) {
			prm_dbg(params, "No DNS Service Discovery hosts on network\n");
			return iio_ptr(-ENOENT);
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

			prm_dbg(params, "'getaddrinfo()' failed: %s. Trying dnssd as a last resort...\n",
				gai_strerror(ret));

			ret = dnssd_resolve_host(params, host, addr_str, sizeof(addr_str));
			if (ret) {
				char buf[256];

				iio_strerror(-ret, buf, sizeof(buf));
				prm_dbg(params, "Unable to find host: %s\n", buf);
				return iio_ptr(ret);
			}

			ret = getaddrinfo(addr_str, port, &hints, &res);
		}
	}

	if (ret) {
		prm_err(params, "Unable to find host: %s\n", gai_strerror(ret));
		return iio_ptr(ret);
	}

	fd = create_socket(res, params->timeout_ms);
	if (fd < 0) {
		ret = fd;
		goto err_free_addrinfo;
	}

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		ret = -ENOMEM;
		goto err_close_socket;
	}

	description = network_get_description(res, params);
	ret = iio_err(description);
	if (ret)
		goto err_free_pdata;

	pdata->addrinfo = res;
	pdata->io_ctx.fd = fd;
	pdata->io_ctx.params = params;
	pdata->io_ctx.timeout_ms = params->timeout_ms;
	pdata->io_ctx.ctx_pdata = pdata;

	ret = setup_cancel(&pdata->io_ctx);
	if (ret)
		goto err_free_description;

	iiod_client = iiod_client_new(params, &pdata->io_ctx,
				      &network_iiod_client_ops);
	ret = iio_err(iiod_client);
	if (ret)
		goto err_cleanup_cancel;

	pdata->iiod_client = iiod_client;

	if (host && host[0])
		iio_snprintf(uri, sizeof(uri), "ip:%s", host);
	else
		iio_snprintf(uri, sizeof(uri), "ip:%s\n", description);

	ctx_values[0] = description;
	ctx_values[1] = uri;

	prm_dbg(params, "Creating context...\n");
	ctx = iiod_client_create_context(pdata->iiod_client,
					 &iio_ip_backend, description,
					 ctx_attrs, ctx_values,
					 ARRAY_SIZE(ctx_values));
	ret = iio_err(ctx);
	if (ret)
		goto err_destroy_iiod_client;

	iio_context_set_pdata(ctx, pdata);

	/* pdata->io_ctx.params points to the 'params' function argument,
	 * switch it to our local one now */
	params = iio_context_get_params(ctx);
	pdata->io_ctx.params = params;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *ppdata;

		ppdata = zalloc(sizeof(*ppdata));
		if (!ppdata) {
			ret = -ENOMEM;
			goto err_network_shutdown;
		}

		iio_device_set_pdata(dev, ppdata);

		ppdata->io_ctx.fd = -1;
		ppdata->io_ctx.timeout_ms = params->timeout_ms;
		ppdata->io_ctx.params = params;
		ppdata->io_ctx.ctx_pdata = pdata;
	}

	iiod_client_set_timeout(pdata->iiod_client,
			calculate_remote_timeout(params->timeout_ms));
	return ctx;

err_network_shutdown:
	free(description);
	iio_context_destroy(ctx);
	return iio_ptr(ret);

err_destroy_iiod_client:
	do_cancel(&pdata->io_ctx);
	pdata->io_ctx.cancelled = true;
	iiod_client_destroy(iiod_client);
err_cleanup_cancel:
	cleanup_cancel(&pdata->io_ctx);
err_free_description:
	free(description);
err_free_pdata:
	free(pdata);
err_close_socket:
	close(fd);
err_free_addrinfo:
	freeaddrinfo(res);
	return iio_ptr(ret);
}
