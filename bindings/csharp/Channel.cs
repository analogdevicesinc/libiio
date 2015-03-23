/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

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
                    throw new Exception("Unable to read channel attribute " + err);
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_channel_attr_write(chn, name, str);
                if (err < 0)
                    throw new Exception("Unable to write channel attribute " + err);
            }
        }


        private IntPtr chn;
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

        internal Channel(IntPtr chn)
        {
            this.chn = chn;
            attrs = new List<Attr>();
            sample_size = (uint)Marshal.ReadInt32(iio_channel_get_data_format(this.chn)) / 8;
            uint nb_attrs = iio_channel_get_attrs_count(chn);

            for (uint i = 0; i < nb_attrs; i++)
                attrs.Add(new ChannelAttr(this.chn, Marshal.PtrToStringAnsi(iio_channel_get_attr(chn, i))));

            IntPtr name_ptr = iio_channel_get_name(this.chn);
            if (name_ptr == IntPtr.Zero)
                name = "";
            else
                name = Marshal.PtrToStringAnsi(name_ptr);

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
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            if (this.output)
                throw new Exception("Unable to read from output channel");

            byte[] array = new byte[(int) (buffer.samples_count * sample_size)];
            MemoryStream stream = new MemoryStream(array, true);
            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
                count = iio_channel_read_raw(this.chn, buffer.buf, addr, buffer.samples_count * sample_size);
            else
                count = iio_channel_read(this.chn, buffer.buf, addr, buffer.samples_count * sample_size);
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
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            if (!this.output)
                throw new Exception("Unable to write to an input channel");

            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
                count = iio_channel_write_raw(this.chn, buffer.buf, addr, (uint) array.Length);
            else
                count = iio_channel_write(this.chn, buffer.buf, addr, (uint) array.Length);
            handle.Free();

            return count;
        }
    }
}
