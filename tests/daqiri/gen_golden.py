#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Analog Devices, Inc.
"""Generate the golden vectors used by the DAQIRI backend tests.

Usage: gen_golden.py <outdir>

Writes the *.bin vectors, manifest.json and expected.h (the C tables the
tests compile in) into <outdir>. Output is deterministic. Run clang-format
on expected.h after regenerating tests/resources/daqiri.

Payload pattern "ramp16_le": sample n has I = n & 0x7FFF and
Q = (0x8000 - n) & 0xFFFF, each packed as a little-endian 16-bit word, I
first. UDP sample mode carries the device's own sample layout and the host
never swaps it; the ADRV9002 and the HDL ramp16 test pattern are
little-endian int16, which is what iio_daqiri_bench --check ramp reads. Every
UDP-mode vector, and every sample file made from them, uses it.

The udp-*.bin vectors are UDP sample mode datagrams (iiod-udp-stream.h): a
16-byte big-endian header (version, flags, payload length, stream ID, 64-bit
sequence number), then the payload. Their manifest entries have "kind":
"udp", the iiod_udp_stream_hdr_unpack() result and what the receive side
makes of them for a stream programmed with UDP_STREAM_ID and
UDP_PAYLOAD_BYTES ("data", "malformed" or "foreign"); expected.h carries them
in udp_golden[] and a test defining DQ_UDP_GOLDEN sees only that table
(struct dq_udp_golden).

The l2-*.bin vectors are Ethernet frames for the raw-queue parser
(daqiri-l2.c): Ethernet II, optional 802.1Q, IPv4 (checksummed), UDP
(checksummed), then a UDP sample mode datagram. Their manifest entries have
"kind": "l2" and the expected dq_l2_parse() status and info fields;
expected.h carries them in l2_golden[]; a test defining DQ_L2_GOLDEN sees
only that table (struct dq_l2_golden).
"""
import json
import os
import struct
import sys

L2_ETH_DST = bytes([0x02, 0, 0, 0, 0, 0x02])
L2_ETH_SRC = bytes([0x02, 0, 0, 0, 0, 0x01])
L2_IP_SRC = 0x0A000001  # 10.0.0.1
L2_IP_DST = 0x0A000002  # 10.0.0.2
L2_UDP_SRC = 49152
L2_UDP_DST = 25101
L2_VLAN_ID = 100

UDP_STREAM_ID = 0xC0FFEE01
UDP_FOREIGN_ID = 0xC0FFEE02
UDP_SAMPLES = 360  # 1440 payload bytes, 90 beats of 16 bytes
UDP_PAYLOAD_BYTES = UDP_SAMPLES * 4
UDP_SEQUENCE = 0x0000000100000002  # both 32-bit halves in use
UDP_HDR_LEN = 16


def ramp16_le(nsamples):
    out = bytearray()
    for n in range(nsamples):
        out += struct.pack('<HH', n & 0x7FFF, (0x8000 - n) & 0xFFFF)
    return bytes(out)


def build_udp(payload, version=1, flags=0, payload_length=None,
              stream_id=UDP_STREAM_ID, sequence=UDP_SEQUENCE):
    """Return one UDP sample mode datagram: the header, then the payload."""
    plen = len(payload) if payload_length is None else payload_length
    return struct.pack('>BBHIQ', version, flags, plen, stream_id,
                       sequence) + payload


