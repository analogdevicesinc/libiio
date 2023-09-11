// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 *         Matej Kenda <matejken@gmail.com>
 *
 * Some of this is insipred from libavahi's example:
 * https://avahi.org/doxygen/html/client-browse-services_8c-example.html
 * which is also LGPL 2.1 or later.
 */

#include "dns_sd.h"
#include "network.h"

#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>

#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define close(s) closesocket(s)
#endif

#define DEFAULT_TIMEOUT_MS 5000

static void dnssd_free_discovery_data(struct dns_sd_discovery_data *d)
{
	free(d->hostname);
	free(d->address);
	free(d);
}

/* Some functions for handling common linked list operations */
static void dnssd_remove_node(const struct iio_context_params *params,
			      struct dns_sd_discovery_data **ddata, int n)
{
	struct dns_sd_discovery_data *d, *ndata, *ldata, *tdata;
	int i;

	d = *ddata;
	ldata = NULL;

	if (n == 0) {
		tdata = d->next;
		dnssd_free_discovery_data(d);
		d = tdata;
	} else {
		for (i = 0, ndata = d; ndata->next != NULL; ndata = ndata->next) {
			if (i == n) {
				/* Could be NULL or node, both are OK */
				tdata = ndata->next;
				/* free the node to be removed */
				dnssd_free_discovery_data(ndata);
				/* snip it out */
				ldata->next = tdata;
				break;
			}
			ldata = ndata;
			i++;
		}
		if (i < n) {
			prm_err(params, "dnssd_remove_node call when %i exceeds "
				"list length (%i)\n", n, i);
		}
	}

	*ddata = d;
}

/* The only way to support scan context from the network is when
 * DNS Service Discovery is turned on
 */

static int dnssd_add_scan_result(const struct iio_context_params *params,
				 struct iio_scan *scan_ctx,
				 char *hostname, char *addr_str, uint16_t port)
{
	struct iio_context *ctx;
	char uri[sizeof("ip:") + FQDN_LEN + sizeof (":65535") + 1];
	char description[255], *p;
	const char *hw_model = NULL, *serial = NULL;
	const struct iio_attr *attr;
	unsigned int i;

	if (port == IIOD_PORT) {
		iio_snprintf(uri, sizeof(uri), "ip:%s", hostname);
	} else {
#ifdef HAVE_IPV6
		if (strchr(addr_str, ':'))
			iio_snprintf(uri, sizeof(uri), "ip:[%s]:%hu", hostname, port);
		else
#endif
			iio_snprintf(uri, sizeof(uri), "ip:%s:%hu", hostname, port);
	}

	ctx = iio_create_context(params, uri);
	if (!ctx) {
		prm_err(params, "No context at %s\n", addr_str);
		return -ENOMEM;
	}

	attr = iio_context_find_attr(ctx, "hw_model");
	if (attr)
		hw_model = iio_attr_get_static_value(attr);
	attr = iio_context_find_attr(ctx, "hw_serial");
	if (attr)
		serial = iio_attr_get_static_value(attr);

	if (hw_model && serial) {
		iio_snprintf(description, sizeof(description), "%s (%s), serial=%s",
				addr_str, hw_model, serial);
	} else if (hw_model) {
		iio_snprintf(description, sizeof(description), "%s %s", addr_str, hw_model);
	} else if (serial) {
		iio_snprintf(description, sizeof(description), "%s %s", addr_str, serial);
	} else if (iio_context_get_devices_count(ctx) == 0) {
		iio_snprintf(description, sizeof(description), "%s",
			     iio_context_get_description(ctx));
	} else {
		iio_snprintf(description, sizeof(description), "%s (", addr_str);
		p = description + strlen(description);
		for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
			const struct iio_device *dev = iio_context_get_device(ctx, i);
			const char *name = iio_device_get_name(dev);
			if (name) {
				iio_snprintf(p, sizeof(description) - strlen(description) -1,
						"%s,",  name);
				p += strlen(p);
			}
		}
		p--;
		*p = ')';
	}

	iio_context_destroy(ctx);

	return iio_scan_add_result(scan_ctx, description, uri);
}

/*
 * remove the ones in the list that you can't connect to
 * This is sort of silly, but we have seen non-iio devices advertised
 * and discovered on the network. Oh well....
 */
void port_knock_discovery_data(const struct iio_context_params *params,
			       struct dns_sd_discovery_data **ddata)
{
	struct dns_sd_discovery_data *d, *ndata;
	unsigned int timeout_ms;
	int i, ret;

	if (params->timeout_ms)
		timeout_ms = params->timeout_ms;
	else
		timeout_ms = DEFAULT_TIMEOUT_MS;

