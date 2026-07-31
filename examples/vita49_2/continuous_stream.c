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

// This script demonstrates how the VITA 49.2 system can be used to retrieve I/Q samples.
// It works by configuring the TX channel on the device to output a sine wave, then sends Control Packets
// on a specific interval to request that data.

// This script doesn't capture nor display the transmitted I/Q data. Instead you can
// use Wireshark dissect the VITA 49.2 Data Packets, and/or use GNU Radio with Difi Source blocks
// to actually digest the I/Q data and plot it on a Data vs. Time or Power vs. Frequency graphs.

// This example was designed for the ADALM Pluto.

// This example borrows heavily from Travis' test_ad9364.c code.

#include <vita49_2/vita49_2_packet_types.h>

#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <unistd.h>

#define DESTINATION_IP "192.168.2.1"

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_2_UDP_PORT 4991

#define DEFAULT_SLEEP 0.5

int main(int argc, char** argv) 
{
    int err;
    int fd = -1;

    // Socket for sending VITA 49.2 Control Packets
    struct sockaddr_in addr;
    uint32_t packet[1024];

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) 
    {
        fprintf(stderr, "Failed to initialize socket. (%d) %s\n", strerror(errno));
        goto cleanup;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VITA49_2_UDP_PORT);
    addr.sin_addr.s_addr = inet_addr(DESTINATION_IP);


    // Control Packet to request I/Q data
    struct vita49_2_control_packet request_iq_packet = {0};
    request_iq_packet.command_prologue.common_prologue.header.packet_count = 1;
    request_iq_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
    request_iq_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
    request_iq_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;
    request_iq_packet.command_prologue.common_prologue.header.has_class_id = 1;

    request_iq_packet.command_prologue.common_prologue.has_timestamp_int = 1;
    request_iq_packet.command_prologue.common_prologue.has_timestamp_frac = 0;

    request_iq_packet.command_prologue.common_prologue.stream_id = 1;
    request_iq_packet.command_prologue.common_prologue.has_stream_id = 1;

    request_iq_packet.command_prologue.common_prologue.class_id.oui = OUI; 
    request_iq_packet.command_prologue.common_prologue.class_id.packet_class_code = VITA49_2_PKT_CLASS_REFILL_TIME_REQUEST;
    request_iq_packet.command_prologue.common_prologue.class_id.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
    request_iq_packet.command_prologue.common_prologue.has_class_id = 1;

    request_iq_packet.command_prologue.control_cam = calloc(1, sizeof(*request_iq_packet.command_prologue.control_cam));
    if (request_iq_packet.command_prologue.control_cam == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for CAM field in Control Packet for requesting I/Q data.\n");
        return 1;
    }

    // Serializing packet
    ssize_t packet_size;
    if ((packet_size = vita49_2_serialize_control_packet(&request_iq_packet, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize Control Packet Requesting I/Q Data! %s\n", strerror(packet_size));
        goto cleanup;
    }

    int sleep_us;
    if (argc == 1)
        sleep_us = DEFAULT_SLEEP*1e6;
    else
        sleep_us = atoi(argv[1])*1e1;

    printf("Sending I/Q samples request every %d useconds\n", sleep_us);

    // Request samples on an interval
    while (1)
    {
        printf("Sending VITA 49.2 Control Packet to Request I/Q Samples from %s:%d\n", DESTINATION_IP, VITA49_2_UDP_PORT);
        if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
        {
            fprintf(stderr, "Failed to send UDP Packet. (%d) %s\n", errno, strerror(errno));
        }
        
        usleep(sleep_us);
    }

    // Cleanup
    cleanup:
    if (fd != -1)
        close(fd);

    return 0;
}