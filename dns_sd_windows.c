// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2022 Analog Devices, Inc.
 * Author: Adrian Suciu <adrian.suciu@analog.com>
 *
 * Based on https://github.com/mjansson/mdns/blob/main/mdns.c
 * which should be sync'ed with the mdns.h file and is Licensed under Public Domain
 */

#include <stdio.h>
#include <errno.h>
#include <winsock2.h>
#include <iphlpapi.h>

#include "debug.h"
#include "dns_sd.h"
#include "iio-lock.h"
#include "iio-private.h"
#include "deps/mdns/mdns.h"

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define MDNS_PORT_STR STRINGIFY(MDNS_PORT)

#ifdef HAVE_IPV6
static const unsigned char localhost[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1
};
static const unsigned char localhost_mapped[] = {
	0, 0, 0,    0,    0,    0, 0, 0,
	0, 0, 0xff, 0xff, 0x7f, 0, 0, 1
};

static bool is_localhost6(const struct sockaddr_in6 *saddr6)
{
	const uint8_t *addr = saddr6->sin6_addr.s6_addr;

	return !memcmp(addr, localhost, sizeof(localhost)) ||
		!memcmp(addr, localhost_mapped, sizeof(localhost_mapped));
}
#endif

static bool is_localhost4(const struct sockaddr_in *saddr)
{
	return saddr->sin_addr.S_un.S_un_b.s_b1 == 127 &&
		saddr->sin_addr.S_un.S_un_b.s_b2 == 0 &&
		saddr->sin_addr.S_un.S_un_b.s_b3 == 0 &&
		saddr->sin_addr.S_un.S_un_b.s_b4 == 1;
}

static struct dns_sd_discovery_data *new_discovery_data(struct dns_sd_discovery_data *dd)
{
	struct dns_sd_discovery_data *d;

	d = zalloc(sizeof(*d));
	if (!d)
		return NULL;

	if (dd)
		d->lock = dd->lock;

	return d;
}

static mdns_string_t ip_address_to_string(char *buffer, size_t capacity,
					  const struct sockaddr *addr, size_t addrlen)
{
	char host[NI_MAXHOST] = { 0 };
	char service[NI_MAXSERV] = { 0 };
	int ret, len = 0;
	mdns_string_t str;

	ret = getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen, host,
			NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);

	if (ret == 0) {
		if (addr->sa_family == AF_INET6 &&
		    ((struct sockaddr_in6 *)addr)->sin6_port != 0 &&
		    strncmp(service, MDNS_PORT_STR, sizeof(MDNS_PORT_STR)))
			len = snprintf(buffer, capacity, "[%s]:%s", host, service);
		else if (((struct sockaddr_in *)addr)->sin_port != 0 &&
			 strncmp(service, MDNS_PORT_STR, sizeof(MDNS_PORT_STR)))
			len = snprintf(buffer, capacity, "%s:%s", host, service);
		else
			len = snprintf(buffer, capacity, "%s", host);
	}

	if (len >= (int)capacity)
		len = (int)capacity - 1;

	str.str = buffer;
	str.length = len;

	return str;
}

