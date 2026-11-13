// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Ethernet / 802.1Q / IPv4 / UDP parser for DAQIRI raw queues; see
 * daqiri-l2.h.
 */

#include "daqiri-l2.h"

#define ETH_HDR_LEN 14
#define VLAN_TAG_LEN 4
#define IPV4_HDR_LEN 20
#define UDP_HDR_LEN 8

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_VLAN 0x8100
#define ETHERTYPE_QINQ 0x88A8

#define IPPROTO_UDP_NUM 17
#define IP_FRAG_MASK 0x3FFF /* More Fragments flag + fragment offset */

static inline uint16_t be16(const uint8_t *p)
{
	return (uint16_t)(((unsigned int)p[0] << 8) | p[1]);
}

static inline uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

enum dq_l2_status dq_l2_parse(const uint8_t *p, uint32_t len, struct dq_l2_info *out)
{
	const uint8_t *ip, *udp;
	uint32_t l2 = ETH_HDR_LEN;
	uint16_t ethertype, vlan_id = 0, ip_len, udp_len;

	if (len < ETH_HDR_LEN + IPV4_HDR_LEN + UDP_HDR_LEN)
		return DQ_L2_TRUNCATED;

	ethertype = be16(p + 12);
	if (ethertype == ETHERTYPE_QINQ)
		return DQ_L2_QINQ;
	if (ethertype == ETHERTYPE_VLAN) {
		if (len < ETH_HDR_LEN + VLAN_TAG_LEN + IPV4_HDR_LEN + UDP_HDR_LEN)
			return DQ_L2_TRUNCATED;
		vlan_id = be16(p + 14) & 0x0FFF;
		ethertype = be16(p + 16);
		l2 += VLAN_TAG_LEN;
		if (ethertype == ETHERTYPE_VLAN || ethertype == ETHERTYPE_QINQ)
			return DQ_L2_QINQ;
	}
	if (ethertype != ETHERTYPE_IPV4)
		return DQ_L2_NOT_IPV4;

	ip = p + l2;
	if (ip[0] != 0x45) /* version 4, IHL 5: no options */
		return DQ_L2_BAD_IHL;
	if (ip[9] != IPPROTO_UDP_NUM)
		return DQ_L2_NOT_UDP;
	if (be16(ip + 6) & IP_FRAG_MASK)
		return DQ_L2_FRAGMENT;
	ip_len = be16(ip + 2);
	if (ip_len < IPV4_HDR_LEN + UDP_HDR_LEN || ip_len > len - l2)
		return DQ_L2_BAD_IP_LEN;

	udp = ip + IPV4_HDR_LEN;
	udp_len = be16(udp + 4);
	if (udp_len < UDP_HDR_LEN || udp_len > ip_len - IPV4_HDR_LEN)
		return DQ_L2_BAD_UDP_LEN;

	out->payload_off = (uint16_t)(l2 + IPV4_HDR_LEN + UDP_HDR_LEN);
	out->payload_len = (uint16_t)(udp_len - UDP_HDR_LEN);
	out->udp_src = be16(udp);
	out->udp_dst = be16(udp + 2);
	out->vlan_id = vlan_id;
	out->ip_src = be32(ip + 12);
	out->ip_dst = be32(ip + 16);
	return DQ_L2_OK;
}

const char *dq_l2_status_name(enum dq_l2_status s)
{
	switch (s) {
	case DQ_L2_OK:
		return "ok";
	case DQ_L2_TRUNCATED:
		return "truncated";
	case DQ_L2_NOT_IPV4:
		return "not-ipv4";
	case DQ_L2_QINQ:
		return "qinq";
	case DQ_L2_BAD_IHL:
		return "bad-ihl";
	case DQ_L2_NOT_UDP:
		return "not-udp";
	case DQ_L2_FRAGMENT:
		return "fragment";
	case DQ_L2_BAD_IP_LEN:
		return "bad-ip-len";
	case DQ_L2_BAD_UDP_LEN:
		return "bad-udp-len";
	}
	return "unknown";
}
