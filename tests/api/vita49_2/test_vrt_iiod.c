/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "iiod/vrt_command.h"

int main()
{
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_channel *chn;
	const struct iio_attr *attr;
	struct vrt_packet pkt;
	uint32_t payload[10];

	printf("test_vrt_iiod: Starting...\n");

	/* Create a local context - we need a real context to test property translation */
	/* Since ad9361-phy might not exist on the mock system, we will see it fail gracefully */
	ctx = iio_create_context(NULL, "local:");
	if (!ctx) {
		printf("test_vrt_iiod: Unable to create local context, skipping test.\n");
		return 0;
	}

	/* Initialize translation layer */
	vrt_command_init(ctx);
	vrt_command_add_mapping(0x12345678, 21, "ad9361-phy", VRT_ATTR_TYPE_CHANNEL, "voltage0", true, "sampling_frequency");
	vrt_command_add_mapping(0x12345678, 29, "ad9361-phy", VRT_ATTR_TYPE_CHANNEL, "voltage0", true, "rf_bandwidth");

	/* Craft a VITA 49.2 Context Packet asserting a sample rate and bandwidth */
	memset(&pkt, 0, sizeof(pkt));
	pkt.header.packet_type = VRT_PKT_TYPE_IF_CONTEXT;
	pkt.has_stream_id = true;
	pkt.stream_id = 0x12345678;

	/* CIF0 at payload[0] */
	/* Set Sample Rate and Bandwidth flags in CIF0 */
	vrt_set_payload_word(payload, 10, 0, (1 << 21) | (1 << 29));
	
	/* Bandwidth (Bit 29 evaluates first): 56 MHz (56e6) */
	vrt_set_payload_double(payload, 10, 1, 56e6);

	/* Sample Rate (Bit 21 evaluates next): 100 MSPS (100e6) */
	vrt_set_payload_double(payload, 10, 3, 100e6);

	pkt.payload = payload;
	pkt.payload_words = 5;

	/* Process the command. 
	 * This will likely print "Device ad9361-phy not found" if it isn't connected.
	 * But we verify that the function runs and doesn't crash.
	 */
	vrt_process_command_packet(ctx, &pkt);

	vrt_command_cleanup();
	iio_context_destroy(ctx);

	printf("test_vrt_iiod: Passed.\n");
	return 0;
}
