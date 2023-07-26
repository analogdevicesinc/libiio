/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 *         Robin Getz
 */

#ifndef __IIO_DNS_SD_H
#define __IIO_DNS_SD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ntddndis.h>
#include <netioapi.h>
#else
#include <net/if.h>
#include <sys/param.h>
#endif

/* IPv6 Max = 4*8 + 7 + 1 for '%' + interface length */
#define DNS_SD_ADDRESS_STR_MAX (40 + IF_NAMESIZE)
#define FQDN_LEN (255)              /* RFC 1035 */

/* MacOS doesn't include ENOMEDIUM (No medium found) like Linux does */
#ifndef ENOMEDIUM
#define ENOMEDIUM ENOENT
#endif

/* Used everywhere */
#define IIOD_PORT 30431

struct addrinfo;
struct AvahiSimplePoll;
struct AvahiAddress;

/* Common structure which all dns_sd_[*] files fill out
 * Anything that is dynamically allocated (malloc) needs to be managed
 */
struct dns_sd_discovery_data {
	struct iio_mutex *lock;
	struct AvahiSimplePoll *poll;
	struct AvahiAddress *address;
	uint16_t found, resolved;
	char addr_str[DNS_SD_ADDRESS_STR_MAX];
	char *hostname;
	uint16_t port, iface;
	struct dns_sd_discovery_data *next;
};


/* This functions is implemented in network.c, but used in dns_sd.c
 */
int create_socket(const struct addrinfo *addrinfo);

/* These functions are common, and implemented in dns_sd_[*].c  based on the
 * implementations: avahi (linux), bonjour (mac), or ServiceDiscovery (Win10)
 */

/* Resolves all IIO hosts on the available networks, and passes back a linked list */
int dnssd_find_hosts(struct dns_sd_discovery_data ** ddata);

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

/* Use dnssd to resolve a given hostname */
int dnssd_resolve_host(const char *hostname, char *ip_addr, const int addr_len);

#endif /* __IIO_DNS_SD_H */
