// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Adrian Suciu <adrian.suciu@analog.com>
 *
 * Based on https://github.com/mjansson/mdns/blob/ce2e4f789f06429008925ff8f18c22036e60201e/mdns.c
 * which is Licensed under Public Domain
 */

#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#define sleep(x) Sleep(x * 1000)
#else
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include "debug.h"
#include "dns_sd.h"
#include "iio-lock.h"
#include "iio-private.h"
#include "deps/mdns/mdns.h"

static struct dns_sd_discovery_data *new_discovery_data(void)
{
	struct dns_sd_discovery_data *d;

	d = zalloc(sizeof(*d));
	if (!d)
		return NULL;

	return d;
}

static mdns_string_t ipv4_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in* addr,
		size_t addrlen) {

	char host[NI_MAXHOST] = {0};
	char service[NI_MAXSERV] = {0};
	ssize_t len = 0;
	int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
			service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);

	if (ret == 0) {
		if (strncmp(service, "5353", sizeof"5353") && addr->sin_port != 0)
			len = iio_snprintf(buffer, capacity, "%s:%s", host, service);
		else
			len = iio_snprintf(buffer, capacity, "%s", host);
	} else {
		IIO_ERROR("Unable to find host: %s\n", gai_strerror(ret));
	}

	if (len >= (ssize_t)capacity)
		len = (ssize_t)capacity - 1;

	mdns_string_t str;
	str.str = buffer;
	str.length = len;
	return str;
}

#ifdef HAVE_IPV6
static mdns_string_t ipv6_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in6* addr,
		size_t addrlen) {

	char host[NI_MAXHOST] = {0};
	char service[NI_MAXSERV] = {0};
	ssize_t len = 0;
	int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
			service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);

	if (ret == 0) {
		if (strncmp(service, "5353", sizeof"5353") && addr->sin6_port != 0)
			len = iio_snprintf(buffer, capacity, "[%s]:%s", host, service);
		else
			len = iio_snprintf(buffer, capacity, "%s", host);
	} else {
		IIO_ERROR("Unable to find host: %s\n", gai_strerror(ret));
	}

	if (len >= (ssize_t)capacity)
		len = (ssize_t)capacity - 1;

	mdns_string_t str;
	str.str = buffer;
	str.length = len;
	return str;
}
#endif

static mdns_string_t
ip_address_to_string(char* buffer, size_t capacity, const struct sockaddr* addr, size_t addrlen) {
#ifdef HAVE_IPV6
	if (addr->sa_family == AF_INET6)
		return ipv6_address_to_string(buffer, capacity, (const struct sockaddr_in6*)addr, addrlen);
#endif
	return ipv4_address_to_string(buffer, capacity, (const struct sockaddr_in*)addr, addrlen);
}

/*
 *  When sending, each socket can only send to one network interface
 * Thus we need to open one socket for each interface and address family
 */
static int open_client_sockets(int *sockets, int max_sockets, int port)
{
	int num_sockets = 0;

#ifdef _WIN32
	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	ULONG address_size = 8000;
	unsigned int ret;
	unsigned int num_retries = 4;
	do {
		adapter_address = (IP_ADAPTER_ADDRESSES*)malloc(address_size);
		ret = GetAdaptersAddresses(
#ifdef HAVE_IPV6
				AF_UNSPEC,   /* Return both IPv4 and IPv6 addresses */
#else
				AF_INET,     /* Return only IPv4 addresses */
#endif
				GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
				adapter_address, &address_size);
		if (ret == ERROR_BUFFER_OVERFLOW) {
			free(adapter_address);
			adapter_address = 0;
			address_size *= 2;
		} else {
			break;
		}
	} while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		free(adapter_address);
		IIO_ERROR("Failed to get network adapter addresses\n");
		return num_sockets;
	}

	int first_ipv4 = 1;
	int first_ipv6 = 1;
	(void)first_ipv6;

	for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO) {
			IIO_DEBUG("Skipping Tunnel %ws\n", adapter->FriendlyName);
			continue;
		}
		if (adapter->OperStatus != IfOperStatusUp) {
			IIO_DEBUG("Skipping down %ws\n", adapter->FriendlyName);
			continue;
		}

		for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
				unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
				if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
						(saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
						(saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
						(saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
					int log_addr = 0;
					if (first_ipv4) {
						first_ipv4 = 0;
						log_addr = 1;
					}
					if (num_sockets < max_sockets) {
						saddr->sin_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv4(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
							log_addr = 1;
						} else {
							log_addr = 0;
						}
					}
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
								sizeof(struct sockaddr_in));
						(void)addr;
						IIO_DEBUG("Local IPv4 address: %.*s\n", MDNS_STRING_FORMAT(addr));
					}
				} else {
					IIO_DEBUG("Skipping local loopback %ws\n", adapter->FriendlyName);
				}
			}
