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

from ctypes import Structure, c_char_p, c_uint, c_int, c_size_t, \
		c_ssize_t, c_char, c_void_p, c_bool, create_string_buffer, \
		POINTER as _POINTER, CDLL as _cdll, memmove as _memmove, byref as _byref
from os import strerror as _strerror
from platform import system as _system
import weakref

if 'Windows' in _system():
	from ctypes import get_last_error
else:
	from ctypes import get_errno

def _checkNull(result, func, arguments):
	if result:
		return result
	else:
		err = get_last_error() if 'Windows' in _system() else get_errno()
		raise OSError(err, _strerror(err))

def _checkNegative(result, func, arguments):
	if result >= 0:
		return result
	else:
		raise OSError(-result, _strerror(-result))

class _ScanContext(Structure):
	pass
class _ContextInfo(Structure):
	pass
class _Context(Structure):
	pass
class _Device(Structure):
	pass
class _Channel(Structure):
	pass
class _Buffer(Structure):
	pass

_ScanContextPtr = _POINTER(_ScanContext)
_ContextInfoPtr = _POINTER(_ContextInfo)
_ContextPtr = _POINTER(_Context)
_DevicePtr = _POINTER(_Device)
_ChannelPtr = _POINTER(_Channel)
_BufferPtr = _POINTER(_Buffer)

_lib = _cdll('libiio.dll' if 'Windows' in _system() else 'libiio.so.0',
		use_errno = True, use_last_error = True)

_get_backends_count = _lib.iio_get_backends_count
_get_backends_count.restype = c_uint

_get_backend = _lib.iio_get_backend
_get_backend.argtypes = (c_uint, )
_get_backend.restype = c_char_p
_get_backend.errcheck = _checkNull

_create_scan_context = _lib.iio_create_scan_context
_create_scan_context.argtypes = (c_char_p, c_uint)
_create_scan_context.restype = _ScanContextPtr
_create_scan_context.errcheck = _checkNull

_destroy_scan_context = _lib.iio_scan_context_destroy
_destroy_scan_context.argtypes = (_ScanContextPtr, )

_get_context_info_list = _lib.iio_scan_context_get_info_list
_get_context_info_list.argtypes = (_ScanContextPtr, _POINTER(_POINTER(_ContextInfoPtr)))
_get_context_info_list.restype = c_ssize_t
_get_context_info_list.errcheck = _checkNegative

_context_info_list_free = _lib.iio_context_info_list_free
_context_info_list_free.argtypes = (_POINTER(_ContextInfoPtr), )

_context_info_get_description = _lib.iio_context_info_get_description
_context_info_get_description.argtypes = (_ContextInfoPtr, )
_context_info_get_description.restype = c_char_p

_context_info_get_uri = _lib.iio_context_info_get_uri
_context_info_get_uri.argtypes = (_ContextInfoPtr, )
_context_info_get_uri.restype = c_char_p

_new_local = _lib.iio_create_local_context
_new_local.restype = _ContextPtr
_new_local.errcheck = _checkNull

_new_xml = _lib.iio_create_xml_context
_new_xml.restype = _ContextPtr
_new_xml.argtypes = (c_char_p, )
_new_xml.errcheck = _checkNull

_new_network = _lib.iio_create_network_context
_new_network.restype = _ContextPtr
_new_network.argtypes = (c_char_p, )
_new_network.errcheck = _checkNull

_new_default = _lib.iio_create_default_context
_new_default.restype = _ContextPtr
_new_default.errcheck = _checkNull

_new_uri = _lib.iio_create_context_from_uri
_new_uri.restype = _ContextPtr
_new_uri.errcheck = _checkNull

_destroy = _lib.iio_context_destroy
_destroy.argtypes = (_ContextPtr, )

_get_name = _lib.iio_context_get_name
_get_name.restype = c_char_p
_get_name.argtypes = (_ContextPtr, )
_get_name.errcheck = _checkNull

_get_description = _lib.iio_context_get_description
_get_description.restype = c_char_p
_get_description.argtypes = (_ContextPtr, )

_get_xml = _lib.iio_context_get_xml
_get_xml.restype = c_char_p
_get_xml.argtypes = (_ContextPtr, )

_get_library_version = _lib.iio_library_get_version
_get_library_version.argtypes = (_POINTER(c_uint), _POINTER(c_uint), c_char_p, )

