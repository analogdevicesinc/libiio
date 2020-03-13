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

#else /* _WIN32 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/socket.h>
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
#define DNS_SD_ADDRESS_STR_MAX (40)
#endif /* HAVE_AVAHI */
#endif /* HAVE_DNS_SD */


/* in network.c */
int create_socket(const struct addrinfo *addrinfo, unsigned int timeout);
#define DEFAULT_TIMEOUT_MS 5000
#define IIOD_PORT 30431

#ifdef HAVE_DNS_SD
struct dns_sd_discovery_data {
	struct iio_mutex *lock;
#ifdef HAVE_AVAHI
	AvahiSimplePoll *poll;
	AvahiAddress *address;
#endif /* HAVE_AVAHI */
	char addr_str[DNS_SD_ADDRESS_STR_MAX];
	char *hostname;
	uint16_t found, resolved;
	uint16_t port;
	struct dns_sd_discovery_data *next;
};

int discover_host(char *addr_str, size_t addr_len, uint16_t *port);
int dnssd_find_hosts(struct dns_sd_discovery_data ** ddata);
void free_all_discovery_data(struct dns_sd_discovery_data *d);

#endif /* HAVE_DNS_SD */

#endif /* __IIO_NET_PRIVATE_H */
