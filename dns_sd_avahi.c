/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
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

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

#include "debug.h"
#include "iio.h"

struct avahi_discovery_data {
	AvahiSimplePoll *poll;
	AvahiAddress *address;
	uint16_t *port;
	bool found, resolved;
};

static void __avahi_resolver_cb(AvahiServiceResolver *resolver,
		__notused AvahiIfIndex iface, __notused AvahiProtocol proto,
		__notused AvahiResolverEvent event, __notused const char *name,
		__notused const char *type, __notused const char *domain,
		__notused const char *host_name, const AvahiAddress *address,
		uint16_t port, __notused AvahiStringList *txt,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct avahi_discovery_data *ddata = (struct avahi_discovery_data *) d;

	memcpy(ddata->address, address, sizeof(*address));
	*ddata->port = port;
	ddata->resolved = true;
	avahi_service_resolver_free(resolver);
}

static void __avahi_browser_cb(AvahiServiceBrowser *browser,
		AvahiIfIndex iface, AvahiProtocol proto,
		AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain,
		__notused AvahiLookupResultFlags flags, void *d)
{
	struct avahi_discovery_data *ddata = (struct avahi_discovery_data *) d;
	struct AvahiClient *client = avahi_service_browser_get_client(browser);

	switch (event) {
	default:
	case AVAHI_BROWSER_NEW:
		ddata->found = !!avahi_service_resolver_new(client, iface,
				proto, name, type, domain,
				AVAHI_PROTO_UNSPEC, 0,
				__avahi_resolver_cb, d);
		break;
	case AVAHI_BROWSER_ALL_FOR_NOW:
		if (ddata->found) {
			while (!ddata->resolved) {
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 4000000;
				nanosleep(&ts, NULL);
			}
		}
		/* fall-through */
	case AVAHI_BROWSER_FAILURE:
		avahi_simple_poll_quit(ddata->poll);
		/* fall-through */
	case AVAHI_BROWSER_CACHE_EXHAUSTED:
		break;
	}
}

int discover_host(char *addr_str, size_t addr_len, uint16_t *port)
{
	struct avahi_discovery_data ddata;
	int ret = 0;
	AvahiAddress address;
	AvahiClient *client;
	AvahiServiceBrowser *browser;
	AvahiSimplePoll *poll = avahi_simple_poll_new();

	memset(&address, 0, sizeof(address));
	if (!poll)
		return -ENOMEM;

	client = avahi_client_new(avahi_simple_poll_get(poll),
			0, NULL, NULL, &ret);
	if (!client) {
		ERROR("Unable to create Avahi DNS-SD client :%s\n",
				avahi_strerror(ret));
		goto err_free_poll;
	}

	memset(&ddata, 0, sizeof(ddata));
	ddata.poll = poll;
	ddata.address = &address;
	ddata.port = port;

	browser = avahi_service_browser_new(client,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			"_iio._tcp", NULL, 0, __avahi_browser_cb, &ddata);
	if (!browser) {
		ret = avahi_client_errno(client);
		ERROR("Unable to create Avahi DNS-SD browser: %s\n",
				avahi_strerror(ret));
		goto err_free_client;
	}

	DEBUG("Trying to discover host\n");
	avahi_simple_poll_loop(poll);

	if (!ddata.found)
		ret = ENXIO;

	if (ret == 0)
		avahi_address_snprint(addr_str, addr_len, &address);

	avahi_service_browser_free(browser);
err_free_client:
	avahi_client_free(client);
err_free_poll:
	avahi_simple_poll_free(poll);
	return -ret; /* we want a negative error code */
}
