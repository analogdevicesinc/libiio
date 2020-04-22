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


def _str_match(string, other_string, ignore_case):
    if ignore_case:
        string = string.lower()
        other_string = other_string.lower()

    return string == other_string


class Arguments:
    """Class for parsing the input arguments."""

    def __init__(self):
        """Arguments class constructor."""
        self.device = None
        self.channel = None
        self.attr = None
        self.buffer = None
        self.search_context = False
        self.search_device = False
        self.search_channel = False
        self.search_buffer = False
        self.search_debug = False
        self.detect_context = None
        self.arg_uri = None
        self.input_only = None
        self.output_only = None
        self.scan_only = None
        self.ignore_case = None
        self.quiet = None

        self.parser = argparse.ArgumentParser(description="iio_attr")
        self._add_required_mutex_group()
        self._add_help_group()
        self._add_context_group()
        self._add_channel_group()
        args = self.parser.parse_args()
        self._read_arguments(args)

    def _add_required_mutex_group(self):
        self._required_mutex_group = self.parser.add_mutually_exclusive_group(required=True)
        self._required_mutex_group.add_argument("-d", "--device-attr",
                                                type=str, metavar="", nargs="*",
                                                help="Usage: [device] [attr] [value]\nRead/Write device attributes")
        self._required_mutex_group.add_argument("-c", "--channel-attr",
                                                type=str, metavar="", nargs="*",
                                                help="Usage: [device] [channel] [attr] [value]\n"
                                                     "Read/Write channel attributes.")
        self._required_mutex_group.add_argument("-B", "--buffer-attr",
                                                type=str, metavar="", nargs="*",
                                                help="Usage: [device] [attr] [value]\nRead/Write buffer attributes.")
        self._required_mutex_group.add_argument("-D", "--debug-attr",
                                                type=str, metavar="", nargs="*",
                                                help="Usage: [device] [attr] [value]\nRead/Write debug attributes.")
        self._required_mutex_group.add_argument("-C", "--context-attr",
                                                type=str, metavar="", nargs="*",
                                                help="Usage: [attr]\nRead IIO context attributes.")

    def _add_help_group(self):
        self._help_group = self.parser.add_argument_group("General")
        self._help_group.add_argument("-I", "--ignore-case", action="store_true", help="Ignore case distinctions.")
        self._help_group.add_argument("-q", "--quiet", action="store_true", help="Return result only.")

    def _add_context_group(self):
        self._context_group = self.parser.add_argument_group("Context connection")
        self._context_group.add_argument("-u", "--uri", metavar="", type=str, nargs=1,
                                         help="Use the context at the provided URI.")
        self._context_group.add_argument("-a", "--auto", action="store_true",
                                         help="Use the first context found.")

    def _add_channel_group(self):
        self._channel_group = self.parser.add_argument_group("Channel qualifiers")
        self._channel_group.add_argument("-i", "--input-channel", action="store_true",
                                         help="Filter Input Channels only.")
        self._channel_group.add_argument("-o", "--output-channel", action="store_true",
                                         help="Filter Output Channels only.")
        self._channel_group.add_argument("-s", "--scan-channel", action="store_true",
                                         help="Filter Scan Channels only.")

    def _read_optional_arguments(self, args):
        self.detect_context = args.auto
        self.arg_uri = args.uri[0] if args.uri is not None else None
        self.input_only = args.input_channel
        self.output_only = args.output_channel
        self.scan_only = args.scan_channel
        self.ignore_case = args.ignore_case
        self.quiet = args.quiet

    def _read_device_arguments(self, args):
        if len(args.device_attr) >= 4:
            print("Too many options for searching for device attributes")
            sys.exit(1)

        self.search_device = True
        self.device = args.device_attr[0] if len(args.device_attr) >= 1 else None
        self.attr = args.device_attr[1] if len(args.device_attr) >= 2 else None
        self.buffer = args.device_attr[2] if len(args.device_attr) >= 3 else None

    def _read_channel_arguments(self, args):
        if len(args.channel_attr) >= 5:
            print("Too many options for searching for channel attributes")
            sys.exit(1)

        self.search_channel = True
        self.device = args.channel_attr[0] if len(args.channel_attr) >= 1 else None
        self.channel = args.channel_attr[1] if len(args.channel_attr) >= 2 else None
        self.attr = args.channel_attr[2] if len(args.channel_attr) >= 3 else None
        self.buffer = args.channel_attr[3] if len(args.channel_attr) >= 4 else None

    def _read_buffer_arguments(self, args):
        if len(args.buffer_attr) >= 4:
            print("Too many options for searching for buffer attributes")
            sys.exit(1)

        self.search_buffer = True
        self.device = args.buffer_attr[0] if len(args.buffer_attr) >= 1 else None
        self.attr = args.buffer_attr[1] if len(args.buffer_attr) >= 2 else None
        self.buffer = args.buffer_attr[2] if len(args.buffer_attr) >= 3 else None

    def _read_debug_arguments(self, args):
        if len(args.debug_attr) >= 4:
            print("Too many options for searching for debug attributes")
            sys.exit(1)

        self.search_debug = True
        self.device = args.debug_attr[0] if len(args.debug_attr) >= 1 else None
        self.attr = args.debug_attr[1] if len(args.debug_attr) >= 2 else None
        self.buffer = args.debug_attr[2] if len(args.debug_attr) >= 3 else None

    def _read_context_arguments(self, args):
        if len(args.context_attr) >= 2:
            print("Too many options for searching for context attributes")
            sys.exit(1)

        self.search_context = True
        self.attr = args.context_attr[0] if len(args.context_attr) >= 1 else None

    def _read_arguments(self, args):
        self._read_optional_arguments(args)
        if args.device_attr is not None:
            self._read_device_arguments(args)
        if args.channel_attr is not None:
            self._read_channel_arguments(args)
        if args.buffer_attr is not None:
            self._read_buffer_arguments(args)
        if args.debug_attr is not None:
            self._read_debug_arguments(args)
        if args.context_attr is not None:
            self._read_context_arguments(args)


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

    def _default(self):
        self.ctx = iio.Context()
        return self

    def create(self):
        """Create the requested context."""
        try:
            if self.arguments.detect_context:
                self._auto()
            elif self.arguments.arg_uri:
                self._uri()
            else:
                self._default()
        except FileNotFoundError:
            raise Exception("Unable to create IIO context!\n")

        return self.ctx


