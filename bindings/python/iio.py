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
		c_char, c_void_p, c_bool, create_string_buffer, byref, memmove
from os import strerror

def _checkNull(result, func, arguments):
	if result:
		return result
	else:
		raise Exception("Null pointer")

def _checkNegative(result, func, arguments):
	if result >= 0:
		return result
	else:
		raise Exception("Error: " + strerror(-result))

def _init():
	class _Context(Structure):
		pass
	class _Device(Structure):
		pass
	class _Channel(Structure):
		pass
	class _Buffer(Structure):
		pass

	ContextPtr = POINTER(_Context)
	DevicePtr = POINTER(_Device)
	ChannelPtr = POINTER(_Channel)
	BufferPtr = POINTER(_Buffer)

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

	global _new_default
	_new_default = lib.iio_create_default_context
	_new_default.restype = ContextPtr
	_new_default.archtypes = None
	_new_default.errcheck = _checkNull

	global _destroy
	_destroy = lib.iio_context_destroy
	_destroy.restype = None
	_destroy.archtypes = (ContextPtr, )

	global _get_name
	_get_name = lib.iio_context_get_name
	_get_name.restype = c_char_p
	_get_name.archtypes = (ContextPtr, )
	_get_name.errcheck = _checkNull

	global _get_version
	_get_version = lib.iio_context_get_version
	_get_version.restype = c_int
	_get_version.archtypes = (ContextPtr, c_uint, c_uint, c_char_p, )
	_get_version.errcheck = _checkNegative

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
	_d_read_attr.errcheck = _checkNegative

	global _d_write_attr
	_d_write_attr = lib.iio_device_attr_write
	_d_write_attr.restype = c_int
	_d_write_attr.archtypes = (DevicePtr, c_char_p, c_char_p)
	_d_write_attr.errcheck = _checkNegative

	global _d_reg_write
	_d_reg_write = lib.iio_device_reg_write
	_d_reg_write.restype = c_int
	_d_reg_write.archtypes = (DevicePtr, c_uint, c_uint)

	global _d_reg_read
	_d_reg_read = lib.iio_device_reg_read
	_d_reg_read.restype = c_int
	_d_reg_read.archtypes = (DevicePtr, c_uint, c_uint)

	global _channels_count
	_channels_count = lib.iio_device_get_channels_count
	_channels_count.restype = c_uint
	_channels_count.archtypes = (DevicePtr, )

	global _get_channel
	_get_channel = lib.iio_device_get_channel
	_get_channel.restype = ChannelPtr
	_get_channel.archtypes = (DevicePtr, c_uint)
	_get_channel.errcheck = _checkNull

	global _get_sample_size
	_get_sample_size = lib.iio_device_get_sample_size
	_get_sample_size.restype = c_int
	_get_sample_size.archtypes = (DevicePtr, )
	_get_sample_size.errcheck = _checkNegative

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

	global _c_get_filename
	_c_get_filename = lib.iio_channel_attr_get_filename
	_c_get_filename.restype = c_char_p
	_c_get_filename.archtypes = (ChannelPtr, c_char_p, )
	_c_get_filename.errcheck = _checkNull

	global _c_read_attr
	_c_read_attr = lib.iio_channel_attr_read
	_c_read_attr.restype = c_int
	_c_read_attr.archtypes = (ChannelPtr, c_char_p, c_char_p, c_uint)
	_c_read_attr.errcheck = _checkNegative

	global _c_write_attr
	_c_write_attr = lib.iio_channel_attr_write
	_c_write_attr.restype = c_int
	_c_write_attr.archtypes = (ChannelPtr, c_char_p, c_char_p)
	_c_write_attr.errcheck = _checkNegative

	global _c_enable
	_c_enable = lib.iio_channel_enable
	_c_enable.archtypes = (ChannelPtr, )

	global _c_disable
	_c_disable = lib.iio_channel_disable
	_c_disable.archtypes = (ChannelPtr, )

	global _c_is_enabled
	_c_is_enabled = lib.iio_channel_is_enabled
	_c_is_enabled.restype = c_bool
	_c_is_enabled.archtypes = (ChannelPtr, )

	global _c_read
	_c_read = lib.iio_channel_read
	_c_read.restype = c_uint
	_c_read.archtypes = (ChannelPtr, BufferPtr, c_void_p, c_uint, )

	global _c_read_raw
	_c_read_raw = lib.iio_channel_read_raw
	_c_read_raw.restype = c_uint
	_c_read_raw.archtypes = (ChannelPtr, BufferPtr, c_void_p, c_uint, )

	global _c_write
	_c_write = lib.iio_channel_write
	_c_write.restype = c_uint
	_c_write.archtypes = (ChannelPtr, BufferPtr, c_void_p, c_uint, )

	global _c_write_raw
	_c_write_raw = lib.iio_channel_write_raw
	_c_write_raw.restype = c_uint
	_c_write_raw.archtypes = (ChannelPtr, BufferPtr, c_void_p, c_uint, )

	global _create_buffer
	_create_buffer = lib.iio_device_create_buffer
	_create_buffer.restype = BufferPtr
	_create_buffer.archtypes = (DevicePtr, c_uint, c_bool, )
	_create_buffer.errcheck = _checkNull

	global _buffer_destroy
	_buffer_destroy = lib.iio_buffer_destroy
	_buffer_destroy.archtypes = (BufferPtr, )

	global _buffer_refill
	_buffer_refill = lib.iio_buffer_refill
	_buffer_refill.restype = c_int
	_buffer_refill.archtypes = (BufferPtr, )
	_buffer_refill.errcheck = _checkNegative

	global _buffer_push
	_buffer_push = lib.iio_buffer_push
	_buffer_push.restype = c_int
	_buffer_push.archtypes = (BufferPtr, )
	_buffer_push.errcheck = _checkNegative

	global _buffer_start
	_buffer_start = lib.iio_buffer_start
	_buffer_start.restype = c_void_p
	_buffer_start.archtypes = (BufferPtr, )

	global _buffer_end
	_buffer_end = lib.iio_buffer_end
	_buffer_end.restype = c_void_p
	_buffer_end.archtypes = (BufferPtr, )

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

	def get_filename(self, attr):
		return _c_get_filename(self._channel, attr)

	def read(self, buf, raw = False):
		array = bytearray(buf.length)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		if raw:
			length = _c_read_raw(self._channel, buf._buffer, c_array, len(array))
		else:
			length = _c_read(self._channel, buf._buffer, c_array, len(array))
		return array[:length]

	def write(self, buf, array, raw = False):
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		if raw:
			return _c_write_raw(self._channel, buf._buffer, c_array, len(array))
		else:
			return _c_write(self._channel, buf._buffer, c_array, len(array))

	def __enable(self, en):
		if en:
			_c_enable(self._channel)
		else:
			_c_disable(self._channel)

	def __is_enabled(self):
		return _c_is_enabled(self._channel)

	enabled = property(__is_enabled, __enable)

