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

class _Context(Structure):
	pass
class _Device(Structure):
	pass
class _Channel(Structure):
	pass
class _Buffer(Structure):
	pass

_ContextPtr = POINTER(_Context)
_DevicePtr = POINTER(_Device)
_ChannelPtr = POINTER(_Channel)
_BufferPtr = POINTER(_Buffer)

lib = cdll.LoadLibrary('libiio.so.0')

_new_local = lib.iio_create_local_context
_new_local.restype = _ContextPtr
_new_local.archtypes = None
_new_local.errcheck = _checkNull

_new_xml = lib.iio_create_xml_context
_new_xml.restype = _ContextPtr
_new_xml.archtypes = (c_char_p, )
_new_xml.errcheck = _checkNull

_new_network = lib.iio_create_network_context
_new_network.restype = _ContextPtr
_new_network.archtypes = (c_char_p, )
_new_network.errcheck = _checkNull

_new_default = lib.iio_create_default_context
_new_default.restype = _ContextPtr
_new_default.archtypes = None
_new_default.errcheck = _checkNull

_destroy = lib.iio_context_destroy
_destroy.restype = None
_destroy.archtypes = (_ContextPtr, )

_get_name = lib.iio_context_get_name
_get_name.restype = c_char_p
_get_name.archtypes = (_ContextPtr, )
_get_name.errcheck = _checkNull

_get_description = lib.iio_context_get_description
_get_description.restype = c_char_p
_get_description.archtypes = (_ContextPtr, )

_get_library_version = lib.iio_library_get_version
_get_library_version.restype = c_int
_get_library_version.archtypes = (c_uint, c_uint, c_char_p, )
_get_library_version.errcheck = _checkNegative

_get_version = lib.iio_context_get_version
_get_version.restype = c_int
_get_version.archtypes = (_ContextPtr, c_uint, c_uint, c_char_p, )
_get_version.errcheck = _checkNegative

_devices_count = lib.iio_context_get_devices_count
_devices_count.restype = c_uint
_devices_count.archtypes = (_ContextPtr, )

_get_device = lib.iio_context_get_device
_get_device.restype = _DevicePtr
_get_device.archtypes = (_ContextPtr, c_uint)
_get_device.errcheck = _checkNull

_set_timeout = lib.iio_context_set_timeout
_set_timeout.restype = c_int
_set_timeout.archtypes = (_ContextPtr, c_uint, )
_set_timeout.errcheck = _checkNegative

_clone = lib.iio_context_clone
_clone.restype = _ContextPtr
_clone.archtypes = (_ContextPtr, )
_clone.errcheck = _checkNull

_d_get_id = lib.iio_device_get_id
_d_get_id.restype = c_char_p
_d_get_id.archtypes = (_DevicePtr, )
_d_get_id.errcheck = _checkNull

_d_get_name = lib.iio_device_get_name
_d_get_name.restype = c_char_p
_d_get_name.archtypes = (_DevicePtr, )

_d_attr_count = lib.iio_device_get_attrs_count
_d_attr_count.restype = c_uint
_d_attr_count.archtypes = (_DevicePtr, )

_d_get_attr = lib.iio_device_get_attr
_d_get_attr.restype = c_char_p
_d_get_attr.archtypes = (_DevicePtr, )
_d_get_attr.errcheck = _checkNull

_d_read_attr = lib.iio_device_attr_read
_d_read_attr.restype = c_int
_d_read_attr.archtypes = (_DevicePtr, c_char_p, c_char_p, c_uint)
_d_read_attr.errcheck = _checkNegative

_d_write_attr = lib.iio_device_attr_write
_d_write_attr.restype = c_int
_d_write_attr.archtypes = (_DevicePtr, c_char_p, c_char_p)
_d_write_attr.errcheck = _checkNegative

_d_reg_write = lib.iio_device_reg_write
_d_reg_write.restype = c_int
_d_reg_write.archtypes = (_DevicePtr, c_uint, c_uint)

