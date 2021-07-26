#!/usr/bin/env python
"""
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


class Arguments:
    """Class for parsing the input arguments."""

    def __init__(self):
        """Arguments class constructor."""
        self.parser = argparse.ArgumentParser(description="iio_readdev")
        self._add_parser_arguments()
        args = self.parser.parse_args()

        self.network = str(args.network) if args.network else None
        self.arg_uri = str(args.uri) if args.uri else None
        self.scan_for_context = args.auto
        self.buffer_size = int(args.buffer_size) if args.buffer_size else 256
        self.num_samples = int(args.samples) if args.samples else 0
        self.timeout = int(args.timeout) if args.timeout else 0
        self.device_name = args.device[0]
        self.channels = args.channel

    def _add_parser_arguments(self):
        self.parser.add_argument(
            "-n",
            "--network",
            type=str,
            metavar="",
            help="Use the network backend with the provided hostname.",
        )
        self.parser.add_argument(
            "-u",
            "--uri",
            type=str,
            metavar="",
            help="Use the context with the provided URI.",
        )
        self.parser.add_argument(
            "-b",
            "--buffer-size",
            type=int,
            metavar="",
            help="Size of the capture buffer. Default is 256.",
        )
        self.parser.add_argument(
            "-s",
            "--samples",
            type=int,
            metavar="",
            help="Number of samples to capture, 0 = infinite. Default is 0.",
        )
        self.parser.add_argument(
            "-T",
            "--timeout",
            type=int,
            metavar="",
            help="Buffer timeout in milliseconds. 0 = no timeout",
        )
        self.parser.add_argument(
            "-a",
            "--auto",
            action="store_true",
            help="Scan for available contexts and if only one is available use it.",
        )
        self.parser.add_argument("device", type=str, nargs=1)
        self.parser.add_argument("channel", type=str, nargs="*")


class ContextBuilder:
    """Class for creating the requested context."""

    def __init__(self, arguments):
        """
        Class constructor.

        Args:
            arguments: type=Arguments
                Contains the input arguments.
        """
        self.ctx = None
        self.arguments = arguments

    def _timeout(self):
        if self.arguments.timeout >= 0:
            self.ctx.timeout = self.arguments.timeout
        return self

    def _auto(self):
        contexts = iio.scan_contexts()
        if len(contexts) == 0:
            raise Exception("No IIO context found.\n")
        if len(contexts) == 1:
            uri, _ = contexts.popitem()
            self.ctx = iio.Context(_context=uri)
        else:
            print("Multiple contexts found. Please select one using --uri!")
            for uri, _ in contexts.items():
                print(uri)
            sys.exit(0)

        return self

    def _uri(self):
        self.ctx = iio.Context(_context=self.arguments.arg_uri)
        return self

    def _network(self):
        self.ctx = iio.NetworkContext(self.arguments.network)
        return self

    def _default(self):
        self.ctx = iio.Context()
        return self

    def create(self):
        """Create the requested context."""
        try:
            if self.arguments.scan_for_context:
                self._auto()
            elif self.arguments.arg_uri:
                self._uri()
            elif self.arguments.arg_ip:
                self._network()
            else:
                self._default()
        except FileNotFoundError:
            raise Exception("Unable to create IIO context!\n")

        self._timeout()

        return self.ctx


class BufferBuilder:
    """Class for creating the buffer."""

    def __init__(self, ctx, arguments):
        """
        Class constructor.

        Args:
            ctx: type=iio.Context
                This buffer's context.
            arguments: type=Arguments
                Contains the input arguments.
        """
        self.ctx = ctx
        self.arguments = arguments
        self.dev = None

    def _device(self):
        self.dev = self.ctx.find_device(self.arguments.device_name)

        if self.dev is None:
            raise Exception("Device %s not found!" % self.arguments.device_name)

        return self

    def _channels(self):
        if len(self.arguments.channels) == 0:
            for channel in self.dev.channels:
                channel.enabled = True
        else:
            for channel_idx in self.arguments.channels:
                self.dev.channels[int(channel_idx)].enabled = True

        return self

    def create(self):
        """Create the IIO buffer."""
        self._device()
        self._channels()
        buffer = iio.Buffer(self.dev, self.arguments.buffer_size)

        if buffer is None:
            raise Exception("Unable to create buffer!\n")

        return buffer


class DataReader:
    """Class for reading samples from the device."""

    def __init__(self, ctx, arguments):
        """
        Class constructor.

        Args:
            ctx: type=iio.Context
                Current context.
            arguments: type=Arguments
                Contains the input arguments.
        """
        buffer_builder = BufferBuilder(ctx, arguments)
        self.buffer = buffer_builder.create()
        self.device = buffer_builder.dev
        self.arguments = arguments

    def read(self):
        """Read data from the buffer."""
        while True:
            self.buffer.refill()
            samples = self.buffer.read()

            if self.arguments.num_samples > 0:
                sys.stdout.buffer.write(
                    samples[: min(self.arguments.num_samples * self.device.sample_size, len(samples))]
                )
                self.arguments.num_samples -= min(
                    self.arguments.num_samples, len(samples)
                )

                if self.arguments.num_samples == 0:
                    break
            else:
                sys.stdout.buffer.write(bytes(samples))


def main():
    """Module's main method."""
    arguments = Arguments()
    context_builder = ContextBuilder(arguments)
    reader = DataReader(context_builder.create(), arguments)
    reader.read()


if __name__ == "__main__":
    main()
