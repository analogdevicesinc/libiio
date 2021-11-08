// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    /// <summary><see cref="iio.Channel"/> class:
    /// Contains the representation of an input or output channel.</summary>
    public class Channel
    {
        private class ChannelAttr : Attr
        {
            private IntPtr chn;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_channel_attr_read(IntPtr chn, [In()] string name, [Out()] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_channel_attr_write(IntPtr chn, [In()] string name, string val);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern IntPtr iio_channel_attr_get_filename(IntPtr chn, [In()] string attr);

            public ChannelAttr(IntPtr chn, string name) : base(name, Marshal.PtrToStringAnsi(iio_channel_attr_get_filename(chn, name)))
            {
                this.chn = chn;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(1024);
                int err = iio_channel_attr_read(chn, name, builder, (uint) builder.Capacity);
                if (err < 0)
                {
                    throw new Exception("Unable to read channel attribute " + err);
                }
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_channel_attr_write(chn, name, str);
                if (err < 0)
                {
                    throw new Exception("Unable to write channel attribute " + err);
                }
            }
        }

        /// <summary><see cref="iio.Channel.ChannelModifier"/> class:
        /// Contains the available channel modifiers.</summary>
        public enum ChannelModifier
        {
            IIO_NO_MOD,
            IIO_MOD_X,
            IIO_MOD_Y,
            IIO_MOD_Z,
            IIO_MOD_X_AND_Y,
            IIO_MOD_X_AND_Z,
            IIO_MOD_Y_AND_Z,
            IIO_MOD_X_AND_Y_AND_Z,
            IIO_MOD_X_OR_Y,
            IIO_MOD_X_OR_Z,
            IIO_MOD_Y_OR_Z,
            IIO_MOD_X_OR_Y_OR_Z,
            IIO_MOD_LIGHT_BOTH,
            IIO_MOD_LIGHT_IR,
            IIO_MOD_ROOT_SUM_SQUARED_X_Y,
            IIO_MOD_SUM_SQUARED_X_Y_Z,
            IIO_MOD_LIGHT_CLEAR,
            IIO_MOD_LIGHT_RED,
            IIO_MOD_LIGHT_GREEN,
            IIO_MOD_LIGHT_BLUE,
            IIO_MOD_QUATERNION,
            IIO_MOD_TEMP_AMBIENT,
            IIO_MOD_TEMP_OBJECT,
            IIO_MOD_NORTH_MAGN,
            IIO_MOD_NORTH_TRUE,
            IIO_MOD_NORTH_MAGN_TILT_COMP,
            IIO_MOD_NORTH_TRUE_TILT_COMP,
            IIO_MOD_RUNNING,
            IIO_MOD_JOGGING,
            IIO_MOD_WALKING,
            IIO_MOD_STILL,
            IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z,
            IIO_MOD_I,
            IIO_MOD_Q,
            IIO_MOD_CO2,
            IIO_MOD_VOC,
            IIO_MOD_LIGHT_UV,
            IIO_MOD_LIGHT_DUV,
            IIO_MOD_PM1,
            IIO_MOD_PM2P5,
            IIO_MOD_PM4,
            IIO_MOD_PM10,
            IIO_MOD_ETHANOL,
            IIO_MOD_H2,
            IIO_MOD_O2
        }

        /// <summary><see cref="iio.Channel.ChannelType"/> class:
        /// Contains the available channel types.</summary>
        public enum ChannelType
        {
            IIO_VOLTAGE,
            IIO_CURRENT,
            IIO_POWER,
            IIO_ACCEL,
            IIO_ANGL_VEL,
            IIO_MAGN,
            IIO_LIGHT,
            IIO_INTENSITY,
            IIO_PROXIMITY,
            IIO_TEMP,
            IIO_INCLI,
            IIO_ROT,
            IIO_ANGL,
            IIO_TIMESTAMP,
            IIO_CAPACITANCE,
            IIO_ALTVOLTAGE,
            IIO_CCT,
            IIO_PRESSURE,
            IIO_HUMIDITYRELATIVE,
            IIO_ACTIVITY,
            IIO_STEPS,
            IIO_ENERGY,
            IIO_DISTANCE,
            IIO_VELOCITY,
            IIO_CONCENTRATION,
            IIO_RESISTANCE,
            IIO_PH,
            IIO_UVINDEX,
            IIO_ELECTRICALCONDUCTIVITY,
            IIO_COUNT,
            IIO_INDEX,
            IIO_GRAVITY,
            IIO_POSITIONRELATIVE,
            IIO_PHASE,
            IIO_MASSCONCENTRATION,
            IIO_CHAN_TYPE_UNKNOWN = Int32.MaxValue
        }

        public struct DataFormat
        {
            /// <summary>Total length of the sample, in bits</summary>
            public uint length;

            /// <summary>Length of valuable data in the sample, in bits</summary>
            public uint bits;

            /// <summary>Right-shift to apply when converting sample</summary>
            public uint shift;

            /// <summary>True if the sample is signed</summary>
            [MarshalAs(UnmanagedType.I1)] public bool is_signed;

            /// <summary>True if the sample if fully defined, sign-extended, etc.</summary>
            [MarshalAs(UnmanagedType.I1)] public bool is_fully_defined;

            /// <summary>True if the sample is in big-endian format</summary>
            [MarshalAs(UnmanagedType.I1)] public bool is_be;

            /// <summary>True if the sample should be scaled when converted</summary>
            [MarshalAs(UnmanagedType.I1)] public bool with_scale;

            /// <summary>Scale to apply if with_scale is True</summary>
            public double scale;

            /// <summary>Number of times length repeats</summary>
            public uint repeat;
        }

        internal IntPtr chn;
        private uint sample_size;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_id(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_name(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_get_attrs_count(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_attr(IntPtr chn, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_output(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_scan_element(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_enable(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_disable(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_enabled(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_read_raw(IntPtr chn, IntPtr buf, IntPtr dst, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_write_raw(IntPtr chn, IntPtr buf, IntPtr src, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_read(IntPtr chn, IntPtr buf, IntPtr dst, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_write(IntPtr chn, IntPtr buf, IntPtr src, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_data_format(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_channel_get_index(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_device(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_context(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_channel_get_modifier(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_channel_get_type(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_find_attr(IntPtr chn, [In] string name);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_convert(IntPtr chn, IntPtr dst, IntPtr src);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_convert_inverse(IntPtr chn, IntPtr dst, IntPtr src);

        /// <summary>The name of this channel.</summary>
        public readonly string name;

        /// <summary>An identifier of this channel.</summary>
        /// <remarks>It is possible that two channels have the same ID,
        /// if one is an input channel and the other is an output channel.</remarks>
        public readonly string id;

        /// <summary>Contains <c>true</c> if the channel is an output channel,
        /// <c>false</c> otherwise.</summary>
        public readonly bool output;

        /// <summary>Contains <c>true</c> if the channel is a scan element,
        /// <c>false</c> otherwise.</summary>
        /// <remarks>If a channel is a scan element, then it is possible to enable it
        /// and use it for I/O operations.</remarks>
        public readonly bool scan_element;

        /// <summary>A <c>list</c> of all the attributes that this channel has.</summary>
        public readonly List<Attr> attrs;

        /// <summary>The modifier of this channel.</summary>
        public ChannelModifier modifier { get; private set; }

        /// <summary>The type of this channel.</summary>
        public ChannelType type { get; private set; }

        /// <summary>Represents the format of a data sample.</summary>
        public DataFormat format { get; private set; }

        internal Channel(IntPtr chn)
        {
            IntPtr fmt_struct = iio_channel_get_data_format(chn);
            uint nb_attrs = iio_channel_get_attrs_count(chn);

            this.chn = chn;
            attrs = new List<Attr>();
            modifier = (ChannelModifier) iio_channel_get_modifier(chn);
            type = (ChannelType) iio_channel_get_type(chn);
            format = (DataFormat)Marshal.PtrToStructure(fmt_struct, typeof(DataFormat));
            sample_size = format.length / 8;

            for (uint i = 0; i < nb_attrs; i++)
            {
                attrs.Add(new ChannelAttr(this.chn, Marshal.PtrToStringAnsi(iio_channel_get_attr(chn, i))));
            }

            IntPtr name_ptr = iio_channel_get_name(this.chn);
            if (name_ptr == IntPtr.Zero)
            {
                name = "";
            }
            else
            {
                name = Marshal.PtrToStringAnsi(name_ptr);
            }

            id = Marshal.PtrToStringAnsi(iio_channel_get_id(this.chn));
            output = iio_channel_is_output(this.chn);
            scan_element = iio_channel_is_scan_element(this.chn);
        }

        /// <summary>Enable the current channel, so that it can be used for I/O operations.</summary>
        public void enable()
        {
            iio_channel_enable(this.chn);
        }

        /// <summary>Disable the current channel.</summary>
        public void disable()
        {
            iio_channel_disable(this.chn);
        }

        /// <summary>Returns whether or not the channel has been enabled.</summary>
        public bool is_enabled()
        {
            return iio_channel_is_enabled(this.chn);
        }

        /// <summary>Extract the samples corresponding to this channel from the
        /// given <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="buffer">A valid instance of the <see cref="iio.IOBuffer"/> class.</param>
        /// <param name="raw">If set to <c>true</c>, the samples are not converted from their
        /// hardware format to their host format.</param>
        /// <returns>A <c>byte</c> array containing the extracted samples.</returns>
        /// <exception cref="System.Exception">The samples could not be read.</exception>
        public byte[] read(IOBuffer buffer, bool raw = false)
        {
            if (!is_enabled())
            {
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            }
            if (this.output)
            {
                throw new Exception("Unable to read from output channel");
            }

            byte[] array = new byte[(int) (buffer.samples_count * sample_size)];
            MemoryStream stream = new MemoryStream(array, true);
            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
            {
                count = iio_channel_read_raw(this.chn, buffer.buf, addr, buffer.samples_count * sample_size);
            }
            else
            {
                count = iio_channel_read(this.chn, buffer.buf, addr, buffer.samples_count * sample_size);
            }
            handle.Free();
            stream.SetLength((long) count);
            return stream.ToArray();

        }

        /// <summary>
        /// Write the specified array of samples corresponding to this channel into the
        /// given <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="buffer">A valid instance of the <see cref="iio.IOBuffer"/> class.</param>
        /// <param name="array">A <c>byte</c> array containing the samples to write.</param>
        /// <param name="raw">If set to <c>true</c>, the samples are not converted from their
        /// host format to their native format.</param>
        /// <returns>The number of bytes written.</returns>
        /// <exception cref="System.Exception">The samples could not be written.</exception>
        public uint write(IOBuffer buffer, byte[] array, bool raw = false)
        {
            if (!is_enabled())
            {
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            }
            if (!this.output)
            {
                throw new Exception("Unable to write to an input channel");
            }

            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
            {
                count = iio_channel_write_raw(this.chn, buffer.buf, addr, (uint) array.Length);
            }
            else
            {
                count = iio_channel_write(this.chn, buffer.buf, addr, (uint) array.Length);
            }
            handle.Free();

            return count;
        }

        /// <summary>Get the index of this channel.</summary>
        public long get_index()
        {
            return iio_channel_get_index(chn);
        }

        /// <summary>Finds the attribute of the current channel with the given name.</summary>
        /// <returns><see cref="iio.Channel.ChannelAttr"/></returns>
        /// <exception cref="System.Exception">There is no attribute with the given name.</exception>
        public Attr find_attribute(string attribute)
        {
            IntPtr attr = iio_channel_find_attr(chn, attribute);

            if (attr == IntPtr.Zero)
            {
                throw new Exception("There is no attribute with the given name!");
            }

            return new ChannelAttr(chn, Marshal.PtrToStringAnsi(attr));
        }

        /// <summary>Finds the device of the current channel.</summary>
        /// <returns><see cref="iio.Device"/></returns>
        public Device get_device()
        {
            IntPtr dev_ptr = iio_channel_get_device(chn);
            return new Device(new Context(dev_ptr), dev_ptr);
        }

        /// <summary>Converts the data from the hardware format to the format of the arhitecture on which libiio is running.</summary>
        public void convert(IntPtr dst, IntPtr src)
        {
            iio_channel_convert(chn, dst, src);
        }

        /// <summary>Converts the data from the arhitecture on which libiio is running to the hardware format.</summary>
        public void convert_inverse(IntPtr dst, IntPtr src)
        {
            iio_channel_convert_inverse(chn, dst, src);
        }
    }
}
