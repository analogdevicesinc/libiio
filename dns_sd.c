/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 *         Matej Kenda <matejken@gmail.com>
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
 * Some of this is insipred from libavahi's example:
 * https://avahi.org/doxygen/html/client-browse-services_8c-example.html
 * which is also LGPL 2.1 or later.
 *
 * */

#include "iio-lock.h"
#include "iio-private.h"
#include "network.h"

#include "debug.h"

/* Some functions for handling common linked list operations */
static void dnssd_remove_node(struct dns_sd_discovery_data **ddata, int n)
{

	struct dns_sd_discovery_data *d, *ndata, *ldata, *tdata;
	int i;

	d = *ddata;

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
	}

	*ddata = d;
}

/*
 * remove the ones in the list that you can't connect to
 * This is sort of silly, but we have seen non-iio devices advertised
 * and discovered on the network. Oh well....
 */
void port_knock_discovery_data(struct dns_sd_discovery_data **ddata)
{
	struct dns_sd_discovery_data *d, *ndata;
	int i, ret;

	d = *ddata;
	iio_mutex_lock(d->lock);
	for (i = 0, ndata = d; ndata->next != NULL; ndata = ndata->next) {
		char port_str[6];
		struct addrinfo hints, *res, *rp;
		int fd;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		iio_snprintf(port_str, sizeof(port_str), "%hu", ndata->port);
		ret = getaddrinfo(ndata->addr_str, port_str, &hints, &res);

		/* getaddrinfo() returns a list of address structures */
		if (ret) {
			DEBUG("Unable to find host ('%s'): %s\n",
					ndata->hostname,
					gai_strerror(ret));
			dnssd_remove_node(&d, i);
		} else {
			for (rp = res; rp != NULL; rp = rp->ai_next) {
				fd = create_socket(rp, DEFAULT_TIMEOUT_MS);
				if (fd < 0) {
					DEBUG("Unable to open %s%s socket ('%s:%d' %s)\n",
							rp->ai_family == AF_INET ? "ipv4" : "",
							rp->ai_family == AF_INET6? "ipv6" : "",
					ndata->hostname, ndata->port, ndata->addr_str);
					dnssd_remove_node(&d, i);
				} else {
					close(fd);
					DEBUG("Something %s%s at '%s:%d' %s)\n",
							rp->ai_family == AF_INET ? "ipv4" : "",
							rp->ai_family == AF_INET6? "ipv6" : "",
							ndata->hostname, ndata->port, ndata->addr_str);
					i++;
				}
			}
		}
		freeaddrinfo(res);
	}
	iio_mutex_unlock(d->lock);
	*ddata = d;

	return;
}

void remove_dup_discovery_data(struct dns_sd_discovery_data **ddata)
{
	struct dns_sd_discovery_data *d, *ndata, *mdata;
	int i, j;

	d = *ddata;

	if (!d)
		return;

	if (!d->next)
		return;

	iio_mutex_lock(d->lock);
	for (i = 0, ndata = d; ndata->next != NULL; ndata = ndata->next) {
		for (j = i + 1, mdata = ndata->next; mdata->next != NULL; mdata = mdata->next) {
			if (!strcmp(mdata->hostname, ndata->hostname) &&
					!strcmp(mdata->addr_str, ndata->addr_str)){
				DEBUG("Removing duplicate in list: '%s'\n",
						ndata->hostname);
				dnssd_remove_node(&d, j);
			}
			j++;
		}
		i++;
	}
	iio_mutex_unlock(d->lock);

	*ddata = d;
}

int dnssd_discover_host(char *addr_str, size_t addr_len, uint16_t *port)
{
	struct dns_sd_discovery_data *ddata;
	int ret = 0;

	ret = dnssd_find_hosts(&ddata);

	if (ret < 0)
		return ret;

	if (ddata) {
		*port = ddata->port;
		strncpy(addr_str, ddata->addr_str, addr_len);
	}

	dnssd_free_all_discovery_data(ddata);

	/* negative error codes, 0 for no data */
	return ret;
}

void dnssd_free_all_discovery_data(struct dns_sd_discovery_data *d)
{
	while (d)
		dnssd_remove_node(&d, 0);
}
