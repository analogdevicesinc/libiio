/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CAPI_INCLUDE_IIOD_UART_H_
#define CAPI_INCLUDE_IIOD_UART_H_

#include <capi_uart.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serve the registered devices to libiio clients on an initialized UART
 * (URI serial:<port>,<baud> on the host), one client after another. The
 * server runs on the caller's thread, so the application calls this last,
 * after registering its devices. Only returns on a setup error. Nothing
 * else may use the UART meanwhile.
 *
 * Receive is interrupt driven when the capi_uart supports it: the
 * application then routes the UART's interrupt vector to capi_uart_isr()
 * (CAPI leaves the vectors to it). Otherwise it polls the receive FIFO,
 * which can lose bytes under streaming.
 *
 * @idle is the application's own work, called while no byte from the host
 * is waiting (may be NULL); each call must be short and must not block or
 * touch @uart. */
int iiod_uart_run(struct capi_uart_handle *uart, void (*idle)(void));

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIOD_UART_H_ */
