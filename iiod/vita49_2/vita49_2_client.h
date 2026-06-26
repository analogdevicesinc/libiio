/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 * 
 * Contributors:
 * 		- Travis Collins <travis.collins@analog.com>
*/

// Runs on the System-on-Module (SoM) and handles processing incoming VITA 49.2 packets,
// executing any necessary commands, querying data, generating VITA 49.2 packets, and sending those packets.

#ifndef __VITA49_2_CLIENT_H__
#define __VITA49_2_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <vita49_2/vita49_2_packet_types.h>
#include "thread-pool.h"

#ifdef __cplusplus
extern "C" {
#endif

// See https://wiki.analog.com/resources/tools-software/linux-software/libiio_internals
// for more information about the different types of attributes
enum vita49_2_attr_type {
	VITA49_2_ATTR_TYPE_CHANNEL = 0,
	VITA49_2_ATTR_TYPE_DEVICE,
	VITA49_2_ATTR_TYPE_DEBUG
};

// Wanted this enum to help with constraining the possible values of the cif_marker variable
enum vita49_2_cif_types {
	CIF0 = 0,
	CIF1 = 1,
	CIF2 = 2,
	CIF3 = 3,
	CIF7 = 7
};

/**
 * @struct vita49_2_stream_entry
 * @brief We need a way of keep track of Stream IDs and be able to look up existing Stream IDs, hence we'll have an array of this struct.
 * 
 */
struct vita49_2_stream_entry {
	uint32_t host_ip_addr;
	uint16_t host_port;
	enum vita49_2_packet_class_codes packet_class_code;

	uint32_t stream_id;
	uint16_t packet_count;

	// // Direction. This matters only for Signal Time Data Packets where we have to differentiate
	// // between whether the host is sending the packet or is the device is sending.
	// bool host_sending;
};

/**
 * @struct vita49_2_cif_mappings
 * @brief Translates a bit in one of the CIF (0 to 7) fields to an attribute on the device that can be modified/queried.
 * 
 */
struct vita49_2_cif_mapping {
	enum vita49_2_cif_types cif_type;		/* Which CIF (0, 1, 2, 3, or 7) is associated with this attribute */
	uint32_t cif_bit;          				/* Which bit in the CIF triggers this (e.g. 21) */
	char device_name[64];       			/* ID of the target iio_device (e.g. "ad9361-phy") */
	enum vita49_2_attr_type attr_type; 		/* Type of the target attribute */
	char channel_name[64];      			/* ID of the target iio_channel (e.g. "voltage0"). Ignored for device/debug attrs */
	bool is_output;             			/* True if channel is an output (TX). Ignored for device/debug attrs */
	char attr_name[64];         			/* Attribute to write to (e.g. "sampling_frequency") */
	struct vita49_2_cif_mapping *next;			/* Next item in the linked list */
};

/**
 * @struct vita49_2_pdata
 * @brief Data we want to pass from iiod.c to the VITA 49.2 pthread when it gets created.
 * 
 */
struct vita49_2_pdata {
	struct thread_pool *pool;
	struct iio_context *ctx;
};

/**
 * @brief Maps Linux errnos to VITA 49.2 warning/error indicators. See Table 8.4.1.2.1-1 in the VITA 49.2 2017 document.
 * 
 * VITA 49.2 Warnings/Errors:
 * 
 * 31 = Field NOT Executed
 * 
 * 30 = Device Failure
 * 
 * 29 = Erroneous Field
 * 
 * 28 = Parameter Out-of-Range
 * 
 * 27 = Parameter Unsupported Precision
 * 
 * 26 = Field Value Invalid
 * 
 * 25 = Timestamp Problem
 * 
 * 24 = Hazardous Power Levels
 * 
 * 23 = Distortion
 * 
 * 22 = In-Band Power Compliance
 * 
 * 21 = Out-of-Band Power Compliance
 * 
 * 20 = Co-Site Interference
 * 
 * 19 = Regional Interference
 * 
 * 18-13 = Reserved
 * 
 * 12-1 = User Defined
 * 
 * 0 = Reserved
 * 
 * See the vita49_2_warnings_error_codes enum.
 */
static const uint8_t VITA49_2_ERRNO_MAP[] = {
    [0]                 = ENONE,
    [EPERM]             = ENOEXECUTE,   /* Operation not permitted */
    [ENOENT]            = ENOFIELD,     /* No such file/directory -> erroneous field */
    [ESRCH]             = ENOFIELD,
    [EINTR]             = ENOEXECUTE,   /* Interrupted -> not executed */
    [EIO]               = EDEVFAIL,     /* I/O error -> device failure */
    [ENXIO]             = EDEVFAIL,     /* No such device or address */
    [E2BIG]             = EOUTRANGE,    /* Argument list too long -> out-of-range */
    [ENOEXEC]           = ENOFIELD,     /* Exec format error */
    [EBADF]             = ENOEXECUTE,
    [ECHILD]            = ENOEXECUTE,
    [EAGAIN]            = ENOEXECUTE,   /* Try again */
    [ENOMEM]            = EDEVFAIL,     /* Out of memory */
    [EACCES]            = ENOEXECUTE,   /* Permission denied */
    [EFAULT]            = EBADARGS,     /* Bad address -> bad arguments */
    [ENOTBLK]           = ENOFIELD,
    [EBUSY]             = ENOEXECUTE,   /* Device busy */
    [EEXIST]            = ENOFIELD,
    [EXDEV]             = ENOFIELD,
    [ENODEV]            = EDEVFAIL,     /* No such device */
    [ENOTDIR]           = ENOFIELD,
    [EISDIR]            = ENOFIELD,
    [EINVAL]            = EINVALID,     /* Invalid argument */
    [ENFILE]            = EDEVFAIL,
    [EMFILE]            = EDEVFAIL,
    [ENOTTY]            = ENOFIELD,
    [ETXTBSY]           = ENOEXECUTE,
    [EFBIG]             = EOUTRANGE,    /* File too large -> out-of-range */
    [ENOSPC]            = EDEVFAIL,     /* No space left */
    [ESPIPE]            = ENOFIELD,
    [EROFS]             = ENOEXECUTE,   /* Read-only */
    [EMLINK]            = ENOFIELD,
    [EPIPE]             = EDEVFAIL,     /* Broken pipe */
    [EDOM]              = EOUTRANGE,    /* Math domain error */
    [ERANGE]            = EOUTRANGE,    /* Result not representable */
    [EDEADLK]           = ENOEXECUTE,
    [ENAMETOOLONG]      = ENOFIELD,
    [ENOLCK]            = EDEVFAIL,
    [ENOSYS]            = ENOFIELD,     /* Function not implemented */
    [ENOTEMPTY]         = ENOFIELD,
    [ELOOP]             = ENOFIELD,
    [41]                = ENOEXECUTE,   /* unused gap */
    [ENOMSG]            = ENOEXECUTE,
    [EIDRM]             = ENOEXECUTE,
    [ECHRNG]            = EOUTRANGE,    /* Channel number out of range */
    [EL2NSYNC]          = EDEVFAIL,
    [EL3HLT]            = EDEVFAIL,
    [EL3RST]            = EDEVFAIL,
    [ELNRNG]            = EOUTRANGE,
    [EUNATCH]           = EDEVFAIL,
    [ENOCSI]            = EDEVFAIL,
    [EL2HLT]            = EDEVFAIL,
    [EBADE]             = EINVALID,     /* Invalid exchange */
    [EBADR]             = ENOFIELD,     /* Invalid request descriptor */
    [EXFULL]            = EOUTRANGE,
    [ENOANO]            = EDEVFAIL,
    [EBADRQC]           = ENOFIELD,     /* Invalid request code */
    [EBADSLT]           = ENOFIELD,     /* Invalid slot */
    [58]                = ENOEXECUTE,   /* unused gap */
    [EBFONT]            = ENOFIELD,
    [ENOSTR]            = ENOFIELD,
    [ENODATA]           = ENOEXECUTE,
    [ETIME]             = ETIMESTAMP,   /* Timer expired -> timestamp problem */
    [ENOSR]             = EDEVFAIL,
    [ENONET]            = ENOEXECUTE,
    [ENOPKG]            = ENOFIELD,
    [EREMOTE]           = EDEVFAIL,
    [ENOLINK]           = EDEVFAIL,
    [EADV]              = ENOEXECUTE,
    [ESRMNT]            = ENOEXECUTE,
    [ECOMM]             = EDEVFAIL,
    [EPROTO]            = EDEVFAIL,
    [EMULTIHOP]         = EDEVFAIL,
    [EDOTDOT]           = EDEVFAIL,
    [EBADMSG]           = ENOFIELD,
    [EOVERFLOW]         = EOUTRANGE,    /* Value too large */
    [ENOTUNIQ]          = ENOFIELD,
    [EBADFD]            = ENOEXECUTE,
    [EREMCHG]           = EDEVFAIL,
    [ELIBACC]           = EDEVFAIL,
    [ELIBBAD]           = EDEVFAIL,
    [ELIBSCN]           = EDEVFAIL,
    [ELIBMAX]           = EDEVFAIL,
    [ELIBEXEC]          = EDEVFAIL,
    [EILSEQ]            = EINVALID,     /* Illegal byte sequence */
    [ERESTART]          = ENOEXECUTE,
    [ESTRPIPE]          = EDEVFAIL,
    [EUSERS]            = ENOEXECUTE,
    [ENOTSOCK]          = ENOFIELD,
    [EDESTADDRREQ]      = ENOFIELD,
    [EMSGSIZE]          = EOUTRANGE,    /* Message too long */
    [EPROTOTYPE]        = ENOFIELD,
    [ENOPROTOOPT]       = ENOFIELD,
    [EPROTONOSUPPORT]   = ENOFIELD,
    [ESOCKTNOSUPPORT]   = ENOFIELD,
    [EOPNOTSUPP]        = ENOFIELD,     /* Operation not supported */
    [EPFNOSUPPORT]      = ENOFIELD,
    [EAFNOSUPPORT]      = ENOFIELD,
    [EADDRINUSE]        = ENOEXECUTE,
    [EADDRNOTAVAIL]     = ENOFIELD,
    [ENETDOWN]          = EDEVFAIL,
    [ENETUNREACH]       = EDEVFAIL,
    [ENETRESET]         = EDEVFAIL,
    [ECONNABORTED]      = EDEVFAIL,
    [ECONNRESET]        = EDEVFAIL,
    [ENOBUFS]           = EDEVFAIL,
    [EISCONN]           = ENOEXECUTE,
    [ENOTCONN]          = EDEVFAIL,
    [ESHUTDOWN]         = ENOEXECUTE,
    [ETOOMANYREFS]      = EDEVFAIL,
    [ETIMEDOUT]         = ENOEXECUTE,   /* Connection timed out */
    [ECONNREFUSED]      = EDEVFAIL,
    [EHOSTDOWN]         = EDEVFAIL,
    [EHOSTUNREACH]      = EDEVFAIL,
    [EALREADY]          = ENOEXECUTE,
    [EINPROGRESS]       = ENOEXECUTE,
    [ESTALE]            = EDEVFAIL,
    [EUCLEAN]           = EDEVFAIL,
    [ENOTNAM]           = ENOFIELD,
    [ENAVAIL]           = EDEVFAIL,
    [EISNAM]            = ENOFIELD,
    [EREMOTEIO]         = EDEVFAIL,     /* Remote I/O error */
    [EDQUOT]            = EOUTRANGE,    /* Quota exceeded */
    [ENOMEDIUM]         = EDEVFAIL,
    [EMEDIUMTYPE]       = ENOFIELD,
    [ECANCELED]         = ENOEXECUTE,   /* Cancelled */
    [ENOKEY]            = ENOEXECUTE,
    [EKEYEXPIRED]       = ENOEXECUTE,
    [EKEYREVOKED]       = ENOEXECUTE,
    [EKEYREJECTED]      = ENOEXECUTE,
    [EOWNERDEAD]        = EDEVFAIL,
    [ENOTRECOVERABLE]   = EDEVFAIL,
    [ERFKILL]           = EHAZPOWER,    /* RF-kill -> hazardous power */
};

// ==============================================================
// FUNCTION DECLARATIONS
// ==============================================================

/**
 * @brief Daemon for VITA 49.2 backend. Manages the VITA 49.2 processing thread.
 * 
 * @param ctx 
 * @param pool 
 * @return int 
 */
int start_vita49_2_daemon(struct iio_context *ctx, struct thread_pool *pool);

/**
 * @brief Worker thread for the VITA 49.2 backend.
 * 
 * @param pool 
 * @param arguments 
 */
static void vita49_2_main(struct thread_pool *pool, void *arguments);

/**
 * @brief Validate IIO context.
 * 
 * @param ctx 
 * @return int 
 */
int vita49_2_command_init(struct iio_context *ctx);

/**
 * @brief Load the CIF-to-hardware mappings from a simple CSV configuration file.
 * Format: stream_id,cif0_bit,device_name,channel_name,is_output,attr_name
 * 
 * @param file_path 
 * @return int 
 */
int vita49_2_command_load_mappings(const char *file_path);

/**
 * @brief Deallocates the linked list of hardware mappings.
 * 
 */
void vita49_2_command_cleanup(void);

// None of the other functions have be declared here as they're all internal to this thread and shouldn't be exposed.

#ifdef __cplusplus
}
#endif

#endif /* __VITA49_2_CLIENT_H__ */