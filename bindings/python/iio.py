#!/usr/bin/env python
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
SPDX-License-Identifier: LGPL-2.1-or-later

Copyright (C) 2014 Analog Devices, Inc.
Author: Paul Cercueil <paul.cercueil@analog.com>
"""

# Imports from package ctypes are not grouped
# pylint: disable=ungrouped-imports
#
# The way the methods and classes are used, violate this, but there
#   isn't a good way to do things otherwise
# pylint: disable=protected-access
from ctypes import (
    Structure,
    c_char_p,
    c_uint,
    c_int,
    c_long,
    c_longlong,
    c_size_t,
    c_ssize_t,
    c_char,
    c_void_p,
    c_bool,
    create_string_buffer,
    c_double,
    cast,
    sizeof,
    POINTER as _POINTER,
    CDLL as _cdll,
    memmove as _memmove,
    byref as _byref,
)
from ctypes.util import find_library
from enum import Enum
from os import strerror as _strerror
from platform import system as _system
import abc

if "Windows" in _system():
    from ctypes import get_last_error
else:
    from ctypes import get_errno
# pylint: enable=ungrouped-imports

# ctypes requires the errcheck to take three arguments, but we don't use them
# pylint: disable=unused-argument


def _check_null(result, func, arguments):
    if result:
        return result
    err = get_last_error() if "Windows" in _system() else get_errno()
    raise OSError(err, _strerror(err))


def _check_negative(result, func, arguments):
    if result >= 0:
        return result
    raise OSError(-result, _strerror(-result))


def _check_ptr_err(result, func, arguments):
    value = cast(result, c_void_p).value
    value = 2 ** (8 * sizeof(c_void_p)) - value

    if value < 4096:
        raise OSError(value, _strerror(value))

    return result


# pylint: enable=unused-argument

# Python 2 and Python 3 compatible _isstring function.
# pylint: disable=basestring-builtin
def _isstring(argument):
    try:
        # attempt to evaluate basestring (Python 2)
        return isinstance(argument, basestring)
    except NameError:
        # No basestring --> Python 3
        return isinstance(argument, str)


# pylint: enable=basestring-builtin


class _Scan(Structure):
    pass


class _Context(Structure):
    pass


class _Device(Structure):
    pass


class _Channel(Structure):
    pass


class _ChannelsMask(Structure):
    pass


class _Buffer(Structure):
    pass

class _Block(Structure):
    pass

class _Stream(Structure):
    pass

class _Attr(Structure):
    pass

class ContextParams(Structure):
    pass # TODO

class DataFormat(Structure):
    """Represents the data format of an IIO channel."""

    _fields_ = [
        ("length", c_uint),
        ("bits", c_uint),
        ("shift", c_uint),
        ("is_signed", c_bool),
        ("is_fully_defined", c_bool),
        ("is_be", c_bool),
        ("with_scale", c_bool),
        ("scale", c_double),
        ("repeat", c_uint),
        ("offset", c_double),
    ]


class ChannelModifier(Enum):
    """Contains the modifier type of an IIO channel."""

    IIO_NO_MOD = 0
    IIO_MOD_X = 1
    IIO_MOD_Y = 2
    IIO_MOD_Z = 3
    IIO_MOD_X_AND_Y = 4
    IIO_MOD_X_AND_Z = 5
    IIO_MOD_Y_AND_Z = 6
    IIO_MOD_X_AND_Y_AND_Z = 7
    IIO_MOD_X_OR_Y = 8
    IIO_MOD_X_OR_Z = 9
    IIO_MOD_Y_OR_Z = 10
    IIO_MOD_X_OR_Y_OR_Z = 11
    IIO_MOD_LIGHT_BOTH = 12
    IIO_MOD_LIGHT_IR = 13
    IIO_MOD_ROOT_SUM_SQUARED_X_Y = 14
    IIO_MOD_SUM_SQUARED_X_Y_Z = 15
    IIO_MOD_LIGHT_CLEAR = 16
    IIO_MOD_LIGHT_RED = 17
    IIO_MOD_LIGHT_GREEN = 18
    IIO_MOD_LIGHT_BLUE = 19
    IIO_MOD_QUATERNION = 20
    IIO_MOD_TEMP_AMBIENT = 21
    IIO_MOD_TEMP_OBJECT = 22
    IIO_MOD_NORTH_MAGN = 23
    IIO_MOD_NORTH_TRUE = 24
    IIO_MOD_NORTH_MAGN_TILT_COMP = 25
    IIO_MOD_NORTH_TRUE_TILT_COMP = 26
    IIO_MOD_RUNNING = 27
    IIO_MOD_JOGGING = 28
    IIO_MOD_WALKING = 29
    IIO_MOD_STILL = 30
    IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z = 31
    IIO_MOD_I = 32
    IIO_MOD_Q = 33
    IIO_MOD_CO2 = 34
    IIO_MOD_VOC = 35
    IIO_MOD_LIGHT_UV = 36
    IIO_MOD_LIGHT_DUV = 37
    IIO_MOD_PM1 = 38
    IIO_MOD_PM2P5 = 39
    IIO_MOD_PM4 = 40
    IIO_MOD_PM10 = 41
    IIO_MOD_ETHANOL = 42
    IIO_MOD_H2 = 43
    IIO_MOD_O2 = 44


class ChannelType(Enum):
    """Contains the type of an IIO channel."""

    IIO_VOLTAGE = 0
    IIO_CURRENT = 1
    IIO_POWER = 2
    IIO_ACCEL = 3
    IIO_ANGL_VEL = 4
    IIO_MAGN = 5
    IIO_LIGHT = 6
    IIO_INTENSITY = 7
    IIO_PROXIMITY = 8
    IIO_TEMP = 9
    IIO_INCLI = 10
    IIO_ROT = 11
    IIO_ANGL = 12
    IIO_TIMESTAMP = 13
    IIO_CAPACITANCE = 14
    IIO_ALTVOLTAGE = 15
    IIO_CCT = 16
    IIO_PRESSURE = 17
    IIO_HUMIDITYRELATIVE = 18
    IIO_ACTIVITY = 19
    IIO_STEPS = 20
    IIO_ENERGY = 21
    IIO_DISTANCE = 22
    IIO_VELOCITY = 23
    IIO_CONCENTRATION = 24
    IIO_RESISTANCE = 25
    IIO_PH = 26
    IIO_UVINDEX = 27
    IIO_ELECTRICALCONDUCTIVITY = 28
    IIO_COUNT = 29
    IIO_INDEX = 30
    IIO_GRAVITY = 31
    IIO_POSITIONRELATIVE = 32
    IIO_PHASE = 33
    IIO_MASSCONCENTRATION = 34
    IIO_CHAN_TYPE_UNKNOWN = 0x7FFFFFFF


# pylint: disable=invalid-name
_ScanPtr = _POINTER(_Scan)
_ContextPtr = _POINTER(_Context)
_DevicePtr = _POINTER(_Device)
_ChannelPtr = _POINTER(_Channel)
_ChannelsMaskPtr = _POINTER(_ChannelsMask)
_BufferPtr = _POINTER(_Buffer)
_BlockPtr = _POINTER(_Block)
_DataFormatPtr = _POINTER(DataFormat)
_ContextParamsPtr = _POINTER(ContextParams)
_StreamPtr = _POINTER(_Stream)
_AttrPtr = _POINTER(_Attr)

if "Windows" in _system():
    _iiolib = "libiio.dll"
else:
    # Non-windows, possibly Posix system
    _iiolib = "iio"

_lib = _cdll("libiio.so.1", use_errno=True, use_last_error=True)

_get_backends_count = _lib.iio_get_builtin_backends_count
_get_backends_count.restype = c_uint

_get_backend = _lib.iio_get_builtin_backend
_get_backend.argtypes = (c_uint,)
_get_backend.restype = c_char_p
_get_backend.errcheck = _check_null

_scan = _lib.iio_scan
_scan.argtypes = (_ContextParamsPtr, c_char_p)
_scan.restype = _ScanPtr
_scan.errcheck = _check_ptr_err

_scan_destroy = _lib.iio_scan_destroy
_scan_destroy.argtypes = (_ScanPtr,)

_scan_get_results_count = _lib.iio_scan_get_results_count
_scan_get_results_count.argtypes = (_ScanPtr,)
_scan_get_results_count.restype = c_size_t

_scan_get_description = _lib.iio_scan_get_description
_scan_get_description.argtypes = (_ScanPtr, c_size_t)
_scan_get_description.restype = c_char_p
_scan_get_description.errcheck = _check_null

_scan_get_uri = _lib.iio_scan_get_uri
_scan_get_uri.argtypes = (_ScanPtr, c_size_t)
_scan_get_uri.restype = c_char_p
_scan_get_uri.errcheck = _check_null

_iio_has_backend = _lib.iio_has_backend
_iio_has_backend.argtypes = (c_char_p,)
_iio_has_backend.restype = c_bool

_iio_strerror = _lib.iio_strerror
_iio_strerror.argtypes = (c_int, c_char_p, c_uint)
_iio_strerror.restype = None

_new_ctx = _lib.iio_create_context
_new_ctx.restype = _ContextPtr
_new_ctx.errcheck = _check_ptr_err
_new_ctx.argtypes = (_ContextParamsPtr, c_char_p)

_destroy = _lib.iio_context_destroy
_destroy.argtypes = (_ContextPtr,)

_get_name = _lib.iio_context_get_name
_get_name.restype = c_char_p
_get_name.argtypes = (_ContextPtr,)
_get_name.errcheck = _check_null

_get_description = _lib.iio_context_get_description
_get_description.restype = c_char_p
_get_description.argtypes = (_ContextPtr,)

_get_xml = _lib.iio_context_get_xml
_get_xml.restype = c_char_p
_get_xml.argtypes = (_ContextPtr,)

_get_version_major = _lib.iio_context_get_version_major
_get_version_major.restype = c_uint
_get_version_major.argtypes = (_ContextPtr,)

_get_version_minor = _lib.iio_context_get_version_minor
_get_version_minor.restype = c_uint
_get_version_minor.argtypes = (_ContextPtr,)

_get_version_tag = _lib.iio_context_get_version_tag
_get_version_tag.restype = c_char_p
_get_version_tag.argtypes = (_ContextPtr,)

_get_attrs_count = _lib.iio_context_get_attrs_count
_get_attrs_count.restype = c_uint
_get_attrs_count.argtypes = (_ContextPtr,)

_get_attr = _lib.iio_context_get_attr
_get_attr.restype = _AttrPtr
_get_attr.argtypes = (_ContextPtr, c_uint)
_get_attr.errcheck = _check_null

_devices_count = _lib.iio_context_get_devices_count
_devices_count.restype = c_uint
_devices_count.argtypes = (_ContextPtr,)

_get_device = _lib.iio_context_get_device
_get_device.restype = _DevicePtr
_get_device.argtypes = (_ContextPtr, c_uint)
_get_device.errcheck = _check_null

_find_device = _lib.iio_context_find_device
_find_device.restype = _DevicePtr
_find_device.argtypes = (_ContextPtr, c_char_p)
_find_device.errcheck = _check_null

_set_timeout = _lib.iio_context_set_timeout
_set_timeout.restype = c_int
_set_timeout.argtypes = (
    _ContextPtr,
    c_uint,
)
_set_timeout.errcheck = _check_negative

_a_get_name = _lib.iio_attr_get_name
_a_get_name.restype = c_char_p
_a_get_name.argtypes = (_AttrPtr,)

_a_get_fname = _lib.iio_attr_get_filename
_a_get_fname.restype = c_char_p
_a_get_fname.argtypes = (_AttrPtr,)

_a_get_value = _lib.iio_attr_get_static_value
_a_get_value.restype = c_char_p
_a_get_value.argtypes = (_AttrPtr,)
_a_get_value.errcheck = _check_null

_a_read = _lib.iio_attr_read_raw
_a_read.restype = c_ssize_t
_a_read.argtypes = (_AttrPtr, c_char_p, c_size_t)
_a_read.errcheck = _check_negative

_a_write = _lib.iio_attr_write_string
_a_write.restype = c_ssize_t
_a_write.argtypes = (_AttrPtr, c_char_p)
_a_write.errcheck = _check_negative

_d_get_id = _lib.iio_device_get_id
_d_get_id.restype = c_char_p
_d_get_id.argtypes = (_DevicePtr,)
_d_get_id.errcheck = _check_null

_d_get_name = _lib.iio_device_get_name
_d_get_name.restype = c_char_p
_d_get_name.argtypes = (_DevicePtr,)

_d_get_label = _lib.iio_device_get_label
_d_get_label.restype = c_char_p
_d_get_label.argtypes = (_DevicePtr,)

_d_attr_count = _lib.iio_device_get_attrs_count
_d_attr_count.restype = c_uint
_d_attr_count.argtypes = (_DevicePtr,)

_d_get_attr = _lib.iio_device_get_attr
_d_get_attr.restype = _AttrPtr
_d_get_attr.argtypes = (_DevicePtr, c_uint)
_d_get_attr.errcheck = _check_null

_d_debug_attr_count = _lib.iio_device_get_debug_attrs_count
_d_debug_attr_count.restype = c_uint
_d_debug_attr_count.argtypes = (_DevicePtr,)

_d_get_debug_attr = _lib.iio_device_get_debug_attr
_d_get_debug_attr.restype = _AttrPtr
_d_get_debug_attr.argtypes = (_DevicePtr, c_uint)
_d_get_debug_attr.errcheck = _check_null

_b_attr_count = _lib.iio_buffer_get_attrs_count
_b_attr_count.restype = c_uint
_b_attr_count.argtypes = (_BufferPtr,)

_b_get_attr = _lib.iio_buffer_get_attr
_b_get_attr.restype = _AttrPtr
_b_get_attr.argtypes = (_BufferPtr, c_uint)
_b_get_attr.errcheck = _check_null

_d_get_context = _lib.iio_device_get_context
_d_get_context.restype = _ContextPtr
_d_get_context.argtypes = (_DevicePtr,)

_d_find_channel = _lib.iio_device_find_channel
_d_find_channel.restype = _ChannelPtr
_d_find_channel.argtypes = (_DevicePtr, c_char_p, c_bool)
_d_find_channel.errcheck = _check_null

_d_reg_write = _lib.iio_device_reg_write
_d_reg_write.restype = c_int
_d_reg_write.argtypes = (_DevicePtr, c_uint, c_uint)
_d_reg_write.errcheck = _check_negative

_d_reg_read = _lib.iio_device_reg_read
_d_reg_read.restype = c_int
_d_reg_read.argtypes = (_DevicePtr, c_uint, _POINTER(c_uint))
_d_reg_read.errcheck = _check_negative

_channels_count = _lib.iio_device_get_channels_count
_channels_count.restype = c_uint
_channels_count.argtypes = (_DevicePtr,)

_get_channel = _lib.iio_device_get_channel
_get_channel.restype = _ChannelPtr
_get_channel.argtypes = (_DevicePtr, c_uint)
_get_channel.errcheck = _check_null

_get_sample_size = _lib.iio_device_get_sample_size
_get_sample_size.restype = c_int
_get_sample_size.argtypes = (_DevicePtr, _ChannelsMaskPtr)
_get_sample_size.errcheck = _check_negative

_d_is_trigger = _lib.iio_device_is_trigger
_d_is_trigger.restype = c_bool
_d_is_trigger.argtypes = (_DevicePtr,)

_d_get_trigger = _lib.iio_device_get_trigger
_d_get_trigger.restype = c_int
_d_get_trigger.argtypes = (
    _DevicePtr,
    _POINTER(_DevicePtr),
)
_d_get_trigger.errcheck = _check_negative

_d_set_trigger = _lib.iio_device_set_trigger
_d_set_trigger.restype = c_int
_d_set_trigger.argtypes = (
    _DevicePtr,
    _DevicePtr,
)
_d_set_trigger.errcheck = _check_negative

_c_get_id = _lib.iio_channel_get_id
_c_get_id.restype = c_char_p
_c_get_id.argtypes = (_ChannelPtr,)
_c_get_id.errcheck = _check_null

_c_get_name = _lib.iio_channel_get_name
_c_get_name.restype = c_char_p
_c_get_name.argtypes = (_ChannelPtr,)

_c_is_output = _lib.iio_channel_is_output
_c_is_output.restype = c_bool
_c_is_output.argtypes = (_ChannelPtr,)

_c_is_scan_element = _lib.iio_channel_is_scan_element
_c_is_scan_element.restype = c_bool
_c_is_scan_element.argtypes = (_ChannelPtr,)

_c_attr_count = _lib.iio_channel_get_attrs_count
_c_attr_count.restype = c_uint
_c_attr_count.argtypes = (_ChannelPtr,)

_c_get_attr = _lib.iio_channel_get_attr
_c_get_attr.restype = _AttrPtr
_c_get_attr.argtypes = (_ChannelPtr, c_uint)
_c_get_attr.errcheck = _check_null

_create_channels_mask = _lib.iio_create_channels_mask
_create_channels_mask.argtypes = (c_uint,)
_create_channels_mask.restype = _ChannelsMaskPtr
_create_channels_mask.errcheck = _check_null

_channels_mask_destroy = _lib.iio_channels_mask_destroy
_channels_mask_destroy.argtypes = (_ChannelsMaskPtr,)

_c_enable = _lib.iio_channel_enable
_c_enable.argtypes = (_ChannelPtr, _ChannelsMaskPtr)

_c_disable = _lib.iio_channel_disable
_c_disable.argtypes = (_ChannelPtr, _ChannelsMaskPtr)

_c_is_enabled = _lib.iio_channel_is_enabled
_c_is_enabled.restype = c_bool
_c_is_enabled.argtypes = (_ChannelPtr,)

_c_read = _lib.iio_channel_read
_c_read.restype = c_ssize_t
_c_read.argtypes = (
    _ChannelPtr,
    _BufferPtr,
    c_void_p,
    c_size_t,
    c_bool,
)

_c_write = _lib.iio_channel_write
_c_write.restype = c_ssize_t
_c_write.argtypes = (
    _ChannelPtr,
    _BufferPtr,
    c_void_p,
    c_size_t,
    c_bool,
)

_channel_get_device = _lib.iio_channel_get_device
_channel_get_device.restype = _DevicePtr
_channel_get_device.argtypes = (_ChannelPtr,)

_channel_get_index = _lib.iio_channel_get_index
_channel_get_index.restype = c_long
_channel_get_index.argtypes = (_ChannelPtr,)

_channel_get_data_format = _lib.iio_channel_get_data_format
_channel_get_data_format.restype = _DataFormatPtr
_channel_get_data_format.argtypes = (_ChannelPtr,)

_channel_get_modifier = _lib.iio_channel_get_modifier
_channel_get_modifier.restype = c_int
_channel_get_modifier.argtypes = (_ChannelPtr,)

_channel_get_type = _lib.iio_channel_get_type
_channel_get_type.restype = c_int
_channel_get_type.argtypes = (_ChannelPtr,)

_create_buffer = _lib.iio_device_create_buffer
_create_buffer.restype = _BufferPtr
_create_buffer.argtypes = (
    _DevicePtr,
    c_uint,
    _ChannelsMaskPtr,
)
_create_buffer.errcheck = _check_ptr_err

_buffer_destroy = _lib.iio_buffer_destroy
_buffer_destroy.argtypes = (_BufferPtr,)

_buffer_enable = _lib.iio_buffer_enable
_buffer_enable.restype = c_int
_buffer_enable.argtypes = (_BufferPtr,)
_buffer_enable.errcheck = _check_negative

_buffer_disable = _lib.iio_buffer_disable
_buffer_disable.restype = c_int
_buffer_disable.argtypes = (_BufferPtr,)

_block_start = _lib.iio_block_start
_block_start.restype = c_void_p
_block_start.argtypes = (_BlockPtr,)

_block_end = _lib.iio_block_end
_block_end.restype = c_void_p
_block_end.argtypes = (_BlockPtr,)

_block_get_buffer = _lib.iio_block_get_buffer
_block_get_buffer.restype = _BufferPtr
_block_get_buffer.argtypes = (_BlockPtr,)

_buffer_cancel = _lib.iio_buffer_cancel
_buffer_cancel.restype = c_void_p
_buffer_cancel.argtypes = (_BufferPtr,)

_buffer_get_device = _lib.iio_buffer_get_device
_buffer_get_device.restype = _DevicePtr
_buffer_get_device.argtypes = (_BufferPtr,)

_buffer_get_channels_mask = _lib.iio_buffer_get_channels_mask
_buffer_get_channels_mask.restype = _ChannelsMaskPtr
_buffer_get_channels_mask.argtypes = (_BufferPtr,)

_create_block = _lib.iio_buffer_create_block
_create_block.restype = _BlockPtr
_create_block.argtypes = (_BufferPtr, c_size_t)
_create_block.errcheck = _check_ptr_err

_block_destroy = _lib.iio_block_destroy
_block_destroy.argtypes = (_BlockPtr,)

_block_enqueue = _lib.iio_block_enqueue
_block_enqueue.restype = c_int
_block_enqueue.argtypes = (_BlockPtr, c_size_t, c_bool)
_block_enqueue.errcheck = _check_negative

_block_dequeue = _lib.iio_block_dequeue
_block_dequeue.restype = c_int
_block_dequeue.argtypes = (_BlockPtr, c_bool)
_block_dequeue.errcheck = _check_negative

_create_stream = _lib.iio_buffer_create_stream
_create_stream.restype = _StreamPtr
_create_stream.argtypes = (_BufferPtr, c_size_t, c_size_t)
_create_stream.errcheck = _check_ptr_err

_stream_destroy = _lib.iio_stream_destroy
_stream_destroy.argtypes = (_StreamPtr,)

_stream_get_next_block = _lib.iio_stream_get_next_block
_stream_get_next_block.restype = _BlockPtr
_stream_get_next_block.argtypes = (_StreamPtr,)
_stream_get_next_block.errcheck = _check_ptr_err


# pylint: enable=invalid-name


def _get_lib_version(ctx = None):
    return (_get_version_major(ctx), _get_version_minor(ctx),
            _get_version_tag(ctx).decode("ascii"))


def _has_backend(backend):
    b_backend = backend.encode("utf-8")
    return _iio_has_backend(b_backend)


def iio_strerror(err, buf, length):
    """Get a string description of an error code."""
    b_buf = buf.encode("utf-8")
    _iio_strerror(err, b_buf, length)


version = _get_lib_version()
backends = [_get_backend(b).decode("ascii") for b in range(0, _get_backends_count())]


class _IIO_Object(object):
    def __init__(self, hdl, parent):
        self._hdl = hdl

        # Hold a reference to the parent object, to ensure that every IIO object
        # is destroyed before its parent.
        self._parent = parent if parent else None

    def __eq__(self, other):
        return cast(self._hdl, c_void_p).value == cast(other._hdl, c_void_p).value


class Attr(_IIO_Object):
    """Represents an attribute."""

    def __init__(self, parent, attr):
        """
        Initialize a new instance of the Attr class.

        :param attr: type=_AttrPtr
            Pointer to an IIO attribute.

        returns: type=iio.Attr
            A new instance of this class
        """
        self._attr = attr
        self._name = _a_get_name(attr).decode("ascii")
        self._filename = _a_get_fname(attr).decode("ascii")
        super().__init__(self._attr, parent)

    def __str__(self):
        return self._name

    def _read(self):
        buf = create_string_buffer(1024)
        _a_read(self._attr, buf, len(buf))
        return buf.value.decode("ascii")

    def _write(self, value):
        _a_write(self._attr, value.encode("ascii"))

    name = property(
        lambda self: self._name, None, None, "The name of this attribute.\n\ttype=str"
    )

    filename = property(
        lambda self: self._filename,
        None,
        None,
        "The filename in sysfs to which this attribute is bound.\n\ttype=str",
    )

    value = property(
        lambda self: self._read(),
        lambda self, x: self._write(x),
        None,
        "Current value of this attribute.\n\ttype=str",
    )

class ChannelsMask(_IIO_Object):
    """A bitmask where each bit corresponds to an enabled channel."""

    def __init__(self, dev):
        self._dev = dev
        self._channels = list()

        self._mask = _create_channels_mask(_channels_count(dev._device))
        super(ChannelsMask, self).__init__(self._mask, dev)

    def __del__(self):
        _channels_mask_destroy(self._mask)

    def _set_channels(self, channels):
        for chn in self._dev.channels:
            if chn in channels:
                _c_enable(chn._channel, self._mask)
            else:
                _c_disable(chn._channel, self._mask)

        self._channels = channels

    channels = property(
            lambda self: self._channels,
            _set_channels,
            None,
            "List of enabled channels",
    )

    @property
    def sample_size(self):
        """
        Return the sample size of the current channels mask.
        type: int
        """
        return _get_sample_size(self._dev._device, self._mask)


class Channel(_IIO_Object):
    """Represents a channel of an IIO device."""

    def __init__(self, dev, _channel):
        """
        Initialize a new instance of the Channel class.

        :param _channel: type=_ChannelPtr
            Pointer to an IIO Channel.

        returns: type=iio.Channel
            An new instance of this class
        """
        self._channel = _channel
        super(Channel, self).__init__(self._channel, dev)

        self._id = _c_get_id(self._channel).decode("ascii")

        name_raw = _c_get_name(self._channel)
        self._name = name_raw.decode("ascii") if name_raw is not None else None
        self._output = _c_is_output(self._channel)
        self._scan_element = _c_is_scan_element(self._channel)

    def read(self, block, raw=False):
        """
        Extract the samples corresponding to this channel from the given iio.Block object.

        :param block: type=iio.Block
            A valid instance of the iio.Block class
        :param raw: type=bool
            If set to True, the samples are not converted from their
            native format to their host format

        returns: type=bytearray
            An array containing the samples for this channel
        """
        array = bytearray(buf._length)
        mytype = c_char * len(array)
        c_array = mytype.from_buffer(array)
        length = _c_read(self._channel, block._block, c_array, len(array), raw)
        return array[:length]

    def write(self, block, array, raw=False):
        """
        Write the specified array of samples corresponding to this channel into the given iio.Block object.

        :param buf: type=iio.Block
            A valid instance of the iio.Block class
        :param array: type=bytearray
            The array containing the samples to copy
        :param raw: type=bool
            If set to True, the samples are not converted from their
            host format to their native format

        returns: type=int
            The number of bytes written
        """
        mytype = c_char * len(array)
        c_array = mytype.from_buffer(array)
        return _c_write(self._channel, block._block, c_array, len(array), raw)

    id = property(
        lambda self: self._id,
        None,
        None,
        "An identifier of this channel.\n\tNote that it is possible that two channels have the same ID, if one is an input channel and the other is an output channel.\n\ttype=str",
    )
    name = property(
        lambda self: self._name, None, None, "The name of this channel.\n\ttype=str"
    )
    attrs = property(
        lambda self: {attr.name: attr for attr in [
            Attr(self, _c_get_attr(self._channel, x))
            for x in range(0, _c_attr_count(self._channel))
        ]},
        None,
        None,
        "List of attributes for this channel.\n\ttype=dict of iio.Attr",
    )
    output = property(
        lambda self: self._output,
        None,
        None,
        "Contains True if the channel is an output channel, False otherwise.\n\ttype=bool",
    )
    scan_element = property(
        lambda self: self._scan_element,
        None,
        None,
        "Contains True if the channel is a scan element, False otherwise.\n\tIf a channel is a scan element, then it is possible to enable it and use it for I/O operations.\n\ttype=bool",
    )

    @property
    def device(self):
        """
        Corresponding device for the channel.
        type: iio.Device
        """
        return self._parent

    @property
    def index(self):
        """Index for the channel."""
        return _channel_get_index(self._channel)

    @property
    def data_format(self):
        """
        Channel data format.
        type: iio.DataFormat
        """
        return _channel_get_data_format(self._channel).contents

    @property
    def modifier(self):
        """
        Channel modifier.
        type: iio.ChannelModifier(Enum)
        """
        return ChannelModifier(_channel_get_modifier(self._channel))

    @property
    def type(self):
        """
        Type for the channel.
        type: iio.ChannelType(Enum)
        """
        return ChannelType(_channel_get_type(self._channel))


class Block(_IIO_Object):
    """Represents a contiguous block of samples."""

    def __init__(self, buffer, size, _block = None):
        """
        Initialize a new instance of the Block class.

        :param buffer: type=iio.Buffer
            The iio.Buffer object that represents the hardware buffer where
            the samples will be enqueued to or dequeued from
        :param size: type=int
            The size of the block, in bytes
        """
        self._block_created = _block is None

        if _block is not None:
            self._block = _block
        else:
            try:
                self._block = _create_block(buffer._buffer, size)
            except Exception:
                self._block = None
                raise

        super(Block, self).__init__(self._block, buffer)

        self._size = size
        self.enqueued = False

    def __del__(self):
        """Destroy this block."""
        if self._block is not None and self._block_created:
            _block_destroy(self._block)

    def __len__(self):
        """Size of this buffer, in bytes."""
        return self._size

    def enqueue(self, size = None, cyclic = False):
        if not self._block_created:
            raise

        _block_enqueue(self._block, size if size is not None else self._size, cyclic)
        self.enqueued = True

    def dequeue(self, nonblock = False):
        if not self._block_created:
            raise

        _block_dequeue(self._block, nonblock)
        self.enqueued = False

    def read(self):
        """
        Retrieve the samples contained inside the Block object.

        returns: type=bytearray
            An array containing the samples
        """
        if self.enqueued:
            raise OSError(16, "Cannot read an enqueued block.")

        start = _block_start(self._block)
        end = _block_end(self._block)
        array = bytearray(end - start)
        mytype = c_char * len(array)
        c_array = mytype.from_buffer(array)
        _memmove(c_array, start, len(array))
        return array

    def write(self, array):
        """
        Copy the given array of samples inside the Block object.

        :param array: type=bytearray
                The array containing the samples to copy

        returns: type=int
            The number of bytes written into the buffer
        """
        if self.enqueued:
            raise OSError(16, "Cannot write an enqueued block.")

        start = _block_start(self._block)
        end = _block_end(self._block)
        length = end - start
        if length > len(array):
            length = len(array)
        mytype = c_char * len(array)
        c_array = mytype.from_buffer(array)
        _memmove(start, c_array, length)
        return length

    @property
    def buffer(self):
        """
        Buffer corresponding to this block object.
        type: iio.Buffer
        """
        return self._parent


class Buffer(_IIO_Object):
    """Represents a hardware I/O buffer."""

    def __init__(self, device, mask, idx=0):
        """
        Initialize a new instance of the Buffer class.

        :param device: type=iio.Device
            The iio.Device object that represents the device where the I/O
            operations will be performed
        :param mask: type=ChannelsMask
            The mask of enabled channels
        :param idx: type=int
            The hardware index of the buffer to use. If unsure, leave it to 0

        returns: type=iio.Buffer
            An new instance of this class
        """
        try:
            self._buffer = _create_buffer(device._device, idx, mask._mask)
        except Exception:
            self._buffer = None
            raise

        super(Buffer, self).__init__(self._buffer, device)

        self._idx = idx
        self._enabled = False

    def __del__(self):
        """Destroy this buffer."""
        if self._buffer is not None:
            _buffer_destroy(self._buffer)

    def cancel(self):
        """Cancel the current buffer."""
        _buffer_cancel(self._buffer)

    @property
    def device(self):
        """
        Device for the buffer.
        type: iio.Device
        """
        return self._parent

    def _set_enabled(self, enabled):
        if enabled:
            _buffer_enable(self._buffer)
        else:
            _buffer_disable(self._buffer)

    enabled = property(
            lambda self: self._enabled,
            _set_enabled,
            None,
            "Represents the state (enabled/disabled) of the hardware buffer.",
    )

    attrs = property(
        lambda self: {attr.name: attr for attr in [
            Attr(self, _b_get_attr(self._buffer, x))
            for x in range(0, _b_attr_count(self._buffer))
        ]},
        None,
        None,
        "List of attributes for this buffer.\n\ttype=dict of iio.Attr",
    )


class Stream(_IIO_Object):
    def __init__(self, buffer, samples_count, nb_blocks = 4):
        try:
            self._stream = _create_stream(buffer._buffer, nb_blocks, samples_count)
        except Exception:
            self._stream = None
            raise

        super(Stream, self).__init__(self._stream, buffer)

        mask_hdl = _buffer_get_channels_mask(buffer._buffer)
        sample_size = _get_sample_size(buffer.device._device, mask_hdl)

        self._block_size = sample_size * samples_count
        self._buffer = buffer

    def __del__(self):
        _stream_destroy(self._stream)

    def __iter__(self):
        return self

    def __next__(self):
        next_hdl = _stream_get_next_block(self._stream)

        return Block(self._buffer, self._block_size, next_hdl)


class _DeviceOrTrigger(_IIO_Object):
    def __init__(self, _ctx, _device):
        super(_DeviceOrTrigger, self).__init__(_device, _ctx)

        self._device = _device
        self._context = _d_get_context(_device)
        self._id = _d_get_id(self._device).decode("ascii")

        name_raw = _d_get_name(self._device)
        self._name = name_raw.decode("ascii") if name_raw is not None else None

        label_raw = _d_get_label(self._device)
        self._label = label_raw.decode("ascii") if label_raw is not None else None

    def reg_write(self, reg, value):
        """
        Set a value to one register of this device.

        :param reg: type=int
            The register address
        :param value: type=int
            The value that will be used for this register
        """
        _d_reg_write(self._device, reg, value)

    def reg_read(self, reg):
        """
        Read the content of a register of this device.

        :param reg: type=int
            The register address

        returns: type=int
            The value of the register
        """
        value = c_uint()
        _d_reg_read(self._device, reg, _byref(value))
        return value.value

    def find_channel(self, name_or_id, is_output=False):
        """

        Find a IIO channel by its name or ID.

        :param name_or_id: type=str
            The name or ID of the channel to find
        :param is_output: type=bool
            Set to True to search for an output channel

        returns: type=iio.Device or type=iio.Trigger
            The IIO Device
        """
        chn = _d_find_channel(self._device, name_or_id.encode("ascii"), is_output)
        return None if bool(chn) is False else Channel(self, chn)

    id = property(
        lambda self: self._id,
        None,
        None,
        "An identifier of this device, only valid in this IIO context.\n\ttype=str",
    )
    name = property(
        lambda self: self._name, None, None, "The name of this device.\n\ttype=str"
    )
    label = property(
        lambda self: self._label, None, None, "The label of this device.\n\ttype=str",
    )
    attrs = property(
        lambda self: {attr.name: attr for attr in [
            Attr(self, _d_get_attr(self._device, x))
            for x in range(0, _d_attr_count(self._device))
        ]},
        None,
        None,
        "List of attributes for this IIO device.\n\ttype=dict of iio.Attr",
    )
    debug_attrs = property(
        lambda self: {attr.name: attr for attr in [
            Attr(self, _d_get_debug_attr(self._device, x))
            for x in range(0, _d_debug_attr_count(self._device))
        ]},
        None,
        None,
        "List of debug attributes for this IIO device.\n\ttype=dict of iio.Attr",
    )
    channels = property(
        lambda self: sorted([
            Channel(self, _get_channel(self._device, x))
            for x in range(0, _channels_count(self._device))
        ], key=lambda c: c.id),
        None,
        None,
        "List of channels available with this IIO device.\n\ttype=list of iio.Channel objects",
    )


class Trigger(_DeviceOrTrigger):
    """Contains the representation of an IIO device that can act as a trigger."""

    def __init__(self, ctx, _device):
        """
        Initialize a new instance of the Trigger class.

        :param _device: type=iio._DevicePtr
            A pointer to an IIO device.

        returns: type=iio.Trigger
            An new instance of this class
        """
        super(Trigger, self).__init__(ctx, _device)

    def _get_rate(self):
        return int(self.attrs["sampling_frequency"].value)

    def _set_rate(self, value):
        self.attrs["sampling_frequency"].value = str(value)

    frequency = property(
        _get_rate,
        _set_rate,
        None,
        "Configured frequency (in Hz) of this trigger\n\ttype=int",
    )


class Device(_DeviceOrTrigger):
    """Contains the representation of an IIO device."""

    def __init__(self, ctx, _device):
        """
        Initialize a new instance of the Device class.

        :param ctx: type=iio.Context
            A valid instance of the iio.Context class.
        :param _device: type=_DevicePtr
            A pointer to an IIO device.

        returns: type=iio.Device
            An new instance of this class
        """
        super(Device, self).__init__(ctx, _device)
        self.ctx = ctx

    def _set_trigger(self, trigger):
        _d_set_trigger(self._device, trigger._device if trigger else None)

    def _get_trigger(self):
        value = _DevicePtr()
        _d_get_trigger(self._device, _byref(value))
        trig = Trigger(value.contents)

        for dev in self.ctx.devices:
            if trig.id == dev.id:
                return dev
        return None

    trigger = property(
        _get_trigger,
        _set_trigger,
        None,
        "Contains the configured trigger for this IIO device.\n\ttype=iio.Trigger",
    )
    hwmon = property(
        lambda self: self._id[:5] == "hwmon", None, None,
        "Contains True if the device is a hardware-monitoring device, False if it is a IIO device.\n\ttype=bool",
    )

    @property
    def context(self):
        """
        Context for the device.
        type: iio.Context
        """
        return self.ctx


class Context(_IIO_Object):
    """Contains the representation of an IIO context."""

    def __init__(self, _context=None):
        """
        Initialize a new instance of the Context class, using the local or the network backend of the IIO library.

        returns: type=iio.Context
            An new instance of this class

        This function will create a network context if the IIOD_REMOTE
        environment variable is set to the hostname where the IIOD server runs.
        If set to an empty string, the server will be discovered using ZeroConf.
        If the environment variable is not set, a local context will be created instead.
        """
        self._context = None

        if _context is not None and not _isstring(_context):
            self._context = _context
        else:
            uri = None if _context is None else _context.encode("ascii")

            try:
                self._context = _new_ctx(None, uri)
            except Exception:
                self._context = None
                raise

        super(Context, self).__init__(self._context, None)

        self._name = _get_name(self._context).decode("ascii")
        self._description = _get_description(self._context).decode("ascii")
        self._xml = _get_xml(self._context).decode("ascii")
        self._version = _get_lib_version(self._context)

    def __del__(self):
        """Destroy this context."""
        if self._context is not None:
            _destroy(self._context)

    def set_timeout(self, timeout):
        """
        Set a timeout for I/O operations.

        :param timeout: type=int
            The timeout value, in milliseconds
        """
        _set_timeout(self._context, timeout)

    def find_device(self, name_or_id_or_label):
        """

        Find a IIO device by its name, ID or label.

        :param name_or_id_or_label: type=str
            The name, ID or label of the device to find

        returns: type=iio.Device or type=iio.Trigger
            The IIO Device
        """
        dev = _find_device(self._context, name_or_id_or_label.encode("ascii"))
        return None if bool(dev) is False else Trigger(self, dev) if _d_is_trigger(dev) else Device(self, dev)

    name = property(
        lambda self: self._name, None, None, "Name of this IIO context.\n\ttype=str"
    )
    description = property(
        lambda self: self._description,
        None,
        None,
        "Description of this IIO context.\n\ttype=str",
    )
    xml = property(
        lambda self: self._xml,
        None,
        None,
        "XML representation of the current context.\n\ttype=str",
    )
    version = property(
        lambda self: self._version,
        None,
        None,
        "Version of the backend.\n\ttype=(int, int, str)",
    )
    attrs = property(
        lambda self: {attr.name: attr.value for attr in [
            Attr(self, _get_attr(self._context, x))
            for x in range(0, _get_attrs_count(self._context))
        ]},
        None,
        None,
        "List of context-specific attributes\n\ttype=dict of iio.Attr",
    )
    devices = property(
        lambda self: [
            Trigger(self, dev) if _d_is_trigger(dev) else Device(self, dev)
            for dev in [
                _get_device(self._context, x)
                for x in range(0, _devices_count(self._context))
            ]
        ],
        None,
        None,
        "List of devices contained in this context.\n\ttype=list of iio.Device and iio.Trigger objects",
    )


class LocalContext(Context):
    """Local IIO Context."""

    def __init__(self):
        """
        Initialize a new instance of the Context class, using the local backend of the IIO library.

        returns: type=iio.LocalContext
            An new instance of this class
        """
        ctx = _new_ctx(None, "local:")
        super(LocalContext, self).__init__(ctx)


class XMLContext(Context):
    """XML IIO Context."""

    def __init__(self, xmlfile):
        """
        Initialize a new instance of the Context class, using the XML backend of the IIO library.

        :param xmlfile: type=str
            Filename of the XML file to build the context from

        returns: type=iio.XMLContext
            An new instance of this class
        """
        ctx = _new_ctx(None, "xml:" + xmlfile.encode("ascii"))
        super(XMLContext, self).__init__(ctx)


class NetworkContext(Context):
    """Network IIO Context."""

    def __init__(self, hostname=None):
        """
        Initialize a new instance of the Context class, using the network backend of the IIO library.

        :param hostname: type=str
            Hostname, IPv4 or IPv6 address where the IIO Daemon is running

        returns: type=iio.NetworkContext
            An new instance of this class
        """
        ctx = _new_ctx(None, "ip:" + (hostname.encode("ascii") if hostname is not None else ""))
        super(NetworkContext, self).__init__(ctx)


def scan_contexts():
    """Scan Context."""
    scan_ctx = dict()

    ctx = _scan(None, None)
    ctx_nb = _scan_get_results_count(ctx)

    for i in range(0, ctx_nb):
        uri = _scan_get_uri(ctx, i).decode("ascii")
        desc = _scan_get_description(ctx, i).decode("ascii")
        scan_ctx[uri] = desc

    _scan_destroy(ctx)
    return scan_ctx
