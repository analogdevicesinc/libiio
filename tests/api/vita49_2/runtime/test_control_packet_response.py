# SPDX-License-Identifier: LGPL-2.1-or-later
#
# libiio - Library for interfacing industrial I/O (IIO) devices
#
# Copyright (C) 2026 Analog Devices, Inc.
# Author: Praveen Perera <praveen.perera@analog.com>

# Uses PyShark to evaluate whether the system appropriately responds to Control Packets under different circumstances
# (AckV requested, AckV/AckS requested, failing controls, etc.).

import pyshark
from ctypes import pointer
from time import sleep
import threading
import socket
import iio
from iio import *

SRC_IP = "192.168.2.1"  # Pluto's IP
SRC_PORT = 4991 # VITA 49.2 convention

PROTO = "v49d2" # The VITA 49.2 Wireshark dissector plugin by Geontech

def consume_packets(capture):
    for packet in capture.sniff_continuously():
        layer = packet[PROTO]
        print(layer.field_names)
        # packet.pretty_print()

# List of situations that we want to exercise:
# Host sends a Control Packet:
    # Action Mode = No-Action Mode:

        # Bad Commands:
            # AckV/AckX/AckS Requested:
                # AckV with warnings
                # AckX not sent
                # AckS should have the original field values (nothing should've changed)

            # AckV Requested:
                # AckV with warnings
                # AckX not sent
                # AckS not sent

            # AckX Requested:
                # AckV with warnings
                # AckX not sent

                # AckS not sent

            # ...

        # Good Commands:
            # AckV/AckX/AckS Requested:
                # AckV with empty payload
                # AckX not sent
                # AckS with new field values

            # AckV Requested:
                # AckV with empty payload
                # AckX not sent
                # AckS not sent

            # AckX Requested:
                # AckV with empty payload
                # AckX not sent
                # AckS not sent

            # ...

    # Action Mode = Execute Mode:

        # Bad Commands:
            # AckV/AckX/AckS Requested:
                # AckV with warnings
                # AckX with warnings/errors
                # AckS should have the original field values (nothing should've changed)

            # AckV Requested:
                # AckV with warnings
                # AckX not sent
                # AckS not sent

            # AckX Requested:
                # AckV with warnings
                # AckX with warnings/errors
                # AckS not sent

            # ...

        # Good Commands:
            # AckV/AckX/AckS Requested:
                # AckV with empty payload
                # AckX with empty payload
                # AckS with new field values

            # AckV Requested:
                # AckV with empty payload
                # AckX not sent
                # AckS not sent

            # AckX Requested:
                # AckV with empty payload
                # AckX with empty payload
                # AckS not sent

            # ...

# I'm doing the most minimal setup for this Control Packet since I have firsthand knowledge of how the VITA 49.2 IIOD backend logic works.
# I'm skipping the initialization of many fields, therefore I would NOT recommend you repeat what I'm doing.
control_pkt = VITA49_2_Control_Packet()
control_pkt.struct.command_prologue.common_prologue.header.has_class_id = True
control_pkt.struct.command_prologue.common_prologue.header.packet_type = VITA49_2_Packet_Types.COMMAND

control_pkt.struct.command_prologue.common_prologue.has_stream_id = True
control_pkt.struct.command_prologue.common_prologue.has_timestamp_int = True
control_pkt.struct.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_Packet_Class_Codes.GENERIC_CONTROL
control_pkt.struct.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_Information_Class_Codes.MODULE_TIME_DATA
control_pkt.struct.command_prologue.common_prologue.has_class_id = True

control_cam = iio._VITA49_2_Control_Cam_Field()
control_cam.request_ack_v = True
control_cam.request_ack_x = True
control_cam.request_ack_s = True
control_cam.action_bits = 2
control_pkt.struct.command_prologue.control_cam = pointer(control_cam)

control_pkt.struct.cif0.cif0_word.has_sample_rate = True
control_pkt.struct.cif0.sample_rate = 2083335

# UDP Socket Creation
udp_send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Live Capture mode with BPF Filter (Berkeley Packet Filter)
capture = pyshark.LiveCapture(
    interface='enp0s9', 
    bpf_filter=f'host {SRC_IP} and udp port {SRC_PORT}',
    decode_as={
        f'udp.port=={SRC_PORT}': PROTO
    }
)

capture_thread = threading.Thread(
    target = consume_packets,
    args=(capture,)
    )
capture_thread.start()

print(f"Capture thread started. Listing for packets from {SRC_IP}:{SRC_PORT}")

sleep(2)

# Sending the Control Packet
udp_send.sendto(control_pkt.to_bytes(), (SRC_IP, SRC_PORT))

while(True):
    pass