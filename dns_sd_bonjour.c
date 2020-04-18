/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Matej Kenda.
 * Author: Matej Kenda <matejken<at>gmail.com>
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
 * */

#include <CFNetwork/CFNetwork.h>

#include "iio-lock.h"
#include "iio-private.h"
#include "network.h"
#include "debug.h"

/*
 Implementation for DNS SD discovery for macOS using CFNetServices.
*/

static int new_discovery_data(struct dns_sd_discovery_data **data)
{
	struct dns_sd_discovery_data *d;

	d = zalloc(sizeof(struct dns_sd_discovery_data));
	if (!d)
		return -ENOMEM;

	*data = d;
	return 0;
}

void dnssd_free_discovery_data(struct dns_sd_discovery_data *d)
{
	free(d->hostname);
	free(d);
}

static void __cfnet_browser_cb (
	CFNetServiceBrowserRef	browser,
	CFOptionFlags 			flags,
	CFTypeRef 				domainOrService,
	CFStreamError* 			error,
	void* 					info)
{
	CFStreamError	anError;

	if ((flags & kCFNetServiceFlagIsDomain) != 0) {
		IIO_ERROR("DNS SD: FATAL! Callback called for domain, not service.\n");
		goto stop_browsing;
	}
	
	struct dns_sd_discovery_data *dd = (struct dns_sd_discovery_data *)info;
	if (dd == NULL) {
		IIO_ERROR("DNS SD: Missing info structure. Stop browsing.\n");
		goto stop_browsing;
	}

	if ((flags & kCFNetServiceFlagRemove) != 0) {
		IIO_DEBUG("DNS SD: Callback to remove service. Ignore.\n");
		return;
	}

	iio_mutex_lock(dd->lock);

	const CFNetServiceRef netService = (CFNetServiceRef)domainOrService;
	if (netService == NULL) {
		IIO_DEBUG("DNS SD: Net service is null.\n");
		goto verify_flags;
	}
	
	if (!CFNetServiceResolveWithTimeout(netService, 10.0, &anError)) {
		IIO_DEBUG("DNS SD: Resolve error: %ld.%d\n", anError.domain, anError.error);
		goto exit;
	}
	
	CFStringRef targetHost = CFNetServiceGetTargetHost(netService);
	if (targetHost == NULL) {
		IIO_DEBUG("DNS SD: No valid target host for service.\n");
		goto exit;
	}

	char hostname[MAXHOSTNAMELEN];
	if (!CFStringGetCString(targetHost, hostname, sizeof(hostname), kCFStringEncodingASCII)) {
		IIO_ERROR("DNS SD: Could not translate hostname\n");
		goto exit;
	}

	CFStringRef svcName = CFNetServiceGetName(netService);
	char name[MAXHOSTNAMELEN];
	if (!CFStringGetCString(svcName, name, sizeof(name), kCFStringEncodingASCII)) {
		IIO_ERROR("DNS SD: Could not translate service name\n");
		goto exit;
	}

	SInt32 port = CFNetServiceGetPortNumber(netService);
	
	CFArrayRef addrArr = CFNetServiceGetAddressing(netService);
	if (addrArr == NULL) {
		IIO_WARNING("DNS SD: No valid addresses for service %s.\n", name);
		goto exit;
	}
	
	bool	have_v4 = FALSE;
	bool	have_v6 = FALSE;
	char	address_v4[DNS_SD_ADDRESS_STR_MAX+1] = "";
	char	address_v6[DNS_SD_ADDRESS_STR_MAX+1] = "";
	for (CFIndex i = 0; i < CFArrayGetCount(addrArr); i++) {
		struct sockaddr_in *sa = (struct sockaddr_in *)
			CFDataGetBytePtr(CFArrayGetValueAtIndex(addrArr, i));
		switch(sa->sin_family) {
			case AF_INET:
				if (inet_ntop(sa->sin_family, &sa->sin_addr,
							  address_v4, sizeof(address_v4))) {
					have_v4 = TRUE;
				}
			case AF_INET6:
				if (inet_ntop(sa->sin_family, &sa->sin_addr,
							  address_v6, sizeof(address_v6))) {
					have_v6 = TRUE;
				}
		}
	}

	if (!have_v4 && !have_v6) {
		IIO_WARNING("DNS SD: Can't resolve valid address for service %s.\n", name);
		goto exit;
	}

	/* Set properties on the last element on the list. */
	while (dd->next)
		dd = dd->next;

	dd->port = port;
	dd->hostname = strdup(hostname);
	if (have_v4) {
		iio_strlcpy(dd->addr_str, address_v4, sizeof(dd->addr_str));
	} else if(have_v6) {
		iio_strlcpy(dd->addr_str, address_v6, sizeof(dd->addr_str));
	}

	IIO_DEBUG("DNS SD: added %s (%s:%d)\n", hostname, dd->addr_str, port);

	if (have_v4 || have_v6) {
		// A list entry was filled, prepare new item on the list.
		if (!new_discovery_data(&dd->next)) {
			/* duplicate lock */
			dd->next->lock = dd->lock;
		} else {
			IIO_ERROR("DNS SD Bonjour Resolver : memory failure\n");
		}
	}

verify_flags:
	if ((flags & kCFNetServiceFlagMoreComing) == 0) {
		IIO_DEBUG("DNS SD: No more entries coming.\n");
		CFNetServiceBrowserStopSearch(browser, &anError);
	}

exit:
	iio_mutex_unlock(dd->lock);
	return;

stop_browsing:
	CFNetServiceBrowserStopSearch(browser, &anError);
}

