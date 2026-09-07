// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef IIOD_EMU_NETWORK_H
#define IIOD_EMU_NETWORK_H

#include <iio/iio.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
/*
 * <Windows.h> pulls in the legacy winsock.h unless WIN32_LEAN_AND_MEAN is set,
 * and that clashes with winsock2.h. The build sets it on the target; repeat it
 * here so this header stays safe to include on its own.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
typedef SOCKET emu_socket;
#define EMU_INVALID_SOCKET INVALID_SOCKET
#else
typedef int emu_socket;
#define EMU_INVALID_SOCKET (-1)
#endif

/* Bring up / tear down the platform's networking stack. */
int emu_network_init(void);
void emu_network_deinit(void);

/* Create a blocking TCP socket bound to <port> and start listening. */
emu_socket emu_socket_listen(uint16_t port, int backlog);

/* Accept one client; <peer> receives a printable "host:port" description. */
emu_socket emu_socket_accept(emu_socket srv, char *peer, size_t peer_len);

void emu_socket_close(emu_socket sock);

/* Read/write exactly <len> bytes. Return <len>, 0 on orderly close, or -1. */
ssize_t emu_socket_read(emu_socket sock, void *dst, size_t len);
ssize_t emu_socket_write(emu_socket sock, const void *src, size_t len);

#endif /* IIOD_EMU_NETWORK_H */
