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

#define _DEFAULT_SOURCE
#include <vita49_2/vita49_2_packet_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define destination_ip "192.168.2.1"

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_2_UDP_PORT 4991

int main() 
{
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

    // Create a Control Packet to Request IQ Data
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

    request_iq_packet.command_prologue.common_prologue.class_id.lower_word.oui = OUI; 
    request_iq_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_REFILL_TIME_REQUEST;
    request_iq_packet.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
    request_iq_packet.command_prologue.common_prologue.has_class_id = 1;

    request_iq_packet.command_prologue.control_cam = calloc(1, sizeof(*request_iq_packet.command_prologue.control_cam));
    if (request_iq_packet.command_prologue.control_cam == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for CAM field.\n");
        return 1;
    }


    // Create a Control Packet to Change the Sampling Frequency
    struct vita49_2_control_packet sampling_freq_packet = {0};
    sampling_freq_packet.command_prologue.common_prologue.header.packet_count = 1;
    sampling_freq_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
    sampling_freq_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
    sampling_freq_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;
    sampling_freq_packet.command_prologue.common_prologue.header.has_class_id = 1;

    sampling_freq_packet.command_prologue.common_prologue.has_timestamp_int = 1;
    sampling_freq_packet.command_prologue.common_prologue.has_timestamp_frac = 0;

    sampling_freq_packet.command_prologue.common_prologue.stream_id = 1;
    sampling_freq_packet.command_prologue.common_prologue.has_stream_id = 1;

    sampling_freq_packet.command_prologue.common_prologue.class_id.lower_word.oui = OUI; 
    sampling_freq_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTROL;
    sampling_freq_packet.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
    sampling_freq_packet.command_prologue.common_prologue.has_class_id = 1;

    sampling_freq_packet.command_prologue.control_cam = calloc(1, sizeof(*sampling_freq_packet.command_prologue.control_cam));
    if (sampling_freq_packet.command_prologue.control_cam == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for CAM field.\n");
        return 1;
    }

    sampling_freq_packet.command_prologue.control_cam->request_ack_v = 1;
    sampling_freq_packet.command_prologue.control_cam->request_ack_x = 1;
    sampling_freq_packet.command_prologue.control_cam->request_ack_s = 1;
    sampling_freq_packet.command_prologue.control_cam->action_bits = 2;

    sampling_freq_packet.cif0.cif0_word.has_sample_rate = 1;
    sampling_freq_packet.cif0.sample_rate = 2083335;



    ssize_t packet_size;


    // Uncomment to send a packet to change the sample rate
    if ((packet_size = vita49_2_generate_control_packet(&sampling_freq_packet, packet, sizeof(packet)/4)) < 0)
    {
        fprintf(stderr, "Failed to serialize Control Packet!\n");
        return 1;
    }

    // Uncomment to instead send a packet to request IQ data
    // if ((packet_size = vita49_2_generate_control_packet(&request_iq_packet, packet, sizeof(packet)/4)) < 0)
    // {
    //     fprintf(stderr, "Failed to serialize Control Packet!\n");
    //     return 1;
    // }

    while (1) 
    {
        printf("Sending VITA 49.2 Control Packet to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
        if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
        {
            perror("sendto");
        }
        usleep(4e6);
    }

    close(fd);
    return 0;
}
