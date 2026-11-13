/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Ethernet / 802.1Q / IPv4 / UDP parser for the packets of a DAQIRI raw
 * queue, which start at the Ethernet header. Pure and allocation free; free
 * of libiio, DAQIRI and CUDA dependencies so it can be unit tested on its
 * own. Checksums are not verified.
 */

#ifndef DAQIRI_L2_H
#define DAQIRI_L2_H

#include <stdint.h>

enum dq_l2_status {
	DQ_L2_OK = 0,
	DQ_L2_TRUNCATED,   /* shorter than Ethernet + IPv4 + UDP headers */
	DQ_L2_NOT_IPV4,	   /* EtherType is not IPv4 (after at most one VLAN tag) */
	DQ_L2_QINQ,	   /* stacked VLAN tags (802.1ad) */
	DQ_L2_BAD_IHL,	   /* IPv4 header with options (IHL != 5) or version != 4 */
	DQ_L2_NOT_UDP,	   /* IPv4 protocol is not UDP */
	DQ_L2_FRAGMENT,	   /* More Fragments set or non-zero fragment offset */
	DQ_L2_BAD_IP_LEN,  /* IPv4 total length < 28 or beyond the frame */
	DQ_L2_BAD_UDP_LEN, /* UDP length < 8 or beyond the IPv4 datagram */
};

struct dq_l2_info {
	uint16_t payload_off;		    /* 42, or 46 with one VLAN tag */
	uint16_t payload_len;		    /* UDP length - 8, bounded by len */
	uint16_t udp_src, udp_dst, vlan_id; /* vlan_id 0 when untagged */
	uint32_t ip_src, ip_dst;	    /* host order */
};

/*
 * Parse the frame at p (len bytes available). On DQ_L2_OK, *out is filled;
 * otherwise it is left untouched. Never reads beyond len.
 */
enum dq_l2_status dq_l2_parse(const uint8_t *p, uint32_t len, struct dq_l2_info *out);

const char *dq_l2_status_name(enum dq_l2_status s);

#endif /* DAQIRI_L2_H */
