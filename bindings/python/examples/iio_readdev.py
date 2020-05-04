"""
!/usr/bin/env python

Copyright (C) 2020 Analog Devices, Inc.
Author: Cristian Iacob <cristian.iacob@analog.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
"""

import sys
import argparse
import iio

PARSER = argparse.ArgumentParser(description='iio_readdev')
PARSER.add_argument('-n', '--network', type=str, metavar='',
                    help='Use the network backend with the provided hostname.')
PARSER.add_argument('-u', '--uri', type=str, metavar='',
                    help='Use the context with the provided URI.')
PARSER.add_argument('-b', '--buffer-size', type=int, metavar='',
                    help='Size of the capture buffer. Default is 256.')
PARSER.add_argument('-s', '--samples', type=int, metavar='',
                    help='Number of samples to capture, 0 = infinite. Default is 0.')
PARSER.add_argument('-T', '--timeout', type=int, metavar='',
                    help='Buffer timeout in milliseconds. 0 = no timeout')
PARSER.add_argument('-a', '--auto', action='store_true',
                    help='Scan for available contexts and if only one is available use it.')
PARSER.add_argument('device', type=str, nargs=1)
PARSER.add_argument('channel', type=str, nargs='*')


def read_arguments():
    """
    Method for reading the command line parameters and setting the corresponding variables.
    """
    arg_ip = ""
    arg_uri = ""
    scan_for_context = False
    buffer_size = 256
    num_samples = 0
    timeout = 0

    args = PARSER.parse_args()

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

    device_name = args.device[0]
    channels = args.channel

    return arg_ip, arg_uri, scan_for_context, buffer_size, num_samples, timeout, device_name, channels


def create_context(scan_for_context, arg_uri, arg_ip):
    """
    Method for creating the corresponding context.

    parameters:
        scan_for_context: type=bool
            Scan for available contexts and if only one is available use it.
        arg_uri: type=string
            The URI on which the program should look for a Context.
        arg_ip: type=string
            The IP on which the program should look for a Network Context.

    returns: type:iio.Context
        The resulted context.
    """
    ctx = None

    try:
        if scan_for_context:
            contexts = iio.scan_contexts()
            if len(contexts) == 0:
                sys.stderr.write("No IIO context found.\n")
                exit(1)
            elif len(contexts) == 1:
                uri, _ = contexts.popitem()
                ctx = iio.Context(_context=uri)
            else:
                print("Multiple contexts found. Please select one using --uri!")

                for uri, _ in contexts.items():
                    print(uri)
        elif arg_uri != "":
            ctx = iio.Context(_context=arg_uri)
        elif arg_ip != "":
            ctx = iio.NetworkContext(arg_ip)
        else:
            ctx = iio.Context()
    except FileNotFoundError:
        sys.stderr.write('Unable to create IIO context\n')
        exit(1)

    return ctx


def read_data(buffer, num_samples):
    """
    Method for reading data from the buffer.

    parameters:
        buffer: type=iio.Buffer
            Current buffer.
        num_samples: type=int
            Number of samples to capture, 0 = infinite. Default is 0.

    returns: type=None
        Reads data from buffer.
    """
    if buffer is None:
        sys.stderr.write('Unable to create buffer!\n')
        exit(1)

    while True:
        buffer.refill()
        samples = buffer.read()

        if num_samples > 0:
            sys.stdout.buffer.write(samples[:min(num_samples, len(samples))])
            num_samples -= min(num_samples, len(samples))

            if num_samples == 0:
                break
        else:
            sys.stdout.buffer.write(bytes(samples))


def main():
    """
    Module's main method.
    """
    (arg_ip, arg_uri, scan_for_context, buffer_size, num_samples,
     timeout, device_name, channels) = read_arguments()

    ctx = create_context(scan_for_context, arg_uri, arg_ip)

    if timeout >= 0:
        ctx.set_timeout(timeout)

    dev = ctx.find_device(device_name)

    if dev is None:
        sys.stderr.write('Device %s not found!\n' % device_name)
        sys.exit(1)

    if len(channels) == 0:
        for channel in dev.channels:
            channel.enabled = True
    else:
        for channel_idx in channels:
            dev.channels[int(channel_idx)].enabled = True

    buffer = iio.Buffer(dev, buffer_size)

    try:
        read_data(buffer, num_samples)
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == '__main__':
    main()
