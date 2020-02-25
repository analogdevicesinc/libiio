/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Matej Kenda.
 * Author: Matej Kenda <matejken<at>gmail.com>
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
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <CFNetwork/CFNetwork.h>

#include "debug.h"

/*
 Implementation for DNS SD discovery for macOS using CFNetServices.
*/

struct cfnet_discovery_data {
	bool	have_v4;
	bool	have_v6;
	char 	address_v4[80];
	char 	address_v6[80];
	int16_t port;
};

static void __cfnet_browser_cb (
	CFNetServiceBrowserRef	browser,
	CFOptionFlags 			flags,
	CFTypeRef 				domainOrService,
	CFStreamError* 			error,
	void* 					info)
{
	CFStreamError anError;
	
	if ((flags & kCFNetServiceFlagIsDomain) != 0) {
		ERROR("DNS SD: FATAL. Callback called for domain, not service.\n");
		goto stop_browsing;
	}
	
	struct cfnet_discovery_data *dd = (struct cfnet_discovery_data *)info;
	if (dd == NULL) {
		ERROR("DNS SD: Missing info structure. Stop browsing.\n");
		goto stop_browsing;
	}
	
	if (dd->have_v4 || dd->have_v6) {
		// Already resolved one IIO service. Stop.
		DEBUG("DNS SD: first service already resolved. Skip.\n");
		goto stop_browsing;
	}

	const CFNetServiceRef netService = (CFNetServiceRef)domainOrService;
	if (netService == NULL) {
		ERROR("DNS SD: Net service is null.\n");
		goto stop_browsing;
	}
	
	if (!CFNetServiceResolveWithTimeout(netService, 10.0, &anError)) {
		ERROR("DNS SD: Resolve error: %ld.%d\n", anError.domain, anError.error);
		goto stop_browsing;
	}
	
	dd->port = CFNetServiceGetPortNumber(netService);
	
	CFArrayRef addrArr = CFNetServiceGetAddressing(netService);
	if (addrArr == NULL) {
		ERROR("DNS SD: No valid addresses for service.\n");
		goto stop_browsing;
	}
	
	for (CFIndex i = 0; i < CFArrayGetCount(addrArr); i++) {
		struct sockaddr_in *sa = (struct sockaddr_in *)
		CFDataGetBytePtr(CFArrayGetValueAtIndex(addrArr, i));
		switch(sa->sin_family) {
			case AF_INET:
				if (inet_ntop(sa->sin_family, &sa->sin_addr,
							  dd->address_v4, sizeof(dd->address_v4))) {
					dd->have_v4 = TRUE;
				}
			case AF_INET6:
				if (inet_ntop(sa->sin_family, &sa->sin_addr,
							  dd->address_v6, sizeof(dd->address_v6))) {
					dd->have_v6 = TRUE;
				}
		}
	}

	if ((flags & kCFNetServiceFlagMoreComing) == 0) {
		DEBUG("DNS SD: No more entries coming.\n");
		goto stop_browsing;
	}
	
	return;

stop_browsing:
	
	CFNetServiceBrowserStopSearch(browser, &anError);
}

int discover_host(char *addr_str, size_t addr_len, uint16_t *port)
{
	int ret = 0;
	
	struct cfnet_discovery_data ddata = { FALSE, FALSE };
	CFNetServiceClientContext clientContext = { 0, &ddata, NULL, NULL, NULL };
	CFStreamError error;
	Boolean result;

	CFStringRef type = CFSTR("_iio._tcp.");
	CFStringRef domain = CFSTR("");

	DEBUG("DNS SD: Resolving hostname.");

	CFNetServiceBrowserRef gServiceBrowserRef = CFNetServiceBrowserCreate(
		kCFAllocatorDefault, __cfnet_browser_cb, &clientContext);

	if (gServiceBrowserRef == NULL) {
		ERROR("DNS SD: Failed to create service browser.\n");
		ret = ENOMEM;
		goto exit;
	}
	result = CFNetServiceBrowserSearchForServices(
		gServiceBrowserRef, domain, type, &error);

	if (result == false) {
		ERROR("DNS SD: CFNetServiceBrowserSearchForServices failed (domain = %ld, error = %d)\n",
			(long)error.domain, error.error);
		ret = ENXIO;
		goto free_browser;
	}
	
	// Resolved address and port. ipv4 has precedence over ipv6.
	*port = ddata.port;
	if (ddata.have_v4) {
		strncpy(addr_str, ddata.address_v4, addr_len);
	}
	else if (ddata.have_v6) {
		strncpy(addr_str, ddata.address_v6, addr_len);
	}
	else {
		ERROR("DNS SD: IIO service not found.\n");
		ret = ENXIO;
	}

free_browser:

	CFRelease(gServiceBrowserRef);
	gServiceBrowserRef = NULL;

exit:
	return ret;
}