#ifdef HAVE_IPV6
			else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
				// Ignore link-local addresses
				if (saddr->sin6_scope_id)
					continue;
				static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
								  0, 0, 0, 0, 0, 0, 0, 1};
				static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
										 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
				if ((unicast->DadState == NldsPreferred) &&
						memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
						memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
					int log_addr = 0;
					if (first_ipv6) {
						first_ipv6 = 0;
						log_addr = 1;
					}
					if (num_sockets < max_sockets) {
						saddr->sin6_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv6(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
							log_addr = 1;
						} else {
							log_addr = 0;
						}
					}
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
								sizeof(struct sockaddr_in6));
						IIO_DEBUG("Local IPv6 address: %.*s\n", MDNS_STRING_FORMAT(addr));
					}
				}
			}
#endif
		}
	}

	free(adapter_address);

#else /* _WIN32 */

	struct ifaddrs* ifaddr = 0;
	struct ifaddrs* ifa = 0;

	if (getifaddrs(&ifaddr) < 0)
		IIO_ERROR("Unable to get interface addresses\n");

	int first_ipv4 = 1;
	int first_ipv6 = 1;
	(void)first_ipv6;

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;
		if (!(ifa->ifa_flags & IFF_UP)       || !(ifa->ifa_flags & IFF_MULTICAST) ||
		     (ifa->ifa_flags & IFF_LOOPBACK) ||  (ifa->ifa_flags & IFF_POINTOPOINT)) {
			if (!(ifa->ifa_flags & IFF_UP))
				IIO_DEBUG("Skipping interface %s not up\n", ifa->ifa_name);
			if (!(ifa->ifa_flags & IFF_MULTICAST))
				IIO_DEBUG("Skipping interface %s MULTICAST\n", ifa->ifa_name);
			if (ifa->ifa_flags & IFF_LOOPBACK)
				IIO_DEBUG("Skipping interface %s loopback\n", ifa->ifa_name);
			if (ifa->ifa_flags & IFF_POINTOPOINT)
				IIO_DEBUG("Skipping interface %s pointtopoint\n", ifa->ifa_name);
			continue;
		}

		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in* saddr = (struct sockaddr_in*)ifa->ifa_addr;
			if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
				int log_addr = 0;
				if (first_ipv4) {
					first_ipv4 = 0;
					log_addr = 1;
				}
				if (num_sockets < max_sockets) {
					saddr->sin_port = htons(port);
					int sock = mdns_socket_open_ipv4(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
						log_addr = 1;
					} else {
						log_addr = 0;
					}
				}
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
							sizeof(struct sockaddr_in));
					(void)addr;
					IIO_DEBUG("Local IPv4 address: %.*s on %s\n", MDNS_STRING_FORMAT(addr), ifa->ifa_name);
				}
			}
		}
#ifdef HAVE_IPV6
		else if (ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6* saddr = (struct sockaddr_in6*)ifa->ifa_addr;
			// Ignore link-local addresses
			if (saddr->sin6_scope_id)
				continue;
			static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
								  0, 0, 0, 0, 0, 0, 0, 1};
			static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
									 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
			if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
					memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
				int log_addr = 0;
				if (first_ipv6) {
					first_ipv6 = 0;
					log_addr = 1;
				}
				if (num_sockets < max_sockets) {
					saddr->sin6_port = htons(port);
					int sock = mdns_socket_open_ipv6(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
						log_addr = 1;
					} else {
						log_addr = 0;
					}
				}
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
							sizeof(struct sockaddr_in6));
					(void)addr;
					IIO_DEBUG("Local IPv6 address: %.*s on %s\n", MDNS_STRING_FORMAT(addr), ifa->ifa_name);
				}
			}
		}
#endif
	}

	freeifaddrs(ifaddr);

#endif

	return num_sockets;
}

