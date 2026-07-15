#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
Listen for VITA 49.2 IF Data packets on TCP, decode payload samples using the
existing iio Python bindings, check packet_count continuity, and plot I/Q time-domain live.
"""

import argparse
import ctypes
import signal
import socket
from collections import defaultdict, deque

import iio
import matplotlib.pyplot as plt


def _decode_iq_words(words):
    """
    Convert 32-bit words into signed 16-bit I/Q.

    VITA 49.2 union in this tree maps, on little-endian hosts:
    - low 16 bits: Q
    - high 16 bits: I
    """
    i_samples = []
    q_samples = []

    for word in words:
        q = ctypes.c_int16(word & 0xFFFF).value
        i = ctypes.c_int16((word >> 16) & 0xFFFF).value
        i_samples.append(i)
        q_samples.append(q)

    return i_samples, q_samples


def _parse_data_packet(raw_bytes):
    """
    Parse a single VITA 49.2 IF Data packet from bytes using existing iio bindings.

    Returns:
        dict with stream_id, packet_count, payload_words
    Raises:
        ValueError if packet is not IF Data or parse fails.
    """
    if len(raw_bytes) < 4 or (len(raw_bytes) % 4) != 0:
        raise ValueError("Datagram length must be a non-zero multiple of 4 bytes")

    # Identify packet type from network-order header before parsing.
    header_word = int.from_bytes(raw_bytes[:4], byteorder="big", signed=False)
    packet_type = (header_word >> 28) & 0xF

    if packet_type == int(iio.VITA49_2_Packet_Types.IF_DATA_WITH_SID):
        with_stream_id = True
    elif packet_type == int(iio.VITA49_2_Packet_Types.IF_DATA_NO_SID):
        with_stream_id = False
    else:
        raise ValueError(f"Unsupported VITA packet type for this listener: {packet_type}")

    packet_size_words = header_word & 0xFFFF
    packet_count = (header_word >> 16) & 0xF
    has_class_id = ((header_word >> 27) & 0x1) == 1
    has_trailer = ((header_word >> 26) & 0x1) == 1
    tsi = (header_word >> 22) & 0x3
    tsf = (header_word >> 20) & 0x3

    words = [
        int.from_bytes(raw_bytes[idx : idx + 4], byteorder="big", signed=False)
        for idx in range(0, len(raw_bytes), 4)
    ]

    if packet_size_words == 0 or packet_size_words > len(words):
        raise ValueError(
            f"Invalid packet_size_words ({packet_size_words}), datagram has {len(words)} words"
        )

    offset = 1  # header
    stream_id = 0

    if with_stream_id:
        if packet_size_words < (offset + 1):
            raise ValueError("Packet truncated before stream_id")
        stream_id = words[offset]
        offset += 1

    packet_class_code = None
    information_class_code = None

    if has_class_id:
        if packet_size_words < (offset + 2):
            raise ValueError("Packet truncated before class_id")
        class_id_upper_word = words[offset + 1]
        packet_class_code = class_id_upper_word & 0xFFFF
        information_class_code = (class_id_upper_word >> 16) & 0xFFFF
        offset += 2

    # TSI: 0 means not present, non-zero means one 32-bit integer timestamp word.
    if tsi != 0:
        if packet_size_words < (offset + 1):
            raise ValueError("Packet truncated before integer timestamp")
        offset += 1

    # TSF: 0 means not present, non-zero means one 64-bit fractional timestamp (2 words).
    if tsf != 0:
        if packet_size_words < (offset + 2):
            raise ValueError("Packet truncated before fractional timestamp")
        offset += 2

    payload_end = packet_size_words - (1 if has_trailer else 0)
    if payload_end < offset:
        raise ValueError("Invalid packet: payload_end precedes payload start")

    payload_words = words[offset:payload_end]

    return {
        "packet_type": packet_type,
        "stream_id": stream_id,
        "packet_count": packet_count,
        "packet_class_code": packet_class_code,
        "information_class_code": information_class_code,
        "payload_words": payload_words,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Listen on TCP 4991 for VITA 49.2 data packets and live-plot IQ"
    )
    parser.add_argument("--bind", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=4991, help="TCP port (default: 4991)")
    parser.add_argument("--recv-chunk", type=int, default=65536, help="TCP recv chunk bytes")
    parser.add_argument("--history", type=int, default=8000, help="Samples to keep in plot history")
    parser.add_argument("--plot-every", type=int, default=5, help="Update plot every N packets")
    args = parser.parse_args()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.bind, args.port))
    server.listen(1)
    server.settimeout(1.0)

    stop = {"value": False}

    def _handle_sigint(_sig, _frame):
        stop["value"] = True

    signal.signal(signal.SIGINT, _handle_sigint)

    i_hist = deque(maxlen=args.history)
    q_hist = deque(maxlen=args.history)

    last_count_by_stream = defaultdict(lambda: None)
    received_packets = 0
    continuity_errors = 0

    plt.ion()
    fig, ax_time = plt.subplots(1, 1, figsize=(10, 6))

    line_i, = ax_time.plot([], [], label="I", linewidth=1.0)
    line_q, = ax_time.plot([], [], label="Q", linewidth=1.0, alpha=0.85)
    ax_time.set_title("I/Q Time Domain")
    ax_time.set_xlabel("Sample Index (rolling)")
    ax_time.set_ylabel("Amplitude")
    ax_time.grid(True, alpha=0.3)
    ax_time.legend(loc="upper right")

    backend = plt.get_backend().lower()
    if "agg" in backend:
        print(
            f"[plot] Current backend '{plt.get_backend()}' is non-interactive; "
            "no live window will be displayed."
        )

    # Explicitly show the figure so interactive backends open a window.
    plt.show(block=False)
    fig.canvas.draw_idle()
    fig.canvas.flush_events()

    print(f"Listening on {args.bind}:{args.port} (TCP) for VITA 49.2 IF Data packets...")

    conn = None
    src = None
    rx_buf = bytearray()

    def _extract_packet_from_stream(buffer):
        # Need at least one VITA header word to determine packet size.
        if len(buffer) < 4:
            return None

        header_word = int.from_bytes(buffer[0:4], byteorder="big", signed=False)
        packet_size_words = header_word & 0xFFFF
        packet_size_bytes = packet_size_words * 4

        # Drop invalid framing if size is zero to re-sync the stream.
        if packet_size_words == 0:
            del buffer[0:4]
            return b""

        if len(buffer) < packet_size_bytes:
            return None

        pkt = bytes(buffer[:packet_size_bytes])
        del buffer[:packet_size_bytes]
        return pkt

    try:
        while not stop["value"]:
            if conn is None:
                try:
                    conn, src = server.accept()
                    conn.settimeout(1.0)
                    rx_buf.clear()
                    print(f"Accepted TCP connection from {src[0]}:{src[1]}")
                except socket.timeout:
                    plt.pause(0.001)
                    continue

            try:
                chunk = conn.recv(args.recv_chunk)
            except socket.timeout:
                plt.pause(0.001)
                continue
            except OSError as ex:
                print(f"[conn] recv error: {ex}")
                conn.close()
                conn = None
                src = None
                rx_buf.clear()
                continue

            if not chunk:
                print("[conn] peer closed connection")
                conn.close()
                conn = None
                src = None
                rx_buf.clear()
                continue

            rx_buf.extend(chunk)

            while True:
                raw_data = _extract_packet_from_stream(rx_buf)
                if raw_data is None:
                    break
                if raw_data == b"":
                    print("[skip] invalid packet_size_words=0 while resyncing stream")
                    continue

                try:
                    parsed = _parse_data_packet(raw_data)
                except ValueError as ex:
                    print(f"[skip] {src}: {ex}")
                    continue

                stream_id = parsed["stream_id"]
                packet_count = parsed["packet_count"]
                packet_type = parsed["packet_type"]
                packet_class_code = parsed["packet_class_code"]
                information_class_code = parsed["information_class_code"]
                payload_words = parsed["payload_words"]

                # packet_count is 4 bits, so we track per logical flow.
                flow_key = (
                    src[0] if src else "",
                    src[1] if src else 0,
                    packet_type,
                    stream_id,
                    packet_class_code,
                    information_class_code,
                )

                last_count = last_count_by_stream[flow_key]
                if last_count is not None:
                    expected = (last_count + 1) & 0xF
                    if packet_count != expected:
                        continuity_errors += 1
                        print(
                            "[gap] "
                            f"src={src[0]}:{src[1]} "
                            f"stream=0x{stream_id:08X} "
                            f"pkt_type={packet_type} "
                            f"class={packet_class_code if packet_class_code is not None else -1} "
                            f"info={information_class_code if information_class_code is not None else -1} "
                            f"expected={expected} got={packet_count} "
                            f"total_gaps={continuity_errors}"
                        )

                last_count_by_stream[flow_key] = packet_count

                i_samples, q_samples = _decode_iq_words(payload_words)
                i_hist.extend(i_samples)
                q_hist.extend(q_samples)
                received_packets += 1

                if (received_packets % args.plot_every) == 0:
                    x = list(range(len(i_hist)))
                    i_vals = list(i_hist)
                    q_vals = list(q_hist)

                    line_i.set_data(x, i_vals)
                    line_q.set_data(x, q_vals)
                    ax_time.set_xlim(0, max(1, len(x)))
                    ax_time.relim()
                    ax_time.autoscale_view(scalex=False, scaley=True)

                    fig.canvas.draw_idle()
                    fig.canvas.flush_events()
                    plt.pause(0.001)

                if (received_packets % 100) == 0:
                    print(
                        f"packets={received_packets} src={src[0]}:{src[1]} "
                        f"stream=0x{stream_id:08X} pkt_type={packet_type} "
                        f"class={packet_class_code if packet_class_code is not None else -1} "
                        f"info={information_class_code if information_class_code is not None else -1} "
                        f"last_count={packet_count} payload_words={len(payload_words)} gaps={continuity_errors}"
                    )

    finally:
        if conn is not None:
            conn.close()
        server.close()
        plt.ioff()
        plt.close(fig)


if __name__ == "__main__":
    main()