_d_reg_read = lib.iio_device_reg_read
_d_reg_read.restype = c_int
_d_reg_read.archtypes = (_DevicePtr, c_uint, c_uint)

_channels_count = lib.iio_device_get_channels_count
_channels_count.restype = c_uint
_channels_count.archtypes = (_DevicePtr, )

_get_channel = lib.iio_device_get_channel
_get_channel.restype = _ChannelPtr
_get_channel.archtypes = (_DevicePtr, c_uint)
_get_channel.errcheck = _checkNull

_get_sample_size = lib.iio_device_get_sample_size
_get_sample_size.restype = c_int
_get_sample_size.archtypes = (_DevicePtr, )
_get_sample_size.errcheck = _checkNegative

_c_get_id = lib.iio_channel_get_id
_c_get_id.restype = c_char_p
_c_get_id.archtypes = (_ChannelPtr, )
_c_get_id.errcheck = _checkNull

_c_get_name = lib.iio_channel_get_name
_c_get_name.restype = c_char_p
_c_get_name.archtypes = (_ChannelPtr, )

_c_is_output = lib.iio_channel_is_output
_c_is_output.restype = c_bool
_c_is_output.archtypes = (_ChannelPtr, )

_c_attr_count = lib.iio_channel_get_attrs_count
_c_attr_count.restype = c_uint
_c_attr_count.archtypes = (_ChannelPtr, )

_c_get_attr = lib.iio_channel_get_attr
_c_get_attr.restype = c_char_p
_c_get_attr.archtypes = (_ChannelPtr, )
_c_get_attr.errcheck = _checkNull

_c_get_filename = lib.iio_channel_attr_get_filename
_c_get_filename.restype = c_char_p
_c_get_filename.archtypes = (_ChannelPtr, c_char_p, )
_c_get_filename.errcheck = _checkNull

_c_read_attr = lib.iio_channel_attr_read
_c_read_attr.restype = c_int
_c_read_attr.archtypes = (_ChannelPtr, c_char_p, c_char_p, c_uint)
_c_read_attr.errcheck = _checkNegative

_c_write_attr = lib.iio_channel_attr_write
_c_write_attr.restype = c_int
_c_write_attr.archtypes = (_ChannelPtr, c_char_p, c_char_p)
_c_write_attr.errcheck = _checkNegative

_c_enable = lib.iio_channel_enable
_c_enable.archtypes = (_ChannelPtr, )

_c_disable = lib.iio_channel_disable
_c_disable.archtypes = (_ChannelPtr, )

_c_is_enabled = lib.iio_channel_is_enabled
_c_is_enabled.restype = c_bool
_c_is_enabled.archtypes = (_ChannelPtr, )

_c_read = lib.iio_channel_read
_c_read.restype = c_uint
_c_read.archtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_uint, )

_c_read_raw = lib.iio_channel_read_raw
_c_read_raw.restype = c_uint
_c_read_raw.archtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_uint, )

_c_write = lib.iio_channel_write
_c_write.restype = c_uint
_c_write.archtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_uint, )

_c_write_raw = lib.iio_channel_write_raw
_c_write_raw.restype = c_uint
_c_write_raw.archtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_uint, )

_create_buffer = lib.iio_device_create_buffer
_create_buffer.restype = _BufferPtr
_create_buffer.archtypes = (_DevicePtr, c_uint, c_bool, )
_create_buffer.errcheck = _checkNull

_buffer_destroy = lib.iio_buffer_destroy
_buffer_destroy.archtypes = (_BufferPtr, )

_buffer_refill = lib.iio_buffer_refill
_buffer_refill.restype = c_int
_buffer_refill.archtypes = (_BufferPtr, )
_buffer_refill.errcheck = _checkNegative

_buffer_push = lib.iio_buffer_push
_buffer_push.restype = c_int
_buffer_push.archtypes = (_BufferPtr, )
_buffer_push.errcheck = _checkNegative

_buffer_start = lib.iio_buffer_start
_buffer_start.restype = c_void_p
_buffer_start.archtypes = (_BufferPtr, )