static int query_callback(int sock, const struct sockaddr *from, size_t addrlen,
			  mdns_entry_type_t entry, uint16_t query_id,
			  uint16_t rtype, uint16_t rclass, uint32_t ttl,
			  const void *data, size_t size, size_t name_offset,
			  size_t name_length,
			  size_t record_offset, size_t record_length,
			  void *user_data)
{
	(void)sizeof(sock);
	(void)sizeof(query_id);
	(void)sizeof(name_length);

	struct dns_sd_discovery_data *dd = user_data;
	char addrbuffer[64];
	char entrybuffer[256];
	char namebuffer[256];
	mdns_record_txt_t txtbuffer[128];

	mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from, addrlen);
	const char* entrytype = (entry == MDNS_ENTRYTYPE_ANSWER) ?
			"answer" :
				((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
	(void)entrytype;
	mdns_string_t entrystr =
			mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));

	if (!strstr(entrystr.str, "_iio._tcp.local"))
		return 0;

	if (rtype == MDNS_RECORDTYPE_PTR) {
		mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length,
				namebuffer, sizeof(namebuffer));
		(void)namestr;
		IIO_DEBUG("%.*s : %s %.*s PTR %.*s rclass 0x%x ttl %u length %d\n",
				MDNS_STRING_FORMAT(fromaddrstr), entrytype, MDNS_STRING_FORMAT(entrystr),
				MDNS_STRING_FORMAT(namestr), rclass, ttl, (int)record_length);
	} else if (rtype == MDNS_RECORDTYPE_SRV) {
		bool found = false;
		mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
				namebuffer, sizeof(namebuffer));
		IIO_DEBUG("%.*s : %s %.*s SRV %.*s priority %d weight %d port %d\n",
				MDNS_STRING_FORMAT(fromaddrstr), entrytype, MDNS_STRING_FORMAT(entrystr),
				MDNS_STRING_FORMAT(srv.name), srv.priority, srv.weight, srv.port);

		if (entry != MDNS_ENTRYTYPE_ANSWER)
			return 0;

		/* lock the list */
		iio_mutex_lock(dd->lock);
		dd->resolved++;

		/* Either find a name match, or Go to the last element in the list */
		while (dd->next) {
			if (dd->hostname && !strncmp(dd->hostname, srv.name.str, srv.name.length - 1) &&
					(!dd->port || dd->port == srv.port)) {
				dd->port = srv.port;
				IIO_DEBUG("DNS SD: updated  %s (%s:%d)\n", dd->hostname, dd->addr_str, dd->port);
				found = true;
			}
			dd = dd->next;
		}

		if (!found) {
			if (srv.name.length > 1) {
				dd->hostname = iio_strndup(srv.name.str, srv.name.length - 1);
				if (!dd->hostname)
					return -ENOMEM;
			}
			/* Putting the server address here is bad form - the DNS server doesn't need
			 * to be the device where IIO is running, we should wait for the A record, but
			 * just in case we miss it...
			 */
			iio_strlcpy(dd->addr_str, fromaddrstr.str, fromaddrstr.length + 1);
			dd->port = srv.port;
			IIO_DEBUG("DNS SD: added %s (%s:%d)\n", dd->hostname, dd->addr_str, dd->port);

			/* A list entry was filled, prepare new item on the list */
			dd->next = new_discovery_data();
			if (!dd->next) {
				IIO_ERROR("DNS SD mDNS Resolver : memory failure\n");
				return -ENOMEM;
			}
			/* duplicate lock */
			dd->next->lock = dd->lock;
		}
		iio_mutex_unlock(dd->lock);
	} else if (rtype == MDNS_RECORDTYPE_A) {
		bool found = false;
		struct sockaddr_in addr;
				mdns_record_parse_a(data, size, record_offset, record_length, &addr);
		mdns_string_t addrstr =
				ipv4_address_to_string(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
		IIO_DEBUG("%.*s : %s %.*s A %.*s\n", MDNS_STRING_FORMAT(fromaddrstr), entrytype,
				MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(addrstr));

		if (entry != MDNS_ENTRYTYPE_ANSWER)
			return 0;

		iio_mutex_lock(dd->lock);
		dd->resolved++;
		/* Find a match */
		while (dd->next) {
			if (dd->hostname && !strncmp(dd->hostname, entrystr.str, entrystr.length - 1)) {
				iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);
				IIO_DEBUG("DNS SD: updated  %s (%s:%d)\n", dd->hostname, dd->addr_str, dd->port);
				found = true;
			}
			dd = dd->next;
		}

		if (!found) {
			dd->hostname = iio_strndup(entrystr.str, entrystr.length - 1);
			if (!dd->hostname)
				return -ENOMEM;
			iio_strlcpy(dd->addr_str, addrstr.str, addrstr.length + 1);

			/* A list entry was filled, prepare new item on the list */
			dd->next = new_discovery_data();
			if (!dd->next) {
				IIO_ERROR("DNS SD mDNS Resolver : memory failure\n");
				return -ENOMEM;
			}
			/* duplicate lock */
			dd->next->lock = dd->lock;
		}
		iio_mutex_unlock(dd->lock);
	}
