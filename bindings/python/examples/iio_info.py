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

	print 'Library version: %u.%u (git tag: %s)' % iio.version

	print 'IIO context created: ' + ctx.name
	print 'Backend version: %u.%u (git tag: %s)' % ctx.version
	print 'Backend description string: ' + ctx.description

	print 'IIO context has %u devices:' % len(ctx.devices)

	for dev in ctx.devices:
		print '\t' + dev.id + ': ' + dev.name

		if dev is iio.Trigger:
			print 'Found trigger! Rate: %u Hz' % dev.frequency

		print '\t\t%u channels found:' % len(dev.channels)

		for chn in dev.channels:
			print '\t\t\t%s: %s (%s)' % (chn.id, chn.name or "", 'output' if chn.output else 'input')

			if len(chn.attrs) != 0:
				print '\t\t\t%u channel-specific attributes found:' % len(chn.attrs)

			for attr in chn.attrs:
				print '\t\t\t\t' + attr + ', value: ' + chn.attrs[attr].value

		if len(dev.attrs) != 0:
			print '\t\t%u device-specific attributes found:' % len(dev.attrs)

		for attr in dev.attrs:
			print '\t\t\t' + attr + ', value: ' + dev.attrs[attr].value

		if len(dev.debug_attrs) != 0:
			print '\t\t%u debug attributes found:' % len(dev.debug_attrs)

		for attr in dev.debug_attrs:
			print '\t\t\t' + attr + ', value: ' + dev.debug_attrs[attr].value

if __name__ == '__main__':
	main()
