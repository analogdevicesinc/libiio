#!/usr/bin/env python
#
# Copyright (C) 2015 Analog Devices, Inc.
# Author: Paul Cercueil <paul.cercueil@analog.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

import iio

def main():
	ctx = iio.Context()

	print ('Library version: {}.{} (git tag: {})'.format(iio.version[0], iio.version[1], iio.version[2].decode()))

	print ('IIO context created: %s' % ctx.name)
	print ('Backend version: {}.{} (git tag: {})'.format(ctx.version[0], ctx.version[1], ctx.version[2].decode()))
	print ('Backend description string: {}'.format(ctx.description.decode()))

	print ('IIO context has %u devices:' % len(ctx.devices))

	for dev in ctx.devices:
		print ('\t{}: {}'.format(dev.id.decode(), dev.name.decode()))

		if dev is iio.Trigger:
			print ('Found trigger! Rate: {} Hz'.format(dev.frequency))

		print ('\t\t{} channels found:'.format(len(dev.channels)))

		for chn in dev.channels:
			print ('\t\t\t{}: {} ({})'.format(chn.id.decode(), chn.name.decode() if chn.name else "", 'output' if chn.output else 'input'))

			if len(chn.attrs) != 0:
				print ('\t\t\t{} channel-specific attributes found:'.format(len(chn.attrs)))

			for attr in chn.attrs:
				print ('\t\t\t\t{}, value: {}'.format(attr.decode(), chn.attrs[attr].value.decode()))

		if len(dev.attrs) != 0:
			print ('\t\t{} device-specific attributes found:'.format(len(dev.attrs)))

		for attr in dev.attrs:
			print ('\t\t\t{}, value: '.format(attr.decode(), dev.attrs[attr].value.decode()))

		if len(dev.debug_attrs) != 0:
			print ('\t\t{} debug attributes found:'.format(len(dev.debug_attrs)))

		for attr in dev.debug_attrs:
			print ('\t\t\t{}, value: '.format(attr.decode(), dev.debug_attrs[attr].value.decode()))

if __name__ == '__main__':
	main()
