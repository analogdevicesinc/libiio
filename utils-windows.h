/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2025 Analog Devices, Inc.
 * Author: Dan Nechita <dan.nechita@analog.com>
 */

#ifndef __IIO_UTILS_WINDOWS_H
#define __IIO_UTILS_WINDOWS_H

#include <winsock2.h>
#include <errno.h>

/**
 * @brief Translate WSA error codes to POSIX error codes
 * 
 * This function provides comprehensive mapping of Windows Socket API error codes
 * to their POSIX equivalents. It handles networking errors, Winsock initialization
 * issues, DNS resolution failures, and system-level errors that can occur in 
 * socket operations on Windows systems.
 * 
 * @param wsa_err WSA error code (positive value from WSAGetLastError())
 * @return Negative POSIX error code suitable for errno usage
 * 
 * @note Some WSA errors don't have direct POSIX equivalents and are mapped
 *       to the closest available error code. Falls back to -EIO for unknown errors.
 *       Covers both standard socket errors and Windows-specific system errors.
 */
static inline int translate_wsa_error_to_posix(int wsa_err)
{
	switch (wsa_err) {
	/* Generic Windows/WSA errors */
	case WSA_INVALID_HANDLE:
		return -EBADF; // Invalid handle, closest equivalent
	case WSA_NOT_ENOUGH_MEMORY:
		return -ENOMEM;
	case WSA_INVALID_PARAMETER:
		return -EINVAL;
	case WSA_OPERATION_ABORTED:
#ifdef ECANCELED
		return -ECANCELED; // Operation was aborted
#else
		return -EINTR; // Fallback to interrupted system call
#endif
	case WSA_IO_PENDING:
		return -EINPROGRESS; // Operation will complete later
	/* Standard socket errors */
	case WSAEACCES:
		return -EACCES;
	case WSAEADDRINUSE:
		return -EADDRINUSE;
	case WSAEADDRNOTAVAIL:
		return -EADDRNOTAVAIL;
	case WSAEAFNOSUPPORT:
		return -EAFNOSUPPORT;
	case WSAEALREADY:
		return -EALREADY;
	case WSAEBADF:
		return -EBADF;
	case WSAECONNREFUSED:
		return -ECONNREFUSED;
	case WSAECONNRESET:
		return -ECONNRESET;
	case WSAECONNABORTED:
		return -ECONNABORTED;
	case WSAEDESTADDRREQ:
		return -EDESTADDRREQ;
	case WSAEDISCON:
		return -ECONNRESET; // Graceful shutdown, closest equivalent
#ifdef EDQUOT
	case WSAEDQUOT:
		return -EDQUOT;
#else
	case WSAEDQUOT:
		return -ENOSPC; // Fallback to "no space left on device"
#endif
	case WSAEFAULT:
		return -EFAULT;
#ifdef EHOSTDOWN
	case WSAEHOSTDOWN:
		return -EHOSTDOWN;
#else
	case WSAEHOSTDOWN:
		return -ENETUNREACH; // Fallback to network unreachable
#endif
	case WSAEHOSTUNREACH:
		return -EHOSTUNREACH;
	case WSAEINPROGRESS:
		return -EINPROGRESS;
	case WSAEINTR:
		return -EINTR;
	case WSAEINVAL:
		return -EINVAL;
	case WSAEISCONN:
		return -EISCONN;
	case WSAELOOP:
		return -ELOOP;
	case WSAEMFILE:
		return -EMFILE;
	case WSAEMSGSIZE:
		return -EMSGSIZE;
	case WSAENAMETOOLONG:
		return -ENAMETOOLONG;
	case WSAENETDOWN:
		return -ENETDOWN;
	case WSAENETRESET:
		return -ENETRESET;
	case WSAENETUNREACH:
		return -ENETUNREACH;
	case WSAENOBUFS:
		return -ENOBUFS;
	case WSAENOPROTOOPT:
		return -ENOPROTOOPT;
	case WSAENOTEMPTY:
		return -ENOTEMPTY;
	case WSAENOTSOCK:
		return -ENOTSOCK;
	case WSAENOTCONN:
		return -ENOTCONN;
	case WSAEOPNOTSUPP:
		return -EOPNOTSUPP;
#ifdef EPFNOSUPPORT
	case WSAEPFNOSUPPORT:
		return -EPFNOSUPPORT;
#else
	case WSAEPFNOSUPPORT:
		return -EAFNOSUPPORT; // Fallback to address family not supported
#endif
	case WSAEPROCLIM:
		return -EAGAIN; // Too many processes, closest equivalent
	case WSAEPROTONOSUPPORT:
		return -EPROTONOSUPPORT;
	case WSAEPROTOTYPE:
		return -EPROTOTYPE;
#ifdef EREMOTE
	case WSAEREMOTE:
		return -EREMOTE;
#else
	case WSAEREMOTE:
		return -EIO; // Fallback to I/O error
#endif
#ifdef ESHUTDOWN
	case WSAESHUTDOWN:
		return -ESHUTDOWN;
#else
	case WSAESHUTDOWN:
		return -ECONNABORTED;
#endif
#ifdef ESOCKTNOSUPPORT
	case WSAESOCKTNOSUPPORT:
		return -ESOCKTNOSUPPORT;
#else
	case WSAESOCKTNOSUPPORT:
		return -EPROTONOSUPPORT; // Fallback to protocol not supported
#endif
#ifdef ESTALE
	case WSAESTALE:
		return -ESTALE;
#else
	case WSAESTALE:
		return -EIO; // Fallback to I/O error
#endif
	case WSAETIMEDOUT:
		return -ETIMEDOUT;
#ifdef ETOOMANYREFS
	case WSAETOOMANYREFS:
		return -ETOOMANYREFS;
#else
	case WSAETOOMANYREFS:
		return -ENOBUFS; // Fallback to closest equivalent
#endif
#ifdef EUSERS
	case WSAEUSERS:
		return -EUSERS;
#else
	case WSAEUSERS:
		return -EAGAIN; // Fallback for user quota exceeded
#endif
	case WSAEWOULDBLOCK:
		return -EAGAIN;
	/* Winsock initialization and system errors */
	case WSASYSNOTREADY:
		return -ENODEV; // Network subsystem unavailable
	case WSAVERNOTSUPPORTED:
		return -ENOSYS; // Version not supported
	case WSANOTINITIALISED:
		return -ENODEV; // WSAStartup not performed
	/* DNS resolution errors */
	case WSAHOST_NOT_FOUND:
		return -ENOENT; // Host not found
	case WSATRY_AGAIN:
		return -EAGAIN; // Temporary DNS failure
	case WSANO_RECOVERY:
		return -EIO; // Non-recoverable DNS error
	case WSANO_DATA:
#ifdef ENODATA
		return -ENODATA; // Valid name but no data record
#else
		return -ENOENT; // Fallback to "no such file or directory"
#endif
	default:
		if (wsa_err > -4096 && wsa_err < 0) // pass through for POSIX errors
			return wsa_err;

		return -EIO;   // generic fallback
	}
}

#endif /* __IIO_UTILS_WINDOWS_H */
