# SPDX-License-Identifier: LGPL-2.1-or-later
#
# libiio - Library for interfacing industrial I/O (IIO) devices
#
# Copyright (C) 2026 Analog Devices, Inc.
# Author: Praveen Perera <praveen.perera@analog.com>

# Uses PyShark to evaluate whether the VITA 49.2 subsystem within IIOD appropriately responds 
# to Control Packets under different circumstances (AckV requested, AckV/AckS requested, failing controls, etc.).

import pyshark
from ctypes import pointer
from time import sleep
from enum import IntEnum
import subprocess 
import threading
import socket
import sys
import builtins
from functools import partial
import iio
from iio import *

SRC_IP = "192.168.2.1"  # Pluto's IP
SRC_PORT = 4991 # VITA 49.2 convention

PROTO = "v49d2" # The VITA 49.2 Wireshark dissector plugin by Geontech

ALL_PACKETS = "all_packets.pcapng"
OUTPUT_FILE = "problematic_packet.pcapng" # For storing the packet that failed the testcase

tprint = partial(builtins.print, "\t\t", sep="")

# ========================================================================================================
# TESTING FRAMEWORK
# ========================================================================================================

# Notice in the comments at the very top that there's a lot of possible tests that we can run for a single instance of a Control Packet.
# It's unsustainable to just manually write the tests to validate the types of incoming packets, and have a bunch of copy and paste code.

# Instead, I've opted to design a testing framework where you can pass the packets you're expecting (in the order that they should be received),
# along with special options to check.
    # There's a limited number of special options that are supported. You'll have to look at the process_packet() "for option in test[1]" loop
    # to see what options are supported. As tests get more complicated, we will expand that section.

# The process_packet() function takes as an argument an array of tuples. The tuple format is as follows:
    # <Packet Type, [Special Options List/Array]>

    # For example:
    # tests = [
    #     ("ackV", ["payload==empty"]),  -> Checks that an AckV packet is received first with an empty payload.
    #     ("ackX", ["payload==empty"]),  -> Checks that an AckX packet is received next with an empty payload.
    #     ("ackS", [])                  -> Checks that an AckS packet is received last. No special options to check.
    # ]
    
# ========================================================================================================
# SCENARIOS
# ========================================================================================================

# List of scenarios that we want to exercise:
# Host sends a Control Packet:
    # Action Mode = No-Action Mode:

        # Bad Commands:
            # No Acks Requested:
                # AckV not sent
                # AckX not sent
                # AckS not sent

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
            # No Acks Requested:
                # AckV not sent
                # AckX not sent
                # AckS not sent
                
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

        # NACK Enabled:
            
            # Good Commands:
                # AckV/X Requested
                    # AckV not sent
                    # AckX not sent

            # Bad Commands:
                # AckV/X Requested
                    # AckV with warnings
                    # AckX with warnings/errors

    # IQ Request
        # Signal Data Packet



class V49_2_PacketTypes (IntEnum):
    """
    Packet type mapped to their codes.
    """

    IF_DATA_NO_SID 		= 0
    IF_DATA_WITH_SID 	= 1
    EXT_DATA_NO_SID 	= 2
    EXT_DATA_WITH_SID 	= 3
    IF_CONTEXT 			= 4
    EXT_CONTEXT 		= 5
    COMMAND 			= 6
    EXT_COMMAND 		= 7


def get_header_field(layer):
    """The v49d2 dissector doesn't have a common "header" field when parsing packets.
    
    Instead they're called "context_header" or "command_header" and thus require some
    extra parsing to extract.
    """

    # exact 'header' first, then anything ending in '_header'
    candidates = ["header"] + [n for n in layer.field_names if n.endswith("_header")]
    for name in candidates:
        if name in layer.field_names:
            val = getattr(layer, name, None)
            if val not in (None, ""):
                return name, val
    return None, None

def dump_packet(packet):
    """Saves a problematic packet to a .pcapng file and prints it to the terminal as well."""

    subprocess.run([
            "tshark",
            "-r", ALL_PACKETS,
            "-Y", f"frame.number == {packet.number}",
            "-w", OUTPUT_FILE
        ], check=True)
    
    print()
    tprint(f"Dumped packet to {OUTPUT_FILE}\n")
    packet.pretty_print()
    