class Buffer(object):
	def __init__(self, device, samples_count, cyclic = False):
		self._buffer = _create_buffer(device._device, samples_count, cyclic)
		self.length = samples_count * device.sample_size

	def __del__(self):
		_buffer_destroy(self._buffer)

	def refill(self):
		_buffer_refill(self._buffer)

	def push(self):
		_buffer_push(self._buffer)

class Device(object):
	def __init__(self, _device):
		self._device = _device
		self.attrs = [ _d_get_attr(self._device, x) \
				for x in xrange(0, _d_attr_count(self._device)) ]
		self.channels = [ Channel(_get_channel(self._device, x)) \
				for x in xrange(0, _channels_count(self._device)) ]
		self.id = _d_get_id(self._device)
		self.name = _d_get_name(self._device)

	def read(self, buf):
		start = _buffer_start(buf._buffer)
		end = _buffer_end(buf._buffer)
		array = bytearray(end - start)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		memmove(c_array, start, len(array))
		return array

	def write(self, buf, array):
		start = _buffer_start(buf._buffer)
		end = _buffer_end(buf._buffer)
		length = end - start
		if length > len(array):
			length = len(array)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		memmove(start, c_array, length)
		return length

	# TODO(pcercuei): Provide a dict-like interface for the attributes
	def read_attr(self, attr):
		buf = create_string_buffer(1024)
		_d_read_attr(self._device, attr, byref(buf), 1024)
		return buf.value

	def write_attr(self, attr, value):
		_d_write_attr(self._device, attr, value)

	def reg_write(self, reg, value):
		_d_reg_write(self._device, reg, value)

	def reg_read(self, reg):
		value = c_uint()
		_d_reg_read(self._device, reg, byref(value))
		return value.value

	@property
	def sample_size(self):
		return _get_sample_size(self._device)

class Context(object):
	def __init__(self, _context = _new_default()):
		self._context = _context
		self.devices = [ Device(_get_device(self._context, x)) \
				for x in xrange(0, _devices_count(self._context)) ]
		self.name = _get_name(self._context)

		major = c_uint()
		minor = c_uint()
		buf = create_string_buffer(8)
		_get_version(self._context, byref(major), byref(minor), buf)
		self.version = (major.value, minor.value, buf.value )

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
	def __init__(self, hostname = None):
		ctx = _new_network(hostname)
		super(NetworkContext, self).__init__(ctx)
