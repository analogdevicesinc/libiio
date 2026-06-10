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
 * @struct vita49_2_cif_mappings
 * @brief Translates a bit in one of the CIF (0 to 7) fields to an attribute on the device that can be modified/queried.
 * 
 */
struct vita49_2_cif_mapping {
	enum vita49_2_cif_types cif_marker;		/* Which CIF (0, 1, 2, 3, or 7) is associated with this attribute */
	uint32_t cif_bit;          				/* Which bit in the CIF triggers this (e.g. 21) */
	char device_name[64];       			/* ID of the target iio_device (e.g. "ad9361-phy") */
	enum vita49_2_attr_type attr_type; 		/* Type of the target attribute */
	char channel_name[64];      			/* ID of the target iio_channel (e.g. "voltage0"). Ignored for device/debug attrs */
	bool is_output;             			/* True if channel is an output (TX). Ignored for device/debug attrs */
	char attr_name[64];         			/* Attribute to write to (e.g. "sampling_frequency") */
	struct vita49_2_mapping *next;			/* Next item in the linked list */
};

/**
 * @brief Worker thread for the VITA 49.2 backend.
 * 
 * @param pool 
 * @param arguments 
 */
static void vita49_2_backend_thread(struct thread_pool *pool, void *arguments);

// None of the other functions have be declared here as they're all internal to this thread and shouldn't be exposed.

#ifdef __cplusplus
}
#endif

#endif /* __VITA49_2_CLIENT_H__ */