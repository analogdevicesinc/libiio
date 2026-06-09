/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 * 
 * Contributors:
 * 		- Praveen Perera <praveen.perera@analog.com>
 */

#ifndef __VITA49_2_BACKEND_H__
#define __VITA49_2_BACKEND_H__

#include <iio/iio.h>
#include "vita49_packet.h"

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

/* Initialize the VITA-49.2 command translation layer mapping context. */
int vrt_command_init(struct iio_context *ctx);

/* Clean up the VITA-49.2 command translation layer. */
void vrt_command_cleanup(void);

/* Load mappings from a simple CSV configuration file.
 * Format: stream_id,cif0_bit,device_name,channel_name,is_output,attr_name
 */
int vrt_command_load_mappings(const char *file_path);

/* Add a mapping programmatically (useful for tests or default config). */
int vrt_command_add_mapping(uint32_t stream_id, uint32_t cif0_bit, 
			    const char *device_name, enum vrt_attr_type attr_type, const char *channel_name,
			    bool is_output, const char *attr_name);

/* Start the UDP listening thread for VITA-49.2 command packets */
int vrt_command_start_listener(struct iio_context *ctx, uint16_t port);

/* Stop the VITA-49.2 command listener */
void vrt_command_stop_listener(void);

/* Process an incoming VRT packet and translate its commands
 * (e.g. IF Context flags) to IIO library attribute writes.
 *
 * It checks the packet TYPE (Command/Context), parses the CIF0 elements,
 * and finds the corresponding IIO device/channel and applies the attributes.
 */
int vrt_process_command_packet(struct iio_context *ctx, const struct vrt_packet *pkt);

#ifdef __cplusplus
}
#endif

#endif /* __VITA49_2_BACKEND_H__ */
