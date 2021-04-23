// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Matej Kenda.
 * Author: Matej Kenda <matejken<at>gmail.com>
 *         Robin Getz <robin.getz@analog.com>
 */

#include "dns_sd.h"
#include "iio-debug.h"
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
	struct dns_sd_cb_data *bdata = info;
	struct dns_sd_discovery_data *dd = bdata->d;
	const struct iio_context_params *params = bdata->params;
	char address_v4[DNS_SD_ADDRESS_STR_MAX+1] = "";
	char address_v6[DNS_SD_ADDRESS_STR_MAX+1] = "";
	char hostname[MAXHOSTNAMELEN];
	char name[MAXHOSTNAMELEN];
	bool have_v4 = false;
	bool have_v6 = false;
	struct sockaddr_in *sa;
	CFStreamError anError;
	CFStringRef targetHost;
	CFStringRef svcName;
	CFArrayRef addrArr;
	CFDataRef data;
	SInt32 port;

	if ((flags & kCFNetServiceFlagIsDomain) != 0) {
		prm_err(params, "DNS SD: FATAL! Callback called for domain, not service.\n");
		goto stop_browsing;
	}

	if (dd == NULL) {
		prm_err(params, "DNS SD: Missing info structure. Stop browsing.\n");
		goto stop_browsing;
	}

	if ((flags & kCFNetServiceFlagRemove) != 0) {
		prm_dbg(params, "DNS SD: Callback to remove service. Ignore.\n");
		return;
	}

	iio_mutex_lock(dd->lock);

	if (netService == NULL) {
		prm_dbg(params, "DNS SD: Net service is null.\n");
		goto verify_flags;
	}

	if (!CFNetServiceResolveWithTimeout(netService, 10.0, &anError)) {
		prm_dbg(params, "DNS SD: Resolve error: %ld.%d\n",
			anError.domain, anError.error);
		goto exit;
	}

	targetHost = CFNetServiceGetTargetHost(netService);
	if (targetHost == NULL) {
		prm_dbg(params, "DNS SD: No valid target host for service.\n");
		goto exit;
	}

	if (!CFStringGetCString(targetHost, hostname,
				sizeof(hostname), kCFStringEncodingASCII)) {
		prm_err(params, "DNS SD: Could not translate hostname\n");
		goto exit;
	}

	svcName = CFNetServiceGetName(netService);
	if (!CFStringGetCString(svcName, name,
				sizeof(name), kCFStringEncodingASCII)) {
		prm_err(params, "DNS SD: Could not translate service name\n");
		goto exit;
	}

	port = CFNetServiceGetPortNumber(netService);
	addrArr = CFNetServiceGetAddressing(netService);
	if (addrArr == NULL) {
		prm_warn(params, "DNS SD: No valid addresses for service %s.\n",
			 name);
		goto exit;
	}

	for (CFIndex i = 0; i < CFArrayGetCount(addrArr); i++) {
		data = CFArrayGetValueAtIndex(addrArr, i);
		sa = (struct sockaddr_in *) CFDataGetBytePtr(data);

		switch(sa->sin_family) {
		case AF_INET:
			if (inet_ntop(sa->sin_family, &sa->sin_addr,
				      address_v4, sizeof(address_v4))) {
				have_v4 = true;
			}
		case AF_INET6:
			if (inet_ntop(sa->sin_family, &sa->sin_addr,
				      address_v6, sizeof(address_v6))) {
				have_v6 = true;
			}
		}
	}

	if (!have_v4 && !have_v6) {
		prm_warn(params, "DNS SD: Can't resolve valid address for "
			 "service %s.\n", name);
		goto exit;
	}

	/* Set properties on the last element on the list. */
	while (dd->next)
		dd = dd->next;

	dd->port = port;
	dd->hostname = strdup(hostname);

	if (have_v4)
		iio_strlcpy(dd->addr_str, address_v4, sizeof(dd->addr_str));
	else if(have_v6)
		iio_strlcpy(dd->addr_str, address_v6, sizeof(dd->addr_str));

	prm_dbg(params, "DNS SD: added %s (%s:%d)\n",
		hostname, dd->addr_str, port);

	if (have_v4 || have_v6) {
		/* A list entry was filled, prepare new item on the list. */
		dd->next = zalloc(sizeof(*dd->next));
		if (dd->next) {
			/* duplicate lock */
			dd->next->lock = dd->lock;
		} else {
			prm_err(params, "DNS SD Bonjour Resolver : memory failure\n");
		}
	}

