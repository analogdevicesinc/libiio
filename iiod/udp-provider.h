/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * iiod's UDP sample stream provider (udp-stream.h): the samples of an input
 * buffer, read from the kernel in blocks and sent as UDP datagrams by a thread
 * of iiod.
 */

#ifndef __IIOD_UDP_PROVIDER_H__
#define __IIOD_UDP_PROVIDER_H__

#include <stddef.h>
#include <stdint.h>

#define IIOD_UDP_PROVIDER_DEFAULT_BLOCK_SIZE 1048576

/* As with iiod's other optional parts, callers test WITH_IIOD_UDP_PROVIDER,
 * which the build sets to 0 where udp-provider.c is not compiled. */

/* Register the provider. The datagrams of every stream leave from UDP port
 * <port>; the kernel blocks they are cut from are <block_size> bytes, rounded
 * down to a whole number of datagrams. Call it before the server takes
 * connections. Returns 0 or a negative error code. */
int iiod_udp_provider_init(uint16_t port, size_t block_size);

/* Remove the provider. Every buffer must be closed by then. */
void iiod_udp_provider_exit(void);

#endif /* __IIOD_UDP_PROVIDER_H__ */
