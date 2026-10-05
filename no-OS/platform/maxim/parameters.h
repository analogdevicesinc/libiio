/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_PARAMETERS_H
#define IIOD_PARAMETERS_H

/* ---------- UART ---------- */
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

#define UART_DEVICE_ID		0
#define UART_BAUDRATE		115200
#define UART_OPS		&max_uart_ops

static struct max_uart_init_param iiod_uart_extra = {
	.flow = MAX_UART_FLOW_DIS,
};
#define UART_EXTRA		&iiod_uart_extra

/* ---------- Network (board wiring; chip binding in NETDEV_HEADER) ---------- */
#ifdef NO_OS_LWIP_NETWORKING

#include "maxim_spi.h"
#include "maxim_gpio.h"

static struct max_spi_init_param iiod_adin_spi_extra = {
	.num_slaves = 1,
	.polarity = SPI_SS_POL_LOW,
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

static struct max_gpio_init_param iiod_adin_gpio_extra = {
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

#define NET_MAC_ADDR	{ 0x00, 0x18, 0x80, 0x03, 0x25, 0x60 }

#include NETDEV_HEADER

#endif /* NO_OS_LWIP_NETWORKING */
#endif /* IIOD_PARAMETERS_H */
