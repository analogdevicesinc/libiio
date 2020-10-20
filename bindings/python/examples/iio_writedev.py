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

import time
import sys
import argparse
import iio
import numpy as np


class Arguments:
    """Class for parsing the input arguments."""

    def __init__(self):
        """Arguments class constructor."""
        self.parser = argparse.ArgumentParser(description="iio_writedev")
        self._add_parser_arguments()
        args = self.parser.parse_args()

        self.network = str(args.network) if args.network else None
        self.arg_uri = str(args.uri) if args.uri else None
        self.scan_for_context = args.auto
        self.buffer_size = int(args.buffer_size) if args.buffer_size else 256
        self.num_samples = int(args.samples) if args.samples else 0
        self.timeout = int(args.timeout) if args.timeout else 0
        self.cyclic = args.cyclic
        self.device_name = args.device[0]
        self.channels = args.channel
        self.sine_generation = args.sine_generation
        self.sine_config = {
            "fs": int(args.sampling_frequency),
            "fc": int(args.tone_frequency),
            "complex": args.complex,
        }

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
        self.parser.add_argument(
            "-c", "--cyclic", action="store_true", help="Use cyclic buffer mode."
        )
        self.parser.add_argument("device", type=str, nargs=1)
        self.parser.add_argument("channel", type=str, nargs="*")

        # Sine wave generation
        self.parser.add_argument(
            "-sine",
            "--sine-generation",
            action="store_true",
            help="Enable sine wave generation.",
        )

        self.parser.add_argument(
            "-fs",
            "--sampling-frequency",
            type=int,
            default=30720000,
            metavar="",
            help="Set assumed sampling frequency of data in Hz.",
        )
        self.parser.add_argument(
            "-fc",
            "--tone-frequency",
            type=int,
            default=1000000,
            metavar="",
            help="Set desired tone frequency of data in Hz.",
        )
        self.parser.add_argument(
            "-cm",
            "--complex",
            action="store_true",
            help="Enable complex data generation.",
        )


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

    def __init__(self, ctx, arguments, data_source=None):
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
        self.num_channels = None
        if data_source:
            self.buffer_size = data_source.buffer_size
        else:
            self.buffer_size = self.arguments.buffer_size

    def _device(self):
        self.dev = self.ctx.find_device(self.arguments.device_name)

        if self.dev is None:
            raise Exception("Device %s not found!" % self.arguments.device_name)

        return self

    def _channels(self):
        if len(self.arguments.channels) == 0:
            self.num_channels = len(self.dev.channels)
            for channel in self.dev.channels:
                channel.enabled = True
        else:
            self.num_channels = len(self.arguments.channels)
            for channel_idx in self.arguments.channels:
                self.dev.channels[int(channel_idx)].enabled = True

        return self

    def create(self):
        """Create the IIO buffer."""
        self._device()
        self._channels()
        buffer = iio.Buffer(self.dev, self.buffer_size, self.arguments.cyclic)

        if buffer is None:
            raise Exception("Unable to create buffer!\n")

        return buffer


class DataSource:
    """ Class for generating waveforms and importing data files """

    def __init__(self, arguments):

        self.gen_source = "sinusoid"
        self.complex_dev = arguments.sine_config["complex"]
        self.fs = arguments.sine_config["fs"]
        self.fc = arguments.sine_config["fc"]
        self.num_channels = None

        self.data_np = self.gen_data()
        self.buffer_size = len(self.data_np)

    def _format(self, data_np):
        if not self.num_channels:
            raise Exception("Channel count must be set before data can be formatted")
        indx = 0
        stride = self.num_channels
        data = np.empty(stride * len(data_np), dtype=np.int16)
        if self.complex_dev:
            for _ in range(self.num_channels // 2):
                i = np.real(data_np)
                q = np.imag(data_np)
                data[indx::stride] = i.astype(int)
                data[indx + 1 :: stride] = q.astype(int)
                indx += 2
        else:
            data = np.empty(stride * len(data_np), dtype=np.int16)
            for _ in range(self.num_channels):
                data[indx::stride] = data_np.astype(int)
                indx += 1
        return data

    def _gen_sine(self):
        """ Generate sinusoid with first and last+1 samples are equivalent """

        fs = self.fs
        fc = self.fc
        ts = 1 / float(fs)
        samples_per_period = float(fs) * 1 / float(fc)

        # determine where we have exact integer of periods
        min_len = 2 ** 10
        max_len = 2 ** 20
        periods = 1
        while (periods * samples_per_period) <= max_len:

            if (periods * samples_per_period) < min_len:
                periods += 1
                continue

            if int(periods * samples_per_period) == periods * samples_per_period:
                break

            periods += 1
            if (periods * samples_per_period) >= max_len:
                raise ("Cannot create a sinusoid without discontinuities")

        seq_len = periods * samples_per_period
        t = np.arange(0, (seq_len) * ts, ts)
        i = np.cos(2 * np.pi * t * fc) * 2 ** 14
        if self.complex_dev:
            i = i + 1j * np.sin(2 * np.pi * t * fc) * 2 ** 14

        return i

    def gen_data(self):
        if self.gen_source == "sinusoid":
            return self._gen_sine()

    def get_data_formated(self, data_np=None):
        if data_np:
            return self._format(data_np)
        else:
            return self._format(self.data_np)


class DataWriter:
    """Class for writing samples to the device."""

    def __init__(self, ctx, arguments, data_source=None):
        """
        Class constructor.

        Args:
            ctx: type=iio.Context
                Current context.
            arguments: type=Arguments
                Contains the input arguments.
        """
        self.data_source = data_source
        buffer_builder = BufferBuilder(ctx, arguments, data_source)
        self.buffer = buffer_builder.create()
        self.device = buffer_builder.dev
        self.arguments = arguments
        self.data_source.num_channels = buffer_builder.num_channels

    def write(self):
        """Push data into the buffer."""
        app_running = True
        num_samples = self.arguments.num_samples

        if self.data_source:
            data = self.data_source.get_data_formated()
            if self.buffer.write(bytearray(data)) == 0:
                raise Exception("Unable to push buffer!")

            self.buffer.push()

        while app_running:
            bytes_to_read = (
                len(self.buffer)
                if num_samples == 0
                else min(len(self.buffer), num_samples * self.device.sample_size)
            )
            write_len = bytes_to_read
            data = []

            while bytes_to_read > 0:
                read_data = sys.stdin.buffer.read(bytes_to_read)
                if len(read_data) == 0:
                    sys.exit(0)
                bytes_to_read -= len(read_data)
                data.extend(read_data)

            if self.buffer.write(bytearray(data)) == 0:
                raise Exception("Unable to push buffer!")

            self.buffer.push()

            while app_running and self.arguments.cyclic:
                time.sleep(1)

            if num_samples > 0:
                num_samples -= write_len // self.device.sample_size
                if num_samples == 0:
                    app_running = False


def main():
    """Module's main method."""
    arguments = Arguments()
    context_builder = ContextBuilder(arguments)
    data_source = DataSource(arguments) if arguments.sine_generation else False
    writer = DataWriter(context_builder.create(), arguments, data_source)
    writer.write()


if __name__ == "__main__":
    main()