_get_version = _lib.iio_context_get_version
_get_version.restype = c_int
_get_version.argtypes = (_ContextPtr, _POINTER(c_uint), _POINTER(c_uint), c_char_p, )
_get_version.errcheck = _checkNegative

_get_attrs_count = _lib.iio_context_get_attrs_count
_get_attrs_count.restype = c_uint
_get_attrs_count.argtypes = (_ContextPtr, )

_get_attr = _lib.iio_context_get_attr
_get_attr.restype = c_int
_get_attr.argtypes = (_ContextPtr, c_uint, _POINTER(c_char_p), _POINTER(c_char_p))
_get_attr.errcheck = _checkNegative

_devices_count = _lib.iio_context_get_devices_count
_devices_count.restype = c_uint
_devices_count.argtypes = (_ContextPtr, )

_get_device = _lib.iio_context_get_device
_get_device.restype = _DevicePtr
_get_device.argtypes = (_ContextPtr, c_uint)
_get_device.errcheck = _checkNull

_set_timeout = _lib.iio_context_set_timeout
_set_timeout.restype = c_int
_set_timeout.argtypes = (_ContextPtr, c_uint, )
_set_timeout.errcheck = _checkNegative

_clone = _lib.iio_context_clone
_clone.restype = _ContextPtr
_clone.argtypes = (_ContextPtr, )
_clone.errcheck = _checkNull

_d_get_id = _lib.iio_device_get_id
_d_get_id.restype = c_char_p
_d_get_id.argtypes = (_DevicePtr, )
_d_get_id.errcheck = _checkNull

_d_get_name = _lib.iio_device_get_name
_d_get_name.restype = c_char_p
_d_get_name.argtypes = (_DevicePtr, )

_d_attr_count = _lib.iio_device_get_attrs_count
_d_attr_count.restype = c_uint
_d_attr_count.argtypes = (_DevicePtr, )

_d_get_attr = _lib.iio_device_get_attr
_d_get_attr.restype = c_char_p
_d_get_attr.argtypes = (_DevicePtr, )
_d_get_attr.errcheck = _checkNull

_d_read_attr = _lib.iio_device_attr_read
_d_read_attr.restype = c_ssize_t
_d_read_attr.argtypes = (_DevicePtr, c_char_p, c_char_p, c_size_t)
_d_read_attr.errcheck = _checkNegative

_d_write_attr = _lib.iio_device_attr_write
_d_write_attr.restype = c_ssize_t
_d_write_attr.argtypes = (_DevicePtr, c_char_p, c_char_p)
_d_write_attr.errcheck = _checkNegative

_d_debug_attr_count = _lib.iio_device_get_debug_attrs_count
_d_debug_attr_count.restype = c_uint
_d_debug_attr_count.argtypes = (_DevicePtr, )

_d_get_debug_attr = _lib.iio_device_get_debug_attr
_d_get_debug_attr.restype = c_char_p
_d_get_debug_attr.argtypes = (_DevicePtr, )
_d_get_debug_attr.errcheck = _checkNull

_d_read_debug_attr = _lib.iio_device_debug_attr_read
_d_read_debug_attr.restype = c_ssize_t
_d_read_debug_attr.argtypes = (_DevicePtr, c_char_p, c_char_p, c_size_t)
_d_read_debug_attr.errcheck = _checkNegative

_d_write_debug_attr = _lib.iio_device_debug_attr_write
_d_write_debug_attr.restype = c_ssize_t
_d_write_debug_attr.argtypes = (_DevicePtr, c_char_p, c_char_p)
_d_write_debug_attr.errcheck = _checkNegative

_d_reg_write = _lib.iio_device_reg_write
_d_reg_write.restype = c_int
_d_reg_write.argtypes = (_DevicePtr, c_uint, c_uint)
_d_reg_write.errcheck = _checkNegative

_d_reg_read = _lib.iio_device_reg_read
_d_reg_read.restype = c_int
_d_reg_read.argtypes = (_DevicePtr, c_uint, _POINTER(c_uint))
_d_reg_read.errcheck = _checkNegative

_channels_count = _lib.iio_device_get_channels_count
_channels_count.restype = c_uint
_channels_count.argtypes = (_DevicePtr, )

