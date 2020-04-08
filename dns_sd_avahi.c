/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
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

#include "iio-private.h"
#include "network.h"
#include "iio-lock.h"

#include <time.h>

#include <unistd.h>
#include "debug.h"

/*
 * Fundamentally, this builds up a linked list to manage
 * potential clients on the network
 */

static int new_discovery_data(struct dns_sd_discovery_data **data)
{
	struct dns_sd_discovery_data *d;

	d = zalloc(sizeof(struct dns_sd_discovery_data));
	if (!d)
		return -ENOMEM;

	d->address = zalloc(sizeof(struct AvahiAddress));
	if (!d->address) {
		free(d);
		return -ENOMEM;
	}

	*data = d;
	return 0;
}

void dnssd_free_discovery_data(struct dns_sd_discovery_data *d)
{
	free(d->hostname);
	free(d->address);
	free(d);
}

/*
 * libavahi callbacks for browser and resolver
 * for more info, check out libavahi docs at:
 * https://avahi.org/doxygen/html/index.html
 */

static void __avahi_resolver_cb(AvahiServiceResolver *resolver,
		__notused AvahiIfIndex iface, __notused AvahiProtocol proto,
		AvahiResolverEvent event, const char *name,
		const char *type, const char *domain,
		const char *host_name, const AvahiAddress *address,
		uint16_t port, AvahiStringList *txt,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct dns_sd_discovery_data *ddata = (struct dns_sd_discovery_data *) d;

	if (!resolver) {
		IIO_ERROR("Fatal Error in Avahi Resolver\n");
		return;
	}

	switch(event) {
	case AVAHI_RESOLVER_FAILURE:
		IIO_ERROR("Avahi Resolver: Failed resolve service '%s' "
				"of type '%s' in domain '%s': %s\n",
				name, type, domain,
				avahi_strerror(
					avahi_client_errno(
						avahi_service_resolver_get_client(
							resolver))));
		break;
	case AVAHI_RESOLVER_FOUND: {
		/* Avahi is multi-threaded, so lock the list */
		iio_mutex_lock(ddata->lock);
		ddata->resolved++;

		/* Find empty data to store things*/
		while (ddata->next)
			ddata = ddata->next;

		/* link a new placeholder to the list */
		avahi_address_snprint(ddata->addr_str,
				sizeof(ddata->addr_str), address);
		memcpy(ddata->address, address, sizeof(*address));
		ddata->port = port;
		ddata->hostname = strdup(host_name);
		ddata->resolved = true;
		/* link a new, empty placeholder to the list */
		if (!new_discovery_data(&ddata->next)) {
			/* duplicate poll & lock info,
			 * since we don't know which might be discarded */
			ddata->next->poll = ddata->poll;
			ddata->next->lock = ddata->lock;
		} else {
			IIO_ERROR("Avahi Resolver : memory failure\n");
		}
		iio_mutex_unlock(ddata->lock);

		IIO_DEBUG("Avahi Resolver : service '%s' of type '%s' in domain '%s':\n",
				name, type, domain);
		IIO_DEBUG("\t\t%s:%u (%s)\n", host_name, port, ddata->addr_str);

		break;
		}
	}
	avahi_service_resolver_free(resolver);
}

static void __avahi_browser_cb(AvahiServiceBrowser *browser,
		AvahiIfIndex iface, AvahiProtocol proto,
		AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct dns_sd_discovery_data *ddata = (struct dns_sd_discovery_data *) d;
	struct AvahiClient *client = avahi_service_browser_get_client(browser);
	int i;

	if (!browser) {
		IIO_ERROR("Fatal Error in Avahi Browser\n");
		return;
	}

	switch (event) {
	case AVAHI_BROWSER_REMOVE:
		IIO_DEBUG("Avahi Browser : REMOVE : "
				"service '%s' of type '%s' in domain '%s'\n",
				name, type, domain);
		break;
	case AVAHI_BROWSER_NEW:
		IIO_DEBUG("Avahi Browser : NEW: "
				"service '%s' of type '%s' in domain '%s'\n",
				name, type, domain);
		if(!avahi_service_resolver_new(client, iface,
				proto, name, type, domain,
				AVAHI_PROTO_UNSPEC, 0,
				__avahi_resolver_cb, d)) {
			IIO_ERROR("Failed to resolve service '%s\n", name);
		} else {
			iio_mutex_lock(ddata->lock);
			ddata->found++;
			iio_mutex_unlock(ddata->lock);
		}
		break;
	case AVAHI_BROWSER_ALL_FOR_NOW:
		/* Wait for a max of 1 second */
		i = 0;
		IIO_DEBUG("Avahi Browser : ALL_FOR_NOW Browser : %d, Resolved : %d\n",
				ddata->found, ddata->resolved);
		/* 200 * 5ms = wait 1 second */
		while ((ddata->found != ddata->resolved)  && i <= 200) {
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 5e6;	/* 5ms in ns*/
			nanosleep(&ts, NULL);
			i++;
		}
		avahi_simple_poll_quit(ddata->poll);
		break;
	case AVAHI_BROWSER_FAILURE:
		IIO_DEBUG("Avahi Browser : FAILURE\n");
		avahi_simple_poll_quit(ddata->poll);
		break;
	case AVAHI_BROWSER_CACHE_EXHAUSTED:
		IIO_DEBUG("Avahi Browser : CACHE_EXHAUSTED\n");
		break;
	}
}

/*
 * This creates the linked lists, tests it (make sure a context is there)
 * and returns. The structure must be freed with free_all_discovery_data();
 * The returned value is zero on success, negative error code on failure.
 */

int dnssd_find_hosts(struct dns_sd_discovery_data **ddata)
{
	struct dns_sd_discovery_data *d;
	AvahiClient *client;
	AvahiServiceBrowser *browser;
	int ret = 0;

	if (new_discovery_data(&d) < 0)
		return -ENOMEM;

	d->lock = iio_mutex_create();
	if (!d->lock) {
		dnssd_free_all_discovery_data(d);
		return -ENOMEM;
	}

	d->poll = avahi_simple_poll_new();
	if (!d->poll) {
		dnssd_free_all_discovery_data(d);
		return -ENOMEM;
	}

	client = avahi_client_new(avahi_simple_poll_get(d->poll),
			0, NULL, NULL, &ret);
	if (!client) {
		IIO_ERROR("Unable to create Avahi DNS-SD client :%s\n",
				avahi_strerror(ret));
		goto err_free_poll;
	}

	browser = avahi_service_browser_new(client,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			"_iio._tcp", NULL, 0, __avahi_browser_cb, d);
	if (!browser) {
		ret = avahi_client_errno(client);
		IIO_ERROR("Unable to create Avahi DNS-SD browser: %s\n",
				avahi_strerror(ret));
		goto err_free_client;
	}

	IIO_DEBUG("Trying to discover host\n");
	avahi_simple_poll_loop(d->poll);

	if (d->resolved) {
		port_knock_discovery_data(&d);
		remove_dup_discovery_data(&d);
	} else
		ret = -ENXIO;

	avahi_service_browser_free(browser);
err_free_client:
	avahi_client_free(client);
err_free_poll:
	avahi_simple_poll_free(d->poll);
	iio_mutex_destroy(d->lock);
	*ddata = d;

	return ret;
}
