/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 *         Robin Getz
 */

#ifndef __IIO_NETWORK_H
#define __IIO_NETWORK_H

#include <stdbool.h>

struct addrinfo;

struct iiod_client_pdata {
	int fd;

	/* Only buffer IO contexts can be cancelled. */
	bool cancellable;
	bool cancelled;
	void * events[2];
	int cancel_fd[2];
	unsigned int timeout_ms;
};

int setup_cancel(struct iiod_client_pdata *io_ctx);
void cleanup_cancel(struct iiod_client_pdata *io_ctx);
void do_cancel(struct iiod_client_pdata *io_ctx);
int wait_cancellable(struct iiod_client_pdata *io_ctx, bool read);

int do_create_socket(const struct addrinfo *addrinfo);

int set_blocking_mode(int s, bool blocking);
int set_socket_timeout(int fd, unsigned int timeout);

int network_get_error(void);
bool network_should_retry(int err);
bool network_is_interrupted(int err);
bool network_connect_in_progress(int err);

#endif /* __IIO_NETWORK_H */
