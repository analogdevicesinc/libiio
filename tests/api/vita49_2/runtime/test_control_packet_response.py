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
from enum import IntEnum
import subprocess 
import threading
import socket
import sys
import iio
from iio import *

SRC_IP = "192.168.2.1"  # Pluto's IP
SRC_PORT = 4991 # VITA 49.2 convention

PROTO = "v49d2" # The VITA 49.2 Wireshark dissector plugin by Geontech

ALL_PACKETS = "all_packets.pcapng"
OUTPUT_FILE = "problematic_packet.pcapng" # For storing the packet that failed the testcase

SNIFF_TIMEOUT = 5

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



# IMPORTANT: This is NOT the same as the vita49_2_packet_type enum in vita49_2_packet_elements.h, nor the packet types table
# in the VITA 49.2 2017 documentation. This is a custom enum to help with creating scaleable testing by adding definitions
# for the Acknowledge Packets since VITA 49.2 lists Acknowledge as Command Type packets, and the only way to determine what kind
# Command Packet you have is to look at indicator bits and CAM Field
class CustomPacketTypes (IntEnum):
	Data 		        = 0
	Context	            = 1
	Control 	        = 2
	AckV 	            = 3
	AckX 			    = 4
	AckS 		        = 5
	Command_Extension 	= 6

class V49_2_PacketTypes (IntEnum):
	IF_DATA_NO_SID 		= 0
	IF_DATA_WITH_SID 	= 1
	EXT_DATA_NO_SID 	= 2
	EXT_DATA_WITH_SID 	= 3
	IF_CONTEXT 			= 4
	EXT_CONTEXT 		= 5
	COMMAND 			= 6
	EXT_COMMAND 		= 7



def get_header_field(layer):
    # exact 'header' first, then anything ending in '_header'
    candidates = ["header"] + [n for n in layer.field_names if n.endswith("_header")]
    for name in candidates:
        if name in layer.field_names:
            val = getattr(layer, name, None)
            if val not in (None, ""):
                return name, val
    return None, None

def consume_packets(capture):
    for packet in capture.sniff_continuously():
        layer = packet[PROTO]

        header_name, header_val = get_header_field(layer)

        # if header_name:
        #     print("header label:", header_name)
        #     print("header value:", header_val)
        # else:
        #     print("No header-like field found")

        # if header_val is not None:
        #     print("packet_type =", getattr(layer, f"{header_name}_packet_type", None))        # print(layer.field_names)

        # print(layer.field_names)
        # print(f"CIF0 present: {'cif0' in layer.field_names}")
        # ackv = getattr(layer?, "cam_request_validation", None)
        print(layer.field_names)
        # if (ackv != None):
        #     print(ackv)
        #     print(packet)
        #     print("\n\n\n")
        # print([f for f in layer.field_names if "header" in f])
        # if ()
        # packet.pretty_print()

# Testing Framework
    # Expected Packets (in the order that they should come)
    # Special Options for what should be in each packet

    # Array of tuples
        # Tuple: Packet Type, special options list

        # EX: <AckX, [payload=empty]>

def dump_packet(frame_number):
    subprocess.run([
            "tshark",
            "-r", ALL_PACKETS,
            "-Y", f"frame.number == {frame_number}",
            "-w", OUTPUT_FILE
        ], check=True)
    
    print(f"\nDumped packet to {OUTPUT_FILE}\n")
    
