/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 *         Robin Getz
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

#ifndef __IIO_NET_PRIVATE_H
#define __IIO_NET_PRIVATE_H

#include "iio-config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close(s) closesocket(s)
/* winsock2.h defines ERROR, we don't want that */
#undef ERROR
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN (MAX_COMPUTERNAME_LENGTH+1)
#endif /* MAXHOSTNAMELEN */
#else /* !_WIN32 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <unistd.h>
#endif /* _WIN32 */

#ifdef HAVE_DNS_SD
#ifdef HAVE_AVAHI
#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#define DNS_SD_ADDRESS_STR_MAX AVAHI_ADDRESS_STR_MAX
#else /* !HAVE_AVAHI */
#define DNS_SD_ADDRESS_STR_MAX (40) /* IPv6 Max = 4*8 + 7 + 1 for NUL */
#endif /* HAVE_AVAHI */

/* MacOS doesn't include ENOMEDIUM (No medium found) like Linux does */
#ifndef ENOMEDIUM
#define ENOMEDIUM ENOENT
#endif

/* Common structure which all dns_sd_[*] files fill out
 * Anything that is dynamically allocated (malloc) needs to be managed
 */
struct dns_sd_discovery_data {
	struct iio_mutex *lock;
#ifdef HAVE_AVAHI
	AvahiSimplePoll *poll;
	AvahiAddress *address;
	uint16_t found, resolved;
#endif /* HAVE_AVAHI */
	char addr_str[DNS_SD_ADDRESS_STR_MAX];
	char *hostname;
	uint16_t port;
	struct dns_sd_discovery_data *next;
};


/* This functions is implemented in network.c, but used in dns_sd.c
 */
int create_socket(const struct addrinfo *addrinfo, unsigned int timeout);

/* These functions are common, and implemented in dns_sd_[*].c  based on the
 * implementations: avahi (linux), bonjour (mac), or ServiceDiscovery (Win10)
 */

/* Resolves all IIO hosts on the available networks, and passes back a linked list */
int dnssd_find_hosts(struct dns_sd_discovery_data ** ddata);

/* Frees memory of one entry on the list */
void dnssd_free_discovery_data(struct dns_sd_discovery_data *d);

/* Deallocates complete list of discovery data */
void dnssd_free_all_discovery_data(struct dns_sd_discovery_data *d);

/* These functions are common, and found in dns_sd.c, but are used in the
 * dns_sd_[*].c implementations or network.c
 */

/* Passed back the first (random) IIOD service resolved by DNS DS. */
int dnssd_discover_host(char *addr_str, size_t addr_len, uint16_t *port);

/* remove duplicates from the list */
void remove_dup_discovery_data(struct dns_sd_discovery_data **ddata);

/* port knocks  */
void port_knock_discovery_data(struct dns_sd_discovery_data **ddata);

#endif /* HAVE_DNS_SD */

/* Used everywhere */
#define DEFAULT_TIMEOUT_MS 5000
#define IIOD_PORT 30431

#endif /* __IIO_NET_PRIVATE_H */