def process_packet(capture, tests):
    """Takes a set of captured packets and evaluates them against the provided tests."""

    packet_count = len(tests) + 1
    tprint(f"Acquiring {packet_count} packets...", end="", flush=True)

    # +1 in case a Context Packet is sent
    capture.sniff(packet_count=packet_count)
    print("Success")

    test_num = 0

    for packet in capture:

        if (test_num >= len(tests)):
            break

        test = tests[test_num]
        print()
        tprint(f"Running Test {test_num + 1}: {test}")

        # tprint(f"{packet}\n\n")

        # Extracting layers and VITA header information
        layers = packet[PROTO]
        header_name, header_val = get_header_field(layers)

        # Ignoring Context Packets since they're sent on an interval (unless this specific tests is looking for Context Packets)
        if ("context" in header_name and test[0] != "context"):
            tprint("Encountered Context Packet. Skipping Packet and repeating test.")
            continue

        # Validating if the received packet matches the expected
        if (header_val is not None):
            packet_type = getattr(layers, f"{header_name}_packet_type", None)

            if (packet_type is None):
                tprint("ERROR: Could not extract Packet Type from incoming message.")
                dump_packet(packet)
                return -1

            tprint("Checking if received packet type is expected...", end="")

            match test[0]:
                case "data":
                    if (packet_type != V49_2_PacketTypes.IF_DATA_WITH_SID and packet_type != V49_2_PacketTypes.IF_DATA_NO_SID):
                        tprint(f"Failure, received packet is not a Data Packet, rather (int) = {packet_type}")
                        dump_packet(packet)
                        return -1

                case "context":
                    if (int(packet_type) != V49_2_PacketTypes.IF_CONTEXT):
                        tprint(f"Failure, received packet is not a Context Type, rather (int) = {packet_type}")
                        dump_packet(packet)
                        return -1

                case "control":
                    tprint("ERROR: Invalid testing setup. The VITA 49.2 subsystem with iiod cannot send Control Packets to the host.")
                    return -1

                case "ackV":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        tprint(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet)
                        return -1

                    ackV_request = getattr(layers, "cam_request_validation", None)
                    if (ackV_request == None):
                        tprint(f"ERROR: Unable to find the 'cam_request_validation' field.")
                        dump_packet(packet)
                        return -1

                    if (ackV_request == False):
                        tprint(f"Failure, 'cam_request_validation' was not asserted. This is not an AckV Packet.")
                        dump_packet(packet)
                        return -1

                case "ackX":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        tprint(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet)
                        return -1

                    ackV_request = getattr(layers, "cam_request_execution", None)
                    if (ackV_request == None):
                        tprint(f"ERROR: Unable to find the 'cam_request_execution' field.")
                        dump_packet(packet)
                        return -1

                    if (ackV_request == False):
                        tprint(f"Failure, 'cam_request_execution' was not asserted. This is not an AckX Packet.")
                        dump_packet(packet)
                        return -1

                case "ackS":
                    if (int(packet_type) != V49_2_PacketTypes.COMMAND):
                        tprint(f"Failure, received packet is not a Command Type, rather (int) = {packet_type}")
                        dump_packet(packet)
                        return -1

                    ackV_request = getattr(layers, "cam_request_query", None)
                    if (ackV_request == None):
                        tprint(f"ERROR: Unable to find the 'cam_request_query' field.")
                        dump_packet(packet)
                        return -1

                    if (ackV_request == False):
                        tprint(f"Failure, 'cam_request_query' was not asserted. This is not an AckS Packet.")
                        dump_packet(packet)
                        return -1

                case "control_extension":
                    tprint("ERROR: Invalid testing setup. Control Extension is unsupported.")
                    return -1

                case _:
                    tprint(f"ERROR: Invalid testing setup. Unrecognized packet type: '{test[0]}'")
                    return -1

            print("Success")

        # Checking any special options
        for option in test[1]:

            if (option == "cif0=0"):
                tprint("Checking for CIF0==0...", end = "")

                if ("cif0" not in layers.field_names):
                    tprint("Failure, couldn't find CIF0")
                    dump_packet(packet)
                    return -1

                cif0 = getattr(layers, "cif0", None)

                if (cif0 == None):
                    print()
                    tprint("ERROR: Couldn't extract CIF0")
                    dump_packet(packet)
                    return -1

                if (cif0 == 0):
                    print("Success")
                else:
                    tprint(f"Failure, CIF0 = {cif0}")
                    dump_packet(packet)
                    return -1

            elif (option == "payload==empty"):
                tprint("Checking for empty payload...", end = "")

                if ("payload" in layers.field_names):
                    tprint("Failure, payload is present")
                    dump_packet(packet)
                    return -1

                else:
                    print("Success")

            elif (option == "payload!=empty"):
                tprint("Checking for payload...", end = "")

                if ("payload" not in layers.field_names):
                    tprint("Failure, payload is not present")
                    dump_packet(packet)
                    return -1

                else:
                    print("Success")

            else:
                tprint(f"ERROR: Invalid testing setup. '{option}' is an unrecognized special option.")
                return -1

        test_num += 1

    if (test_num < len(tests)):
        print()
        tprint(f"ERROR: {test_num} of {len(tests)} tests were conducted")
        return -1

    print("\n")
    capture.clear()
    return 0

