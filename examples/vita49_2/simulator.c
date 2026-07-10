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

// This script is meant to send VITA 49.2 Packets to an ADI SDR to demonstrate the functionality
// of the V49.2 subsystem within iiod.

// It's also useful if you'd like to analyze some of the V49.2 Packet Structures with Wireshark.

#define _DEFAULT_SOURCE
#include <vita49_2/vita49_2_packet_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define destination_ip "192.168.2.1"

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_2_UDP_PORT 4991

#define RX_GAIN 71.0
#define TX_GAIN -10.0

#define SAMPLING_RATE 30890000

int main() 
{
    int fd;
    struct sockaddr_in addr;
    uint32_t packet[1024];

    char options[][100] = 
    {
        "Control Packet to Request I/Q Data",
        "Control Packet to Modify RX Gain to " TO_STRING(RX_GAIN) " and TX Gain to " TO_STRING(TX_GAIN),
        "Control Packet to Set Sampling Rate to " TO_STRING(SAMPLING_RATE)
    };

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) 
    {
        fprintf(stderr, "Failed to initialize socket. (%d) %s\n", strerror(errno));
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

    // Create a base Control Packet for modifying CIF Attributes
    struct vita49_2_control_packet base_control_packet = {0};
    base_control_packet.command_prologue.common_prologue.header.packet_count = 1;
    base_control_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
    base_control_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
    base_control_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;
    base_control_packet.command_prologue.common_prologue.header.has_class_id = 1;

    base_control_packet.command_prologue.common_prologue.has_timestamp_int = 1;
    base_control_packet.command_prologue.common_prologue.has_timestamp_frac = 0;

    base_control_packet.command_prologue.common_prologue.stream_id = 1;
    base_control_packet.command_prologue.common_prologue.has_stream_id = 1;

    base_control_packet.command_prologue.common_prologue.class_id.oui = OUI; 
    base_control_packet.command_prologue.common_prologue.class_id.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTROL;
    base_control_packet.command_prologue.common_prologue.class_id.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
    base_control_packet.command_prologue.common_prologue.has_class_id = 1;

    base_control_packet.command_prologue.control_cam = calloc(1, sizeof(*base_control_packet.command_prologue.control_cam));
    if (base_control_packet.command_prologue.control_cam == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for CAM field in Base Control Packet.\n");
        return 1;
    }

    base_control_packet.command_prologue.control_cam->request_ack_v = 1;
    base_control_packet.command_prologue.control_cam->request_ack_x = 1;
    base_control_packet.command_prologue.control_cam->request_ack_s = 1;
    base_control_packet.command_prologue.control_cam->action_bits = 2;

    ssize_t packet_size;
    int option;

    while (1)
    {
        for (int i = 0; i < sizeof(options)/sizeof(options[0]); i++)
            printf("(%d) %s\n", i+1, options[i]);

        printf("\nChoose an option...");
        option = getchar() - '0';
        while (getchar() != '\n'); 
        printf("\n");

        switch (option)
        {
            // Send Control Packet requesting I/Q data
            case 1:

                if ((packet_size = vita49_2_serialize_control_packet(&request_iq_packet, packet, sizeof(packet)/4)) < 0)
                {
                    fprintf(stderr, "Failed to serialize Control Packet Requesting I/Q Data! %s\n", strerror(packet_size));
                    return 1;
                }

                printf("Requesting I/Q data...\n");

                break;

            // Modifying Gain
            case 2:

                base_control_packet.cif0.word.word = 0;                
                base_control_packet.cif0.word.has_gain = 1;
                base_control_packet.cif0.gains.gain_stage_1 = RX_GAIN;
                base_control_packet.cif0.gains.gain_stage_2 = TX_GAIN;

                if ((packet_size = vita49_2_serialize_control_packet(&base_control_packet, packet, sizeof(packet)/4)) < 0)
                {
                    fprintf(stderr, "Failed to serialize Control Packet to Modify Gain! %s\n", strerror(packet_size));
                    return 1;
                }

                printf("Modifying Gain...\n");

                break;

            // Modifying Sampling Rate
            case 3:

                base_control_packet.cif0.word.word = 0;                
                base_control_packet.cif0.word.has_sample_rate = 1;
                base_control_packet.cif0.sample_rate = SAMPLING_RATE;

                if ((packet_size = vita49_2_serialize_control_packet(&base_control_packet, packet, sizeof(packet)/4)) < 0)
                {
                    fprintf(stderr, "Failed to serialize Control Packet to Modify Sampling Rate! %s\n", strerror(packet_size));
                    return 1;
                }

                printf("Modifying Sampling Rate...\n");

                break;

            // Unrecognized Option
            default:
                
                fprintf(stderr, "Unrecognized option '%d'.\n\n", option);
                continue;
        }

        printf("Sending VITA 49.2 Control Packet to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
        if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
        {
            perror("sendto");
        }

        printf("\n");
    }
    
    close(fd);
    return 0;
}
