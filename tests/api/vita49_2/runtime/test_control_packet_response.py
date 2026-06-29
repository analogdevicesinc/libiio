# SPDX-License-Identifier: LGPL-2.1-or-later
#
# libiio - Library for interfacing industrial I/O (IIO) devices
#
# Copyright (C) 2026 Analog Devices, Inc.
# Author: Praveen Perera <praveen.perera@analog.com>

# Uses PyShark to evaluate whether the system appropriately responds to Control Packets under different circumstances
# (AckV requested, AckV/AckS requested, failing controls, etc.).

import pyshark
import iio

SRC_IP = "192.168.2.1"  # Pluto's IP
SRC_PORT = "4991" # VITA 49.2 convention

PROTO = "v49d2" # The VITA 49.2 Wireshark dissector plugin by Geontech

# Live Capture mode with BPF Filter (Berkeley Packet Filter)
capture = pyshark.LiveCapture(
    interface='enp0s9', 
    bpf_filter=f'host {SRC_IP} and udp port {SRC_PORT}',
    decode_as={
        f'udp.port=={SRC_PORT}': PROTO
    }
)

print(f"Listing for packets from {SRC_IP}:{SRC_PORT}")

for packet in capture.sniff_continuously():
    layer = packet[PROTO]
    print(layer.field_names)
    if ("context_header" in layer.field_names):
        print(packet[PROTO].context_header_packet_type)

pkt = iio.VITA49_2_Data_Packet()