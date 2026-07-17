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
#include "iio/iio.h"

#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <unistd.h>

#define DESTINATION_IP "192.168.2.1"

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_2_UDP_PORT 4991

// User Set
#define N_TX_SAMPLES 128

// Calculated/Constant
#define N_CHANNELS 2
#define BYTES_PER_SAMPLE 2

// Use (void) to silence unused warnings.
#define assertm(exp, msg) assert(((void)msg, exp))

struct iio_context *ctx;
struct iio_device *phy, *tx;
const struct iio_attr *attr;
struct iio_channel *chn;
struct iio_channels_mask *txmask;
struct iio_buffer *txbuf;
struct iio_block *txblock;

#define DEFAULT_SLEEP_S 3

// For TX we want to take a value between -1/+1 and scale it to the full range of the 16-bit DAC
// which is -32768 to 32767. In case we get values outside of -1/+1, we need to clamp them.
int16_t float_to_i16_tx(float sample) 
{
    if (sample > 1.0 || sample < -1.0)
        fprintf(stderr, "Warning: Sample '%f' will be clamped because it is outside of the -1.0 to 1.0 range.\n", sample);

    // Clamp to [-1.0, 1.0]
    float clamped = fmaxf(-1.0f, fminf(1.0f, sample));
    // Scale to int16_t range
    return (int16_t)(clamped * 32767.0f);
}

int main(int argc, char** argv) 
{
    int err;
    int fd = -1;
    char error_msg[256];

    // Grab a handle to the device
    ctx = iio_create_context(NULL, "ip:" DESTINATION_IP);
    phy = iio_context_find_device(ctx, "ad9361-phy");
    assertm(phy, "Unable to find AD9361-phy device");

    // Handle to TX
    tx = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
    assertm(tx, "Unable to find TX device");

    // Configure device into loopback mode
    attr = iio_device_find_debug_attr(phy, "loopback");
    assertm(attr, "Unable to find loopback attribute");
    iio_attr_write_string(attr, "1");

    // TX Side
    txmask = iio_create_channels_mask(iio_device_get_channels_count(tx));
    assertm(txmask, "Unable to create TX mask");

    // Channel 0 (In-Phase)
    chn = iio_device_find_channel(tx, "voltage0", true);
    assertm(chn, "Unable to find TX channel");
    iio_channel_enable(chn, txmask);

    // Channel 1 (Quadrature)
    chn = iio_device_find_channel(tx, "voltage1", true);
    assertm(chn, "Unable to find TX channel");
    iio_channel_enable(chn, txmask);

    txbuf = iio_device_create_buffer(tx, 0, txmask);
    assertm(txbuf, "Unable to create TX buffer");

    txblock = iio_buffer_create_block(txbuf, N_TX_SAMPLES * BYTES_PER_SAMPLE *
                                                N_CHANNELS);
    assertm(txblock, "Unable to create TX block");

    // Generate sine wave signal on both I and Q channels
    int16_t *p_dat, *p_end;
    ptrdiff_t p_inc;
    int16_t idx = 0;
    const float two_pi = 6.28318530717958647692f;

    // I'm not specificying a specific frequency like 100 kHz, rather I'm specifying that a complete cycle of the sinusoid
    // is capture in this buffer. From there, the frequency of the transmitted signal is just the sampling rate of the AD9361
    // divided by the number of samples.
        // So if the sampling rate is 3 MHz and the TX buffer is 128 samples, 3E6/128 = 23,400 meaning the buffer is played
        // back 23,400 times a second, hence the signal being transmitted is at 23.4 kHz.
    // const float phase_step = two_pi / N_TX_SAMPLES;
    const float phase_step = (two_pi) / 128;

    p_end = iio_block_end(txblock);
    p_inc = iio_device_get_sample_size(tx, txmask);
    chn = iio_device_find_channel(tx, "voltage0", true);

    for (p_dat = iio_block_first(txblock, chn); p_dat < p_end; p_dat += p_inc / sizeof(*p_dat)) 
    {
        p_dat[0] = float_to_i16_tx(cosf(phase_step * idx));
        p_dat[1] = float_to_i16_tx(sinf(phase_step * idx));
        idx++;
    }

    // Load the data onto the Pluto
    if ((err = iio_block_enqueue(txblock, 0, true)) < 0)
    {
        iio_strerror(err, error_msg, sizeof(error_msg));
        fprintf(stderr, "Could not enqueue TX block. (%d) %s\n", err, error_msg);
        goto cleanup;
    }

    if ((err = iio_buffer_enable(txbuf)) < 0)
    {
        iio_strerror(err, error_msg, sizeof(error_msg));
        fprintf(stderr, "Could not enable TX buffer. (%d) %s\n", err, error_msg);
        goto cleanup;
    }


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
        sleep_us = DEFAULT_SLEEP_S * 1e6;
    else
        sleep_us = atoi(argv[1]) * 1e6;

    printf("Sending I/Q samples request every %d seconds\n", sleep_us);

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
    iio_block_destroy(txblock);
    iio_buffer_destroy(txbuf);

    return 0;
}