#ifdef HAVE_IPv6
	else if (rtype == MDNS_RECORDTYPE_AAAA) {
		struct sockaddr_in6 addr;
		mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
		mdns_string_t addrstr =
				ipv6_address_to_string(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
		IIO_DEBUG("%.*s : %s %.*s AAAA %.*s\n", MDNS_STRING_FORMAT(fromaddrstr), entrytype,
				MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(addrstr));
	}
#endif
	else if (rtype == MDNS_RECORDTYPE_TXT) {
		size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer,
				sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
		for (size_t itxt = 0; itxt < parsed; ++itxt) {
			if (txtbuffer[itxt].value.length) {
				IIO_DEBUG("%.*s : %s %.*s TXT %.*s = %.*s\n", MDNS_STRING_FORMAT(fromaddrstr),
						entrytype, MDNS_STRING_FORMAT(entrystr),
						MDNS_STRING_FORMAT(txtbuffer[itxt].key),
						MDNS_STRING_FORMAT(txtbuffer[itxt].value));
			} else {
				IIO_DEBUG("%.*s : %s %.*s TXT %.*s\n", MDNS_STRING_FORMAT(fromaddrstr), entrytype,
						MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(txtbuffer[itxt].key));
			}
		}
	} else {
		IIO_DEBUG("%.*s : %s %.*s type %u rclass 0x%x ttl %u length %d\n",
				MDNS_STRING_FORMAT(fromaddrstr), entrytype, MDNS_STRING_FORMAT(entrystr), rtype,
				rclass, ttl, (int)record_length);
	}
	return 0;
}

int dnssd_find_hosts(struct dns_sd_discovery_data **ddata)
{
	const char service[] = "_iio._tcp.local";
	size_t records, capacity = 2048;
	struct dns_sd_discovery_data *d;
	uint16_t isock, num_sockets;
	void *buffer;
	int sockets[32];
	int query_id[32];
	int ret = -ENOMEM;

#ifdef _WIN32
	WORD versionWanted = MAKEWORD(1, 1);
	WSADATA wsaData;
	if (WSAStartup(versionWanted, &wsaData)) {
		IIO_ERROR("Failed to initialize WinSock\n");
		return -WSAGetLastError();
	}
#endif

	IIO_DEBUG("DNS SD: Start service discovery.\n");

	d = new_discovery_data();
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

	num_sockets = (uint16_t)open_client_sockets(sockets, ARRAY_SIZE(sockets), MDNS_PORT);
	if (num_sockets <= 0) {
		IIO_ERROR("Failed to open any client sockets\n");
		goto out_free_buffer;
	}

	IIO_DEBUG("Opened %d socket%s for DNS-SD\n", num_sockets, num_sockets > 1 ? "s" : "");

	IIO_DEBUG("Sending mDNS query");
	struct mdns_query_t query[1];
	query[0].type = MDNS_RECORDTYPE_PTR;
	query[0].name = service;
	query[0].length = strnlen(service, 20);
	size_t count = 1;

	for (isock = 0; isock < num_sockets; ++isock) {
		query_id[isock] =
				mdns_multiquery_send(sockets[isock], query, count, buffer, capacity, 0);
		if (query_id[isock] < 0) {
			char err_str[256];
			iio_strerror(-ret, err_str, sizeof(err_str));
			IIO_ERROR("Failed to send mDNS query: %s\n", err_str);
		}
	}

	// This is a simple implementation that loops for 3 seconds or as long as we get replies
	int res;
	IIO_DEBUG("Reading mDNS query replies\n");
	records = 0;
	do {
		struct timeval timeout;
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;

		int nfds = 0;
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
					size_t rec = mdns_query_recv(sockets[isock], buffer, capacity, query_callback,
							d, query_id[isock]);
					if (rec > 0)
						records += rec;
				}
				FD_SET(sockets[isock], &readfs);
			}
		}
	} while (res > 0);

	IIO_DEBUG("Read %zu records\n", records);

	for (isock = 0; isock < num_sockets; ++isock)
		mdns_socket_close(sockets[isock]);
	IIO_DEBUG("Closed socket%s\n", num_sockets ? "s" : "");

	IIO_DEBUG("Closed socket%s\n", (num_sockets > 1) ? "s" : "");

	dump_discovery_data(&d);
	remove_dup_discovery_data(&d);

	ret = 0;
out_free_buffer:
	free(buffer);
out_destroy_lock:
	iio_mutex_destroy(d->lock);
out_wsa_cleanup:
#ifdef _WIN32
        WSACleanup();
#endif
	return ret;
}

int dnssd_resolve_host(const char *hostname, char *ip_addr, const int addr_len)
{
	return -ENOENT;
}