_buffer_end = lib.iio_buffer_end
_buffer_end.restype = c_void_p
_buffer_end.archtypes = (_BufferPtr, )

def _get_lib_version():
	major = c_uint()
	minor = c_uint()
	buf = create_string_buffer(8)
	_get_library_version(byref(major), byref(minor), buf)
	return (major.value, minor.value, buf.value )

version = _get_lib_version()

class Attr(object):
	def __init__(self, name, filename = None):
		self.name = name
		self.filename = name if filename is None else filename

	def __str__(self):
		return self.name

	value = property(lambda self: self.__read(), lambda self, x: self.__write(x))

class ChannelAttr(Attr):
	def __init__(self, channel, name):
		super(ChannelAttr, self).__init__(name, _c_get_filename(channel, name))
		self._channel = channel

	def _Attr__read(self):
		buf = create_string_buffer(1024)
		_c_read_attr(self._channel, self.name, byref(buf), 1024)
		return buf.value

	def _Attr__write(self, value):
		_c_write_attr(self._channel, self.name, value)

class DeviceAttr(Attr):
	def __init__(self, device, name):
		super(DeviceAttr, self).__init__(name)
		self._device = device

	def _Attr__read(self):
		buf = create_string_buffer(1024)
		_d_read_attr(self._device, self.name, byref(buf), 1024)
		return buf.value

	def _Attr__write(self, value):
		_d_write_attr(self._device, self.name, value)

class Channel(object):
	def __init__(self, dev, _channel):
		self.dev = dev
		self._channel = _channel
		self.attrs = { name : ChannelAttr(_channel, name) for name in \
				[_c_get_attr(_channel, x) for x in xrange(0, _c_attr_count(_channel))] }
		self.id = _c_get_id(self._channel)
		self.name = _c_get_name(self._channel)
		self.output = _c_is_output(self._channel)

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

	enabled = property(lambda self: _c_is_enabled(self._channel), \
			lambda self, x: _c_enable(self._channel) if x else _c_disable(self._channel))

class Buffer(object):
	def __init__(self, device, samples_count, cyclic = False):
		self.dev = device
		self._buffer = _create_buffer(device._device, samples_count, cyclic)
		self.length = samples_count * device.sample_size

	def __del__(self):
		_buffer_destroy(self._buffer)

	def refill(self):
		_buffer_refill(self._buffer)

	def push(self):
		_buffer_push(self._buffer)

	def read(self):
		start = _buffer_start(self._buffer)
		end = _buffer_end(self._buffer)
		array = bytearray(end - start)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		memmove(c_array, start, len(array))
		return array

	def write(self, array):
		start = _buffer_start(self._buffer)
		end = _buffer_end(self._buffer)
		length = end - start
		if length > len(array):
			length = len(array)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		memmove(start, c_array, length)
		return length

class Device(object):
	def __init__(self, ctx, _device):
		self.ctx = ctx
		self._device = _device
		self.attrs = { name : DeviceAttr(_device, name) for name in \
				[_d_get_attr(_device, x) for x in xrange(0, _d_attr_count(_device))] }
		self.channels = [ Channel(self, _get_channel(self._device, x)) \
				for x in xrange(0, _channels_count(self._device)) ]
		self.id = _d_get_id(self._device)
		self.name = _d_get_name(self._device)

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
	def __init__(self, _context=None):
		if(_context is None):
			self._context = _new_default()
		else:
			self._context = _context
		self.devices = [ Device(self, _get_device(self._context, x)) \
				for x in xrange(0, _devices_count(self._context)) ]
		self.name = _get_name(self._context)
		self.description = _get_description(self._context)

		major = c_uint()
		minor = c_uint()
		buf = create_string_buffer(8)
		_get_version(self._context, byref(major), byref(minor), buf)
		self.version = (major.value, minor.value, buf.value )

	def __del__(self):
		if(self._context is not None):
			_destroy(self._context)

	def set_timeout(self, timeout):
		_set_timeout(self._context, timeout)

	def clone(self):
		return Context(_clone(self._context))

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