_get_channel = _lib.iio_device_get_channel
_get_channel.restype = _ChannelPtr
_get_channel.argtypes = (_DevicePtr, c_uint)
_get_channel.errcheck = _checkNull

_get_sample_size = _lib.iio_device_get_sample_size
_get_sample_size.restype = c_int
_get_sample_size.argtypes = (_DevicePtr, )
_get_sample_size.errcheck = _checkNegative

_d_is_trigger = _lib.iio_device_is_trigger
_d_is_trigger.restype = c_bool
_d_is_trigger.argtypes = (_DevicePtr, )

_d_get_trigger = _lib.iio_device_get_trigger
_d_get_trigger.restype = c_int
_d_get_trigger.argtypes = (_DevicePtr, _DevicePtr, )
_d_get_trigger.errcheck = _checkNegative

_d_set_trigger = _lib.iio_device_set_trigger
_d_set_trigger.restype = c_int
_d_set_trigger.argtypes = (_DevicePtr, _DevicePtr, )
_d_set_trigger.errcheck = _checkNegative

_d_set_buffers_count = _lib.iio_device_set_kernel_buffers_count
_d_set_buffers_count.restype = c_int
_d_set_buffers_count.argtypes = (_DevicePtr, c_uint)
_d_set_buffers_count.errcheck = _checkNegative

_c_get_id = _lib.iio_channel_get_id
_c_get_id.restype = c_char_p
_c_get_id.argtypes = (_ChannelPtr, )
_c_get_id.errcheck = _checkNull

_c_get_name = _lib.iio_channel_get_name
_c_get_name.restype = c_char_p
_c_get_name.argtypes = (_ChannelPtr, )

_c_is_output = _lib.iio_channel_is_output
_c_is_output.restype = c_bool
_c_is_output.argtypes = (_ChannelPtr, )

_c_is_scan_element = _lib.iio_channel_is_scan_element
_c_is_scan_element.restype = c_bool
_c_is_scan_element.argtypes = (_ChannelPtr, )

_c_attr_count = _lib.iio_channel_get_attrs_count
_c_attr_count.restype = c_uint
_c_attr_count.argtypes = (_ChannelPtr, )

_c_get_attr = _lib.iio_channel_get_attr
_c_get_attr.restype = c_char_p
_c_get_attr.argtypes = (_ChannelPtr, )
_c_get_attr.errcheck = _checkNull

_c_get_filename = _lib.iio_channel_attr_get_filename
_c_get_filename.restype = c_char_p
_c_get_filename.argtypes = (_ChannelPtr, c_char_p, )
_c_get_filename.errcheck = _checkNull

_c_read_attr = _lib.iio_channel_attr_read
_c_read_attr.restype = c_ssize_t
_c_read_attr.argtypes = (_ChannelPtr, c_char_p, c_char_p, c_size_t)
_c_read_attr.errcheck = _checkNegative

_c_write_attr = _lib.iio_channel_attr_write
_c_write_attr.restype = c_ssize_t
_c_write_attr.argtypes = (_ChannelPtr, c_char_p, c_char_p)
_c_write_attr.errcheck = _checkNegative

_c_enable = _lib.iio_channel_enable
_c_enable.argtypes = (_ChannelPtr, )

_c_disable = _lib.iio_channel_disable
_c_disable.argtypes = (_ChannelPtr, )

_c_is_enabled = _lib.iio_channel_is_enabled
_c_is_enabled.restype = c_bool
_c_is_enabled.argtypes = (_ChannelPtr, )

_c_read = _lib.iio_channel_read
_c_read.restype = c_ssize_t
_c_read.argtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_size_t, )

_c_read_raw = _lib.iio_channel_read_raw
_c_read_raw.restype = c_ssize_t
_c_read_raw.argtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_size_t, )

_c_write = _lib.iio_channel_write
_c_write.restype = c_ssize_t
_c_write.argtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_size_t, )

_c_write_raw = _lib.iio_channel_write_raw
_c_write_raw.restype = c_ssize_t
_c_write_raw.argtypes = (_ChannelPtr, _BufferPtr, c_void_p, c_size_t, )

_create_buffer = _lib.iio_device_create_buffer
_create_buffer.restype = _BufferPtr
_create_buffer.argtypes = (_DevicePtr, c_size_t, c_bool, )
_create_buffer.errcheck = _checkNull