def inet_csum(data):
    """RFC 1071 ones-complement sum of 16-bit big-endian words."""
    if len(data) % 2:
        data += b'\x00'
    total = sum(struct.unpack('>%dH' % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF


def build_l2(payload, *, vlan=None, qinq=False, proto=17, ihl=5, frag=0,
             ip_len=None, udp_len=None, truncate=None, ethertype=None):
    """Return (bytes, dict) for one Ethernet/[802.1Q]/IPv4/UDP frame.

    vlan: VLAN ID of a single 802.1Q tag. qinq: outer 802.1ad tag (ID 200)
    plus inner 802.1Q tag (vlan, default L2_VLAN_ID). frag: the IPv4
    flags/fragment-offset field. ip_len / udp_len override the lengths
    written into the headers; truncate cuts the frame to that many bytes
    after the checksums are computed; ethertype overrides the final
    EtherType (the IPv4 header is still built).
    """
    udp_total = 8 + len(payload) if udp_len is None else udp_len
    real_udp = 8 + len(payload)
    udp = struct.pack('>HHHH', L2_UDP_SRC, L2_UDP_DST, udp_total, 0)
    udp += payload
    pseudo = struct.pack('>IIBBH', L2_IP_SRC, L2_IP_DST, 0, proto,
                         udp_total)
    csum = inet_csum(pseudo + udp) or 0xFFFF
    udp = udp[:6] + struct.pack('>H', csum) + udp[8:]
    ip_total = ihl * 4 + real_udp if ip_len is None else ip_len
    ip = struct.pack('>BBHHHBBHII', (4 << 4) | ihl, 0, ip_total, 0x1234,
                     frag, 64, proto, 0, L2_IP_SRC, L2_IP_DST)
    ip += bytes(4 * (ihl - 5))  # zero options
    ip = ip[:10] + struct.pack('>H', inet_csum(ip)) + ip[12:]
    eth = L2_ETH_DST + L2_ETH_SRC
    if qinq:
        eth += struct.pack('>HHHH', 0x88A8, 200, 0x8100,
                           L2_VLAN_ID if vlan is None else vlan)
    elif vlan is not None:
        eth += struct.pack('>HH', 0x8100, vlan)
    eth += struct.pack('>H', 0x0800 if ethertype is None else ethertype)
    data = eth + ip + udp
    if truncate is not None:
        data = data[:truncate]
    off = len(eth) + ihl * 4 + 8
    entry = {
        'payload_off': len(eth) + 20 + 8 if ihl == 5 else off,
        'payload_len': len(payload),
        'udp_src': L2_UDP_SRC, 'udp_dst': L2_UDP_DST,
        'vlan_id': 0 if vlan is None or qinq else vlan,
        'ip_src': L2_IP_SRC, 'ip_dst': L2_IP_DST,
    }
    return data, entry


L2_FIELDS = ['payload_off', 'payload_len', 'udp_src', 'udp_dst', 'vlan_id',
             'ip_src', 'ip_dst']


def l2_vectors():
    """Return [(name, data, manifest entry)] for the l2-*.bin vectors."""
    out = []
    dgram = build_udp(ramp16_le(16))  # 16 + 64 bytes
    assert len(dgram) == 80

    def add(name, status, data, entry):
        entry = dict(entry)
        if status != 'DQ_L2_OK':
            entry.update({k: 0 for k in L2_FIELDS})
        out.append((name, data, dict(kind='l2', status=status, **entry)))

    d, e = build_l2(dgram)
    add('l2-plain.bin', 'DQ_L2_OK', d, e)
    d, e = build_l2(dgram, vlan=L2_VLAN_ID)
    add('l2-vlan.bin', 'DQ_L2_OK', d, e)
    d, e = build_l2(dgram, qinq=True)
    add('l2-qinq.bin', 'DQ_L2_QINQ', d, e)
    d, e = build_l2(dgram, ethertype=0x86DD)
    add('l2-ipv6.bin', 'DQ_L2_NOT_IPV4', d, e)
    d, e = build_l2(dgram, ihl=6)
    add('l2-ihl6.bin', 'DQ_L2_BAD_IHL', d, e)
    d, e = build_l2(dgram, proto=6)
    add('l2-tcp.bin', 'DQ_L2_NOT_UDP', d, e)
    d, e = build_l2(dgram, frag=0x2000)  # More Fragments
    add('l2-frag.bin', 'DQ_L2_FRAGMENT', d, e)
    d, e = build_l2(dgram, ip_len=2000, truncate=120)
    assert len(d) == 120
    add('l2-trunc-ip.bin', 'DQ_L2_BAD_IP_LEN', d, e)
    d, e = build_l2(dgram, udp_len=2000)
    add('l2-trunc-udp.bin', 'DQ_L2_BAD_UDP_LEN', d, e)
    d, e = build_l2(dgram, truncate=41)
    assert len(d) == 41
    add('l2-short.bin', 'DQ_L2_TRUNCATED', d, e)
    return out


def udp_vectors():
    """Return [(name, data, manifest entry)] for the udp-*.bin vectors."""
    out = []
    payload = ramp16_le(UDP_SAMPLES)

    def add(name, data, unpack, expect, version=1, flags=0,
            payload_length=UDP_PAYLOAD_BYTES, stream_id=UDP_STREAM_ID):
        out.append((name, data, dict(
            kind='udp', version=version, flags=flags,
            payload_length=payload_length, stream_id=stream_id,
            sequence=UDP_SEQUENCE, length=len(data), unpack=unpack,
            expect=expect, payload_pattern='ramp16_le')))

    valid = build_udp(payload)
    assert len(valid) == UDP_HDR_LEN + UDP_PAYLOAD_BYTES
    add('udp-valid.bin', valid, '0', 'data')
    # The reserved flags byte is not checked
    add('udp-flags.bin', build_udp(payload, flags=0x80), '0', 'data',
        flags=0x80)
    add('udp-bad-version.bin', build_udp(payload, version=2), '-EPROTO',
        'malformed', version=2)
    add('udp-short-header.bin', valid[:UDP_HDR_LEN - 1], '-EBADMSG',
        'malformed')
    # The header announces the stream's payload size, 16 bytes are missing
    add('udp-length-mismatch.bin',
        build_udp(payload[:-16], payload_length=UDP_PAYLOAD_BYTES), '0',
        'malformed')
    # Consistent in itself, but not the stream's payload size
    add('udp-payload-length.bin', build_udp(payload[:-16]), '0', 'malformed',
        payload_length=UDP_PAYLOAD_BYTES - 16)
    add('udp-foreign.bin', build_udp(payload, stream_id=UDP_FOREIGN_ID), '0',
        'foreign', stream_id=UDP_FOREIGN_ID)
    return out


UDP_FIELDS = ['unpack', 'version', 'flags', 'payload_length', 'stream_id',
              'sequence', 'length', 'expect']


def c_val(v):
    if isinstance(v, str):
        return v
    return '0x%XULL' % v if v > 0xFFFFFFFF else '%d' % v


def udp_c_val(key, v):
    if key == 'stream_id':
        return '0x%08XU' % v
    if key == 'sequence':
        return '0x%016XULL' % v
    if key == 'expect':
        return 'DQ_UDP_EXPECT_%s' % v.upper()
    return c_val(v)


def main(argv):
    args = argv[1:]
    if len(args) != 1:
        sys.stderr.write(__doc__)
        return 2
    outdir = args[0]
    os.makedirs(outdir, exist_ok=True)
    files = []
    l2rows = []
    for name, data, entry in l2_vectors():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)
        files.append(dict(name=name, **entry))
        l2rows.append('\t{"%s", %s},' % (name, ', '.join(
            ['%s' % entry['status']] + [
                '0x%08XU' % entry[k] if k.startswith('ip_') else
                c_val(entry[k]) for k in L2_FIELDS])))
    udprows = []
    for name, data, entry in udp_vectors():
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)
        files.append(dict(name=name, **entry))
        udprows.append('\t{"%s", %s},' % (name, ', '.join(
            udp_c_val(k, entry[k]) for k in UDP_FIELDS)))
    with open(os.path.join(outdir, 'manifest.json'), 'w') as f:
        json.dump({'udp_stream': {'stream_id': UDP_STREAM_ID,
                                  'payload_bytes': UDP_PAYLOAD_BYTES},
                   'files': files}, f, indent=2)
        f.write('\n')
    with open(os.path.join(outdir, 'expected.h'), 'w') as f:
        f.write('/* SPDX-License-Identifier: MIT */\n')
        f.write('/* Copyright (C) 2026 Analog Devices, Inc. */\n')
        f.write('/* Generated by tests/daqiri/gen_golden.py; '
                'do not edit. */\n')
        f.write('#if defined(DQ_L2_GOLDEN)\n')
        f.write('static const struct dq_l2_golden l2_golden[] = {\n')
        f.write('\n'.join(l2rows) + '\n};\n')
        f.write('#elif defined(DQ_UDP_GOLDEN)\n')
        f.write('/* The stream the expectations of udp_golden[] are for */\n')
        f.write('#define DQ_UDP_GOLDEN_STREAM_ID 0x%08XU\n' % UDP_STREAM_ID)
        f.write('#define DQ_UDP_GOLDEN_PAYLOAD_BYTES %d\n'
                % UDP_PAYLOAD_BYTES)
        f.write('static const struct dq_udp_golden udp_golden[] = {\n')
        f.write('\n'.join(udprows) + '\n};\n')
        f.write('#endif\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
