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

# AD9361 IIO streaming example - Python bindings

import iio
import numpy as np
from sys import argv

DEVICE_RX = "cf-ad9361-lpc"
DEVICE_TX = "cf-ad9361-dds-core-lpc"
DEVICE_PHY = "ad9361-phy"


def main():
	if len(argv) == 3 and argv[1] == '--uri':
		uri = argv[2]
	else:
		contexts = iio.scan_contexts()
		if len(contexts) > 1:
			print('Multiple contexts found. Please select one using --uri:')
			for index, each in enumerate(contexts):
				print('\t%d: %s [%s]' % (index, contexts[each], each))
			return

		uri = next(iter(contexts), None)

	context = iio.Context(uri)

	if uri is not None:
		print('Using auto-detected IIO context at URI \"%s\"' % uri)

	# Generating samples for two sine waves (one for I, one for Q)

	N = 1024
	lo = 2400000000
	fs = 2500000
	fc = fs // 16
	ts = 1 / float(fs)
	t = np.arange(0, N * ts, ts)
	i = np.cos(2 * np.pi * t * fc) * 2 ** 14
	q = np.sin(2 * np.pi * t * fc) * 2 ** 14

	data = np.empty(2 * len(t), dtype=np.int16)

	data[0::2] = i.astype(int)
	data[1::2] = q.astype(int)

	device_phy = context.find_device(DEVICE_PHY)
	device_rx = context.find_device(DEVICE_RX)
	device_tx = context.find_device(DEVICE_TX)

	rx_lo_channel = device_phy.find_channel('altvoltage0', True)
	tx_lo_channel = device_phy.find_channel('altvoltage1', True)
	v0phy_channel = device_phy.find_channel('voltage0', True)
	rx_lo_channel.attrs['frequency'].value = str(lo)
	tx_lo_channel.attrs['frequency'].value = str(lo)
	v0phy_channel.attrs['sampling_frequency'].value = str(fs)

	v0rx_channel = device_rx.find_channel('voltage0', False)
	v1rx_channel = device_rx.find_channel('voltage1', False)
	v0rx_channel.enabled = True
	v1rx_channel.enabled = True

	v0tx_channel = device_tx.find_channel('voltage0', True)
	v1tx_channel = device_tx.find_channel('voltage1', True)
	v0tx_channel.enabled = True
	v1tx_channel.enabled = True

	# Writing the data into the tx buffer and pushing it.

	tx_buf = iio.Buffer(device_tx, N, True)

	tx_buf.write(bytearray(data))
	tx_buf.push()

	# Reading the data from the rx buffer and printing it to stdin.

	rx_buf = iio.Buffer(device_rx, N, False)

	while True:
		rx_buf.refill()
		samples = rx_buf.read()

		dt = np.dtype(np.int16)
		dt = dt.newbyteorder('<')
		values = np.frombuffer(samples, dtype=dt)

		for value in values:
			print(value, end=" ")


if __name__ == '__main__':
	main()