	d = *ddata;
	iio_mutex_lock(d->lock);
	for (i = 0, ndata = d; ndata->next != NULL; ) {
		char port_str[6];
		struct addrinfo hints, *res, *rp;
		int fd;
		bool found = false;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		iio_snprintf(port_str, sizeof(port_str), "%hu", ndata->port);
		ret = getaddrinfo(ndata->addr_str, port_str, &hints, &res);

		/* getaddrinfo() returns a list of address structures */
		if (ret) {
			prm_dbg(params, "Unable to find host ('%s'): %s\n",
				ndata->hostname,
				gai_strerror(ret));
		} else {
			for (rp = res; rp != NULL; rp = rp->ai_next) {
				fd = create_socket(rp, timeout_ms);
				if (fd < 0) {
					prm_dbg(params, "Unable to open %s%s socket ('%s:%d' %s)\n",
						rp->ai_family == AF_INET ? "ipv4" : "",
						rp->ai_family == AF_INET6? "ipv6" : "",
						ndata->hostname, ndata->port,
						ndata->addr_str);
				} else {
					close(fd);
					prm_dbg(params, "Something %s%s at '%s:%d' %s)\n",
						rp->ai_family == AF_INET ? "ipv4" : "",
						rp->ai_family == AF_INET6? "ipv6" : "",
						ndata->hostname, ndata->port,
						ndata->addr_str);
					found = true;
				}
			}
		}
		freeaddrinfo(res);
		ndata = ndata->next;
		if (found) {
			i++;
		} else {
			dnssd_remove_node(params, &d, i);
		}
	}
	iio_mutex_unlock(d->lock);
	*ddata = d;

	return;
}

void remove_dup_discovery_data(const struct iio_context_params *params,
			       struct dns_sd_discovery_data **ddata)
{
	struct dns_sd_discovery_data *d, *ndata, *mdata, *prev;
	int i, j;

	d = *ddata;

	if (!d)
		return;

	if (!d->next)
		return;

	iio_mutex_lock(d->lock);
	/* since we are removing nodes in the linked list, we keep track of the
	 * previous "good" node, so we always can link from the last to the next
	 */
	for (i = 0, ndata = d; ndata->next != NULL; ndata = ndata->next) {
		prev = ndata;
		for (j = i + 1, mdata = ndata->next; mdata->next != NULL; mdata = mdata->next) {
			if (!strcmp(mdata->hostname, ndata->hostname) &&
					!strcmp(mdata->addr_str, ndata->addr_str) &&
					mdata->port == ndata->port){
				prm_dbg(params,
					"Removing duplicate in list: %i '%s' '%s' port: %hu\n",
					j, ndata->hostname, ndata->addr_str,
					ndata->port);
				dnssd_remove_node(params, &d, j);

				/* back up one, so the mdata->next will point to the
				 * next one to be tested.
				 */
				mdata = prev;
				continue;
			}
			prev = mdata;
			j++;
		}
		i++;
	}

	prev = NULL;
	ndata = d;
	i = 0;
	while (ndata->next) {
		if (!strcmp(ndata->addr_str, "127.0.0.1") ||
				!strcmp(ndata->addr_str, "::1")) {
			prm_dbg(params,
				"Removing localhost in list: %i '%s' '%s' port: %hu\n",
				i, ndata->hostname, ndata->addr_str, ndata->port);
			dnssd_remove_node(params, &d, i);
			if (!prev)
				ndata = d;
			else
				ndata = prev->next;
			continue;
		}
		i++;
		prev = ndata;
		ndata = ndata->next;
	}
	iio_mutex_unlock(d->lock);

	*ddata = d;
}

int dnssd_context_scan(const struct iio_context_params *params,
		       struct iio_scan *ctx, const char *args)
{
	struct dns_sd_discovery_data *ddata, *ndata;
	int ret = 0;

	ret = dnssd_find_hosts(params, &ddata);

	/* if we return an error when no devices are found, then other scans will fail */
	if (ret == -ENXIO) {
		ret = 0;
		goto fail;
	}

	if (ret < 0)
		goto fail;

	for (ndata = ddata; ndata->next != NULL; ndata = ndata->next) {
		ret = dnssd_add_scan_result(params, ctx, ndata->hostname,
					    ndata->addr_str,ndata->port);
		if (ret < 0) {
			prm_dbg(params, "Failed to add %s (%s) err: %d\n",
				ndata->hostname, ndata->addr_str, ret);
			break;
		}
	}

fail:
	dnssd_free_all_discovery_data(params, ddata);
	return ret;
}

int dnssd_discover_host(const struct iio_context_params *params, char *addr_str,
			size_t addr_len, uint16_t *port)
{
	struct dns_sd_discovery_data *ddata, *ndata;
	int ret = 0;

	ret = dnssd_find_hosts(params, &ddata);

	if (ret < 0)
		goto host_fail;

	for (ndata = ddata; ndata->next != NULL; ndata = ndata->next) {
		if (ndata->port == *port) {
			*port = ndata->port;
			iio_strlcpy(addr_str, ndata->addr_str, addr_len);
			break;
		}
	}

	if (!ndata->next) {
		addr_str[0] = '\0';
		*port = 0;
	}

host_fail:
	dnssd_free_all_discovery_data(params, ddata);

	/* negative error codes, 0 for no data */
	return ret;
}

void dnssd_free_all_discovery_data(const struct iio_context_params *params,
				   struct dns_sd_discovery_data *d)
{
	while (d)
		dnssd_remove_node(params, &d, 0);
}