_buffer_destroy = _lib.iio_buffer_destroy
_buffer_destroy.argtypes = (_BufferPtr, )

_buffer_refill = _lib.iio_buffer_refill
_buffer_refill.restype = c_ssize_t
_buffer_refill.argtypes = (_BufferPtr, )
_buffer_refill.errcheck = _checkNegative

_buffer_push_partial = _lib.iio_buffer_push_partial
_buffer_push_partial.restype = c_ssize_t
_buffer_push_partial.argtypes = (_BufferPtr, c_uint, )
_buffer_push_partial.errcheck = _checkNegative

_buffer_start = _lib.iio_buffer_start
_buffer_start.restype = c_void_p
_buffer_start.argtypes = (_BufferPtr, )

_buffer_end = _lib.iio_buffer_end
_buffer_end.restype = c_void_p
_buffer_end.argtypes = (_BufferPtr, )

def _get_lib_version():
	major = c_uint()
	minor = c_uint()
	buf = create_string_buffer(8)
	_get_library_version(_byref(major), _byref(minor), buf)
	return (major.value, minor.value, buf.value.decode('ascii') )

version = _get_lib_version()
backends = [ _get_backend(x).decode('ascii') for x in range(0, _get_backends_count()) ]

class _Attr(object):
	def __init__(self, name, filename = None):
		self._name = name
		self._name_ascii = name.encode('ascii')
		self._filename = name if filename is None else filename

	def __str__(self):
		return self._name

	name = property(lambda self: self._name, None, None,
			"The name of this attribute.\n\ttype=str")
	filename = property(lambda self: self._filename, None, None,
			"The filename in sysfs to which this attribute is bound.\n\ttype=str")
	value = property(lambda self: self.__read(), lambda self, x: self.__write(x),
			None, "Current value of this attribute.\n\ttype=str")

class ChannelAttr(_Attr):
	"""Represents an attribute of a channel."""

	def __init__(self, channel, name):
		super(ChannelAttr, self).__init__(name, _c_get_filename(channel, name.encode('ascii')).decode('ascii'))
		self._channel = channel

	def _Attr__read(self):
		buf = create_string_buffer(1024)
		_c_read_attr(self._channel, self._name_ascii, buf, len(buf))
		return buf.value.decode('ascii')

	def _Attr__write(self, value):
		_c_write_attr(self._channel, self._name_ascii, value.encode('ascii'))

class DeviceAttr(_Attr):
	"""Represents an attribute of an IIO device."""

	def __init__(self, device, name):
		super(DeviceAttr, self).__init__(name)
		self._device = device

	def _Attr__read(self):
		buf = create_string_buffer(1024)
		_d_read_attr(self._device, self._name_ascii, buf, len(buf))
		return buf.value.decode('ascii')

	def _Attr__write(self, value):
		_d_write_attr(self._device, self._name_ascii, value.encode('ascii'))

class DeviceDebugAttr(DeviceAttr):
	"""Represents a debug attribute of an IIO device."""

	def __init__(self, device, name):
		super(DeviceDebugAttr, self).__init__(device, name)

	def _Attr__read(self):
		buf = create_string_buffer(1024)
		_d_read_debug_attr(self._device, self._name_ascii, buf, len(buf))
		return buf.value.decode('ascii')

	def _Attr__write(self, value):
		_d_write_debug_attr(self._device, self._name_ascii, value.encode('ascii'))