def process_packet(capture, tests):

    packet_count = len(tests) + 1
    print(f"Acquiring {packet_count} packets...", end="", flush=True)

    # +1 in case a Context Packet is sent
    capture.sniff(packet_count=packet_count)
    print("Success")

    test_num = 0

    for packet in capture:

        if (test_num >= len(tests)):
            return

        test = tests[test_num]
        print(f"\nRunning Test {test_num + 1}: {test}")

        # print(f"{packet}\n\n")

        # Extracting layers and VITA header information
        layers = packet[PROTO]
        header_name, header_val = get_header_field(layers)

        # Ignoring Context Packets since they're sent on an interval and not part of
        # our scenarios.
        if ("context" in header_name):
            print("Encountered Context Packet. Skipping Packet and repeating test.")
            continue

        # Validating if the received packet matches the expected
        if (header_val is not None):
            packet_type = getattr(layers, f"{header_name}_packet_type", None)

            if (packet_type is None):
                print("ERROR: Could not extract Packet Type from incoming message.")
                sys.exit(-1)

            print("Checking if received packet type is expected...", end="")

            match test[0]:
                case "data":
                    if (packet_type == V49_2_PacketTypes.IF_DATA_WITH_SID or packet_type == V49_2_PacketTypes.IF_DATA_NO_SID):
                        print("Success")
                    else:
                        print(f"Failure, expected a Data Packet but received Message Type (int) = {packet_type}")
                        dump_packet(packet.number)
                        sys.exit(-1)

                case "context":
                    print("ERROR: Invalid testing setup. Context Packets are ignored.")
                    sys.exit(-1)

                case "control":
                    print("ERROR: Invalid testing setup. The VITA 49.2 subsystem with iiod cannot send Control Packets to the host.")
                    sys.exit(-1)

                case "ackV":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        print(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    ackV_request = getattr(layers, "cam_request_validation", None)
                    if (ackV_request == None):
                        print(f"ERROR: Unable to find the 'cam_request_validation' field.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    if (ackV_request == False):
                        print(f"Failure, 'cam_request_validation' was not asserted. This is not an AckV Packet.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                case "ackX":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        print(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    ackV_request = getattr(layers, "cam_request_execution", None)
                    if (ackV_request == None):
                        print(f"ERROR: Unable to find the 'cam_request_execution' field.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    if (ackV_request == False):
                        print(f"Failure, 'cam_request_execution' was not asserted. This is not an AckX Packet.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                case "ackS":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        print(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    ackV_request = getattr(layers, "cam_request_query", None)
                    if (ackV_request == None):
                        print(f"ERROR: Unable to find the 'cam_request_query' field.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                    if (ackV_request == False):
                        print(f"Failure, 'cam_request_query' was not asserted. This is not an AckS Packet.")
                        dump_packet(packet.number)
                        sys.exit(-1)

                case "control_extension":
                    print("ERROR: Invalid testing setup. Control Extension is unsupported.")
                    sys.exit(-1)

                case _:
                    print(f"ERROR: Invalid testing setup. Unrecognized packet type: '{test[0]}'")
                    sys.exit(-1)

            print("Success")

        # Checking any special options
        for option in test[1]:

            if (option == "cif0=0"):
                print("Checking for CIF0==0...", end = "")

                if ("cif0" not in layers.field_names):
                    print("Failure, couldn't find CIF0")
                    dump_packet(packet.number)
                    sys.exit(-1)

                cif0 = getattr(layers, "cif0", None)

                if (cif0 == None):
                    print("\nERROR: Couldn't extract CIF0")
                    dump_packet(packet.number)
                    sys.exit(-1)

                if (cif0 == 0):
                    print("Success")
                else:
                    print(f"Failure, CIF0 = {cif0}")
                    dump_packet(packet.number)
                    sys.exit(-1)

            elif (option == "payload=empty"):
                print("Checking for empty payload...", end = "")

                if ("payload" in layers.field_names):
                    print("Failure, payload is present")
                    dump_packet(packet.number)
                    sys.exit(-1)

                else:
                    print("Success")

        test_num += 1

    if (test_num < len(tests)):
        print(f"ERROR: Only {test_num} of {len(tests)} were conducted")
        sys.exit(-1)

    return



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
    bpf_filter=f"src host {SRC_IP} and udp src port {SRC_PORT}",
    decode_as={
        f'udp.port=={SRC_PORT}': PROTO
    },
    # output_file=ALL_PACKETS,
)

tests = [
    ("ackV", ["payload=empty"]),
    ("ackX", ["payload=empty"]),
    ("ackS", [])
]

# Spawning a separate thread so that the listener is able to pick up any packets
# sent immediately in response to the first Control Packet that we send.
capture_thread = threading.Thread(
    target = process_packet,
    args=(capture, tests)
    )
# capture_thread = threading.Thread(
#     target = consume_packets,
#     args=(capture,)
#     )

print(f"Capture thread starting. Listing for packets from {SRC_IP}:{SRC_PORT}")
capture_thread.start()


sleep(5)

# Sending the Control Packet
udp_send.sendto(control_pkt.to_bytes(), (SRC_IP, SRC_PORT))

capture_thread.join()