int dnssd_find_hosts(struct dns_sd_discovery_data ** ddata)
{
	int ret = 0;
	struct dns_sd_discovery_data *d;

	IIO_DEBUG("DNS SD: Start service discovery.\n");

	if (new_discovery_data(&d) < 0) {
		return -ENOMEM;
	}

	d->lock = iio_mutex_create();
	if (!d->lock) {
		dnssd_free_all_discovery_data(d);
		return -ENOMEM;
	}

	CFNetServiceClientContext clientContext = { 0, d, NULL, NULL, NULL };
	CFNetServiceBrowserRef serviceBrowser = CFNetServiceBrowserCreate(
		kCFAllocatorDefault, __cfnet_browser_cb, &clientContext);

	if (serviceBrowser == NULL) {
		IIO_ERROR("DNS SD: Failed to create service browser.\n");
		dnssd_free_all_discovery_data(d);
		ret = -ENOMEM;
		goto exit;
	}

	CFRunLoopRef runLoop = CFRunLoopGetCurrent();
	CFNetServiceBrowserScheduleWithRunLoop(serviceBrowser, runLoop, kCFRunLoopDefaultMode);

	CFStringRef type = CFSTR("_iio._tcp.");
	CFStringRef domain = CFSTR("");
	CFStreamError error;
	Boolean result = CFNetServiceBrowserSearchForServices(serviceBrowser, domain, type, &error);

	if (result == false) {
		IIO_ERROR("DNS SD: CFNetServiceBrowserSearchForServices failed (domain = %ld, error = %d)\n",
			(long)error.domain, error.error);

		ret = -ENXIO;
	} else {
		CFRunLoopRunResult runRes = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, TRUE);

		if (runRes != kCFRunLoopRunHandledSource && runRes != kCFRunLoopRunTimedOut) {
			if (runRes == kCFRunLoopRunFinished)
				IIO_ERROR("DSN SD: CFRunLoopRunInMode completed kCFRunLoopRunFinished (%d)\n", runRes);
			else if (runRes == kCFRunLoopRunStopped)
				IIO_ERROR("DSN SD: CFRunLoopRunInMode completed kCFRunLoopRunStopped (%d)\n", runRes);
			else
				IIO_ERROR("DSN SD: CFRunLoopRunInMode completed for unknown reason (%d)\n", runRes);
		} else {
			if (runRes == kCFRunLoopRunHandledSource)
				IIO_DEBUG("DSN SD: CFRunLoopRunInMode completed kCFRunLoopRunHandledSource (%d)\n", runRes);
			else
				IIO_DEBUG("DSN SD: CFRunLoopRunInMode completed kCFRunLoopRunTimedOut (%d)\n", runRes);
		}

		port_knock_discovery_data(&d);
		remove_dup_discovery_data(&d);
		*ddata = d;
	}

	CFNetServiceBrowserUnscheduleFromRunLoop(serviceBrowser, runLoop, kCFRunLoopDefaultMode);
	CFRelease(serviceBrowser);
	serviceBrowser = NULL;

	IIO_DEBUG("DNS SD: Completed service discovery, return code : %d\n", ret);

exit:
	iio_mutex_destroy(d->lock);
	return ret;
}
