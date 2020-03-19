#!/usr/bin/env python
#
# Copyright (C) 2020 Analog Devices, Inc.
# Author: Cristian Iacob <cristian.iacob@analog.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
import time

import iio
import sys
import signal
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('-n', '--network', type=str, metavar='',
                    help='Use the network backend with the provided hostname.')
parser.add_argument('-u', '--uri', type=str, metavar='',
                    help='Use the context with the provided URI.')
parser.add_argument('-b', '--buffer-size', type=int, metavar='',
                    help='Size of the capture buffer. Default is 256.')
parser.add_argument('-s', '--samples', type=int, metavar='',
                    help='Number of samples to capture, 0 = infinite. Default is 0.')
parser.add_argument('-T', '--timeout', type=int, metavar='',
                    help='Buffer timeout in milliseconds. 0 = no timeout')
parser.add_argument('-a', '--auto', action='store_true',
                    help='Scan for available contexts and if only one is available use it.')
parser.add_argument('-c', '--cyclic', action='store_true',
                    help='Use cyclic buffer mode.')
parser.add_argument('device', type=str, nargs=1)
parser.add_argument('channel', type=str, nargs='*')

arg_ip = ''
arg_uri = ''
scan_for_context = False
buffer_size = 256
num_samples = 0
timeout = 0
cyclic = False
device_name = None
channels = None


def read_arguments():
    global arg_ip, arg_uri, scan_for_context, buffer_size, num_samples, timeout, cyclic, device_name, channels

    args = parser.parse_args()

    if args.network is not None:
        arg_ip = str(args.network)

    if args.uri is not None:
        arg_uri = str(args.uri)

    if args.auto is True:
        scan_for_context = True

    if args.buffer_size is not None:
        buffer_size = int(args.buffer_size)

    if args.samples is not None:
        num_samples = int(args.samples)

    if args.timeout is not None:
        timeout = int(args.timeout)

    if args.cyclic is True:
        cyclic = True

    device_name = args.device[0]
    channels = args.channel


def create_context(scan_for_context, arg_uri, arg_ip):
    ctx = None

    try:
        if scan_for_context:
            contexts = iio.scan_contexts()
            if len(contexts) == 0:
                sys.stderr.write('No IIO context found.')
                exit(1)
            elif len(contexts) == 1:
                uri, _ = contexts.popitem()
                ctx = iio.Context(_context=uri)
            else:
                print('Multiple contexts found. Please select one using --uri!')

                for uri, _ in contexts:
                    print(uri)
        elif arg_uri != '':
            ctx = iio.Context(_context=arg_uri)
        elif arg_ip != '':
            ctx = iio.NetworkContext(arg_ip)
        else:
            ctx = iio.Context()
    except FileNotFoundError:
        sys.stderr.write('Unable to create IIO context')
        exit(1)

    return ctx


def keyboard_interrupt_handler(signal, frame):
    sys.exit(0)


signal.signal(signal.SIGINT, keyboard_interrupt_handler)


def write_data(dev, buffer, num_samples, buffer_size, cyclic):
    app_running = True
    num_samples_set = num_samples > 0

    while app_running:
        bytes_to_read = buffer._length if not num_samples_set else min(buffer._length, num_samples * dev.sample_size)
        write_len = bytes_to_read
        data = []

        while bytes_to_read > 0:
            read_data = sys.stdin.buffer.read(bytes_to_read)
            bytes_to_read -= len(read_data)
            data.extend(read_data)

            if len(read_data) == 0:
                exit(0)

        if num_samples_set:
            num_samples -= int(write_len / dev.sample_size)

            if num_samples == 0 and write_len < buffer_size * dev.sample_size:
                exit(0)

        ret = buffer.write(bytearray(data))
        buffer.push()

        if ret == 0:
            if app_running:
                sys.stderr.write('Unable to push buffer!')
                exit(1)
            break

        while app_running and cyclic:
            time.sleep(1)


def main():
    read_arguments()

    ctx = create_context(scan_for_context, arg_uri, arg_ip)

    if timeout >= 0:
        ctx.set_timeout(timeout)

    dev = ctx.find_device(device_name)

    if dev is None:
        sys.stderr.write('Device %s not found!' % device_name)
        exit(1)

    if len(channels) == 0:
        for channel in dev.channels:
            channel.enabled = True
    else:
        for channel_idx in channels:
            dev.channels[int(channel_idx)].enabled = True

    buffer = iio.Buffer(dev, buffer_size, cyclic=cyclic)

    if buffer is None:
        sys.stderr.write('Unable to create buffer!')
        exit(1)

    write_data(dev, buffer, num_samples, buffer_size, cyclic)


if __name__ == '__main__':
    main()