class Channel(object):
	def __init__(self, _channel):
		self._channel = _channel
		self._attrs = { name : ChannelAttr(_channel, name) for name in \
				[_c_get_attr(_channel, x).decode('ascii') for x in range(0, _c_attr_count(_channel))] }
		self._id = _c_get_id(self._channel).decode('ascii')

		name_raw = _c_get_name(self._channel)
		self._name = name_raw.decode('ascii') if name_raw is not None else None
		self._output = _c_is_output(self._channel)
		self._scan_element = _c_is_scan_element(self._channel)

	def read(self, buf, raw = False):
		"""
		Extract the samples corresponding to this channel from the given iio.Buffer object.

		parameters:
			buf: type=iio.Buffer
				A valid instance of the iio.Buffer class
			raw: type=bool
				If set to True, the samples are not converted from their
				native format to their host format

		returns: type=bytearray
			An array containing the samples for this channel
		"""
		array = bytearray(buf._length)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		if raw:
			length = _c_read_raw(self._channel, buf._buffer, c_array, len(array))
		else:
			length = _c_read(self._channel, buf._buffer, c_array, len(array))
		return array[:length]

	def write(self, buf, array, raw = False):
		"""
		Write the specified array of samples corresponding to this channel into the given iio.Buffer object.

		parameters:
			buf: type=iio.Buffer
				A valid instance of the iio.Buffer class
			array: type=bytearray
				The array containing the samples to copy
			raw: type=bool
				If set to True, the samples are not converted from their
				host format to their native format

		returns: type=int
			The number of bytes written
		"""
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		if raw:
			return _c_write_raw(self._channel, buf._buffer, c_array, len(array))
		else:
			return _c_write(self._channel, buf._buffer, c_array, len(array))

	id = property(lambda self: self._id, None, None,
			"An identifier of this channel.\n\tNote that it is possible that two channels have the same ID, if one is an input channel and the other is an output channel.\n\ttype=str")
	name = property(lambda self: self._name, None, None,
			"The name of this channel.\n\ttype=str")
	attrs = property(lambda self: self._attrs, None, None,
			"List of attributes for this channel.\n\ttype=dict of iio.ChannelAttr")
	output = property(lambda self: self._output, None, None,
			"Contains True if the channel is an output channel, False otherwise.\n\ttype=bool")
	scan_element = property(lambda self: self._scan_element, None, None,
			"Contains True if the channel is a scan element, False otherwise.\n\tIf a channel is a scan element, then it is possible to enable it and use it for I/O operations.\n\ttype=bool")
	enabled = property(lambda self: _c_is_enabled(self._channel), \
			lambda self, x: _c_enable(self._channel) if x else _c_disable(self._channel),
			None, "Configured state of the channel\n\ttype=bool")

class Buffer(object):
	"""The class used for all I/O operations."""

	def __init__(self, device, samples_count, cyclic = False):
		"""
		Initializes a new instance of the Buffer class.

		parameters:
			device: type=iio.Device
				The iio.Device object that represents the device where the I/O
				operations will be performed
			samples_count: type=int
				The size of the buffer, in samples
			circular: type=bool
				If set to True, the buffer is circular

		returns: type=iio.Buffer
			An new instance of this class
		"""
		try:
			self._buffer = _create_buffer(device._device, samples_count, cyclic)
		except:
			self._buffer = None
			raise
		self._length = samples_count * device.sample_size
		self._samples_count = samples_count

	def __del__(self):
		if self._buffer is not None:
			_buffer_destroy(self._buffer)

	def __len__(self):
		"""The size of this buffer, in bytes."""
		return self._length

	def refill(self):
		"""Fetch a new set of samples from the hardware."""
		_buffer_refill(self._buffer)

	def push(self, samples_count = None):
		"""
		Submit the samples contained in this buffer to the hardware.

		parameters:
			samples_count: type=int
				The number of samples to submit, default = full buffer
		"""
		_buffer_push_partial(self._buffer, samples_count or self._samples_count)

	def read(self):
		"""
		Retrieve the samples contained inside the Buffer object.

		returns: type=bytearray
			An array containing the samples
		"""

		start = _buffer_start(self._buffer)
		end = _buffer_end(self._buffer)
		array = bytearray(end - start)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		_memmove(c_array, start, len(array))
		return array

	def write(self, array):
		"""
		Copy the given array of samples inside the Buffer object.

		parameters:
			array: type=bytearray
				The array containing the samples to copy

		returns: type=int
			The number of bytes written into the buffer
		"""
		start = _buffer_start(self._buffer)
		end = _buffer_end(self._buffer)
		length = end - start
		if length > len(array):
			length = len(array)
		mytype = c_char * len(array)
		c_array = mytype.from_buffer(array)
		_memmove(start, c_array, length)
		return length

