// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * Socket portability layer.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "network.h"

#ifdef _WIN32

typedef int emu_socklen;
#define EMU_SEND_FLAGS 0
#define emu_iobuf(p) ((char *)(p))
#define emu_iolen(l) ((int)(l))
#define emu_optval(p) ((const char *)(p))

static int emu_errno(void)
{
	return WSAGetLastError();
}

static bool emu_should_retry(int err)
{
	return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT || err == WSAEINTR;
}

int emu_network_init(void)
{
	WSADATA wsa_data;

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data)) {
		fprintf(stderr, "Unable to initialize Winsock\n");
		return -1;
	}

	return 0;
}

void emu_network_deinit(void)
{
	WSACleanup();
}

#else /* !_WIN32 */

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

typedef socklen_t emu_socklen;
#ifdef MSG_NOSIGNAL
#define EMU_SEND_FLAGS MSG_NOSIGNAL
#else
#define EMU_SEND_FLAGS 0
#endif
#define emu_iobuf(p) (p)
#define emu_iolen(l) (l)
#define emu_optval(p) (p)
#define closesocket(s) close(s)

static int emu_errno(void)
{
	return errno;
}

static bool emu_should_retry(int err)
{
	return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}

int emu_network_init(void)
{
	/* macOS has no MSG_NOSIGNAL; do not die when a client vanishes. */
	signal(SIGPIPE, SIG_IGN);

	return 0;
}

void emu_network_deinit(void)
{
}

#endif /* _WIN32 */

emu_socket emu_socket_listen(uint16_t port, int backlog)
{
	struct sockaddr_in addr;
	emu_socket sock;
	int yes = 1;

	/* Not a designated initializer: s_addr is a macro on Windows. */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == EMU_INVALID_SOCKET) {
		fprintf(stderr, "Unable to create socket: %d\n", emu_errno());
		return EMU_INVALID_SOCKET;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, emu_optval(&yes), sizeof(yes))) {
		fprintf(stderr, "Unable to set SO_REUSEADDR: %d\n", emu_errno());
		goto err_close;
	}

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		fprintf(stderr, "Unable to bind port %u: %d\n", port, emu_errno());
		goto err_close;
	}

	if (listen(sock, backlog)) {
		fprintf(stderr, "Unable to listen: %d\n", emu_errno());
		goto err_close;
	}

	return sock;

err_close:
	closesocket(sock);
	return EMU_INVALID_SOCKET;
}

emu_socket emu_socket_accept(emu_socket srv, char *peer, size_t peer_len)
{
	struct sockaddr_in addr;
	emu_socklen addr_len = sizeof(addr);
	emu_socket sock;
	int yes = 1;

	sock = accept(srv, (struct sockaddr *)&addr, &addr_len);
	if (sock == EMU_INVALID_SOCKET) {
		fprintf(stderr, "Unable to accept connection: %d\n", emu_errno());
		return EMU_INVALID_SOCKET;
	}

	/*
	 * IIOD answers with a header and then a payload, in two writes. Nagle
	 * holds the second one back until the client acknowledges the first,
	 * which costs about 25ms per reply. The client disables it on its end
	 * too, so do the same here. Not fatal if it fails, we just stay slow.
	 */
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, emu_optval(&yes), sizeof(yes)))
		fprintf(stderr, "Unable to set TCP_NODELAY: %d\n", emu_errno());

	if (peer && peer_len) {
		/*
		 * inet_ntop() requires _WIN32_WINNT >= 0x600, which is only set
		 * for MinGW here. We always bind AF_INET, so format by hand.
		 */
		uint32_t ip = ntohl(addr.sin_addr.s_addr);

		snprintf(peer, peer_len, "%u.%u.%u.%u:%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff,
				(ip >> 8) & 0xff, ip & 0xff, ntohs(addr.sin_port));
	}

	return sock;
}

void emu_socket_close(emu_socket sock)
{
	closesocket(sock);
}

ssize_t emu_socket_read(emu_socket sock, void *dst, size_t len)
{
	char *ptr = dst;
	size_t done = 0;

	while (done < len) {
		ssize_t ret = recv(sock, emu_iobuf(ptr + done), emu_iolen(len - done), 0);

		if (ret > 0) {
			done += (size_t)ret;
		} else if (ret == 0) {
			/* Orderly shutdown; rw.c turns this into -EPIPE. */
			return 0;
		} else if (!emu_should_retry(emu_errno())) {
			return -1;
		}
	}

	return (ssize_t)done;
}

ssize_t emu_socket_write(emu_socket sock, const void *src, size_t len)
{
	const char *ptr = src;
	size_t done = 0;

	while (done < len) {
		ssize_t ret = send(sock, emu_iobuf((char *)ptr + done), emu_iolen(len - done),
				EMU_SEND_FLAGS);

		if (ret > 0)
			done += (size_t)ret;
		else if (!emu_should_retry(emu_errno()))
			return -1;
	}

	return (ssize_t)done;
}