def send_packet(socket, packet, delay=2):
    """The call to sniff() in PyShark is blocking, so I wanted to put sending the Control Packet in a separate thread, hence why I have a separate function for it."""

    # I've found that setting up the packet sniffer can take a long time
    sleep(delay)

    # print("Sending Control Packet.")
    socket.sendto(packet.to_bytes(), (SRC_IP, SRC_PORT))


def initiate_test(socket, capture, tests, control_pkt):
    """Creates a thread to send the Control Packet and calls the processing function to compare the received packets against the provided tests."""

    # Spawning a separate thread so that the listener is able to pick up any packets
    # sent immediately in response to the first Control Packet that we send.
    send_thread = threading.Thread(
        target = send_packet,
        args=(socket, control_pkt)
    )
    send_thread.start()

    if (process_packet(capture, tests) < 0):
        send_thread.join()
        sys.exit(-1)
    else:
        send_thread.join()


def main():

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
    control_pkt.struct.command_prologue.control_cam = pointer(control_cam)

    control_pkt.struct.cif0.cif0_word.has_sample_rate = True
    # control_pkt.struct.cif0.sample_rate = 2083335

    # UDP Socket Creation
    udp_send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Live Capture mode with BPF Filter (Berkeley Packet Filter)
    capture = pyshark.LiveCapture(
        interface='enp0s9', 
        bpf_filter=f"src host {SRC_IP} and udp src port {SRC_PORT}",
        decode_as={
            f'udp.port=={SRC_PORT}': PROTO,
        },
        output_file=ALL_PACKETS
    )

    print(f"Listening for packets from {SRC_IP}:{SRC_PORT}")


    # ========================================================================================================
    # TESTS
    # ========================================================================================================
    print("\nRunning Tests")

    if ("no-exec" in sys.argv or len(sys.argv) == 1):
        # ==================================================================
        # NO-EXECUTION TESTS
        # ==================================================================
        print("Execution Disabled")
        control_cam.action_bits = 0

        # =============================================
        # BAD COMMAND
        # =============================================
        print("\n\tBad Command\n")
        control_pkt.struct.cif0.sample_rate = 2083332 # Bad value

        # ================================
        # No ACK REQUESTED
        # ================================
        tprint("No ACK Requested")
        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV REQUESTED
        # ================================
        tprint("AckV Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload!=empty"]),

            # Since the VITA 49.2 backend is a synchronous single-threaded subsystem, we're not generating/sending
            # different VITA packets concurrently. Therefore if even a single Context Packet is received after an AckV,
            # that means the system never generated an AckX/S packet.
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X/S REQUESTED
        # ================================
        tprint("AckV/X/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX REQUESTED
        # ================================
        tprint("AckX Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckS REQUESTED
        # ================================
        tprint("AckS Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX/S REQUESTED
        # ================================
        tprint("AckX/S Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/S REQUESTED
        # ================================
        tprint("AckV/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)



        # =============================================
        # GOOD COMMAND
        # =============================================
        print("\tGood Command\n")
        control_pkt.struct.cif0.sample_rate = 2083335 # Good value

        # ================================
        # No ACK REQUESTED
        # ================================
        tprint("No ACK Requested")
        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV REQUESTED
        # ================================
        tprint("AckV Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload==empty"]),

            # Since the VITA 49.2 backend is a synchronous single-threaded subsystem, we're not generating/sending
            # different VITA packets concurrently. Therefore if even a single Context Packet is received after an AckV,
            # that means the system never generated an AckX/S packet.
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload==empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X/S REQUESTED
        # ================================
        tprint("AckV/X/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload==empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX REQUESTED
        # ================================
        tprint("AckX Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckS REQUESTED
        # ================================
        tprint("AckS Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX/S REQUESTED
        # ================================
        tprint("AckX/S Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/S REQUESTED
        # ================================
        tprint("AckV/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload==empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)



    if ("exec" in sys.argv or len(sys.argv) == 1):
        # ==================================================================
        # EXECUTION ENABLED TESTS
        # ==================================================================
        print("Execution Enabled")
        control_cam.action_bits = 2

        # =============================================
        # BAD COMMAND
        # =============================================
        print("\n\tBad Command\n")
        control_pkt.struct.cif0.sample_rate = 2083332 # Bad value

        # ================================
        # No ACK REQUESTED
        # ================================
        tprint("No ACK Requested")
        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV REQUESTED
        # ================================
        tprint("AckV Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload!=empty"]),

            # Since the VITA 49.2 backend is a synchronous single-threaded subsystem, we're not generating/sending
            # different VITA packets concurrently. Therefore if even a single Context Packet is received after an AckV,
            # that means the system never generated an AckX/S packet.
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)


        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackX", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X/S REQUESTED
        # ================================
        tprint("AckV/X/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackX", ["payload!=empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX REQUESTED
        # ================================
        tprint("AckX Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackX", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckS REQUESTED
        # ================================
        tprint("AckS Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX/S REQUESTED
        # ================================
        tprint("AckX/S Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackX", ["payload!=empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/S REQUESTED
        # ================================
        tprint("AckV/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)



        # =============================================
        # GOOD COMMAND
        # =============================================
        print("\tGood Command\n")
        control_pkt.struct.cif0.sample_rate = 2083335 # Good value

        # ================================
        # No ACK REQUESTED
        # ================================
        tprint("No ACK Requested")
        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV REQUESTED
        # ================================
        tprint("AckV Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload==empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload==empty"]),
            ("ackX", ["payload==empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/X/S REQUESTED
        # ================================
        tprint("AckV/X/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload==empty"]),
            ("ackX", ["payload==empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX REQUESTED
        # ================================
        tprint("AckX Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackX", ["payload==empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckS REQUESTED
        # ================================
        tprint("AckS Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackS", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckX/S REQUESTED
        # ================================
        tprint("AckX/S Requested")

        control_cam.request_ack_v = False
        control_cam.request_ack_x = True
        control_cam.request_ack_s = True

        tests = [
            ("ackX", ["payload==empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # ================================
        # AckV/S REQUESTED
        # ================================
        tprint("AckV/S Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = False
        control_cam.request_ack_s = True

        tests = [
            ("ackV", ["payload==empty"]),
            ("ackS", ["payload!=empty"]),
        ]

        initiate_test(udp_send, capture, tests, control_pkt)


    if ("nack" in sys.argv or len(sys.argv) == 1):
        # In these tests we'll be checking if the VITA system handles the NACK bit properly (See Table 8.3.1-1 in the VITA 49.2 2017 documentation).
        # When NACK is enabled, AckV/X packets should only be sent if warnings/errors are generated. When disabled, AckV/AckX packets can be sent
        # whenever they're requested, even if they have an empty payload.

        # =============================================
        # Not-acK Only (NACK)
        # =============================================
        print("NACK Enabled")
        control_cam.nack = 1

        # =============================================
        # GOOD COMMAND
        # =============================================
        print("\tGood Command\n")
        control_pkt.struct.cif0.sample_rate = 2083335

        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            # Since NACK is enabled, no AckV/X packets should be sent i fthere's no warnings or errors, which
            # should be the case since we're issuing a packet with a good command.
            ("context", []),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)

        # =============================================
        # BAD COMMAND
        # =============================================
        print("\tBad Command\n")
        control_pkt.struct.cif0.sample_rate = 2083332

        # ================================
        # AckV/X REQUESTED
        # ================================
        tprint("AckV/X Requested")

        control_cam.request_ack_v = True
        control_cam.request_ack_x = True
        control_cam.request_ack_s = False

        tests = [
            ("ackV", ["payload!=empty"]),
            ("ackX", ["payload!=empty"]),
            ("context", [])
        ]

        initiate_test(udp_send, capture, tests, control_pkt)


    # Data Packet tests don't work at the moment because Signal Data Packets are around ~65kB which exceeds MTU
    # for most interfaces, causing the packet to get fragmented. PyShark isn't able to restitch them, or at least I haven't figured it out.

    # if ("data" in sys.argv or len(sys.argv) == 1):

    #     # Testing if the VITA system responds with a Signal Data Packet.
    #     # =============================================
    #     # SIGNAL DATA PACKET REQUESTED
    #     # =============================================

    #     control_pkt.struct.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_Packet_Class_Codes.REFILL_TIME_REQUEST

    #     tprint("Signal Data Packet Requested")

    #     tests = [
    #         ("data", ["payload!=empty"]),
    #         ("context", [])
    #     ]

    #     initiate_test(udp_send, capture, tests, control_pkt)

if __name__ == "__main__":
    main()