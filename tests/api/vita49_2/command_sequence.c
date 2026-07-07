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
#include <vita49_2/vita49_2_packet_elements.h>
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

int main(int argc, char** argv) 
{
    if (argc != 2)
    {
        fprintf(stderr, "ERROR: Expecting a single command line argument, either 'explicit' or 'implicit' to indicate what payload format to use for the Control Extension Packet.\n");
        return 1;
    }

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

    struct vita49_2_control_extension_packet command = {0};
    command.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_EXT_COMMAND;
    command.command_prologue.common_prologue.header.has_class_id = 1;
    command.command_prologue.common_prologue.has_class_id = 1;
    command.command_prologue.common_prologue.has_stream_id = 1;

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

    
    // ADI has 2 formats for the payload of Control Extension Packets called "Explicit" and "Implicit". See the definition of the
    // vita49_2_control_extension_description union in vita49_2_packet_elements.h for more information.
    // This script will demonstrate useage of both.

    // =============================================================================
    // IMPLICIT
    // =============================================================================

    if (strcmp(argv[1], "implicit") == 0)
    {    
        printf("Payload Format: implicit\n\n");

        command.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_CTRL_EXT_IMPLICIT;

        // =============================================================================
        // FIRST COMMAND
        // =============================================================================
        command.payload->control_extension.implicit.data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B;
        command.payload->control_extension.implicit.mapping = 0;
        command.payload->data.b = false;

        ssize_t packet_size;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        command.payload->control_extension.implicit.mapping = 1;
        command.payload->data.b = true;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        command.payload->control_extension.implicit.mapping = 2;
        command.payload->data.b = true;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        command.payload->control_extension.implicit.mapping = 3;
        command.payload->data.b = true;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
    }

    // =============================================================================
    // EXPLICIT
    // =============================================================================

    else if (strcmp(argv[1], "explicit") == 0)
    {
        printf("Payload Format: explicit\n\n");

        command.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_CTRL_EXT_EXPLICIT;

        // =============================================================================
        // FIRST COMMAND
        // =============================================================================

        char c1_attribute_name[] = "adi,frequency-division-duplex-mode-enable";
        char c1_channel_name[] = "debug";
        char c1_device_name[] = "ad9361-phy";

        command.payload = calloc(1, sizeof(struct vita49_2_control_extension_word_node));
        if (command.payload == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for first node in payload.\n");
            return 1;
        }

        command.payload->control_extension.explicit.data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B;
        command.payload->control_extension.explicit.data_length = sizeof(bool);
        command.payload->data.b = false;

        command.payload->control_extension.explicit.attribute_name_length = sizeof(c1_attribute_name);
        command.payload->control_extension.explicit.channel_name_length = sizeof(c1_channel_name);
        command.payload->control_extension.explicit.device_name_length = sizeof(c1_device_name);

        command.payload->device_name = c1_device_name;
        command.payload->channel_name = c1_channel_name;
        command.payload->attribute_name = c1_attribute_name;

        ssize_t packet_size;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
        {
            fprintf(stderr, "Failed to serialize first command. Error: %d\n", packet_size);
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
        char c2_attribute_name[] = "adi,gpo0-slave-rx-enable";
        char c2_channel_name[] = "debug";
        char c2_device_name[] = "ad9361-phy";

        command.payload->data.b = true;

        command.payload->control_extension.explicit.attribute_name_length = sizeof(c2_attribute_name);
        command.payload->control_extension.explicit.channel_name_length = sizeof(c2_channel_name);
        command.payload->control_extension.explicit.device_name_length = sizeof(c2_device_name);

        command.payload->device_name = c2_device_name;
        command.payload->channel_name = c2_channel_name;
        command.payload->attribute_name = c2_attribute_name;


        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        char c3_attribute_name[] = "adi,gpo1-slave-tx-enable";
        char c3_channel_name[] = "debug";
        char c3_device_name[] = "ad9361-phy";

        command.payload->data.b = true;

        command.payload->control_extension.explicit.attribute_name_length = sizeof(c3_attribute_name);
        command.payload->control_extension.explicit.channel_name_length = sizeof(c3_channel_name);
        command.payload->control_extension.explicit.device_name_length = sizeof(c3_device_name);

        command.payload->device_name = c3_device_name;
        command.payload->channel_name = c3_channel_name;
        command.payload->attribute_name = c3_attribute_name;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        char c4_attribute_name[] = "initialize";
        char c4_channel_name[] = "debug";
        char c4_device_name[] = "ad9361-phy";

        command.payload->data.b = true;

        command.payload->control_extension.explicit.attribute_name_length = sizeof(c4_attribute_name);
        command.payload->control_extension.explicit.channel_name_length = sizeof(c4_channel_name);
        command.payload->control_extension.explicit.device_name_length = sizeof(c4_device_name);

        command.payload->device_name = c4_device_name;
        command.payload->channel_name = c4_channel_name;
        command.payload->attribute_name = c4_attribute_name;

        if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
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
        

        // Unnecessary command, just wanted to test if sending string attributes is handled properly.
        
        // // =============================================================================
        // // FIFTH COMMAND
        // // =============================================================================
        // char c5_attribute_name[] = "rf_port_select";
        // char c5_channel_name[] = "voltage0";
        // char c5_device_name[] = "ad9361-phy";

        // command.payload->control_extension.explicit.data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S;
        // command.payload->control_extension.explicit.is_output = false;
        // command.payload->string_data = "B_BALANCED";

        // command.payload->control_extension.explicit.attribute_name_length = sizeof(c5_attribute_name);
        // command.payload->control_extension.explicit.channel_name_length = sizeof(c5_channel_name);
        // command.payload->control_extension.explicit.device_name_length = sizeof(c5_device_name);
        // command.payload->control_extension.explicit.data_length = sizeof("B_BALANCED");

        // command.payload->device_name = c5_device_name;
        // command.payload->channel_name = c5_channel_name;
        // command.payload->attribute_name = c5_attribute_name;

        // if ((packet_size = vita49_2_serialize_control_extension_packet(&command, packet, sizeof(packet)/4)) < 0)
        // {
        //     fprintf(stderr, "Failed to serialize fifth command.\n");
        //     return 1;
        // }

        // printf("Sending fifth command to %s:%d\n", destination_ip, VITA49_2_UDP_PORT);
        // if (sendto(fd, packet, packet_size*4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
        // {
        //     fprintf(stderr, "Failed to send packet: %s", strerror(errno));
        //     close(fd);
        //     return 1;
        // }
    }
    
    close(fd);
    return 0;
}