class _DeviceOrTrigger(object):
	def __init__(self, _device):
		self._device = _device
		self._attrs = { name : DeviceAttr(_device, name) for name in \
				[_d_get_attr(_device, x).decode('ascii') for x in range(0, _d_attr_count(_device))] }
		self._debug_attrs = { name: DeviceDebugAttr(_device, name) for name in \
				[_d_get_debug_attr(_device, x).decode('ascii') for x in range(0, _d_debug_attr_count(_device))] }

		# TODO(pcercuei): Use a dictionary for the channels.
		chans = [ Channel(_get_channel(self._device, x))
			for x in range(0, _channels_count(self._device)) ]
		self._channels = sorted(chans, key=lambda c: c.id)
		self._id = _d_get_id(self._device).decode('ascii')

		name_raw = _d_get_name(self._device)
		self._name = name_raw.decode('ascii') if name_raw is not None else None

	def reg_write(self, reg, value):
		"""
		Set a value to one register of this device.

		parameters:
			reg: type=int
				The register address
			value: type=int
				The value that will be used for this register
		"""
		_d_reg_write(self._device, reg, value)

	def reg_read(self, reg):
		"""
		Read the content of a register of this device.

		parameters:
			reg: type=int
				The register address

		returns: type=int
			The value of the register
		"""
		value = c_uint()
		_d_reg_read(self._device, reg, _byref(value))
		return value.value

	def find_channel(self, name_or_id, is_output = False):
		"""

		Find a IIO channel by its name or ID.

		parameters:
			name_or_id: type=str
				The name or ID of the channel to find
			is_output: type=bool
				Set to True to search for an output channel

		returns: type=iio.Device or type=iio.Trigger
			The IIO Device
		"""
		return next((x for x in self.channels \
				if name_or_id == x.name or name_or_id == x.id and \
				x.output == is_output), None)

	def set_kernel_buffers_count(self, count):
		"""

		Set the number of kernel buffers to use with the specified device.

		parameters:
			count: type=int
				The number of kernel buffers

		"""
		return _d_set_buffers_count(self._device, count)

	@property
	def sample_size(self):
		"""
		Current sample size of this device.
		type: int

		The sample size varies each time channels get enabled or disabled."""
		return _get_sample_size(self._device)

	id = property(lambda self: self._id, None, None,
			"An identifier of this device, only valid in this IIO context.\n\ttype=str")
	name = property(lambda self: self._name, None, None,
			"The name of this device.\n\ttype=str")
	attrs = property(lambda self: self._attrs, None, None,
			"List of attributes for this IIO device.\n\ttype=dict of iio.DeviceAttr")
	debug_attrs = property(lambda self: self._debug_attrs, None, None,
			"List of debug attributes for this IIO device.\n\ttype=dict of iio.DeviceDebugAttr")
	channels = property(lambda self: self._channels, None, None,
			"List of channels available with this IIO device.\n\ttype=list of iio.Channel objects")

class Trigger(_DeviceOrTrigger):
	"""Contains the representation of an IIO device that can act as a trigger."""

	def __init__(self, _device):
		super(Trigger, self).__init__(_device)

	def _get_rate(self):
		return int(self._attrs['frequency'].value)

	def _set_rate(self, value):
		self._attrs['frequency'].value = str(value)

	frequency = property(_get_rate, _set_rate, None,
			"Configured frequency (in Hz) of this trigger\n\ttype=int")

class Device(_DeviceOrTrigger):
	"""Contains the representation of an IIO device."""

	def __init__(self, ctx, _device):
		super(Device, self).__init__(_device)
		self.ctx = weakref.ref(ctx)

	def _set_trigger(self, trigger):
		_d_set_trigger(self._device, trigger._device if trigger else None)

	def _get_trigger(self):
		value = _Device()
		_d_get_trigger(self._device, _byref(value))

		for dev in self.ctx()._devices:
			if value == dev._device:
				return dev
		return None

	trigger = property(_get_trigger, _set_trigger, None, \
			"Contains the configured trigger for this IIO device.\n\ttype=iio.Trigger")

