/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_NETDEV_ADIN1110_H
#define IIOD_NETDEV_ADIN1110_H

#include "lwip_adin1110.h"
#include "adin1110.h"

static struct adin1110_init_param iiod_adin_ip = {
	.chip_type = ADIN1110,
	.comm_param = {
		.device_id = 3,
		.max_speed_hz = 15000000,
		.bit_order = NO_OS_SPI_BIT_ORDER_MSB_FIRST,
		.mode = NO_OS_SPI_MODE_0,
		.platform_ops = &max_spi_ops,
		.chip_select = 0,
		.extra = &iiod_adin_spi_extra,
	},
	.reset_param = {
		.port = 0,
		.number = 15,
		.pull = NO_OS_PULL_NONE,
		.platform_ops = &max_gpio_ops,
		.extra = &iiod_adin_gpio_extra,
	},
	.mac_address = NET_MAC_ADDR,
	.append_crc = true,
};

#define NET_LWIP_OPS	(&adin1110_lwip_ops)
#define NET_MAC_PARAM	(&iiod_adin_ip)

#endif /* IIOD_NETDEV_ADIN1110_H */
