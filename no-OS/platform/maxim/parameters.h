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

#endif /* IIOD_PARAMETERS_H */