static int open_client_sockets(int *sockets, unsigned int max_sockets)
{
	IP_ADAPTER_UNICAST_ADDRESS *unicast;
	IP_ADAPTER_ADDRESSES *adapter_address = 0;
	PIP_ADAPTER_ADDRESSES adapter;
	ULONG address_size = 8000;
	unsigned int i, ret, num_retries = 4, num_sockets = 0;
	struct sockaddr_in *saddr;
	unsigned long param = 1;
	int sock;

	/* When sending, each socket can only send to one network interface
	 * Thus we need to open one socket for each interface and address family */

	do {
		adapter_address = malloc(address_size);
		if (!adapter_address)
			return -ENOMEM;

		ret = GetAdaptersAddresses(AF_UNSPEC,
					   GAA_FLAG_SKIP_MULTICAST
#ifndef HAVE_IPV6
					   | GAA_FLAG_SKIP_ANYCAST
#endif
					   , 0,
					   adapter_address, &address_size);
		if (ret != ERROR_BUFFER_OVERFLOW)
			break;

		free(adapter_address);
		adapter_address = 0;
	} while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		free(adapter_address);
		IIO_ERROR("Failed to get network adapter addresses\n");
		return num_sockets;
	}

	for (adapter = adapter_address; adapter; adapter = adapter->Next) {
		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
			continue;
		if (adapter->OperStatus != IfOperStatusUp)
			continue;

		for (unicast = adapter->FirstUnicastAddress;
		     unicast; unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				saddr = (struct sockaddr_in *)unicast->Address.lpSockaddr;

				if (!is_localhost4(saddr) &&
				    num_sockets < max_sockets) {
					saddr->sin_port = htons((unsigned short)MDNS_PORT);
					sock = mdns_socket_open_ipv4(saddr);
					if (sock >= 0)
						sockets[num_sockets++] = sock;
				}
			}
#ifdef HAVE_IPV6
			else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				struct sockaddr_in6 *saddr6;

				saddr6 = (struct sockaddr_in6 *)unicast->Address.lpSockaddr;

				if (unicast->DadState == NldsPreferred &&
				    !is_localhost6(saddr6) &&
				    num_sockets < max_sockets) {
					saddr6->sin6_port = htons((unsigned short)MDNS_PORT);
					sock = mdns_socket_open_ipv6(saddr6);
					if (sock >= 0)
						sockets[num_sockets++] = sock;
				}
			}
#endif
		}
	}

	free(adapter_address);

	for (i = 0; i < num_sockets; i++)
		ioctlsocket(sockets[i], FIONBIO, &param);

	return num_sockets;
}

/* We should get:
 *  - "service" record (SRV) specifying host (name) and port
 *  - IPv4 "address" record (A) specifying IPv4 address of a given host
 *  - IPv6 "address" record (AAAA) specifying IPv6 address of a given host
 * It's this routine that gets called, and needs to stitch things together
 * The DNS host doesn't necessary need to be the acutal host (but for
 * mdns - it usually is.
 */
