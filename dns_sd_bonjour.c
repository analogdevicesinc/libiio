// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Matej Kenda.
 * Author: Matej Kenda <matejken<at>gmail.com>
 *         Robin Getz <robin.getz@analog.com>
 */

#include "debug.h"
#include "dns_sd.h"
#include "iio-lock.h"
#include "iio-private.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <CFNetwork/CFNetwork.h>

/*
 * Implementation for DNS SD discovery for macOS using CFNetServices.
 */

static void __cfnet_browser_cb(CFNetServiceBrowserRef browser,
			       CFOptionFlags flags,
			       CFTypeRef domainOrService,
			       CFStreamError *error,
			       void *info)
{
	const CFNetServiceRef netService = (CFNetServiceRef)domainOrService;
	struct dns_sd_discovery_data *dd = info;
	char address[DNS_SD_ADDRESS_STR_MAX+1] = "";
	char hostname[FQDN_LEN];
	char name[FQDN_LEN];
	struct sockaddr_in6 *sa6;
	struct sockaddr_in *sa;
	CFStreamError anError;
	CFStringRef targetHost;
	CFStringRef svcName;
	CFArrayRef addrArr;
	CFDataRef data;
	SInt32 port;
	char *ptr;

	if ((flags & kCFNetServiceFlagIsDomain) != 0) {
		IIO_ERROR("DNS SD: FATAL! Callback called for domain, not service.\n");
		goto stop_browsing;
	}

	if (dd == NULL) {
		IIO_ERROR("DNS SD: Missing info structure. Stop browsing.\n");
		goto stop_browsing;
	}

	if ((flags & kCFNetServiceFlagRemove) != 0) {
		IIO_DEBUG("DNS SD: Callback to remove service. Ignore.\n");
		return;
	}

	iio_mutex_lock(dd->lock);

	if (netService == NULL) {
		IIO_DEBUG("DNS SD: Net service is null.\n");
		goto verify_flags;
	}

	if (!CFNetServiceResolveWithTimeout(netService, 10.0, &anError)) {
		IIO_DEBUG("DNS SD: Resolve error: %ld.%d\n",
			  anError.domain, anError.error);
		goto exit;
	}

	targetHost = CFNetServiceGetTargetHost(netService);
	if (targetHost == NULL) {
		IIO_DEBUG("DNS SD: No valid target host for service.\n");
		goto exit;
	}

	if (!CFStringGetCString(targetHost, hostname,
				sizeof(hostname), kCFStringEncodingASCII)) {
		IIO_ERROR("DNS SD: Could not translate hostname\n");
		goto exit;
	}

	svcName = CFNetServiceGetName(netService);
	if (!CFStringGetCString(svcName, name,
				sizeof(name), kCFStringEncodingASCII)) {
		IIO_ERROR("DNS SD: Could not translate service name\n");
		goto exit;
	}

	port = CFNetServiceGetPortNumber(netService);
	addrArr = CFNetServiceGetAddressing(netService);
	if (addrArr == NULL) {
		IIO_WARNING("DNS SD: No valid addresses for service %s.\n",
			    name);
		goto exit;
	}

	/* Set properties on the last element on the list. */
	while (dd->next)
		dd = dd->next;

	for (CFIndex i = 0; i < CFArrayGetCount(addrArr); i++) {
		data = CFArrayGetValueAtIndex(addrArr, i);
		sa = (struct sockaddr_in *) CFDataGetBytePtr(data);
		sa6 = (struct sockaddr_in6 *) sa;

		switch(sa->sin_family) {
		case AF_INET:
			if (inet_ntop(sa->sin_family, &sa->sin_addr,
				      address, sizeof(address)))
				break;
			continue;
		case AF_INET6:
			if (inet_ntop(sa->sin_family, &sa6->sin6_addr,
				      address, sizeof(address)))
				break;
			continue;
		default:
			continue;
		}

		dd->port = port;
		dd->hostname = strdup(hostname);

		iio_strlcpy(dd->addr_str, address, sizeof(dd->addr_str));

		ptr = dd->addr_str + strnlen(dd->addr_str, DNS_SD_ADDRESS_STR_MAX);

		if (sa->sin_family == AF_INET6
		    && sa6->sin6_addr.s6_addr[0] == 0xfe
		    && sa6->sin6_addr.s6_addr[1] == 0x80
		    && if_indextoname((unsigned int)sa6->sin6_scope_id, ptr + 1)) {
			*ptr = '%';
		}

		/* A list entry was filled, prepare new item on the list. */
		dd->next = zalloc(sizeof(*dd->next));
		if (dd->next) {
			/* duplicate lock */
			dd->next->lock = dd->lock;
		} else {
			IIO_ERROR("DNS SD Bonjour Resolver : memory failure\n");
		}

		IIO_DEBUG("DNS SD: added %s (%s:%d)\n", hostname, dd->addr_str, port);

		dd = dd->next;
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

int dnssd_find_hosts(struct dns_sd_discovery_data **ddata)
{
	CFNetServiceClientContext clientContext = { 0 };
	CFNetServiceBrowserRef serviceBrowser;
	struct dns_sd_discovery_data *d;
	CFRunLoopRunResult runRes;
	CFRunLoopRef runLoop;
	CFStringRef type;
	CFStringRef domain;
	CFStreamError error;
	Boolean result;
	int ret = 0;

	IIO_DEBUG("DNS SD: Start service discovery.\n");

	d = zalloc(sizeof(*d));
	if (!d)
		return -ENOMEM;

	d->lock = iio_mutex_create();
	if (!d->lock) {
		dnssd_free_all_discovery_data(d);
		return -ENOMEM;
	}

	clientContext.info = d;
	serviceBrowser = CFNetServiceBrowserCreate(kCFAllocatorDefault,
						   __cfnet_browser_cb,
						   &clientContext);

	if (serviceBrowser == NULL) {
		IIO_ERROR("DNS SD: Failed to create service browser.\n");
		dnssd_free_all_discovery_data(d);
		ret = -ENOMEM;
		goto exit;
	}

	runLoop = CFRunLoopGetCurrent();
	CFNetServiceBrowserScheduleWithRunLoop(serviceBrowser, runLoop,
					       kCFRunLoopDefaultMode);

	type = CFSTR("_iio._tcp.");
	domain = CFSTR("");
	result = CFNetServiceBrowserSearchForServices(serviceBrowser,
						      domain, type, &error);

	if (result == false) {
		IIO_ERROR("DNS SD: CFNetServiceBrowserSearchForServices "
			  "failed (domain = %ld, error = %d)\n",
			  (long)error.domain, error.error);

		ret = -ENXIO;
	} else {
		runRes = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, true);

		if (runRes != kCFRunLoopRunHandledSource && runRes != kCFRunLoopRunTimedOut) {
			if (runRes == kCFRunLoopRunFinished) {
				IIO_ERROR("DNS SD: CFRunLoopRunInMode completed "
					  "kCFRunLoopRunFinished (%d)\n", runRes);
			} else if (runRes == kCFRunLoopRunStopped) {
				IIO_ERROR("DNS SD: CFRunLoopRunInMode completed "
					  "kCFRunLoopRunStopped (%d)\n", runRes);
			} else {
				IIO_ERROR("DNS SD: CFRunLoopRunInMode completed "
					  "for unknown reason (%d)\n", runRes);
			}
		} else {
			if (runRes == kCFRunLoopRunHandledSource) {
				IIO_DEBUG("DNS SD: CFRunLoopRunInMode completed "
					  "kCFRunLoopRunHandledSource (%d)\n", runRes);
			} else {
				IIO_DEBUG("DNS SD: CFRunLoopRunInMode completed "
					  "kCFRunLoopRunTimedOut (%d)\n", runRes);
			}
		}

		remove_dup_discovery_data(&d);
		port_knock_discovery_data(&d);
		*ddata = d;
	}

	CFNetServiceBrowserUnscheduleFromRunLoop(serviceBrowser, runLoop, kCFRunLoopDefaultMode);
	CFRelease(serviceBrowser);
	serviceBrowser = NULL;

	IIO_DEBUG("DNS SD: Completed service discovery, "
		  "return code : %d\n", ret);

exit:
	iio_mutex_destroy(d->lock);
	return ret;
}

int dnssd_resolve_host(const char *hostname, char *ip_addr, const int addr_len)
{
	return -ENOENT;
}
