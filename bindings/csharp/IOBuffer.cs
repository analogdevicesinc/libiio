// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary><see cref="iio.IOBuffer"/> class:
    /// The class used for all I/O operations.</summary>
    public class IOBuffer
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_buffer_get_attrs_count(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_get_attr(IntPtr buf, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_buffer_is_output(IntPtr buf);

        /// <summary>The associated <see cref="iio.Device"/> object.</summary>
        public readonly Device dev;

        /// <summary>A <c>list</c> of all the attributes that this buffer has.</summary>
        public readonly List<Attr> attrs;

        internal IntPtr buf;

        internal IOBuffer(Device dev, IntPtr buffer)
        {
            this.dev = dev;
            this.buf = buffer;

            attrs = new List<Attr>();
            uint nb_buffer_attrs = iio_buffer_get_attrs_count(buf);
            for (uint i = 0; i < nb_buffer_attrs; i++)
            {
                attrs.Add(new Attr(iio_buffer_get_attr(buf, i)));
            }
        }

        /// <summary>Returns true if the buffer is an output (TX) buffer.</summary>
        public bool output
        {
            get { return iio_buffer_is_output(buf); }
        }

        /// <summary>Open this buffer for data streaming.</summary>
        /// <param name="mask">Channels mask for this stream. If NULL, use the default mask.</param>
        /// <exception cref="IioLib.IIOException">The buffer stream could not be opened.</exception>
        public BufferStream open(ChannelsMask mask)
        {
            if (mask == null)
                throw new IIOException("A channels mask is required to open a buffer");

            return new BufferStream(this, mask);
        }
    }

    /// <summary><see cref="iio.BufferStream"/> class:
    /// The class used for streaming operations on opened buffers.</summary>
    public class BufferStream : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_buffer_open(IntPtr buf, IntPtr mask);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_close(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_stream_cancel(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_stream_start(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_stream_stop(IntPtr stream);

        public readonly IOBuffer buf;
        public readonly ChannelsMask mask;

        private bool is_started;

        public bool started {
            get { return is_started; }
            set {
                int err;
                if (value)
                    err = iio_buffer_stream_start(hdl);
                else
                    err = iio_buffer_stream_stop(hdl);
                if (err != 0)
                    throw new IIOException("Unable to " + (value ? "start" : "stop") + " buffer stream", err);
                this.is_started = value;
            }
        }

        public BufferStream(IOBuffer buf, ChannelsMask mask)
        {
            this.buf = buf;
            this.mask = mask;

            IIOPtr ptr = iio_buffer_open(buf.buf, mask.hdl);
            if (!ptr)
                throw new IIOException("Unable to open buffer stream", ptr);

            this.hdl = ptr.ptr;
            this.is_started = false;
        }

        public void cancel()
        {
            iio_buffer_stream_cancel(hdl);
        }

        public Block create_block(uint size)
        {
            return new Block(this, size);
        }

        /// <summary>Gets the step size for this buffer stream.</summary>
        public uint step()
        {
            return buf.dev.get_sample_size(mask);
        }

        protected override void Destroy()
        {
            iio_buffer_close(hdl);
        }
    }
}