class Context(object):
	"""Contains the representation of an IIO context."""

	def __init__(self, _context=None):
		"""
		Initializes a new instance of the Context class, using the local or the network backend of the IIO library.

		returns: type=iio.Context
			An new instance of this class

		This function will create a network context if the IIOD_REMOTE
		environment variable is set to the hostname where the IIOD server runs.
		If set to an empty string, the server will be discovered using ZeroConf.
		If the environment variable is not set, a local context will be created instead.
		"""
		self._context = None

		if(_context is None):
			self._context = _new_default()
		elif type(_context) is str or type(_context) is unicode:
			self._context = _new_uri(_context.encode('ascii'))
		else:
			self._context = _context

		self._attrs = {}
		for x in range(0, _get_attrs_count(self._context)):
			str1 = c_char_p()
			str2 = c_char_p()
			_get_attr(self._context, x, _byref(str1), _byref(str2))
			self._attrs[str1.value.decode('ascii')] = str2.value.decode('ascii')

		# TODO(pcercuei): Use a dictionary for the devices.
		self._devices = [ Trigger(dev) if _d_is_trigger(dev) else Device(self, dev) for dev in \
				[ _get_device(self._context, x) for x in range(0, _devices_count(self._context)) ]]
		self._name = _get_name(self._context).decode('ascii')
		self._description = _get_description(self._context).decode('ascii')
		self._xml = _get_xml(self._context).decode('ascii')

		major = c_uint()
		minor = c_uint()
		buf = create_string_buffer(8)
		_get_version(self._context, _byref(major), _byref(minor), buf)
		self._version = (major.value, minor.value, buf.value.decode('ascii') )

	def __del__(self):
		if(self._context is not None):
			_destroy(self._context)

	def set_timeout(self, timeout):
		"""
		Set a timeout for I/O operations.

		parameters:
			timeout: type=int
				The timeout value, in milliseconds
		"""
		_set_timeout(self._context, timeout)

	def clone(self):
		"""
		Clone this instance.

		returns: type=iio.LocalContext
			An new instance of this class
		"""
		return Context(_clone(self._context))

	def find_device(self, name_or_id):
		"""

		Find a IIO device by its name or ID.

		parameters:
			name_or_id: type=str
				The name or ID of the device to find

		returns: type=iio.Device or type=iio.Trigger
			The IIO Device
		"""
		return next((x for x in self.devices \
				if name_or_id == x.name or name_or_id == x.id), None)

	name = property(lambda self: self._name, None, None, \
			"Name of this IIO context.\n\ttype=str")
	description = property(lambda self: self._description, None, None, \
			"Description of this IIO context.\n\ttype=str")
	xml = property(lambda self: self._xml, None, None, \
			"XML representation of the current context.\n\ttype=str")
	version = property(lambda self: self._version, None, None, \
			"Version of the backend.\n\ttype=(int, int, str)")
	attrs = property(lambda self: self._attrs, None, None, \
			"List of context-specific attributes\n\ttype=dict of str objects")
	devices = property(lambda self: self._devices, None, None, \
			"List of devices contained in this context.\n\ttype=list of iio.Device and iio.Trigger objects")

class LocalContext(Context):
	def __init__(self):
		"""
		Initializes a new instance of the Context class, using the local backend of the IIO library.

		returns: type=iio.LocalContext
			An new instance of this class
		"""
		ctx = _new_local()
		super(LocalContext, self).__init__(ctx)

class XMLContext(Context):
	def __init__(self, xmlfile):
		"""
		Initializes a new instance of the Context class, using the XML backend of the IIO library.

		parameters:
			xmlfile: type=str
				Filename of the XML file to build the context from

		returns: type=iio.XMLContext
			An new instance of this class
		"""
		ctx = _new_xml(xmlfile.encode('ascii'))
		super(XMLContext, self).__init__(ctx)

class NetworkContext(Context):
	def __init__(self, hostname = None):
		"""
		Initializes a new instance of the Context class, using the network backend of the IIO library.

		parameters:
			hostname: type=str
				Hostname, IPv4 or IPv6 address where the IIO Daemon is running

		returns: type=iio.NetworkContext
			An new instance of this class
		"""
		ctx = _new_network(hostname.encode('ascii') if hostname is not None else None)
		super(NetworkContext, self).__init__(ctx)

def scan_contexts():
	d = dict()
	ptr = _POINTER(_ContextInfoPtr)()

	ctx = _create_scan_context(None, 0)
	nb = _get_context_info_list(ctx, _byref(ptr));

	for i in range(0, nb):
		d[_context_info_get_uri(ptr[i]).decode('ascii')] = _context_info_get_description(ptr[i]).decode('ascii')

	_context_info_list_free(ptr)
	_destroy_scan_context(ctx)
	return d
