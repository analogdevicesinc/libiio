// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
 *
 * Some of this is insipred from libavahi's example:
 * https://avahi.org/doxygen/html/client-browse-services_8c-example.html
 * which is also LGPL 2.1 or later.
 */

#include "debug.h"
#include "dns_sd.h"
#include "iio-private.h"
#include "iio-lock.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

/*
 * Fundamentally, this builds up a linked list to manage
 * potential clients on the network
 */

static struct dns_sd_discovery_data *new_discovery_data(void)
{
	struct dns_sd_discovery_data *d;

	d = zalloc(sizeof(*d));
	if (!d)
		return NULL;

	d->address = zalloc(sizeof(*d->address));
	if (!d->address) {
		free(d);
		return NULL;
	}

	return d;
}

static void avahi_process_resolved(struct dns_sd_discovery_data *ddata,
				   AvahiIfIndex iface,
				   const AvahiAddress *addr,
				   const char *host_name,
				   const uint16_t port)
{
	char *ptr;

	/* Avahi is multi-threaded, so lock the list */
	iio_mutex_lock(ddata->lock);
	ddata->resolved++;

	/* Find empty data to store things*/
	while (ddata->next)
		ddata = ddata->next;

	/* link a new placeholder to the list */
	avahi_address_snprint(ddata->addr_str,
			      sizeof(ddata->addr_str), addr);
	memcpy(ddata->address, addr, sizeof(*ddata->address)); /* Flawfinder: ignore */
	ddata->port = port;
	ddata->hostname = strdup(host_name);
	ddata->resolved = true;

	/* link a new, empty placeholder to the list */
	ddata->next = new_discovery_data();
	if (ddata->next) {
		/* duplicate poll & lock info,
		 * since we don't know which might be discarded */
		ddata->next->poll = ddata->poll;
		ddata->next->lock = ddata->lock;
	} else {
		IIO_ERROR("Avahi Resolver : memory failure\n");
	}
	iio_mutex_unlock(ddata->lock);

	ptr = ddata->addr_str + strnlen(ddata->addr_str, DNS_SD_ADDRESS_STR_MAX);

	if (addr->proto == AVAHI_PROTO_INET6
	    && addr->data.ipv6.address[0] == 0xfe
	    && addr->data.ipv6.address[1] == 0x80
	    && iface != AVAHI_IF_UNSPEC
	    && if_indextoname((unsigned int)iface, ptr + 1)) {
		*ptr = '%';
	}

	IIO_DEBUG("\t\t%s:%u (%s)\n", host_name, port, ddata->addr_str);
}

/*
 * libavahi callbacks for browser and resolver
 * for more info, check out libavahi docs at:
 * https://avahi.org/doxygen/html/index.html
 */

static void __avahi_resolver_cb(AvahiServiceResolver *resolver,
				AvahiIfIndex iface,
				AvahiProtocol proto,
				AvahiResolverEvent event,
				const char *name,
				const char *type,
				const char *domain,
				const char *host_name,
				const AvahiAddress *address,
				uint16_t port,
				AvahiStringList *txt,
				AvahiLookupResultFlags flags,
				void *d)
{
	struct dns_sd_discovery_data *ddata = d;
	AvahiClient *client;
	int err;

	if (!resolver) {
		IIO_ERROR("Fatal Error in Avahi Resolver\n");
		return;
	}

	switch(event) {
	case AVAHI_RESOLVER_FAILURE:
		client = avahi_service_resolver_get_client(resolver);
		err = avahi_client_errno(client);

		IIO_ERROR("Avahi Resolver: Failed resolve service '%s' "
			  "of type '%s' in domain '%s': %s\n",
			  name, type, domain,
			  avahi_strerror(err));
		break;
	case AVAHI_RESOLVER_FOUND: {
		avahi_process_resolved(ddata, iface, address, host_name, port);
		IIO_DEBUG("Avahi Resolver : service '%s' of type '%s' in domain '%s':\n",
			  name, type, domain);
		break;
	}
	}
	avahi_service_resolver_free(resolver);
}

static void avahi_host_resolver(AvahiHostNameResolver *resolver,
				AvahiIfIndex iface,
				AvahiProtocol proto,
				AvahiResolverEvent event,
				const char *host_name,
				const AvahiAddress *address,
				AvahiLookupResultFlags flags,
				void *d)
{
	struct dns_sd_discovery_data *ddata = d;
	AvahiClient *client;
	int err;

	switch(event) {
	case AVAHI_RESOLVER_FAILURE:
		client = avahi_host_name_resolver_get_client(resolver);
		err = avahi_client_errno(client);

		IIO_ERROR("Avahi Resolver: Failed to resolve host '%s' : %s\n",
			  host_name, avahi_strerror(err));
		break;
	case AVAHI_RESOLVER_FOUND:
		avahi_process_resolved(ddata, iface, address, host_name, IIOD_PORT);
		break;
	}

	avahi_host_name_resolver_free(resolver);
	avahi_simple_poll_quit(ddata->poll);
}

