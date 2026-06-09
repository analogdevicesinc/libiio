/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef __VRT_COMMAND_H__
#define __VRT_COMMAND_H__

#include <iio/iio.h>
#include "vita49_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vrt_attr_type {
	VRT_ATTR_TYPE_CHANNEL = 0,
	VRT_ATTR_TYPE_DEVICE,
	VRT_ATTR_TYPE_DEBUG
};

struct vrt_mapping {
	uint32_t stream_id;
	uint32_t cif0_bit;          /* Which bit in CIF0 triggers this (e.g. 21) */
	char device_name[64];       /* ID of the target iio_device (e.g. "ad9361-phy") */
	enum vrt_attr_type attr_type; /* Type of the target attribute */
	char channel_name[64];      /* ID of the target iio_channel (e.g. "voltage0"). Ignored for device/debug attrs */
	bool is_output;             /* True if channel is an output (TX). Ignored for device/debug attrs */
	char attr_name[64];         /* Attribute to write to (e.g. "sampling_frequency") */
	struct vrt_mapping *next;
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

#endif /* __VRT_COMMAND_H__ */
