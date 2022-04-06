// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil
 */

#include "network.h"

#include <errno.h>
#include <ws2tcpip.h>
#define close(s) closesocket(s)

int set_blocking_mode(int s, bool blocking)
{
	unsigned long nonblock;
	int ret;

	nonblock = blocking ? 0 : 1;

	ret = ioctlsocket(s, FIONBIO, &nonblock);
	if (ret == SOCKET_ERROR) {
		ret = -WSAGetLastError();
		return ret;
	}

	return 0;
}

int setup_cancel(struct iiod_client_pdata *io_ctx)
{
	io_ctx->events[0] = WSACreateEvent();
	if (io_ctx->events[0] == WSA_INVALID_EVENT)
		return -ENOMEM; /* Pretty much the only error that can happen */

	io_ctx->events[1] = WSACreateEvent();
	if (io_ctx->events[1] == WSA_INVALID_EVENT) {
		WSACloseEvent(io_ctx->events[0]);
		return -ENOMEM;
	}

	return 0;
}

void cleanup_cancel(struct iiod_client_pdata *io_ctx)
{
	WSACloseEvent(io_ctx->events[0]);
	WSACloseEvent(io_ctx->events[1]);
}

void do_cancel(struct iiod_client_pdata *io_ctx)
{
	WSASetEvent(io_ctx->events[1]);
}

int wait_cancellable(struct iiod_client_pdata *io_ctx, bool read)
{
	long wsa_events = FD_CLOSE;
	DWORD ret;

	if (!io_ctx->cancellable)
		return 0;

	if (read)
		wsa_events |= FD_READ;
	else
		wsa_events |= FD_WRITE;

	WSAEventSelect(io_ctx->fd, NULL, 0);
	WSAResetEvent(io_ctx->events[0]);
	WSAEventSelect(io_ctx->fd, io_ctx->events[0], wsa_events);

	ret = WSAWaitForMultipleEvents(2, io_ctx->events, FALSE,
		WSA_INFINITE, FALSE);

	if (ret == WSA_WAIT_EVENT_0 + 1)
		return -EBADF;

	return 0;
}

int network_get_error(void)
{
	return -WSAGetLastError();
}

bool network_should_retry(int err)
{
	return err == -WSAEWOULDBLOCK || err == -WSAETIMEDOUT;
}

bool network_is_interrupted(int err)
{
	return false;
}

bool network_connect_in_progress(int err)
{
	return err == -WSAEWOULDBLOCK;
}

/* Use it if available */
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0
#endif

int do_create_socket(const struct addrinfo *addrinfo)
{
	SOCKET s;

	s = WSASocketW(addrinfo->ai_family, addrinfo->ai_socktype, 0, NULL, 0,
		WSA_FLAG_NO_HANDLE_INHERIT | WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET)
		return -WSAGetLastError();

	return (int) s;
}

int set_socket_timeout(int fd, unsigned int timeout)
{
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
				(const char *) &timeout, sizeof(timeout)) < 0 ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
				(const char *) &timeout, sizeof(timeout)) < 0)
		return -WSAGetLastError();
	else
		return 0;
}