verify_flags:
	if ((flags & kCFNetServiceFlagMoreComing) == 0) {
		prm_dbg(params, "DNS SD: No more entries coming.\n");
		CFNetServiceBrowserStopSearch(browser, &anError);
	}

exit:
	iio_mutex_unlock(dd->lock);
	return;

stop_browsing:
	CFNetServiceBrowserStopSearch(browser, &anError);
}

int dnssd_find_hosts(const struct iio_context_params *params,
		     struct dns_sd_discovery_data **ddata)
{
	CFNetServiceClientContext clientContext = { 0 };
	CFNetServiceBrowserRef serviceBrowser;
	struct dns_sd_discovery_data *d;
	struct dns_sd_cb_data bdata;
	CFRunLoopRunResult runRes;
	CFRunLoopRef runLoop;
	CFStringRef type;
	CFStringRef domain;
	CFStreamError error;
	Boolean result;
	int ret = 0;

	prm_dbg(params, "DNS SD: Start service discovery.\n");

	d = zalloc(sizeof(*d));
	if (!d)
		return -ENOMEM;

	d->lock = iio_mutex_create();
	if (!d->lock) {
		dnssd_free_all_discovery_data(params, d);
		return -ENOMEM;
	}

	bdata.d = d;
	bdata.params = params;

	clientContext.info = &bdata;
	serviceBrowser = CFNetServiceBrowserCreate(kCFAllocatorDefault,
						   __cfnet_browser_cb,
						   &clientContext);

	if (serviceBrowser == NULL) {
		prm_err(params, "DNS SD: Failed to create service browser.\n");
		dnssd_free_all_discovery_data(params, d);
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
		prm_err(params, "DNS SD: CFNetServiceBrowserSearchForServices "
			"failed (domain = %ld, error = %d)\n",
			(long)error.domain, error.error);

		ret = -ENXIO;
	} else {
		runRes = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, true);

		if (runRes != kCFRunLoopRunHandledSource && runRes != kCFRunLoopRunTimedOut) {
			if (runRes == kCFRunLoopRunFinished) {
				prm_err(params, "DNS SD: CFRunLoopRunInMode completed "
					"kCFRunLoopRunFinished (%d)\n", runRes);
			} else if (runRes == kCFRunLoopRunStopped) {
				prm_err(params, "DNS SD: CFRunLoopRunInMode completed "
					"kCFRunLoopRunStopped (%d)\n", runRes);
			} else {
				prm_err(params, "DNS SD: CFRunLoopRunInMode completed "
					"for unknown reason (%d)\n", runRes);
			}
		} else {
			if (runRes == kCFRunLoopRunHandledSource) {
				prm_dbg(params, "DNS SD: CFRunLoopRunInMode completed "
					"kCFRunLoopRunHandledSource (%d)\n", runRes);
			} else {
				prm_dbg(params, "DNS SD: CFRunLoopRunInMode completed "
					"kCFRunLoopRunTimedOut (%d)\n", runRes);
			}
		}

		port_knock_discovery_data(params, &d);
		remove_dup_discovery_data(params, &d);
		*ddata = d;
	}

	CFNetServiceBrowserUnscheduleFromRunLoop(serviceBrowser, runLoop, kCFRunLoopDefaultMode);
	CFRelease(serviceBrowser);
	serviceBrowser = NULL;

	prm_dbg(params, "DNS SD: Completed service discovery, "
		"return code : %d\n", ret);

exit:
	iio_mutex_destroy(d->lock);
	return ret;
}

int dnssd_resolve_host(const struct iio_context_params *params,
		       const char *hostname, char *ip_addr, const int addr_len)
{
	return -ENOENT;
}
