/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Adrian Suciu <adrian.suciu@analog.com>
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
 * Based on https://github.com/mjansson/mdns/blob/ce2e4f789f06429008925ff8f18c22036e60201e/mdns.c
 * which is Licensed under Public Domain
 */

#include <stdio.h>
#include <errno.h>
#include <winsock2.h>
#include <iphlpapi.h>

#include "iio-private.h"
#include "mdns.h"
#include "network.h"
#include "debug.h"

static int new_discovery_data(struct dns_sd_discovery_data** data)
{
	struct dns_sd_discovery_data* d;

	d = zalloc(sizeof(struct dns_sd_discovery_data));
	if (!d)
		return -ENOMEM;

	*data = d;
	return 0;
}

void dnssd_free_discovery_data(struct dns_sd_discovery_data* d)
{
	free(d->hostname);
	free(d);
}

static int
open_client_sockets(int* sockets, int max_sockets) {
	// When sending, each socket can only send to one network interface
	// Thus we need to open one socket for each interface and address family
	int num_sockets = 0;

	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	ULONG address_size = 8000;
	unsigned int ret;
	unsigned int num_retries = 4;
	do {
		adapter_address = malloc(address_size);
		if (adapter_address == NULL) {
			return -ENOMEM;
		}
		ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
			adapter_address, &address_size);
		if (ret == ERROR_BUFFER_OVERFLOW) {
			free(adapter_address);
			adapter_address = 0;
		}
		else {
			break;
		}
	} while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		free(adapter_address);
		IIO_ERROR("Failed to get network adapter addresses\n");
		return num_sockets;
	}

	for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
			continue;
		if (adapter->OperStatus != IfOperStatusUp)
			continue;

		for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
			unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
				if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
					(saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
					(saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
					(saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
					if (num_sockets < max_sockets) {
						int sock = mdns_socket_open_ipv4(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
						}
					}
				}
			}
#ifdef HAVE_IPV6
			else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
				static const unsigned char localhost[] = { 0, 0, 0, 0, 0, 0, 0, 0,
														  0, 0, 0, 0, 0, 0, 0, 1 };
				static const unsigned char localhost_mapped[] = { 0, 0, 0,    0,    0,    0, 0, 0,
																 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1 };
				if ((unicast->DadState == NldsPreferred) &&
					memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
					memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
					if (num_sockets < max_sockets) {
						int sock = mdns_socket_open_ipv6(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
						}
					}
				}
			}
#endif
		}
	}

	free(adapter_address);

	for (int isock = 0; isock < num_sockets; ++isock) {
		unsigned long param = 1;
		ioctlsocket(sockets[isock], FIONBIO, &param);
	}

	return num_sockets;
}


static int
query_callback(int sock, const struct sockaddr* from, size_t addrlen,
	mdns_entry_type_t entry, uint16_t transaction_id,
	uint16_t rtype, uint16_t rclass, uint32_t ttl,
	const void* data, size_t size, size_t offset, size_t length,
	void* user_data) {

	char addrbuffer[64];
	char servicebuffer[64];
	char namebuffer[256];

	struct dns_sd_discovery_data* dd = (struct dns_sd_discovery_data*)user_data;
	if (dd == NULL) {
		IIO_ERROR("DNS SD: Missing info structure. Stop browsing.\n");
		goto quit;
	}

	if (rtype != MDNS_RECORDTYPE_SRV)
		goto quit;

	getnameinfo((const struct sockaddr*)from, (socklen_t)addrlen,
		addrbuffer, NI_MAXHOST, servicebuffer, NI_MAXSERV,
		NI_NUMERICSERV | NI_NUMERICHOST);

	mdns_record_srv_t srv = mdns_record_parse_srv(data, size, offset, length,
		namebuffer, sizeof(namebuffer));
	IIO_DEBUG("%s : SRV %.*s priority %d weight %d port %d\n",
		addrbuffer,
		MDNS_STRING_FORMAT(srv.name), srv.priority, srv.weight, srv.port);

	// Go to the last element in the list
	while (dd->next != NULL)
		dd = dd->next;

	if (srv.name.length > 1)
	{
		dd->hostname = malloc(srv.name.length);
		if (dd->hostname == NULL) {
			return -ENOMEM;
		}
		iio_strlcpy(dd->hostname, srv.name.str, srv.name.length);
	}
	iio_strlcpy(dd->addr_str, addrbuffer, DNS_SD_ADDRESS_STR_MAX);
	dd->port = srv.port;

	IIO_DEBUG("DNS SD: added %s (%s:%d)\n", dd->hostname, dd->addr_str, dd->port);
	// A list entry was filled, prepare new item on the list.
	if (new_discovery_data(&dd->next)) {
		IIO_ERROR("DNS SD mDNS Resolver : memory failure\n");
	}

quit:
	return 0;
}

int dnssd_find_hosts(struct dns_sd_discovery_data** ddata)
{

	WORD versionWanted = MAKEWORD(1, 1);
	WSADATA wsaData;
	if (WSAStartup(versionWanted, &wsaData)) {
		printf("Failed to initialize WinSock\n");
		return -1;
	}

	struct dns_sd_discovery_data* d;

	IIO_DEBUG("DNS SD: Start service discovery.\n");

	if (new_discovery_data(&d) < 0) {
		return -ENOMEM;
	}
	*ddata = d;

	size_t capacity = 2048;
	void* buffer = malloc(capacity);
	if (buffer == NULL) {
		return -ENOMEM;
	}
	const char service[] = "_iio._tcp.local";

	IIO_DEBUG("Sending DNS-SD discovery\n");

	int sockets[32];
	int transaction_id[32];
	int num_sockets = open_client_sockets(sockets, sizeof(sockets) / sizeof(sockets[0]));
	if (num_sockets <= 0) {
		IIO_ERROR("Failed to open any client sockets\n");
		return -1;
	}
	IIO_DEBUG("Opened %d socket%s for mDNS query\n", num_sockets, num_sockets ? "s" : "");

	IIO_DEBUG("Sending mDNS query: %s\n", service);
	for (int isock = 0; isock < num_sockets; ++isock) {
		transaction_id[isock] = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR, service, sizeof(service)-1, buffer,
			capacity);
		if (transaction_id[isock] <= 0)
		{
			IIO_ERROR("Failed to send mDNS query: errno %d\n", errno);
		}
	}

	// This is a simple implementation that loops for 10 seconds or as long as we get replies
	// A real world implementation would probably use select, poll or similar syscall to wait
	// until data is available on a socket and then read it
	IIO_DEBUG("Reading mDNS query replies\n");
	for (int i = 0; i < 10; ++i) {
		size_t records;
		do {
			records = 0;
			for (int isock = 0; isock < num_sockets; ++isock) {
				if (transaction_id[isock] > 0)
				records +=
					mdns_query_recv(sockets[isock], buffer, capacity, query_callback, d, transaction_id[isock]);
			}
		} while (records);
		if (records)
			i = 0;
		Sleep(100);
	}

	free(buffer);
	for (int isock = 0; isock < num_sockets; ++isock)
		mdns_socket_close(sockets[isock]);
	IIO_DEBUG("Closed socket%s\n", num_sockets ? "s" : "");

	WSACleanup();

	return 0;
}