static int query_callback(int sock, const struct sockaddr *from, size_t addrlen,
			  mdns_entry_type_t entry, uint16_t query_id,
			  uint16_t rtype, uint16_t rclass, uint32_t ttl,
			  const void *data, size_t size, size_t name_offset,
			  size_t name_length,
			  size_t record_offset, size_t record_length,
			  void *user_data)
{
	struct dns_sd_discovery_data *dd = user_data;
	char addrbuffer[64];
	char entrybuffer[256];
	char namebuffer[256];
	mdns_record_srv_t srv;
	bool found = false;
	mdns_string_t entrystr, fromaddrstr, addrstr;
	unsigned short port = 0;

	if (!dd) {
		IIO_ERROR("DNS SD: Missing info structure. Stop browsing.\n");
		goto quit;
	}

	if (rtype != MDNS_RECORDTYPE_SRV && rtype != MDNS_RECORDTYPE_A &&
	    rtype != MDNS_RECORDTYPE_AAAA)
		goto quit;

	if (entry != MDNS_ENTRYTYPE_ANSWER)
		goto quit;

	entrystr = mdns_string_extract(data, size, &name_offset,
				       entrybuffer, sizeof(entrybuffer));

	if (!strstr(entrystr.str, "_iio._tcp.local"))
		goto quit;

	fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer),
					   from, addrlen);

	iio_mutex_lock(dd->lock);
	if (rtype == MDNS_RECORDTYPE_SRV) {
		srv = mdns_record_parse_srv(data, size, record_offset, record_length,
					    namebuffer, sizeof(namebuffer));
		IIO_DEBUG("%.*s : %.*s SRV %.*s priority %d weight %d port %d\n",
			  MDNS_STRING_FORMAT(fromaddrstr), MDNS_STRING_FORMAT(entrystr),
			  MDNS_STRING_FORMAT(srv.name), srv.priority, srv.weight, srv.port);

		/* find a match based on name/port/ipv[46] & update it, otherwise add it */
		while (dd->next) {
			if (dd->hostname &&
			    !strncmp(dd->hostname, srv.name.str, srv.name.length - 1) &&
			    (!dd->port || dd->port == srv.port) &&
			    dd->found == (from->sa_family != AF_INET)) {
				dd->port = srv.port;
				IIO_DEBUG("DNS SD: updated SRV %s (%s port: %hu)\n",
					  dd->hostname, dd->addr_str, dd->port);
				found = true;
			}
			dd = dd->next;
		}
		if (!found) {
			/* new hostname and port */
			if (srv.name.length > 1) {
				dd->hostname = iio_strndup(srv.name.str,
							   srv.name.length - 1);
				if (!dd->hostname)
					goto mem_fail;
			}

			iio_strlcpy(dd->addr_str, fromaddrstr.str, fromaddrstr.length + 1);
			dd->port = srv.port;
			dd->found = (from->sa_family != AF_INET);
			IIO_DEBUG("DNS SD: added SRV %s (%s port: %hu)\n",
				  dd->hostname, dd->addr_str, dd->port);

			/* A list entry was filled, prepare new item on the list */
			dd->next = new_discovery_data(dd);
			if (!dd->next)
				goto mem_fail;
		}
	} else if (rtype == MDNS_RECORDTYPE_A) {
		struct sockaddr_in addr;

		mdns_record_parse_a(data, size, record_offset, record_length, &addr);
		addrstr = ip_address_to_string(namebuffer, sizeof(namebuffer),
					       (struct sockaddr *) &addr, sizeof(addr));
		IIO_DEBUG("%.*s : %.*s A %.*s\n", MDNS_STRING_FORMAT(fromaddrstr),
			  MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(addrstr));

		/* find a match based on name/ipv4 or 6 & update it, otherwise add it */
		while (dd->next) {
			if (dd->hostname &&
			    !strncmp(dd->hostname, entrystr.str, entrystr.length - 1) &&
			    !dd->found) {
				iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);
				IIO_DEBUG("DNS SD: updated A %s (%s port: %hu)\n",
					  dd->hostname, dd->addr_str, dd->port);
				found = true;
			}
			dd = dd->next;
		}
		if (!found) {
			dd->hostname = iio_strndup(entrystr.str, entrystr.length - 1);
			if (!dd->hostname)
				goto mem_fail;
			iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);
			dd->found = 0;
			IIO_DEBUG("DNS SD: Added A %s (%s port: %hu)\n",
				  dd->hostname, dd->addr_str, dd->port);
			/* A list entry was filled, prepare new item on the list */
			dd->next = new_discovery_data(dd);
			if (!dd->next)
				goto mem_fail;
		}
	}
#ifdef HAVE_IPV6
	else if (rtype == MDNS_RECORDTYPE_AAAA) {
		struct sockaddr_in6 addr;

		mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
		addrstr = ip_address_to_string(namebuffer, sizeof(namebuffer),
					       (struct sockaddr *) &addr, sizeof(addr));
		IIO_DEBUG("%.*s : %.*s AAAA %.*s\n", MDNS_STRING_FORMAT(fromaddrstr),
			  MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(addrstr));

		/* find a match based on name/port/ipv[46] & update it, otherwise add it */
		while (dd->next) {
			if (dd->hostname &&
			    !strncmp(dd->hostname, entrystr.str, entrystr.length - 1) &&
			    dd->found) {
				iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);
				IIO_DEBUG("DNS SD: updated AAAA %s (%s port: %hu)\n",
					  dd->hostname, dd->addr_str, dd->port);
				found = true;
			}
			else if (dd->hostname &&
				!strncmp(dd->hostname, entrystr.str, entrystr.length - 1)) {
				port = dd->port;
			}
			dd = dd->next;
		}
		if (!found) {
			dd->hostname = iio_strndup(entrystr.str, entrystr.length - 1);
			if (!dd->hostname)
				goto mem_fail;
			iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);
			dd->found = 1;
			if (port)
				dd->port = port;
			IIO_DEBUG("DNS SD: added AAAA %s (%s port: %hu)\n",
				  dd->hostname, dd->addr_str, dd->port);
			/* A list entry was filled, prepare new item on the list */
			dd->next = new_discovery_data(dd);
			if (!dd->next)
				goto mem_fail;
		}
	}
