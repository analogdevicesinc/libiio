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

#include "vita49_2_iiod_helpers.h"
#include <vita49_2/vita49_2_packet_types.h>
#include "thread-pool.h"
#include <iio/iio.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * @struct vita49_2_device_plugin_node
 * @brief Represents a loadable library for executing custom commands when a Control/Control Extension Packet is received with the
 * appropriate CIF fields enabled or proper payload.
 * 
 */
struct vita49_2_device_plugin_node {

	int (*validate)(struct iio_context *ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_ackV_packet* const ackV_packet);	// Pointer to a function that validates the commands in the plugin and returns warnings (if any)
	int (*execute)(struct iio_context *ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_ackX_packet* const ackX_packet);	// Pointer to a function that executes the commands in the plugin and returns warnings (if any)
	struct vita49_2_device_plugin_node* next;

};

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
 * @brief Deallocates the linked list of hardware mappings.
 * 
 */
void vita49_2_command_cleanup(void);

// None of the other functions have be declared here as they're all internal to this thread and shouldn't be exposed.

#ifdef __cplusplus
}
#endif

#endif /* __VITA49_2_CLIENT_H__ */