static void __avahi_browser_cb(AvahiServiceBrowser *browser,
			       AvahiIfIndex iface,
			       AvahiProtocol proto,
			       AvahiBrowserEvent event,
			       const char *name,
			       const char *type,
			       const char *domain,
			       AvahiLookupResultFlags flags,
			       void *d)
{
	struct dns_sd_discovery_data *ddata = (struct dns_sd_discovery_data *) d;
	struct AvahiClient *client = avahi_service_browser_get_client(browser);
	unsigned int i;
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 5e6; /* 5ms in ns */

	if (!browser) {
		IIO_ERROR("Fatal Error in Avahi Browser\n");
		return;
	}

	switch (event) {
	case AVAHI_BROWSER_REMOVE:
		IIO_DEBUG("Avahi Browser : REMOVE : service '%s' of type '%s' in domain '%s'\n",
			  name, type, domain);
		break;
	case AVAHI_BROWSER_NEW:
		IIO_DEBUG("Avahi Browser : NEW: service '%s' of type '%s' in domain '%s'\n",
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
		IIO_DEBUG("Avahi Browser : ALL_FOR_NOW Browser : %d, Resolved : %d\n",
			  ddata->found, ddata->resolved);

		/* 200 * 5ms = wait 1 second */
		for (i = 0; ddata->found != ddata->resolved && i <= 200; i++)
			nanosleep(&ts, NULL);

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

	d = new_discovery_data();
	if (!d)
		return -ENOMEM;

	d->lock = iio_mutex_create();
	if (!d->lock) {
		dnssd_free_all_discovery_data(d);
		return -ENOMEM;
	}

	d->poll = avahi_simple_poll_new();
	if (!d->poll) {
		iio_mutex_destroy(d->lock);
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
					    "_iio._tcp", NULL, 0,
					    __avahi_browser_cb, d);
	if (!browser) {
		ret = avahi_client_errno(client);
		IIO_ERROR("Unable to create Avahi DNS-SD browser: %s\n",
			  avahi_strerror(ret));
		goto err_free_client;
	}

	IIO_DEBUG("Trying to discover host\n");
	avahi_simple_poll_loop(d->poll);

	if (d->resolved) {
		remove_dup_discovery_data(&d);
		port_knock_discovery_data(&d);
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

static void avahi_resolve_host(struct dns_sd_discovery_data *d,
			       const char *hostname,
			       const AvahiProtocol proto)
{
	AvahiClient *client;
	AvahiHostNameResolver *resolver;
	int ret;

	d->poll = avahi_simple_poll_new();
	if (!d->poll)
		return;

	client = avahi_client_new(avahi_simple_poll_get(d->poll), 0, NULL, NULL, &ret);
	if (!client) {
		IIO_ERROR("Unable to create Avahi DNS-SD client :%s\n",
			  avahi_strerror(ret));
		goto err_free_poll;
	}

	resolver = avahi_host_name_resolver_new(client, AVAHI_IF_UNSPEC, proto, hostname, proto, 0,
						avahi_host_resolver, d);
	if (!resolver) {
		ret = avahi_client_errno(client);
		IIO_ERROR("Unable to create Avahi DNS-SD browser: %s\n",
			  avahi_strerror(ret));
		goto err_free_client;
	}

	IIO_DEBUG("Trying to resolve host: %s, proto: %d\n", hostname, proto);
	avahi_simple_poll_loop(d->poll);

err_free_client:
	avahi_client_free(client);
err_free_poll:
	avahi_simple_poll_free(d->poll);
}

int dnssd_resolve_host(const char *hostname,
		       char *ip_addr,
		       const int addr_len)
{
	struct dns_sd_discovery_data *d;
	int ret = 0;

	if (!hostname || hostname[0] == '\0')
		return -EINVAL;

	d = new_discovery_data();
	if (!d)
		return -ENOMEM;

	d->lock = iio_mutex_create();
	if (!d->lock) {
		ret = -ENOMEM;
		goto err_free_data;
	}
	/*
	 * The reason not to use AVAHI_PROTO_UNSPEC is that avahi sometimes resolves the host
	 * to an ipv6 link local address which is not suitable to be used by connect. In fact,
	 * `port_knock_discovery_data()` would discard this entry. On the other hand, some users
	 * might really want to use ipv6 and have their environment correctly configured. Hence,
	 * we try to resolve both in ipv4 and ipv6...
	 */
	avahi_resolve_host(d, hostname, AVAHI_PROTO_INET);
#ifdef HAVE_IPV6
	avahi_resolve_host(d, hostname, AVAHI_PROTO_INET6);
#endif

	if (d->resolved) {
		remove_dup_discovery_data(&d);
		port_knock_discovery_data(&d);
	} else {
		ret = -ENXIO;
		goto err_mutex_destroy;
	}

	/* If next is null it means that d is empty */
	if (!d->next) {
		ret = -ENXIO;
		goto err_mutex_destroy;
	}

	iio_strlcpy(ip_addr, d->addr_str, addr_len);

err_mutex_destroy:
	iio_mutex_destroy(d->lock);
err_free_data:
	dnssd_free_all_discovery_data(d);
	return ret;
}