#endif /* HAVE_IPV6 */
	iio_mutex_unlock(dd->lock);
quit:
	return 0;

mem_fail:
	iio_mutex_unlock(dd->lock);
	IIO_ERROR("DNS SD mDNS Resolver : memory failure\n");
	return -ENOMEM;
}

int dnssd_find_hosts(struct dns_sd_discovery_data **ddata)
{
	WORD versionWanted = MAKEWORD(1, 1);
	WSADATA wsaData;
	const char service[] = "_iio._tcp.local";
	size_t rec, records, capacity = 2048;
	struct dns_sd_discovery_data *d;
	unsigned int isock, num_sockets;
	void *buffer;
	int sockets[32];
	int transaction_id[32];
	int nfds, res, ret = -ENOMEM;
	struct timeval timeout;

	if (WSAStartup(versionWanted, &wsaData)) {
		IIO_ERROR("Failed to initialize WinSock\n");
		return -WSAGetLastError();
	}

	IIO_DEBUG("DNS SD: Start service discovery.\n");

	d = new_discovery_data(NULL);
	if (!d)
		goto out_wsa_cleanup;

	/* pass the structure back, so it can be freed if err */
	*ddata = d;

	d->lock = iio_mutex_create();
	if (!d->lock)
		goto out_wsa_cleanup;

	buffer = malloc(capacity);
	if (!buffer)
		goto out_destroy_lock;

	IIO_DEBUG("Sending DNS-SD discovery\n");

	ret = open_client_sockets(sockets, ARRAY_SIZE(sockets));
	if (ret <= 0) {
		IIO_ERROR("Failed to open any client sockets\n");
		goto out_free_buffer;
	}

	num_sockets = (unsigned int)ret;
	IIO_DEBUG("Opened %d socket%s for mDNS query\n",
		  num_sockets, (num_sockets > 1) ? "s" : "");

	IIO_DEBUG("Sending mDNS query: %s\n", service);

	/* Walk through all the open interfaces/sockets, and send a query */
	for (isock = 0; isock < num_sockets; isock++) {
		ret = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR,
				      service, sizeof(service)-1, buffer,
				      capacity, 0);
		if (ret < 0)
			IIO_ERROR("Failed to send mDNS query: errno %d\n", errno);

		transaction_id[isock] = ret;
	}

	/* This is a simple implementation that loops as long as we get replies  */
	IIO_DEBUG("Reading mDNS query replies\n");

	records = 0;
	do {
		nfds = 0;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;

		fd_set readfs;
		FD_ZERO(&readfs);
		for (isock = 0; isock < num_sockets; ++isock) {
			if (sockets[isock] >= nfds)
				nfds = sockets[isock] + 1;
			FD_SET(sockets[isock], &readfs);
		}

		res = select(nfds, &readfs, 0, 0, &timeout);
		if (res > 0) {
			for (isock = 0; isock < num_sockets; ++isock) {
				if (FD_ISSET(sockets[isock], &readfs)) {
					rec = mdns_query_recv(sockets[isock], buffer,
							      capacity, query_callback,
							      d, transaction_id[isock]);
					if (rec > 0)
						records += rec;
				}
				FD_SET(sockets[isock], &readfs);
			}
		}
	} while (res > 0);

	for (isock = 0; isock < num_sockets; ++isock)
		mdns_socket_close(sockets[isock]);

	IIO_DEBUG("Closed %i socket%s, processed %i record%s\n",
		   num_sockets, (num_sockets > 1) ? "s" : "",
		   records, (records > 1) ? "s" : "" );

	port_knock_discovery_data(&d);
	remove_dup_discovery_data(&d);

	/* since d may have changed, make sure we pass back the start */
	*ddata = d;

	ret = 0;
out_free_buffer:
	free(buffer);
out_destroy_lock:
	iio_mutex_destroy(d->lock);
out_wsa_cleanup:
	WSACleanup();
	return ret;
}

int dnssd_resolve_host(const char *hostname, char *ip_addr, const int addr_len)
{
	return -ENOENT;
}
