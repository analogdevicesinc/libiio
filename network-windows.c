// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil
 */

#include "network.h"
#include "utils-windows.h"

#include <errno.h>
#include <ws2tcpip.h>
#include <iio/iio-debug.h>

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

int wait_cancellable(struct iiod_client_pdata *io_ctx,
		     bool read, unsigned int timeout_ms)
{
	long wsa_events = FD_CLOSE;
	DWORD ret, timeout = timeout_ms > 0 ? (DWORD) timeout_ms : WSA_INFINITE;

	if (read)
		wsa_events |= FD_READ;
	else
		wsa_events |= FD_WRITE;

	WSAEventSelect(io_ctx->fd, NULL, 0);
	WSAResetEvent(io_ctx->events[0]);
	WSAEventSelect(io_ctx->fd, io_ctx->events[0], wsa_events);

	ret = WSAWaitForMultipleEvents(2, io_ctx->events, FALSE,
				       timeout, FALSE);

	if (ret == WSA_WAIT_TIMEOUT)
		return -ETIMEDOUT;

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

int do_select(int fd, unsigned int timeout)
{
	struct timeval tv;
	struct timeval *ptv;
	fd_set set;
	int ret;

#ifdef _MSC_BUILD
	/*
	 * This is so stupid, but studio emits a signed/unsigned mismatch
	 * on their own FD_ZERO macro, so turn the warning off/on
	 */
#pragma warning(disable : 4389)
#endif
	FD_ZERO(&set);
	FD_SET(fd, &set);
#ifdef _MSC_BUILD
#pragma warning(default: 4389)
#endif

	if (timeout != 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		ptv = &tv;
	} else {
		ptv = NULL;
	}

	ret = select(fd + 1, NULL, &set, &set, ptv);
	if (ret < 0)
		return -WSAGetLastError();

	if (ret == 0)
		return -WSAETIMEDOUT;

	return 0;
}

int network_platform_init(const struct iio_context_params *params)
{
	WSADATA wsaData;
	WORD versionWanted = MAKEWORD(2, 2);
	int ret;

	ret = WSAStartup(versionWanted, &wsaData);
	if (ret) {
		prm_err(params, "Failed to initialize WinSock\n");
		return translate_wsa_error_to_posix(ret);
	}

	return 0;
}