class Information:
    """Class for receiving the requested information about the attributes."""

    def __init__(self, arguments, context):
        """
        Class constructor.

        Args:
            arguments: type=Arguments
                Contains the input arguments.
            context:  type=iio.Context
                The created context.
        """
        self.arguments = arguments
        self.context = context

    def write_information(self):
        """Write the requested information."""
        self._context_information()

        if self.arguments.search_device or self.arguments.search_channel or \
                self.arguments.search_buffer or self.arguments.search_debug:
            self._devices_information()

    def _context_information(self):
        if self.context is None:
            print("Unable to create IIO context")
            sys.exit(1)

        if self.arguments.search_context:
            if self.arguments.attr is None and len(self.context.attrs) > 0:
                print("IIO context with " + str(len(self.context.attrs)) + " attributes:")

            for key, value in self.context.attrs.items():
                if self.arguments.attr is None or \
                        _str_match(key, self.arguments.attr, self.arguments.ignore_case):
                    print(key + ": " + value)

    def _devices_information(self):
        if self.arguments.device is None:
            print("IIO context has " + str(len(self.context.devices)) + " devices:")

        for dev in self.context.devices:
            self._device_information(dev)
            self._device_attributes_information(dev)
            self._buffer_attributes_information(dev)
            self._debug_attributes_information(dev)

    def _device_information(self, dev):
        if self.arguments.device is not None and \
                not _str_match(dev.name, self.arguments.device, self.arguments.ignore_case):
            return

        if self.arguments.device is None:
            print("\t" + dev.id + ":", end="")

            if dev.name is not None:
                print(" " + dev.name, end="")

            print(", ", end="")

            if self.arguments.search_channel and self.arguments.device is None:
                print("found " + str(len(dev.channels)) + " channels")

        for channel in dev.channels:
            self._channel_information(dev, channel)

    def _channel_information(self, dev, channel):
        channel_stop = not self.arguments.search_channel or self.arguments.device is None
        input_stop = self.arguments.input_only and channel.output
        output_stop = self.arguments.output_only and not channel.output
        scan_stop = self.arguments.scan_only and not channel.scan_element

        if channel_stop or input_stop or output_stop or scan_stop:
            return

        type_name = "output" if channel.output else "input"

        if not _str_match(self.arguments.device, dev.name, self.arguments.ignore_case):
            return

        if self.arguments.channel is not None and \
                not _str_match(channel.id, self.arguments.channel, self.arguments.ignore_case) and \
                (channel.name is None or (channel.name is not None and
                                          not _str_match(channel.name, self.arguments.channel,
                                                         self.arguments.ignore_case))):
            return

        if (not self.arguments.scan_only and self.arguments.channel is None) or \
                (self.arguments.scan_only and channel.scan_element):
            print("dev \'" + dev.name + "\', channel \'" + channel.id + "\'", end="")

            if channel.name is not None:
                print(", id \'" + channel.name + "\'", end="")

            print(" (" + type_name, end="")

            if channel.scan_element:
                self._scan_channel_information(channel)
            else:
                print("), ", end="")

        self._channel_attributes_information(dev, channel)

    def _scan_channel_information(self, channel):
        sign = "s" if channel.data_format.is_signed else "u"
        if channel.data_format.is_fully_defined:
            sign = sign.upper()

        if channel.data_format.repeat > 1:
            print("X" + str(channel.data_format.repeat), end="")

        print(", index: " + str(channel.index) + ", format: "
              + "b" if channel.data_format.is_be else "l"
              + "e:" + sign + str(channel.data_format.bits)
              + "/" + str(channel.data_format.length) + str(channel.data_format.repeat)
              + ">>" + str(channel.data_format.shift))

        print("" if self.arguments.scan_only else ", ", end="")

    def _channel_attributes_information(self, dev, channel):
        if self.arguments.channel is None:
            print("found " + str(len(channel.attrs)) + " channel-specific attributes")

        if len(channel.attrs) == 0 or self.arguments.channel is None:
            return

        for key, _ in channel.attrs.items():
            if self.arguments.attr is not None and \
                    not _str_match(key, self.arguments.attr, self.arguments.ignore_case):
                continue
            self._channel_attribute_information(dev, channel, key)

    def _channel_attribute_information(self, dev, channel, attr):
        if self.arguments.buffer is None or not self.arguments.quiet:
            type_name = "output" if channel.output else "input"

            if not self.arguments.quiet:
                print("dev \'" + dev.name + "\', channel \'" + channel.id + "\' (" + type_name + "), ", end="")

                if channel.name is not None:
                    print("id \'" + channel.name + "\', ", end="")

                print("attr \'" + attr + "\', ", end="")

            try:
                print(channel.attrs[attr].value if self.arguments.quiet else
                      "value \'" + channel.attrs[attr].value + "\'")
            except OSError as err:
                print("ERROR: " + err.strerror + " (-" + str(err.errno) + ")")

        if self.arguments.buffer is not None:
            channel.attrs[attr].value = self.arguments.buffer

            if not self.arguments.quiet:
                print("wrote " + str(len(self.arguments.buffer)) + " bytes to " + attr)

            self.arguments.buffer = None
            self._channel_attribute_information(dev, channel, attr)

    def _device_attributes_information(self, dev):
        if self.arguments.search_device and self.arguments.device is None:
            print("found " + str(len(dev.attrs)) + " device attributes")

        if self.arguments.search_device and self.arguments.device is not None and len(dev.attrs) > 0:
            for key, _ in dev.attrs.items():
                if self.arguments.attr is not None and \
                        not _str_match(key, self.arguments.attr, self.arguments.ignore_case):
                    continue

                self._device_attribute_information(dev, key)

    def _device_attribute_information(self, dev, attr):
        if self.arguments.buffer is None or not self.arguments.quiet:
            if not self.arguments.quiet:
                print("dev \'" + dev.name + "\', attr \'" + attr + "\', value: ", end="")

            try:
                print(dev.attrs[attr].value if self.arguments.quiet else "\'" + dev.attrs[attr].value + "\'")
            except OSError as err:
                print("ERROR: " + err.strerror + " (-" + str(err.errno) + ")")

        if self.arguments.buffer is not None:
            dev.attrs[attr].value = self.arguments.buffer

            if not self.arguments.quiet:
                print("wrote " + str(len(self.arguments.buffer)) + " bytes to " + attr)

            self.arguments.buffer = None
            self._device_attribute_information(dev, attr)

    def _buffer_attributes_information(self, dev):
        if self.arguments.search_buffer and self.arguments.device is None:
            print("found " + str(len(dev.buffer_attrs)) + " buffer attributes")
        elif self.arguments.search_buffer and _str_match(self.arguments.device, dev.name, self.arguments.ignore_case) \
                and len(dev.buffer_attrs) > 0:
            for key, _ in dev.buffer_attrs.items():
                if (self.arguments.attr is not None and
                        _str_match(key, self.arguments.attr, self.arguments.ignore_case)) or \
                        self.arguments.attr is None:
                    self._buffer_attribute_information(dev, key)

    def _buffer_attribute_information(self, dev, attr):
        if self.arguments.buffer is None or not self.arguments.quiet:
            if not self.arguments.quiet:
                print("dev \'" + dev.name + "\', buffer attr \'" + attr + "\', value: ", end="")

            try:
                print(dev.buffer_attrs[attr].value if self.arguments.quiet else
                      "\'" + dev.buffer_attrs[attr].value + "\'")
            except OSError as err:
                print("ERROR: " + err.strerror + " (-" + str(err.errno) + ")")

        if self.arguments.buffer is not None:
            dev.buffer_attrs[attr].value = self.arguments.buffer

            if not self.arguments.quiet:
                print("wrote " + str(len(self.arguments.buffer)) + " bytes to " + attr)

            self.arguments.buffer = None
            self._buffer_attribute_information(dev, attr)

    def _debug_attributes_information(self, dev):
        if self.arguments.search_debug and self.arguments.device is None:
            print("found " + str(len(dev.debug_attrs)) + " debug attributes")
        elif self.arguments.search_debug and _str_match(self.arguments.device, dev.name, self.arguments.ignore_case) \
                and len(dev.debug_attrs) > 0:
            for key, _ in dev.debug_attrs.items():
                if (self.arguments.attr is not None and _str_match(key, self.arguments.attr, self.arguments.quiet)) \
                        or self.arguments.attr is None:
                    self._debug_attribute_information(dev, key)

    def _debug_attribute_information(self, dev, attr):
        if self.arguments.buffer is None or not self.arguments.quiet:
            if not self.arguments.quiet:
                print("dev \'" + dev.name + "\', debug attr \'" + attr + "\', value: ", end="")

            try:
                print(dev.debug_attrs[attr].value if self.arguments.quiet else
                      "\'" + dev.debug_attrs[attr].value + "\'")
            except OSError as err:
                print("ERROR: " + err.strerror + " (-" + str(err.errno) + ")")

        if self.arguments.buffer is not None:
            dev.debug_attrs[attr].value = self.arguments.buffer

            if not self.arguments.quiet:
                print("wrote " + str(len(self.arguments.buffer)) + " bytes to " + attr)

            self.arguments.buffer = None
            self._debug_attribute_information(dev, attr)


def main():
    """Module's main method."""
    arguments = Arguments()
    context_builder = ContextBuilder(arguments)
    context = context_builder.create()
    information = Information(arguments, context)
    information.write_information()


if __name__ == "__main__":
    main()
