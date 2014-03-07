#!/usr/bin/env python
#
# Copyright (C) 2014 Analog Devices, Inc.
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

from ctypes import POINTER, Structure, cdll, c_char_p, c_uint, c_int, \
		c_bool, create_string_buffer, byref
from sys import argv

def _checkNull(result, func, arguments):
	if result:
		return result
	else:
		raise IOError("Null pointer")

def _checkRW(result, func, arguments):
	if result > 0:
		return result
	else:
		raise IOError("I/O error")

def _init():
	class _Context(Structure):
		pass
	class _Device(Structure):
		pass
	class _Channel(Structure):
		pass

	ContextPtr = POINTER(_Context)
	DevicePtr = POINTER(_Device)
	ChannelPtr = POINTER(_Channel)

	lib = cdll.LoadLibrary('libiio.so.0')

	global _new_local
	_new_local = lib.iio_create_local_context
	_new_local.restype = ContextPtr
	_new_local.archtypes = None
	_new_local.errcheck = _checkNull

	global _new_xml
	_new_xml = lib.iio_create_xml_context
	_new_xml.restype = ContextPtr
	_new_xml.archtypes = (c_char_p, )
	_new_xml.errcheck = _checkNull

	global _new_network
	_new_network = lib.iio_create_network_context
	_new_network.restype = ContextPtr
	_new_network.archtypes = (c_char_p, )
	_new_network.errcheck = _checkNull

	global _destroy
	_destroy = lib.iio_context_destroy
	_destroy.restype = None
	_destroy.archtypes = (ContextPtr, )

	global _get_name
	_get_name = lib.iio_context_get_name
	_get_name.restype = c_char_p
	_get_name.archtypes = (ContextPtr, )
	_get_name.errcheck = _checkNull

	global _devices_count
	_devices_count = lib.iio_context_get_devices_count
	_devices_count.restype = c_uint
	_devices_count.archtypes = (ContextPtr, )

	global _get_device
	_get_device = lib.iio_context_get_device
	_get_device.restype = DevicePtr
	_get_device.archtypes = (ContextPtr, c_uint)
	_get_device.errcheck = _checkNull

	global _d_get_id
	_d_get_id = lib.iio_device_get_id
	_d_get_id.restype = c_char_p
	_d_get_id.archtypes = (DevicePtr, )
	_d_get_id.errcheck = _checkNull

	global _d_get_name
	_d_get_name = lib.iio_device_get_name
	_d_get_name.restype = c_char_p
	_d_get_name.archtypes = (DevicePtr, )

	global _d_attr_count
	_d_attr_count = lib.iio_device_get_attrs_count
	_d_attr_count.restype = c_uint
	_d_attr_count.archtypes = (DevicePtr, )

	global _d_get_attr
	_d_get_attr = lib.iio_device_get_attr
	_d_get_attr.restype = c_char_p
	_d_get_attr.archtypes = (DevicePtr, )
	_d_get_attr.errcheck = _checkNull

	global _d_read_attr
	_d_read_attr = lib.iio_device_attr_read
	_d_read_attr.restype = c_int
	_d_read_attr.archtypes = (DevicePtr, c_char_p, c_char_p, c_uint)
	_d_read_attr.errcheck = _checkRW

	global _d_write_attr
	_d_write_attr = lib.iio_device_attr_write
	_d_write_attr.restype = c_int
	_d_write_attr.archtypes = (DevicePtr, c_char_p, c_char_p)
	_d_write_attr.errcheck = _checkRW

	global _channels_count
	_channels_count = lib.iio_device_get_channels_count
	_channels_count.restype = c_uint
	_channels_count.archtypes = (DevicePtr, )

	global _get_channel
	_get_channel = lib.iio_device_get_channel
	_get_channel.restype = ChannelPtr
	_get_channel.archtypes = (DevicePtr, c_uint)
	_get_channel.errcheck = _checkNull

	global _c_get_id
	_c_get_id = lib.iio_channel_get_id
	_c_get_id.restype = c_char_p
	_c_get_id.archtypes = (ChannelPtr, )
	_c_get_id.errcheck = _checkNull

	global _c_get_name
	_c_get_name = lib.iio_channel_get_name
	_c_get_name.restype = c_char_p
	_c_get_name.archtypes = (ChannelPtr, )

	global _c_is_output
	_c_is_output = lib.iio_channel_is_output
	_c_is_output.restype = c_bool
	_c_is_output.archtypes = (ChannelPtr, )

	global _c_attr_count
	_c_attr_count = lib.iio_channel_get_attrs_count
	_c_attr_count.restype = c_uint
	_c_attr_count.archtypes = (ChannelPtr, )

	global _c_get_attr
	_c_get_attr = lib.iio_channel_get_attr
	_c_get_attr.restype = c_char_p
	_c_get_attr.archtypes = (ChannelPtr, )
	_c_get_attr.errcheck = _checkNull

	global _c_read_attr
	_c_read_attr = lib.iio_channel_attr_read
	_c_read_attr.restype = c_int
	_c_read_attr.archtypes = (ChannelPtr, c_char_p, c_char_p, c_uint)
	_c_read_attr.errcheck = _checkRW

	global _c_write_attr
	_c_write_attr = lib.iio_channel_attr_write
	_c_write_attr.restype = c_int
	_c_write_attr.archtypes = (ChannelPtr, c_char_p, c_char_p)
	_c_write_attr.errcheck = _checkRW

_init()

class Channel(object):
	def __init__(self, _channel):
		self._channel = _channel
		self.attrs = [ _c_get_attr(self._channel, x) \
				for x in xrange(0, _c_attr_count(self._channel)) ]
		self.id = _c_get_id(self._channel)
		self.name = _c_get_name(self._channel)
		self.output = _c_is_output(self._channel)

	# TODO(pcercuei): Provide a dict-like interface for the attributes
	def read_attr(self, attr):
		buf = create_string_buffer(1024)
		_c_read_attr(self._channel, attr, buf, 1024)
		return buf.value

	def write_attr(self, attr, value):
		_c_write_attr(self._channel, attr, value)

class Device(object):
	def __init__(self, _device):
		self._device = _device
		self.attrs = [ _d_get_attr(self._device, x) \
				for x in xrange(0, _d_attr_count(self._device)) ]
		self.channels = [ Channel(_get_channel(self._device, x)) \
				for x in xrange(0, _channels_count(self._device)) ]
		self.id = _d_get_id(self._device)
		self.name = _d_get_name(self._device)

	# TODO(pcercuei): Provide a dict-like interface for the attributes
	def read_attr(self, attr):
		buf = create_string_buffer(1024)
		_d_read_attr(self._device, attr, byref(buf), 1024)
		return buf.value

	def write_attr(self, attr, value):
		_d_write_attr(self._device, attr, value)

class Context(object):
	def __init__(self, _context):
		self._context = _context
		nb_devices = _devices_count(self._context)
		self.devices = [ Device(_get_device(self._context, x)) \
				for x in xrange(0, _devices_count(self._context)) ]
		self.name = _get_name(self._context)

	def __del__(self):
		_destroy(self._context)

class LocalContext(Context):
	def __init__(self):
		ctx = _new_local()
		super(LocalContext, self).__init__(ctx)

class XMLContext(Context):
	def __init__(self, xmlfile):
		ctx = _new_xml(xmlfile)
		super(XMLContext, self).__init__(ctx)

class NetworkContext(Context):
	def __init__(self, hostname):
		ctx = _new_network(hostname)
		super(NetworkContext, self).__init__(ctx)
