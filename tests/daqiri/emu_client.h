/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * A connection to iiod-emu for the emulator tests: the iiod-client API over a
 * plain TCP socket, as the daqiri: backend uses it, and the context built from
 * the XML the server sends.
 */

#ifndef EMU_CLIENT_H
#define EMU_CLIENT_H

#include <iio/iio.h>
#include <iio/iiod-client.h>

#define EMU_PORT 30431 /* with_emu.sh */

struct emu_client {
	int fd; /* first: the transport's iiod_client_pdata */
	struct iiod_client *client;
	struct iio_context *ctx;
};

/* Connect to 127.0.0.1:EMU_PORT and create the client and the context.
 * Returns 0, or a negative error code after saying what failed. */
int emu_client_open(struct emu_client *ec);

/* Disconnect: the server then frees whatever the connection owned */
void emu_client_close(struct emu_client *ec);

#endif /* EMU_CLIENT_H */
