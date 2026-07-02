/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 *
 */

// This script demonstrates how a sequence of commands can be issued to the VITA subsystem within iiod.
// This example is based on this article: https://wiki.analog.com/university/tools/pluto/hacking/power_amp

#include <vita49_2/vita49_2_packet_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#define destination_ip "192.168.2.1"

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_2_UDP_PORT 4991

int main() 
{
    // Socket setup
    int fd;
    struct sockaddr_in addr;
    uint32_t packet[1024];

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VITA49_2_UDP_PORT);
    addr.sin_addr.s_addr = inet_addr(destination_ip);

    
    // The attributes that we'll be modifiying are:
        // ad9361-phy adi,frequency-division-duplex-mode-enable = 1
        // ad9361-phy adi,gpo0-slave-rx-enable = 0
        // ad9361-phy adi,gpo1-slave-tx-enable = 0
        // ad9361-phy initialize = 0

        // These attributes don't map to standard CIF fields on the Pluto, hence we'll need to use Control Extension Packets and a modified mapping conf file.


    // TODO: Create AckV/X/S Extension Packets

    // // In case a subcommand fails in our sequence, it's useful to know the initial values so that we can revert the device.
    // // To obtain those initial attribute values, we can send a Control Packet with the actions bits in the CAM set to "No-Action Mode",
    // // and assert the CAM bit to request an AckS Packet containing the initial values of the attributes we're interested in.

    // // Don't replicate what I'm doing below for a proper setup. Because I have knowledge of the VITA backend within iiod, I'm skipping
    // // initialization of some fields that I know won't be needed.
    // struct vita49_2_control_packet initial_values_rq = {0};
    // initial_values_rq.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;
    // initial_values_rq.command_prologue.common_prologue.header.has_class_id = 1;
    // initial_values_rq.command_prologue.common_prologue.has_class_id = 1;
    // initial_values_rq.command_prologue.common_prologue.has_stream_id = 1;
    // initial_values_rq.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTROL;

    // initial_values_rq.command_prologue.control_cam = calloc(1, sizeof(*initial_values_rq.command_prologue.control_cam));
    // if (initial_values_rq.command_prologue.control_cam == NULL)
    // {
    //     fprintf(stderr, "Failed to allocate memory for CAM field.\n");
    //     return 1;
    // }

    // initial_values_rq.command_prologue.control_cam->request_ack_s = 1;
    // initial_values_rq.cif0.cif0_word.has_sample_rate = 1;
    // initial_values_rq.cif0.sample_rate = 2083335;

    // ssize_t packet_size;

    // =============================================================================
    // FIRST COMMAND
    // =============================================================================
    struct vita49_2_control_extension_packet command = {0};
    command.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_EXT_COMMAND;
    command.command_prologue.common_prologue.header.has_class_id = 1;
    command.command_prologue.common_prologue.has_class_id = 1;
    command.command_prologue.common_prologue.has_stream_id = 1;
    command.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTROL;

    command.command_prologue.control_cam = calloc(1, sizeof(*command.command_prologue.control_cam));
    if (command.command_prologue.control_cam == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for CAM field.\n");
        return 1;
    }

    command.command_prologue.control_cam->action_bits = 2;

    command.payload = calloc(1, sizeof(struct vita49_2_control_extension_word_node));
    if (command.payload == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for first node in payload.\n");
        return 1;
    }

    command.payload->control_extension.data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B;
    command.payload->control_extension.mapping = 0;
    command.payload->data.b = false;

    ssize_t packet_size;

    if ((packet_size = vita49_2_generate_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize first command.\n");
        return 1;
    }

    printf("Sending first command to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
    if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "Failed to send packet: %s", strerror(errno));
        close(fd);
        return 1;
    }

    // =============================================================================
    // SECOND COMMAND
    // =============================================================================
    command.payload->control_extension.mapping = 1;
    command.payload->data.b = true;

    if ((packet_size = vita49_2_generate_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize second command.\n");
        return 1;
    }

    printf("Sending second command to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
    if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "Failed to send packet: %s", strerror(errno));
        close(fd);
        return 1;
    }

    // =============================================================================
    // THIRD COMMAND
    // =============================================================================
    command.payload->control_extension.mapping = 2;
    command.payload->data.b = true;

    if ((packet_size = vita49_2_generate_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize third command.\n");
        return 1;
    }

    printf("Sending third command to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
    if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "Failed to send packet: %s", strerror(errno));
        close(fd);
        return 1;
    }

    // =============================================================================
    // FOURTH COMMAND
    // =============================================================================
    command.payload->control_extension.mapping = 3;
    command.payload->data.b = true;

    if ((packet_size = vita49_2_generate_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize fourth command.\n");
        return 1;
    }

    printf("Sending fourth command to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
    if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "Failed to send packet: %s", strerror(errno));
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
